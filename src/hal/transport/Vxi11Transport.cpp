#include <peakemi/core/Logging.h>
#include <peakemi/hal/Scpi.h>
#include <peakemi/hal/Vxi11Transport.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTcpSocket>

#include <algorithm>
#include <cstring>
#include <exception>
#include <utility>

namespace peakemi::hal {
namespace {

/// The portmapper always listens here (RFC 1833).
constexpr quint16 PortmapPort = 111;
/// Slice of a blocking wait, so cancellation lands promptly (FR-HAL-5).
constexpr int WaitSliceMs = 25;
/// VXI-11 reads are chunked; ask for a sensible block at a time.
constexpr std::uint32_t ReadChunkSize = 16384;

[[nodiscard]] std::string hostOf(const TransportDescriptor& descriptor)
{
    // A descriptor may name the instrument as "192.168.1.10/inst1"; the part
    // after the slash is the VXI-11 device name, not a path.
    const auto slash = descriptor.address.find('/');
    return slash == std::string::npos ? descriptor.address : descriptor.address.substr(0, slash);
}

[[nodiscard]] std::string deviceOf(const TransportDescriptor& descriptor)
{
    const auto slash = descriptor.address.find('/');
    return slash == std::string::npos ? std::string{"inst0"} : descriptor.address.substr(slash + 1);
}

/// Receive exactly @p wanted bytes, or fail with a timeout or cancellation.
[[nodiscard]] Status receive(QTcpSocket& socket,
                             QByteArray& buffer,
                             qsizetype wanted,
                             std::chrono::milliseconds timeout,
                             const CancelToken& cancel)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (buffer.size() < wanted) {
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "read cancelled");
        }
        if (socket.state() != QAbstractSocket::ConnectedState) {
            return fail(ErrorCode::TransportFailure, "connection dropped");
        }
        if (elapsed.elapsed() >= timeout.count()) {
            return fail(ErrorCode::Timeout,
                        "instrument did not answer within " + std::to_string(timeout.count()) +
                            " ms");
        }
        if (socket.waitForReadyRead(WaitSliceMs)) {
            buffer.append(socket.readAll());
        }
    }
    return {};
}

} // namespace

Vxi11Transport::Vxi11Transport(TransportDescriptor descriptor)
    : m_descriptor{std::move(descriptor)}
    , m_device{deviceOf(m_descriptor)}
{}

Vxi11Transport::~Vxi11Transport()
{
    // Closing sends destroy_link, which allocates, so it can throw in principle.
    // A destructor must not let that escape, and the link is being dropped
    // either way once the socket goes.
    try {
        Vxi11Transport::close();
    } catch (const std::exception& error) {
        qCWarning(lcTransport) << "closing the VXI-11 link failed:" << error.what();
    } catch (...) {
        qCWarning(lcTransport) << "closing the VXI-11 link failed";
    }
}

Result<QByteArray> Vxi11Transport::call(QTcpSocket& socket,
                                        std::uint32_t transactionId,
                                        std::uint32_t program,
                                        std::uint32_t version,
                                        std::uint32_t procedure,
                                        const QByteArray& parameters,
                                        std::chrono::milliseconds timeout,
                                        const CancelToken& cancel)
{
    const QByteArray message = vxi11::addRecordMark(
        vxi11::encodeCall(transactionId, program, version, procedure, parameters));
    if (socket.write(message) != message.size()) {
        return fail(ErrorCode::TransportFailure, socket.errorString().toStdString());
    }
    if (!socket.waitForBytesWritten(static_cast<int>(timeout.count()))) {
        return fail(ErrorCode::Timeout, "timed out sending an RPC call");
    }

    // A reply may arrive as several fragments; the last one is flagged.
    QByteArray payload;
    QByteArray buffer;
    for (;;) {
        if (auto status = receive(socket, buffer, 4, timeout, cancel); !status) {
            return std::unexpected(status.error());
        }
        auto mark = vxi11::parseRecordMark(buffer.left(4));
        if (!mark) {
            return std::unexpected(mark.error());
        }
        const auto total = 4 + static_cast<qsizetype>(mark->length);
        if (auto status = receive(socket, buffer, total, timeout, cancel); !status) {
            return std::unexpected(status.error());
        }
        payload.append(buffer.mid(4, static_cast<qsizetype>(mark->length)));
        buffer.remove(0, total);
        if (mark->last) {
            break;
        }
    }
    return vxi11::decodeReply(payload, transactionId);
}

