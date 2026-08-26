#include <peakemi/hal/UsbTmcProtocol.h>

#include <QtEndian>

#include <array>
#include <cstring>

namespace peakemi::hal::usbtmc {
namespace {

/// Header layout shared by both directions, before the message-specific bytes.
void writeHeaderPrefix(QByteArray& header, MessageId message, std::uint8_t tag)
{
    header.append(static_cast<char>(static_cast<std::uint8_t>(message)));
    header.append(static_cast<char>(tag));
    // The standard requires the one's complement of the tag as a sanity check.
    header.append(static_cast<char>(static_cast<std::uint8_t>(~tag)));
    header.append('\0');
}

void writeLittleEndian(QByteArray& header, std::uint32_t value)
{
    std::array<char, 4> bytes{};
    qToLittleEndian(value, bytes.data());
    header.append(bytes.data(), 4);
}

} // namespace

std::uint8_t nextTag(std::uint8_t current)
{
    return current == 255 ? 1 : static_cast<std::uint8_t>(current + 1);
}

int paddingFor(std::uint32_t size)
{
    return static_cast<int>((4 - (size % 4)) % 4);
}

QByteArray encodeMessageOut(std::uint8_t tag, std::string_view payload, bool endOfMessage)
{
    const auto size = static_cast<std::uint32_t>(payload.size());

    QByteArray transfer;
    transfer.reserve(HeaderSize + static_cast<qsizetype>(size) + 3);
    writeHeaderPrefix(transfer, MessageId::DevDepMsgOut, tag);
    writeLittleEndian(transfer, size);
    transfer.append(static_cast<char>(endOfMessage ? AttributeEndOfMessage : 0));
    transfer.append(3, '\0'); // reserved

    transfer.append(payload.data(), static_cast<qsizetype>(payload.size()));
    transfer.append(paddingFor(size), '\0');
    return transfer;
}

QByteArray encodeRequestIn(std::uint8_t tag,
                           std::uint32_t maximumSize,
                           char terminationCharacter,
                           bool useTerminationCharacter)
{
    QByteArray transfer;
    transfer.reserve(HeaderSize);
    writeHeaderPrefix(transfer, MessageId::RequestDevDepMsgIn, tag);
    writeLittleEndian(transfer, maximumSize);
    transfer.append(static_cast<char>(useTerminationCharacter ? AttributeTerminationCharacter : 0));
    transfer.append(useTerminationCharacter ? terminationCharacter : '\0');
    transfer.append(2, '\0'); // reserved
    return transfer;
}

Result<MessageInHeader> parseMessageIn(std::span<const std::byte> data, std::uint8_t expectedTag)
{
    if (data.size() < static_cast<std::size_t>(HeaderSize)) {
        return fail(ErrorCode::ProtocolViolation,
                    "USBTMC bulk-in transfer is shorter than its header");
    }

    const auto byteAt = [data](std::size_t index) {
        return static_cast<std::uint8_t>(data[index]);
    };

    if (byteAt(0) != static_cast<std::uint8_t>(MessageId::DevDepMsgIn)) {
        return fail(ErrorCode::ProtocolViolation,
                    "unexpected USBTMC message id " + std::to_string(byteAt(0)));
    }
    const std::uint8_t tag = byteAt(1);
    if (byteAt(2) != static_cast<std::uint8_t>(~tag)) {
        return fail(ErrorCode::ProtocolViolation, "USBTMC tag complement does not match the tag");
    }
    if (tag != expectedTag) {
        return fail(ErrorCode::ProtocolViolation,
                    "USBTMC reply carries tag " + std::to_string(tag) + ", expected " +
                        std::to_string(expectedTag));
    }

    std::uint32_t transferSize = 0;
    std::array<std::uint8_t, 4> sizeBytes{byteAt(4), byteAt(5), byteAt(6), byteAt(7)};
    std::memcpy(&transferSize, sizeBytes.data(), sizeof(transferSize));
    transferSize = qFromLittleEndian(transferSize);

    return MessageInHeader{.tag = tag,
                           .transferSize = transferSize,
                           .endOfMessage = (byteAt(8) & AttributeEndOfMessage) != 0};
}

} // namespace peakemi::hal::usbtmc
