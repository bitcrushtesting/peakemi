#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.h>
#include <peakemi/python/PluginManifest.h>

#include <chrono>
#include <memory>
#include <string>

namespace peakemi::python {

/// Presents a Python driver as an ordinary AbstractAnalyzerDriver.
///
/// Nothing above the HAL can tell the difference. Every call acquires the GIL,
/// converts the plugin's exceptions into DriverError values carrying the
/// formatted traceback, and is time-boxed, so a misbehaving plugin cannot crash
/// or hang the application (FR-EXT-4).
class PythonDriverProxy final : public AbstractAnalyzerDriver
{
public:
    /// Takes the already-instantiated plugin object. The registry builds it.
    PythonDriverProxy(PluginManifest manifest, void* driverInstance, std::string origin);
    ~PythonDriverProxy() override;

    [[nodiscard]] DriverInfo info() const override;
    [[nodiscard]] Capabilities capabilities() const override;

    [[nodiscard]] Status open(TransportPtr transport) override;
    [[nodiscard]] bool isOpen() const override;
    void close() override;

    [[nodiscard]] Result<InstrumentId> identify() override;
    [[nodiscard]] Status configureSweep(const SweepParams& params) override;
    [[nodiscard]] Status armAndTrigger(const CancelToken& cancel) override;
    [[nodiscard]] Result<Trace> fetchTrace(const CancelToken& cancel) override;
    void abort() override;
    [[nodiscard]] std::vector<InstrumentError> lastErrors() override;
    void setTimeout(std::chrono::milliseconds timeout) override;

private:
    struct Impl;

    std::unique_ptr<Impl> m_impl;
};

} // namespace peakemi::python
