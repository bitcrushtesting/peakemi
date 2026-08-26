#pragma once

#include <peakemi/core/ITransport.h>

#include <QString>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace peakemi::hal {

/// Optional VISA transport (FR-COM-4).
///
/// The VISA runtime is resolved at run time, never linked: PeakEmi must be
/// fully functional on a machine that has no VISA installed, and on such a
/// machine this transport simply reports itself unavailable.
class VisaTransport final : public ITransport
{
public:
    explicit VisaTransport(TransportDescriptor descriptor);
    ~VisaTransport() override;

    /// True when a VISA runtime was found and its entry points resolved.
    [[nodiscard]] static bool isAvailable();

    /// Name of the loaded runtime, for the log and the instrument dock.
    [[nodiscard]] static QString runtimeName();

    /// Resource strings the runtime reports, e.g. "TCPIP0::192.168.1.10::INSTR".
    [[nodiscard]] static Result<std::vector<std::string>>
    findResources(std::string_view filter = "?*INSTR");

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
