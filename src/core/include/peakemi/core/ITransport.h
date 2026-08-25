#pragma once

#include <peakemi/core/CancelToken.h>
#include <peakemi/core/Error.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace peakemi {

enum class TransportKind : std::uint8_t
{
    Tcp,
    Vxi11,
    UsbTmc,
    Serial,
    Visa,
    Simulated
};

/// Where a transport connects to, in a form that survives persistence: the
/// "known instruments" list stores exactly this (FR-DIS-5).
struct TransportDescriptor
{
    TransportKind kind{TransportKind::Tcp};
    std::string address; ///< host, device path or resource string
    int port{5025};
    int baudRate{115200};
    std::string terminator{"\n"};
    std::chrono::milliseconds defaultTimeout{5000};

    [[nodiscard]] std::string displayName() const;

    friend bool operator==(const TransportDescriptor&, const TransportDescriptor&) = default;
};

[[nodiscard]] std::string_view transportKindKey(TransportKind kind);
[[nodiscard]] std::optional<TransportKind> transportKindFromKey(std::string_view key);

/// One interface for every bus, so drivers are transport-agnostic (FR-COM-5).
///
/// Implementations are used from a single worker thread at a time; only
/// `clear()` and destruction may race with a blocked read, and only through the
/// cancellation token handed to that read.
class ITransport
{
public:
    ITransport() = default;
    virtual ~ITransport() = default;

    ITransport(const ITransport&) = delete;
    ITransport& operator=(const ITransport&) = delete;
    ITransport(ITransport&&) = delete;
    ITransport& operator=(ITransport&&) = delete;

    [[nodiscard]] virtual Status open() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual Status write(std::string_view command) = 0;

    /// Read one terminated response.
    [[nodiscard]] virtual Result<std::string> read(std::chrono::milliseconds timeout,
                                                   const CancelToken& cancel) = 0;

    /// Read an IEEE 488.2 definite-length block, e.g. a binary trace.
    [[nodiscard]] virtual Result<std::vector<std::byte>>
    readBinaryBlock(std::chrono::milliseconds timeout, const CancelToken& cancel) = 0;

    /// Write and read in one step; the common SCPI query.
    [[nodiscard]] virtual Result<std::string>
    query(std::string_view command, std::chrono::milliseconds timeout, const CancelToken& cancel);

    /// Discard buffered data and any pending operation.
    virtual void clear() = 0;

    [[nodiscard]] virtual TransportDescriptor descriptor() const = 0;
};

using TransportPtr = std::shared_ptr<ITransport>;

} // namespace peakemi
