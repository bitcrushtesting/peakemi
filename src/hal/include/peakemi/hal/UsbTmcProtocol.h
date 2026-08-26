#pragma once

#include <peakemi/core/Error.h>

#include <QByteArray>

#include <cstdint>
#include <span>
#include <string_view>

/// USBTMC bulk transfer headers (USBTMC 1.0 / USB488) (FR-COM-2).
///
/// The header packing is separated from libusb so the wire format is testable
/// in every build, including the ones configured without USB support.
namespace peakemi::hal::usbtmc {

/// Every bulk transfer starts with a twelve-byte header.
inline constexpr int HeaderSize = 12;

enum class MessageId : std::uint8_t
{
    DevDepMsgOut = 1,
    RequestDevDepMsgIn = 2,
    DevDepMsgIn = 2,
    VendorSpecificOut = 126,
    VendorSpecificIn = 127
};

/// bmTransferAttributes bits.
inline constexpr std::uint8_t AttributeEndOfMessage = 0x01;
inline constexpr std::uint8_t AttributeTerminationCharacter = 0x02;

/// USBTMC class-specific requests on the control endpoint.
inline constexpr std::uint8_t RequestInitiateClear = 5;
inline constexpr std::uint8_t RequestCheckClearStatus = 6;
inline constexpr std::uint8_t RequestGetCapabilities = 7;

/// Interface class and subclass identifying a USBTMC device.
inline constexpr std::uint8_t InterfaceClass = 0xFE;
inline constexpr std::uint8_t InterfaceSubClass = 0x03;

/// bTag cycles through 1..255; zero is reserved.
[[nodiscard]] std::uint8_t nextTag(std::uint8_t current);

/// DEV_DEP_MSG_OUT: a command travelling to the instrument.
[[nodiscard]] QByteArray
encodeMessageOut(std::uint8_t tag, std::string_view payload, bool endOfMessage = true);

/// REQUEST_DEV_DEP_MSG_IN: ask for up to @p maximumSize bytes back.
[[nodiscard]] QByteArray encodeRequestIn(std::uint8_t tag,
                                         std::uint32_t maximumSize,
                                         char terminationCharacter,
                                         bool useTerminationCharacter);

struct MessageInHeader
{
    std::uint8_t tag{0};
    std::uint32_t transferSize{0};
    bool endOfMessage{false};
};

/// Validate and unpack a bulk-in header, checking the tag echo and the
/// complement byte the standard requires.
[[nodiscard]] Result<MessageInHeader> parseMessageIn(std::span<const std::byte> data,
                                                     std::uint8_t expectedTag);

/// Bytes of padding a payload of @p size needs to keep the next header aligned.
[[nodiscard]] int paddingFor(std::uint32_t size);

} // namespace peakemi::hal::usbtmc