Result<std::uint16_t> Vxi11Transport::queryCorePort(const std::string& host,
                                                    std::chrono::milliseconds timeout)
{
    QTcpSocket socket;
    socket.connectToHost(QString::fromStdString(host), PortmapPort);
    if (!socket.waitForConnected(static_cast<int>(timeout.count()))) {
        return fail(ErrorCode::TransportFailure,
                    host + ":111 " + socket.errorString().toStdString());
    }

    const CancelToken cancel;
    auto reply = call(
        socket,
        1,
        vxi11::PortmapProgram,
        vxi11::PortmapVersion,
        vxi11::PortmapGetPort,
        vxi11::encodeGetPort(vxi11::CoreProgram, vxi11::CoreVersion, vxi11::PortmapProtocolTcp),
        timeout,
        cancel);
    socket.disconnectFromHost();
    if (!reply) {
        return std::unexpected(reply.error());
    }
    return vxi11::decodeGetPort(*reply);
}

Status Vxi11Transport::open()
{
    if (isOpen()) {
        return {};
    }

    const std::string host = hostOf(m_descriptor);
    // Port 0 means "ask the portmapper"; a configured port skips that step.
    std::uint16_t port = static_cast<std::uint16_t>(m_descriptor.port);
    if (port == 0 || port == PortmapPort) {
        auto discovered = queryCorePort(host, m_descriptor.defaultTimeout);
        if (!discovered) {
            return std::unexpected(discovered.error());
        }
        port = *discovered;
    }

    m_socket = std::make_unique<QTcpSocket>();
    m_socket->connectToHost(QString::fromStdString(host), port);
    if (!m_socket->waitForConnected(static_cast<int>(m_descriptor.defaultTimeout.count()))) {
        const auto reason = m_socket->errorString().toStdString();
        m_socket.reset();
        return fail(ErrorCode::TransportFailure, m_descriptor.displayName() + ": " + reason);
    }
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    const CancelToken cancel;
    auto reply = coreCall(
        vxi11::CoreProcedure::CreateLink,
        vxi11::encodeCreateLink(
            static_cast<std::uint32_t>(QCoreApplication::applicationPid() & 0xFFFF), 0, m_device),
        m_descriptor.defaultTimeout,
        cancel);
    if (!reply) {
        m_socket.reset();
        return std::unexpected(reply.error());
    }
    auto link = vxi11::decodeCreateLink(*reply);
    if (!link) {
        m_socket.reset();
        return std::unexpected(link.error());
    }
    if (link->error != 0) {
        m_socket.reset();
        return std::unexpected(vxi11::toError(link->error, "create_link"));
    }

    m_linkId = link->linkId;
    m_maximumReceiveSize = std::max(link->maximumReceiveSize, 1024U);
    m_linked = true;
    qCInfo(lcTransport) << "VXI-11 link" << m_linkId << "to"
                        << QString::fromStdString(host + '/' + m_device) << "on port" << port;
    return {};
}

bool Vxi11Transport::isOpen() const
{
    return m_linked && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void Vxi11Transport::close()
{
    if (m_socket && m_linked) {
        // Releasing the link lets the instrument accept the next client; if it
        // refuses, say so and drop the socket anyway.
        const CancelToken cancel;
        if (auto reply = coreCall(vxi11::CoreProcedure::DestroyLink,
                                  vxi11::encodeLinkOnly(m_linkId),
                                  m_descriptor.defaultTimeout,
                                  cancel);
            !reply)
        {
            qCDebug(lcTransport) << "destroy_link failed:"
                                 << QString::fromStdString(reply.error().message());
        }
    }
    m_linked = false;
    m_linkId = 0;
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket.reset();
    }
}

Result<QByteArray> Vxi11Transport::coreCall(vxi11::CoreProcedure procedure,
                                            const QByteArray& parameters,
                                            std::chrono::milliseconds timeout,
                                            const CancelToken& cancel)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    return call(*m_socket,
                m_transactionId++,
                vxi11::CoreProgram,
                vxi11::CoreVersion,
                static_cast<std::uint32_t>(procedure),
                parameters,
                timeout,
                cancel);
}

