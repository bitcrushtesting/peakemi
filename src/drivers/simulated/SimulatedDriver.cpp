#include <peakemi/core/Logging.hpp>
#include <peakemi/drivers/SimulatedDriver.hpp>

#include <QThread>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>

namespace peakemi::drivers {
namespace {

/// Gaussian line shape of one emitter, in dB below its peak.
[[nodiscard]] double emitterContribution(const SimulatedEmitter& emitter,
                                         Hertz centre,
                                         double amplitude,
                                         Hertz frequency)
{
    const double width = std::max(1.0, static_cast<double>(emitter.width.value()));
    const double offset = static_cast<double>((frequency - centre).value()) / width;
    const double attenuation = 12.0 * offset * offset; // ~12 dB down at one width
    return attenuation > 80.0 ? 0.0 : std::pow(10.0, (amplitude - attenuation) / 20.0);
}

[[nodiscard]] double detectorOffset(const SimulatedInstrumentConfig& config, Detector detector)
{
    switch (detector) {
        case Detector::QuasiPeak:
            return config.quasiPeakOffsetDb;
        case Detector::Average:
            return config.averageOffsetDb;
        case Detector::Rms:
            return config.averageOffsetDb / 2.0;
        case Detector::Peak:
        case Detector::Sample:
            break;
    }
    return 0.0;
}

} // namespace

SimulatedInstrumentConfig SimulatedInstrumentConfig::demoBench()
{
    SimulatedInstrumentConfig config;
    config.emitters = {
        // 48 MHz oscillator and its harmonics: the classic radiated offender.
        SimulatedEmitter{.frequency = megahertz(48),
                         .amplitude = 44.0,
                         .width = kilohertz(120),
                         .harmonics = 12,
                         .harmonicRolloffDb = 3.5},
        // Switch-mode converter fundamental and harmonics in the conducted band.
        SimulatedEmitter{.frequency = kilohertz(420),
                         .amplitude = 68.0,
                         .width = kilohertz(9),
                         .harmonics = 20,
                         .harmonicRolloffDb = 2.0},
        // A wideband hump around a DDR clock.
        SimulatedEmitter{.frequency = megahertz(667),
                         .amplitude = 41.0,
                         .width = megahertz(2),
                         .harmonics = 0,
                         .harmonicRolloffDb = 0.0},
        SimulatedEmitter{.frequency = megahertz(133),
                         .amplitude = 39.5,
                         .width = kilohertz(200),
                         .harmonics = 4,
                         .harmonicRolloffDb = 4.0},
    };
    return config;
}

SimulatedDriver::SimulatedDriver(SimulatedInstrumentConfig config) : m_config{std::move(config)}
{
    m_capabilities = Capabilities{
        .range = FrequencyRange{hertz(9000), gigahertz(3.2)},
        .minimumPoints = 101,
        .maximumPoints = 40001,
        .detectors = {Detector::Peak, Detector::QuasiPeak, Detector::Average, Detector::Rms,
                      Detector::Sample},
        .resolutionBandwidths = {hertz(10), hertz(30), hertz(100), hertz(200), hertz(300),
                                 kilohertz(1), kilohertz(3), kilohertz(9), kilohertz(10),
                                 kilohertz(30), kilohertz(100), kilohertz(120), kilohertz(300),
                                 megahertz(1)},
        .videoBandwidths = {hertz(10), hertz(100), kilohertz(1), kilohertz(10), kilohertz(100),
                            megahertz(1), megahertz(3)},
        .minimumAttenuation = decibel(0.0),
        .maximumAttenuation = decibel(40.0),
        .attenuationStep = decibel(5.0),
        .minimumRefLevel = decibel(-100.0),
        .maximumRefLevel = decibel(120.0),
        .preamp = true,
        .trackingGenerator = false,
        .zeroSpan = true,
        .nativeUnit = m_config.unit};
}

SimulatedDriver::~SimulatedDriver()
{
    SimulatedDriver::close();
}

DriverInfo SimulatedDriver::staticInfo()
{
    return DriverInfo{.id = "peakemi.simulated",
                      .name = "Simulated analyzer",
                      .vendor = "PeakEmi",
                      .version = "1.0",
                      .origin = "built-in",
                      .supportedTransports = {TransportKind::Simulated}};
}

void SimulatedDriver::setConfig(SimulatedInstrumentConfig config)
{
    m_config = std::move(config);
    m_capabilities.nativeUnit = m_config.unit;
}

Capabilities SimulatedDriver::capabilities() const
{
    return m_capabilities;
}

Status SimulatedDriver::open(TransportPtr /*transport*/)
{
    // The simulated instrument deliberately ignores the transport: it is the one
    // driver that must work with no hardware and no network at all.
    m_open = true;
    m_abortRequested.store(false);
    m_errors.clear();
    return {};
}

void SimulatedDriver::close()
{
    m_open = false;
    m_armed = false;
}

Result<InstrumentId> SimulatedDriver::identify()
{
    if (!m_open) {
        return fail(ErrorCode::NotConnected, "simulated driver is not open");
    }
    return InstrumentId{.manufacturer = "PeakEmi",
                        .model = "Simulated Analyzer",
                        .serial = "SIM-0001",
                        .firmware = "1.0",
                        .raw = "PeakEmi,Simulated Analyzer,SIM-0001,1.0"};
}

Status SimulatedDriver::configureSweep(const SweepParams& params)
{
    if (!m_open) {
        return fail(ErrorCode::NotConnected, "simulated driver is not open");
    }
    if (auto status = m_capabilities.validate(params); !status) {
        m_errors.push_back(InstrumentError{-221, status.error().detail});
        return status;
    }
    m_params = params;
    m_armed = false;
    return {};
}

Status SimulatedDriver::armAndTrigger(const CancelToken& cancel)
{
    if (!m_open) {
        return fail(ErrorCode::NotConnected, "simulated driver is not open");
    }
    m_abortRequested.store(false);

    // Model the sweep taking time, in slices, so cancellation is observable.
    const auto configured = m_params.sweepTime.count() > 0
                                ? m_params.sweepTime
                                : std::chrono::milliseconds{50};
    const auto simulated = std::chrono::milliseconds{
        static_cast<std::int64_t>(static_cast<double>(configured.count()) * m_config.timeScale)};
    constexpr std::int64_t SliceMs = 20;
    for (std::int64_t waited = 0; waited < simulated.count(); waited += SliceMs) {
        if (cancel.isCancelled() || m_abortRequested.load()) {
            return fail(ErrorCode::Cancelled, "sweep aborted");
        }
        QThread::msleep(static_cast<unsigned long>(std::min(SliceMs, simulated.count() - waited)));
    }
    if (cancel.isCancelled() || m_abortRequested.load()) {
        return fail(ErrorCode::Cancelled, "sweep aborted");
    }
    m_armed = true;
    return {};
}

double SimulatedDriver::amplitudeAt(Hertz frequency, Detector detector) const
{
    double linear = 0.0;
    for (const auto& emitter : m_config.emitters) {
        linear += emitterContribution(emitter, emitter.frequency, emitter.amplitude, frequency);
        for (int harmonic = 2; harmonic <= emitter.harmonics + 1; ++harmonic) {
            const Hertz centre = emitter.frequency * harmonic;
            const double amplitude =
                emitter.amplitude - emitter.harmonicRolloffDb * std::log2(static_cast<double>(harmonic));
            linear += emitterContribution(emitter, centre, amplitude, frequency);
        }
    }
    if (linear <= 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    return 20.0 * std::log10(linear) + detectorOffset(m_config, detector);
}

Result<Trace> SimulatedDriver::fetchTrace(const CancelToken& cancel)
{
    if (!m_open) {
        return fail(ErrorCode::NotConnected, "simulated driver is not open");
    }
    if (!m_armed) {
        return fail(ErrorCode::ProtocolViolation, "fetchTrace() before armAndTrigger()");
    }
    m_armed = false;

    // Seeded from the sweep parameters: identical configuration, identical trace.
    std::seed_seq seed{static_cast<std::uint32_t>(m_config.seed),
                       static_cast<std::uint32_t>(m_params.span.start.value() & 0xffffffff),
                       static_cast<std::uint32_t>(m_params.span.stop.value() & 0xffffffff),
                       static_cast<std::uint32_t>(m_params.points),
                       static_cast<std::uint32_t>(m_params.detector)};
    std::mt19937 generator{seed};
    std::normal_distribution<double> noise{m_config.noiseFloor, m_config.noiseSigmaDb};

    Trace trace;
    trace.axis = FrequencyAxis::linear(m_params.span, m_params.points);
    trace.unit = m_config.unit;
    trace.detector = m_params.detector;
    trace.params = m_params;
    trace.acquiredAt = std::chrono::system_clock::now();
    trace.amplitudes.reserve(static_cast<std::size_t>(m_params.points));

    auto identity = identify();
    if (identity) {
        trace.source = *identity;
    }

    for (int i = 0; i < m_params.points; ++i) {
        if (cancel.isCancelled() || m_abortRequested.load()) {
            return fail(ErrorCode::Cancelled, "trace fetch aborted");
        }
        const Hertz frequency = trace.axis.frequencyAt(i);
        const double floorLevel = noise(generator);
        const double signal = amplitudeAt(frequency, m_params.detector);
        // Powers add, not decibels.
        const double linear =
            std::pow(10.0, floorLevel / 20.0)
            + (std::isfinite(signal) ? std::pow(10.0, signal / 20.0) : 0.0);
        trace.amplitudes.push_back(20.0 * std::log10(linear));
    }
    return trace;
}

void SimulatedDriver::abort()
{
    m_abortRequested.store(true);
}

std::vector<InstrumentError> SimulatedDriver::lastErrors()
{
    return std::exchange(m_errors, {});
}

void SimulatedDriver::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

} // namespace peakemi::drivers
