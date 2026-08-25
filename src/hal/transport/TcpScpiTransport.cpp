#include <peakemi/core/Logging.h>
#include <peakemi/hal/Scpi.h>
#include <peakemi/hal/TcpScpiTransport.h>

#include <QElapsedTimer>
#include <QTcpSocket>

#include <cstring>
#include <utility>

namespace peakemi::hal {
namespace {

/// Slice length of a blocking wait. Short enough that abort feels immediate,
/// long enough not to spin the CPU.
constexpr int WaitSliceMs = 25;

} // namespace

TcpScpiTransport::TcpScpiTransport(TransportDescriptor descriptor)
    : m_descriptor{std::move(descriptor)}
{}

TcpScpiTransport::~TcpScpiTransport()
{
    TcpScpiTransport::close();
}

Status TcpScpiTransport::open()
{
    if (isOpen()) {
        return {};
    }
    m_socket = std::make_unique<QTcpSocket>();
    m_buffer.clear();

    const auto host = QString::fromStdString(m_descriptor.address);
    m_socket->connectToHost(host, static_cast<quint16>(m_descriptor.port));
    if (!m_socket->waitForConnected(static_cast<int>(m_descriptor.defaultTimeout.count()))) {
        const auto reason = m_socket->errorString().toStdString();
        m_socket.reset();
        return fail(ErrorCode::TransportFailure, m_descriptor.displayName() + ": " + reason);
    }
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    qCInfo(lcTransport) << "connected to" << host << m_descriptor.port;
    return {};
}

bool TcpScpiTransport::isOpen() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void TcpScpiTransport::close()
{
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket.reset();
    }
    m_buffer.clear();
}

Status TcpScpiTransport::write(std::string_view command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    QByteArray payload{command.data(), static_cast<qsizetype>(command.size())};
    if (!payload.endsWith(QByteArray::fromStdString(m_descriptor.terminator))) {
        payload.append(QByteArray::fromStdString(m_descriptor.terminator));
    }
    qCDebug(lcScpi).noquote() << ">" << QString::fromUtf8(payload).trimmed();

    qint64 written = 0;
    while (written < payload.size()) {
        const qint64 chunk =
            m_socket->write(payload.constData() + written, payload.size() - written);
        if (chunk < 0) {
            return fail(ErrorCode::TransportFailure, m_socket->errorString().toStdString());
        }
        written += chunk;
    }
    if (!m_socket->waitForBytesWritten(static_cast<int>(m_descriptor.defaultTimeout.count()))) {
        return fail(ErrorCode::Timeout, "timed out writing to " + m_descriptor.displayName());
    }
    return {};
}

template<class Predicate>
Status TcpScpiTransport::pump(std::chrono::milliseconds timeout,
                              const CancelToken& cancel,
                              Predicate predicate)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate()) {
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "read cancelled");
        }
        if (!isOpen()) {
            return fail(ErrorCode::NotConnected, m_descriptor.displayName());
        }
        if (elapsed.elapsed() >= timeout.count()) {
            return fail(ErrorCode::Timeout,
                        "no response from " + m_descriptor.displayName() + " within " +
                            std::to_string(timeout.count()) + " ms");
        }
        if (m_socket->waitForReadyRead(WaitSliceMs)) {
            m_buffer.append(m_socket->readAll());
        } else if (m_socket->state() != QAbstractSocket::ConnectedState) {
            return fail(ErrorCode::TransportFailure,
                        "connection to " + m_descriptor.displayName() + " dropped");
        }
    }
    return {};
}

Result<std::string> TcpScpiTransport::read(std::chrono::milliseconds timeout,
                                           const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    const QByteArray terminator = QByteArray::fromStdString(m_descriptor.terminator);
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.contains(terminator); }); !status)
    {
        return std::unexpected(status.error());
    }

    const auto end = m_buffer.indexOf(terminator);
    const QByteArray line = m_buffer.left(end);
    m_buffer.remove(0, end + terminator.size());
    qCDebug(lcScpi).noquote() << "<" << QString::fromUtf8(line).trimmed();
    return std::string{line.constData(), static_cast<std::size_t>(line.size())};
}

Result<std::vector<std::byte>> TcpScpiTransport::readBinaryBlock(std::chrono::milliseconds timeout,
                                                                 const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }

    // First the `#<n><length>` header, then exactly that many payload bytes.
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.size() >= 2; }); !status) {
        return std::unexpected(status.error());
    }
    const auto digits = static_cast<qsizetype>(m_buffer.at(1) - '0');
    if (m_buffer.at(0) != '#' || digits <= 0) {
        return fail(ErrorCode::ProtocolViolation, "expected a definite-length block header");
    }
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.size() >= 2 + digits; }); !status)
    {
        return std::unexpected(status.error());
    }

    auto header = scpi::parseBlockHeader(
        std::string_view{m_buffer.constData(), static_cast<std::size_t>(m_buffer.size())});
    if (!header) {
        return std::unexpected(header.error());
    }
    const auto total = static_cast<qsizetype>(header->headerSize + header->payloadSize);
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.size() >= total; }); !status) {
        return std::unexpected(status.error());
    }

    std::vector<std::byte> payload(header->payloadSize);
    std::memcpy(payload.data(), m_buffer.constData() + header->headerSize, header->payloadSize);
    m_buffer.remove(0, total);
    qCDebug(lcScpi) << "< binary block of" << header->payloadSize << "bytes";
    return payload;
}

void TcpScpiTransport::clear()
{
    m_buffer.clear();
    if (m_socket) {
        m_socket->readAll();
    }
}

} // namespace peakemi::hal
