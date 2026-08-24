#include <peakemi/core/ITransport.hpp>

#include <array>

namespace peakemi {
namespace {

constexpr std::array<std::string_view, 6> TransportKindKeys{
    "tcp", "vxi11", "usbtmc", "serial", "visa", "simulated"};

} // namespace

std::string_view transportKindKey(TransportKind kind)
{
    return TransportKindKeys[static_cast<std::size_t>(kind)];
}

std::optional<TransportKind> transportKindFromKey(std::string_view key)
{
    for (std::size_t i = 0; i < TransportKindKeys.size(); ++i) {
        if (TransportKindKeys[i] == key) {
            return static_cast<TransportKind>(i);
        }
    }
    return std::nullopt;
}

std::string TransportDescriptor::displayName() const
{
    switch (kind) {
        case TransportKind::Tcp:
        case TransportKind::Vxi11:
            return address + ':' + std::to_string(port);
        case TransportKind::Serial:
            return address + '@' + std::to_string(baudRate);
        case TransportKind::UsbTmc:
        case TransportKind::Visa:
        case TransportKind::Simulated:
            break;
    }
    return address;
}

Result<std::string> ITransport::query(std::string_view command,
                                      std::chrono::milliseconds timeout,
                                      const CancelToken& cancel)
{
    if (auto status = write(command); !status) {
        return std::unexpected(status.error());
    }
    return read(timeout, cancel);
}

} // namespace peakemi
