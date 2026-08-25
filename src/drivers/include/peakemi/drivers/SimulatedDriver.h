#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace peakemi::drivers {

/// A narrowband emitter injected into the synthetic spectrum.
struct SimulatedEmitter
{
    Hertz frequency{};
    double amplitude{60.0};        ///< peak level in the instrument's unit
    Hertz width{kilohertz(30)};    ///< -3 dB width of the Gaussian shape
    int harmonics{0};              ///< additional harmonics to generate
    double harmonicRolloffDb{6.0}; ///< level drop per harmonic
};

/// Deterministic synthetic instrument (FR-HAL-7).
///
/// The reference target for UI work, demos and CI: no hardware, and two runs
/// with the same configuration produce byte-identical traces because the noise
/// is drawn from a generator seeded with the sweep parameters.
struct SimulatedInstrumentConfig
{
    double noiseFloor{18.0};  ///< mean noise level in `unit`
    double noiseSigmaDb{1.2}; ///< spread of the noise
    AmplitudeUnit unit{AmplitudeUnit::dBuV};
    std::uint32_t seed{0x50454b45U};
    std::vector<SimulatedEmitter> emitters;
    /// Fraction of the configured sweep/dwell time actually spent waiting.
    /// 0 makes the driver instantaneous, which is what the test suite wants.
    double timeScale{0.0};
    /// Detector behaviour: quasi-peak and average read below the peak detector,
    /// which is what makes a Phase 2 verification worth doing.
    double quasiPeakOffsetDb{-2.0};
    double averageOffsetDb{-8.0};

    /// A spectrum that fails CISPR 32 class B in a few places: switching noise
    /// plus the harmonics of a 48 MHz clock.
    [[nodiscard]] static SimulatedInstrumentConfig demoBench();
};

class SimulatedDriver final : public AbstractAnalyzerDriver
{
public:
    explicit SimulatedDriver(
        SimulatedInstrumentConfig config = SimulatedInstrumentConfig::demoBench());
    ~SimulatedDriver() override;

    [[nodiscard]] static DriverInfo staticInfo();

    void setConfig(SimulatedInstrumentConfig config);

    [[nodiscard]] const SimulatedInstrumentConfig& config() const { return m_config; }

    [[nodiscard]] DriverInfo info() const override { return staticInfo(); }

    [[nodiscard]] Capabilities capabilities() const override;

    [[nodiscard]] Status open(TransportPtr transport) override;

    [[nodiscard]] bool isOpen() const override { return m_open; }

    void close() override;

    [[nodiscard]] Result<InstrumentId> identify() override;
    [[nodiscard]] Status configureSweep(const SweepParams& params) override;
    [[nodiscard]] Status armAndTrigger(const CancelToken& cancel) override;
    [[nodiscard]] Result<Trace> fetchTrace(const CancelToken& cancel) override;
    void abort() override;
    [[nodiscard]] std::vector<InstrumentError> lastErrors() override;
    void setTimeout(std::chrono::milliseconds timeout) override;

private:
    [[nodiscard]] double amplitudeAt(Hertz frequency, Detector detector) const;

    SimulatedInstrumentConfig m_config;
    SweepParams m_params;
    Capabilities m_capabilities;
    bool m_open{false};
    bool m_armed{false};
    std::atomic_bool m_abortRequested{false};
    std::chrono::milliseconds m_timeout{5000};
    std::vector<InstrumentError> m_errors;
};

/// Registers the simulated driver with hal::DriverRegistry::instance().
void registerBuiltInDrivers();

} // namespace peakemi::drivers
