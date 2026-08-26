#pragma once

#include <peakemi/core/Error.h>

#include <QByteArray>

#include <cstdint>
#include <string>
#include <string_view>

/// ONC-RPC and VXI-11 message encoding (TCP/IP Instrument Protocol, VXI-11.3).
///
/// Everything here is pure byte manipulation with no socket in sight, so the
/// wire format is unit-testable against the specification's examples
/// (architecture.md 4.2).
namespace peakemi::hal::vxi11 {

inline constexpr std::uint32_t PortmapProgram = 100000U;
inline constexpr std::uint32_t PortmapVersion = 2U;
inline constexpr std::uint32_t PortmapGetPort = 3U;
inline constexpr std::uint32_t PortmapProtocolTcp = 6U;

inline constexpr std::uint32_t CoreProgram = 0x0607AFU;
inline constexpr std::uint32_t CoreVersion = 1U;

/// Procedures of the VXI-11 core channel.
enum class CoreProcedure : std::uint32_t
{
    CreateLink = 10,
    DeviceWrite = 11,
    DeviceRead = 12,
    DeviceReadStb = 13,
    DeviceTrigger = 14,
    DeviceClear = 15,
    DeviceLocal = 17,
    DestroyLink = 23
};

/// Operation flags of device_write / device_read.
inline constexpr std::uint32_t FlagWaitLock = 0x01U;
inline constexpr std::uint32_t FlagEnd = 0x08U;
inline constexpr std::uint32_t FlagTerminationCharacterSet = 0x80U;

/// Reasons a device_read ended, as reported by the instrument.
inline constexpr std::uint32_t ReasonRequestCountReached = 0x01U;
inline constexpr std::uint32_t ReasonTerminationCharacterSeen = 0x02U;
inline constexpr std::uint32_t ReasonEnd = 0x04U;

/// Big-endian XDR writer. All VXI-11 fields are 4-byte aligned.
class XdrWriter
{
public:
    void putUnsigned(std::uint32_t value);
    void putSigned(std::int32_t value);
    /// Length-prefixed and padded to a multiple of four bytes.
    void putOpaque(std::string_view value);

    [[nodiscard]] QByteArray take() { return std::move(m_data); }

private:
    QByteArray m_data;
};

/// Big-endian XDR reader. Every read is bounds-checked; once a read runs past
/// the end the reader stays failed, so callers check once at the end.
class XdrReader
{
public:
    explicit XdrReader(QByteArray data) : m_data{std::move(data)} {}

    [[nodiscard]] std::uint32_t takeUnsigned();
    [[nodiscard]] std::int32_t takeSigned();
    [[nodiscard]] QByteArray takeOpaque();

    [[nodiscard]] bool ok() const { return m_ok; }

    [[nodiscard]] qsizetype remaining() const { return m_data.size() - m_offset; }

private:
    QByteArray m_data;
    qsizetype m_offset{0};
    bool m_ok{true};
};

/// Wrap a message in an ONC-RPC record mark (RFC 5531 section 11).
[[nodiscard]] QByteArray addRecordMark(const QByteArray& message);

/// Length of the fragment described by a four-byte record mark, and whether it
/// is the last fragment of the record.
struct RecordMark
{
    std::uint32_t length{0};
    bool last{false};
};

[[nodiscard]] Result<RecordMark> parseRecordMark(const QByteArray& header);

/// An RPC call with AUTH_NULL credentials and verifier.
[[nodiscard]] QByteArray encodeCall(std::uint32_t transactionId,
                                    std::uint32_t program,
                                    std::uint32_t version,
                                    std::uint32_t procedure,
                                    const QByteArray& parameters);

/// Strip the RPC reply header and return the procedure's result bytes.
[[nodiscard]] Result<QByteArray> decodeReply(const QByteArray& message,
                                             std::uint32_t expectedTransactionId);

// --- Portmapper -------------------------------------------------------------

[[nodiscard]] QByteArray
encodeGetPort(std::uint32_t program, std::uint32_t version, std::uint32_t protocol);
[[nodiscard]] Result<std::uint16_t> decodeGetPort(const QByteArray& result);

// --- Core channel -----------------------------------------------------------

struct LinkReply
{
    std::uint32_t error{0};
    std::uint32_t linkId{0};
    std::uint16_t abortPort{0};
    std::uint32_t maximumReceiveSize{0};
};

[[nodiscard]] QByteArray
encodeCreateLink(std::uint32_t clientId, std::uint32_t lockTimeoutMs, std::string_view device);
[[nodiscard]] Result<LinkReply> decodeCreateLink(const QByteArray& result);

[[nodiscard]] QByteArray encodeDeviceWrite(std::uint32_t linkId,
                                           std::uint32_t ioTimeoutMs,
                                           std::uint32_t lockTimeoutMs,
                                           std::uint32_t flags,
                                           std::string_view data);

struct WriteReply
{
    std::uint32_t error{0};
    std::uint32_t size{0};
};

[[nodiscard]] Result<WriteReply> decodeDeviceWrite(const QByteArray& result);

[[nodiscard]] QByteArray encodeDeviceRead(std::uint32_t linkId,
                                          std::uint32_t requestSize,
                                          std::uint32_t ioTimeoutMs,
                                          std::uint32_t lockTimeoutMs,
                                          std::uint32_t flags,
                                          char terminationCharacter);

struct ReadReply
{
    std::uint32_t error{0};
    std::uint32_t reason{0};
    QByteArray data;
};

[[nodiscard]] Result<ReadReply> decodeDeviceRead(const QByteArray& result);

[[nodiscard]] QByteArray encodeLinkOnly(std::uint32_t linkId);
[[nodiscard]] QByteArray encodeDeviceGeneric(std::uint32_t linkId,
                                             std::uint32_t ioTimeoutMs,
                                             std::uint32_t lockTimeoutMs,
                                             std::uint32_t flags);
[[nodiscard]] Result<std::uint32_t> decodeErrorOnly(const QByteArray& result);

/// Map a VXI-11 error code onto the project's error taxonomy, with the
/// instrument's own wording where the standard defines one.
[[nodiscard]] Error toError(std::uint32_t vxiError, std::string_view context);

} // namespace peakemi::hal::vxi11