Status Vxi11Transport::write(std::string_view command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }

    std::string payload{command};
    if (!payload.ends_with(m_descriptor.terminator)) {
        payload += m_descriptor.terminator;
    }
    qCDebug(lcScpi).noquote() << ">" << QString::fromStdString(payload).trimmed();

    const CancelToken cancel;
    const auto timeout = m_descriptor.defaultTimeout;
    std::size_t sent = 0;
    while (sent < payload.size()) {
        // Chunks larger than the link's receive size must be split, and only
        // the final chunk carries END.
        const std::size_t chunk =
            std::min<std::size_t>(m_maximumReceiveSize, payload.size() - sent);
        const bool last = sent + chunk >= payload.size();
        auto reply =
            coreCall(vxi11::CoreProcedure::DeviceWrite,
                     vxi11::encodeDeviceWrite(m_linkId,
                                              static_cast<std::uint32_t>(timeout.count()),
                                              0,
                                              last ? vxi11::FlagEnd : 0U,
                                              std::string_view{payload}.substr(sent, chunk)),
                     timeout,
                     cancel);
        if (!reply) {
            return std::unexpected(reply.error());
        }
        auto written = vxi11::decodeDeviceWrite(*reply);
        if (!written) {
            return std::unexpected(written.error());
        }
        if (written->error != 0) {
            return std::unexpected(vxi11::toError(written->error, "device_write"));
        }
        sent += std::max<std::size_t>(written->size, 1);
    }
    return {};
}

Result<QByteArray> Vxi11Transport::readResponse(std::chrono::milliseconds timeout,
                                                const CancelToken& cancel,
                                                bool useTerminator)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }

    const char terminator = m_descriptor.terminator.empty() ? '\n' : m_descriptor.terminator.back();
    const std::uint32_t flags = useTerminator ? vxi11::FlagTerminationCharacterSet : 0U;

    QByteArray response;
    QElapsedTimer elapsed;
    elapsed.start();
    for (;;) {
        auto reply = coreCall(vxi11::CoreProcedure::DeviceRead,
                              vxi11::encodeDeviceRead(m_linkId,
                                                      ReadChunkSize,
                                                      static_cast<std::uint32_t>(timeout.count()),
                                                      0,
                                                      flags,
                                                      terminator),
                              timeout,
                              cancel);
        if (!reply) {
            return std::unexpected(reply.error());
        }
        auto chunk = vxi11::decodeDeviceRead(*reply);
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        if (chunk->error != 0) {
            return std::unexpected(vxi11::toError(chunk->error, "device_read"));
        }
        response.append(chunk->data);

        const bool finished =
            (chunk->reason & (vxi11::ReasonEnd | vxi11::ReasonTerminationCharacterSeen)) != 0;
        if (finished) {
            break;
        }
        if (elapsed.elapsed() >= timeout.count()) {
            return fail(ErrorCode::Timeout, "instrument response did not terminate");
        }
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "read cancelled");
        }
    }
    return response;
}

Result<std::string> Vxi11Transport::read(std::chrono::milliseconds timeout,
                                         const CancelToken& cancel)
{
    auto response = readResponse(timeout, cancel, true);
    if (!response) {
        return std::unexpected(response.error());
    }
    QByteArray trimmed = *response;
    while (!trimmed.isEmpty() && (trimmed.endsWith('\n') || trimmed.endsWith('\r'))) {
        trimmed.chop(1);
    }
    qCDebug(lcScpi).noquote() << "<" << QString::fromUtf8(trimmed);
    return std::string{trimmed.constData(), static_cast<std::size_t>(trimmed.size())};
}

Result<std::vector<std::byte>> Vxi11Transport::readBinaryBlock(std::chrono::milliseconds timeout,
                                                               const CancelToken& cancel)
{
    // Binary payloads may contain the terminator, so the read ends on END only.
    auto response = readResponse(timeout, cancel, false);
    if (!response) {
        return std::unexpected(response.error());
    }

    auto header = scpi::parseBlockHeader(
        std::string_view{response->constData(), static_cast<std::size_t>(response->size())});
    if (!header) {
        return std::unexpected(header.error());
    }
    const auto begin = static_cast<qsizetype>(header->headerSize);
    const auto length = static_cast<qsizetype>(header->payloadSize);
    if (response->size() < begin + length) {
        return fail(ErrorCode::ProtocolViolation, "binary block is shorter than its header claims");
    }

    std::vector<std::byte> payload(static_cast<std::size_t>(length));
    std::memcpy(payload.data(), response->constData() + begin, static_cast<std::size_t>(length));
    return payload;
}

void Vxi11Transport::clear()
{
    if (!isOpen()) {
        return;
    }
    const CancelToken cancel;
    auto reply = coreCall(
        vxi11::CoreProcedure::DeviceClear,
        vxi11::encodeDeviceGeneric(
            m_linkId, static_cast<std::uint32_t>(m_descriptor.defaultTimeout.count()), 0, 0),
        m_descriptor.defaultTimeout,
        cancel);
    if (!reply) {
        qCDebug(lcTransport) << "device_clear failed:"
                             << QString::fromStdString(reply.error().message());
    }
}

} // namespace peakemi::hal
