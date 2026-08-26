#include <peakemi/hal/Vxi11Protocol.h>

#include <QtEndian>

#include <array>
#include <cstring>
#include <utility>

namespace peakemi::hal::vxi11 {
namespace {

constexpr std::uint32_t RpcVersion = 2U;
constexpr std::uint32_t CallMessage = 0U;
constexpr std::uint32_t ReplyMessage = 1U;
constexpr std::uint32_t MessageAccepted = 0U;
constexpr std::uint32_t AcceptSuccess = 0U;
constexpr std::uint32_t AuthNull = 0U;
constexpr std::uint32_t LastFragmentBit = 0x80000000U;

[[nodiscard]] std::string_view acceptStateText(std::uint32_t state)
{
    switch (state) {
        case 1:
            return "program unavailable";
        case 2:
            return "program version mismatch";
        case 3:
            return "procedure unavailable";
        case 4:
            return "garbage arguments";
        case 5:
            return "system error";
        default:
            break;
    }
    return "unknown accept state";
}

} // namespace

void XdrWriter::putUnsigned(std::uint32_t value)
{
    std::array<char, 4> bytes{};
    qToBigEndian(value, bytes.data());
    m_data.append(bytes.data(), 4);
}

void XdrWriter::putSigned(std::int32_t value)
{
    putUnsigned(static_cast<std::uint32_t>(value));
}

void XdrWriter::putOpaque(std::string_view value)
{
    putUnsigned(static_cast<std::uint32_t>(value.size()));
    m_data.append(value.data(), static_cast<qsizetype>(value.size()));
    // XDR pads every variable-length field to a four-byte boundary.
    const qsizetype padding = (4 - (static_cast<qsizetype>(value.size()) % 4)) % 4;
    m_data.append(padding, '\0');
}

std::uint32_t XdrReader::takeUnsigned()
{
    if (!m_ok || remaining() < 4) {
        m_ok = false;
        return 0;
    }
    const auto value = qFromBigEndian<std::uint32_t>(m_data.constData() + m_offset);
    m_offset += 4;
    return value;
}

std::int32_t XdrReader::takeSigned()
{
    return static_cast<std::int32_t>(takeUnsigned());
}

QByteArray XdrReader::takeOpaque()
{
    const auto length = static_cast<qsizetype>(takeUnsigned());
    if (!m_ok || length < 0 || remaining() < length) {
        m_ok = false;
        return {};
    }
    QByteArray value = m_data.mid(m_offset, length);
    m_offset += length + ((4 - (length % 4)) % 4);
    return value;
}

QByteArray addRecordMark(const QByteArray& message)
{
    XdrWriter writer;
    writer.putUnsigned(LastFragmentBit | static_cast<std::uint32_t>(message.size()));
    QByteArray framed = writer.take();
    framed.append(message);
    return framed;
}

Result<RecordMark> parseRecordMark(const QByteArray& header)
{
    if (header.size() < 4) {
        return fail(ErrorCode::ProtocolViolation, "record mark is shorter than four bytes");
    }
    const auto value = qFromBigEndian<std::uint32_t>(header.constData());
    return RecordMark{.length = value & ~LastFragmentBit, .last = (value & LastFragmentBit) != 0};
}

QByteArray encodeCall(std::uint32_t transactionId,
                      std::uint32_t program,
                      std::uint32_t version,
                      std::uint32_t procedure,
                      const QByteArray& parameters)
{
    XdrWriter writer;
    writer.putUnsigned(transactionId);
    writer.putUnsigned(CallMessage);
    writer.putUnsigned(RpcVersion);
    writer.putUnsigned(program);
    writer.putUnsigned(version);
    writer.putUnsigned(procedure);
    // AUTH_NULL credentials and verifier: flavour and zero-length body each.
    writer.putUnsigned(AuthNull);
    writer.putUnsigned(0);
    writer.putUnsigned(AuthNull);
    writer.putUnsigned(0);

    QByteArray message = writer.take();
    message.append(parameters);
    return message;
}

Result<QByteArray> decodeReply(const QByteArray& message, std::uint32_t expectedTransactionId)
{
    XdrReader reader{message};
    const auto transactionId = reader.takeUnsigned();
    const auto messageType = reader.takeUnsigned();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated RPC reply");
    }
    if (transactionId != expectedTransactionId) {
        return fail(ErrorCode::ProtocolViolation,
                    "RPC reply transaction id " + std::to_string(transactionId) +
                        " does not match " + std::to_string(expectedTransactionId));
    }
    if (messageType != ReplyMessage) {
        return fail(ErrorCode::ProtocolViolation, "RPC message is not a reply");
    }

    const auto replyState = reader.takeUnsigned();
    if (replyState != MessageAccepted) {
        return fail(ErrorCode::ProtocolViolation, "RPC call was rejected by the instrument");
    }
    // Verifier: flavour plus an opaque body we do not use.
    (void)reader.takeUnsigned();
    (void)reader.takeOpaque();

    const auto acceptState = reader.takeUnsigned();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated RPC reply header");
    }
    if (acceptState != AcceptSuccess) {
        return fail(ErrorCode::ProtocolViolation,
                    "RPC call failed: " + std::string{acceptStateText(acceptState)});
    }

    const qsizetype consumed = message.size() - reader.remaining();
    return message.mid(consumed);
}

