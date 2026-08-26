#pragma once

#include <peakemi/core/ITransport.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace peakemi::hal {

/// A USBTMC endpoint as enumerated on the bus.
struct UsbTmcDevice
{
    std::uint16_t vendorId{0};
    std::uint16_t productId{0};
    std::string serial;
    std::string manufacturer;
    std::string product;

    /// "1ab1:0960:DSA8A1234" — the address form a TransportDescriptor carries.
    [[nodiscard]] std::string address() const;

    friend bool operator==(const UsbTmcDevice&, const UsbTmcDevice&) = default;
};

/// Parse the address form above. The serial may be omitted, which then matches
/// the first device with that vendor and product id.
[[nodiscard]] Result<UsbTmcDevice> parseUsbAddress(std::string_view address);

/// USBTMC transport over libusb (FR-COM-2).
///
/// libusb types stay out of this header: the UI links against the HAL and has
/// no business seeing them, and the class must still be declarable in builds
/// configured without USB support.
class UsbTmcTransport final : public ITransport
{
public:
    explicit UsbTmcTransport(TransportDescriptor descriptor);
    ~UsbTmcTransport() override;

    /// Every USBTMC device currently on the bus. Empty, with an error, when the
    /// build has no USB support.
    [[nodiscard]] static Result<std::vector<UsbTmcDevice>> enumerate();

    /// Whether this build can talk to USB instruments at all.
    [[nodiscard]] static bool isSupported();

    [[nodiscard]] Status open() override;
    [[nodiscard]] bool isOpen() const override;
    void close() override;

    [[nodiscard]] Status write(std::string_view command) override;
    [[nodiscard]] Result<std::string> read(std::chrono::milliseconds timeout,
                                           const CancelToken& cancel) override;
    [[nodiscard]] Result<std::vector<std::byte>>
    readBinaryBlock(std::chrono::milliseconds timeout, const CancelToken& cancel) override;
    void clear() override;

    [[nodiscard]] TransportDescriptor descriptor() const override { return m_descriptor; }

private:
    struct Impl;

    TransportDescriptor m_descriptor;
    std::unique_ptr<Impl> m_impl;
};

} // namespace peakemi::hal