QByteArray encodeGetPort(std::uint32_t program, std::uint32_t version, std::uint32_t protocol)
{
    XdrWriter writer;
    writer.putUnsigned(program);
    writer.putUnsigned(version);
    writer.putUnsigned(protocol);
    writer.putUnsigned(0); // port, ignored in a GETPORT request
    return writer.take();
}

Result<std::uint16_t> decodeGetPort(const QByteArray& result)
{
    XdrReader reader{result};
    const auto port = reader.takeUnsigned();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated portmapper reply");
    }
    if (port == 0 || port > 0xFFFFU) {
        return fail(ErrorCode::ProtocolViolation,
                    "portmapper reports no VXI-11 core channel on this host");
    }
    return static_cast<std::uint16_t>(port);
}

QByteArray
encodeCreateLink(std::uint32_t clientId, std::uint32_t lockTimeoutMs, std::string_view device)
{
    XdrWriter writer;
    writer.putUnsigned(clientId);
    writer.putUnsigned(0); // lockDevice: exclusive access is never requested
    writer.putUnsigned(lockTimeoutMs);
    writer.putOpaque(device);
    return writer.take();
}

Result<LinkReply> decodeCreateLink(const QByteArray& result)
{
    XdrReader reader{result};
    LinkReply reply;
    reply.error = reader.takeUnsigned();
    reply.linkId = reader.takeUnsigned();
    reply.abortPort = static_cast<std::uint16_t>(reader.takeUnsigned());
    reply.maximumReceiveSize = reader.takeUnsigned();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated create_link reply");
    }
    return reply;
}

QByteArray encodeDeviceWrite(std::uint32_t linkId,
                             std::uint32_t ioTimeoutMs,
                             std::uint32_t lockTimeoutMs,
                             std::uint32_t flags,
                             std::string_view data)
{
    XdrWriter writer;
    writer.putUnsigned(linkId);
    writer.putUnsigned(ioTimeoutMs);
    writer.putUnsigned(lockTimeoutMs);
    writer.putUnsigned(flags);
    writer.putOpaque(data);
    return writer.take();
}

Result<WriteReply> decodeDeviceWrite(const QByteArray& result)
{
    XdrReader reader{result};
    WriteReply reply;
    reply.error = reader.takeUnsigned();
    reply.size = reader.takeUnsigned();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated device_write reply");
    }
    return reply;
}

QByteArray encodeDeviceRead(std::uint32_t linkId,
                            std::uint32_t requestSize,
                            std::uint32_t ioTimeoutMs,
                            std::uint32_t lockTimeoutMs,
                            std::uint32_t flags,
                            char terminationCharacter)
{
    XdrWriter writer;
    writer.putUnsigned(linkId);
    writer.putUnsigned(requestSize);
    writer.putUnsigned(ioTimeoutMs);
    writer.putUnsigned(lockTimeoutMs);
    writer.putUnsigned(flags);
    writer.putUnsigned(
        static_cast<std::uint32_t>(static_cast<unsigned char>(terminationCharacter)));
    return writer.take();
}

Result<ReadReply> decodeDeviceRead(const QByteArray& result)
{
    XdrReader reader{result};
    ReadReply reply;
    reply.error = reader.takeUnsigned();
    reply.reason = reader.takeUnsigned();
    reply.data = reader.takeOpaque();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated device_read reply");
    }
    return reply;
}

QByteArray encodeLinkOnly(std::uint32_t linkId)
{
    XdrWriter writer;
    writer.putUnsigned(linkId);
    return writer.take();
}

QByteArray encodeDeviceGeneric(std::uint32_t linkId,
                               std::uint32_t ioTimeoutMs,
                               std::uint32_t lockTimeoutMs,
                               std::uint32_t flags)
{
    XdrWriter writer;
    writer.putUnsigned(linkId);
    writer.putUnsigned(flags);
    writer.putUnsigned(lockTimeoutMs);
    writer.putUnsigned(ioTimeoutMs);
    return writer.take();
}

Result<std::uint32_t> decodeErrorOnly(const QByteArray& result)
{
    XdrReader reader{result};
    const auto error = reader.takeUnsigned();
    if (!reader.ok()) {
        return fail(ErrorCode::ProtocolViolation, "truncated reply");
    }
    return error;
}

Error toError(std::uint32_t vxiError, std::string_view context)
{
    std::string detail{context};
    detail += ": ";
    switch (vxiError) {
        case 0:
            return Error{ErrorCode::None, {}};
        case 1:
            detail += "syntax error";
            break;
        case 3:
            detail += "device not accessible";
            break;
        case 4:
            detail += "invalid link identifier";
            break;
        case 5:
            detail += "parameter error";
            break;
        case 6:
            detail += "channel not established";
            break;
        case 8:
            detail += "operation not supported";
            break;
        case 9:
            detail += "out of resources";
            break;
        case 11:
            detail += "device locked by another link";
            break;
        case 12:
            detail += "no lock held by this link";
            break;
        case 15:
            return Error{ErrorCode::Timeout, detail + "I/O timeout"};
        case 17:
            return Error{ErrorCode::TransportFailure, detail + "I/O error"};
        case 21:
            detail += "invalid address";
            break;
        case 23:
            return Error{ErrorCode::Cancelled, detail + "abort"};
        case 29:
            detail += "channel already established";
            break;
        default:
            detail += "VXI-11 error " + std::to_string(vxiError);
            break;
    }
    return Error{ErrorCode::InstrumentError, detail};
}

} // namespace peakemi::hal::vxi11
