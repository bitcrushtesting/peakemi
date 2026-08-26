#include <peakemi/core/Logging.h>
#include <peakemi/drivers/InstrumentProfiles.h>
#include <peakemi/drivers/ScpiAnalyzerDriver.h>
#include <peakemi/hal/Scpi.h>

#include <cmath>
#include <utility>

namespace peakemi::drivers {
namespace {

[[nodiscard]] std::string seconds(std::chrono::milliseconds duration)
{
    return std::to_string(static_cast<double>(duration.count()) / 1000.0);
}

} // namespace

ScpiAnalyzerDriver::ScpiAnalyzerDriver(DriverInfo info,
                                       Capabilities capabilities,
                                       ScpiDialect dialect)
    : m_dialect{std::move(dialect)}
    , m_info{std::move(info)}
    , m_capabilities{std::move(capabilities)}
{}

ScpiAnalyzerDriver::~ScpiAnalyzerDriver()
{
    ScpiAnalyzerDriver::abort();
    ScpiAnalyzerDriver::close();
}

Status ScpiAnalyzerDriver::open(TransportPtr transport)
{
    if (!transport) {
        return fail(ErrorCode::InvalidConfiguration, "no transport supplied");
    }
    m_transport = std::move(transport);
    if (!m_transport->isOpen()) {
        if (auto status = m_transport->open(); !status) {
            return status;
        }
    }
    m_abortRequested.store(false);
    m_abortToken.reset();

    if (auto status = send(m_dialect.clearStatus); !status) {
        return status;
    }
    return sendValue(m_dialect.continuousSweep, "OFF");
}

bool ScpiAnalyzerDriver::isOpen() const
{
    return m_transport && m_transport->isOpen();
}

void ScpiAnalyzerDriver::close()
{
    if (m_transport) {
        // Leave the instrument sweeping on its own rather than frozen in a
        // single-sweep state the user cannot see (FR-RUN-5, NFR-UX-2). A
        // failure here is worth a log line but must not stop the disconnect:
        // the transport is going away either way.
        if (auto status = sendValue(m_dialect.continuousSweep, "ON"); !status) {
            qCDebug(lcDriver) << "could not restore continuous sweep:"
                              << QString::fromStdString(status.error().message());
        }
        m_transport->close();
        m_transport.reset();
    }
}

Status ScpiAnalyzerDriver::send(const std::string& command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    return m_transport->write(command);
}

Status ScpiAnalyzerDriver::sendValue(const std::string& command, const std::string& value)
{
    return send(command + ' ' + value);
}

Result<std::string> ScpiAnalyzerDriver::query(const std::string& command, const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    return m_transport->query(command, m_timeout, cancel);
}

std::string ScpiAnalyzerDriver::detectorKeyword(Detector detector) const
{
    switch (detector) {
        case Detector::Peak:
            return m_dialect.peakDetector;
        case Detector::QuasiPeak:
            return m_dialect.quasiPeakDetector;
        case Detector::Average:
            return m_dialect.averageDetector;
        case Detector::Rms:
            return m_dialect.rmsDetector;
        case Detector::Sample:
            return m_dialect.sampleDetector;
    }
    return m_dialect.peakDetector;
}

Result<InstrumentId> ScpiAnalyzerDriver::identify()
{
    if (!m_identity.raw.empty()) {
        return m_identity;
    }
    auto response = query("*IDN?", m_abortToken);
    if (!response) {
        return std::unexpected(response.error());
    }
    m_identity = scpi::parseIdn(*response);
    adoptProfileFor(m_identity);
    return m_identity;
}

void ScpiAnalyzerDriver::adoptProfileFor(const InstrumentId& identity)
{
    auto profile = profileFor(identity.manufacturer, identity.model);
    if (!profile) {
        // An unknown model of a known family keeps the family defaults, which
        // are the widest of the family: better to attempt a sweep the
        // instrument may refuse than to reject one it would have accepted.
        qCInfo(lcDriver) << "no profile for" << QString::fromStdString(identity.model)
                         << "- keeping the family defaults";
        return;
    }

    m_capabilities = profile->capabilities;
    m_dialect = profile->dialect;
    m_info.name = profile->name;
    m_profileAdopted = true;
    qCInfo(lcDriver) << "profile adopted:" << QString::fromStdString(profile->name)
                     << "- range up to" << toMegahertz(profile->capabilities.range.stop) << "MHz,"
                     << profile->capabilities.maximumPoints << "points";
}

Status ScpiAnalyzerDriver::configureSweep(const SweepParams& requested)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    if (auto status = m_capabilities.validate(requested); !status) {
        return status;
    }
    m_params = requested;

    const std::vector<std::pair<std::string, std::string>> steps{
        {m_dialect.startFrequency, scpi::formatHertz(requested.span.start)},
        {m_dialect.stopFrequency, scpi::formatHertz(requested.span.stop)},
        {m_dialect.sweepPoints, std::to_string(requested.points)},
        {m_dialect.detector, detectorKeyword(requested.detector)},
        {m_dialect.referenceLevel, scpi::formatDecibel(requested.refLevel)},
        {m_dialect.amplitudeUnit,
         m_capabilities.nativeUnit == AmplitudeUnit::dBm ? m_dialect.dBmKeyword
                                                         : m_dialect.dBuVKeyword},
        {m_dialect.preamp, requested.preamp ? "ON" : "OFF"},
    };
    for (const auto& [command, value] : steps) {
        // An empty command means this model has no such setting -- the Siglent
        // point count, for instance, is fixed and refuses to be written.
        if (command.empty()) {
            continue;
        }
        if (auto status = sendValue(command, value); !status) {
            return status;
        }
    }

    if (requested.rbw > Hertz{0}) {
        if (auto status =
                sendValue(m_dialect.resolutionBandwidth, scpi::formatHertz(requested.rbw));
            !status)
        {
            return status;
        }
    }
    if (requested.vbw > Hertz{0}) {
        if (auto status = sendValue(m_dialect.videoBandwidth, scpi::formatHertz(requested.vbw));
            !status)
        {
            return status;
        }
    }
    if (requested.automaticAttenuation) {
        if (auto status = sendValue(m_dialect.attenuationAuto, "ON"); !status) {
            return status;
        }
    } else {
        if (auto status = sendValue(m_dialect.attenuationAuto, "OFF"); !status) {
            return status;
        }
        if (auto status =
                sendValue(m_dialect.attenuation, scpi::formatDecibel(requested.attenuation));
            !status)
        {
            return status;
        }
    }
    if (requested.sweepTime.count() > 0) {
        if (auto status = sendValue(m_dialect.sweepTime, seconds(requested.sweepTime)); !status) {
            return status;
        }
    }
    return {};
}

Status ScpiAnalyzerDriver::armAndTrigger(const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    m_abortRequested.store(false);
    if (auto status = send(m_dialect.singleSweep); !status) {
        return status;
    }

    // *OPC? blocks until the sweep is done; the transport slices the wait so the
    // token still takes effect (FR-HAL-5).
    const auto sweepTimeout =
        m_params.sweepTime.count() > 0 ? m_params.sweepTime + m_timeout : m_timeout;
    auto response = m_transport->query(m_dialect.operationComplete, sweepTimeout, cancel);
    if (!response) {
        return std::unexpected(response.error());
    }
    if (m_abortRequested.load()) {
        return fail(ErrorCode::Cancelled, "sweep aborted");
    }
    return {};
}

Result<Trace> ScpiAnalyzerDriver::fetchTrace(const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    if (auto status = sendValue(m_dialect.traceFormat, m_dialect.traceFormatAscii); !status) {
        return std::unexpected(status.error());
    }
    auto response = query(m_dialect.traceQuery, cancel);
    if (!response) {
        return std::unexpected(response.error());
    }
    auto values = scpi::parseAsciiTrace(*response);
    if (!values) {
        return std::unexpected(values.error());
    }

    Trace trace;
    trace.axis = FrequencyAxis::linear(m_params.span, static_cast<int>(values->size()));
    trace.amplitudes = std::move(*values);
    trace.unit = m_capabilities.nativeUnit;
    trace.detector = m_params.detector;
    trace.params = m_params;
    trace.acquiredAt = std::chrono::system_clock::now();
    if (auto identity = identify()) {
        trace.source = *identity;
    }
    return trace;
}

void ScpiAnalyzerDriver::abort()
{
    m_abortRequested.store(true);
    m_abortToken.cancel();
    if (m_transport) {
        m_transport->clear();
    }
}

std::vector<InstrumentError> ScpiAnalyzerDriver::lastErrors()
{
    std::vector<InstrumentError> errors;
    if (!isOpen()) {
        return errors;
    }
    // Drain the queue; instruments report 0 ("No error") when it is empty.
    for (int i = 0; i < 16; ++i) {
        auto response = query(m_dialect.errorQuery, m_abortToken);
        if (!response) {
            break;
        }
        auto entry = scpi::parseErrorQueueEntry(*response);
        if (!entry || entry->first == 0) {
            break;
        }
        errors.push_back(InstrumentError{entry->first, entry->second});
    }
    if (!errors.empty()) {
        qCWarning(lcDriver) << QString::fromStdString(m_info.name) << "reported" << errors.size()
                            << "error(s)";
    }
    return errors;
}

void ScpiAnalyzerDriver::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

DriverPtr makeSiglentSsaDriver()
{
    const auto family = familyProfile("Siglent");
    const DriverInfo info{.id = "siglent.ssa3000x",
                          .name = family.name,
                          .vendor = "Siglent",
                          .version = "1.0",
                          .origin = "built-in",
                          .supportedTransports = {TransportKind::Tcp,
                                                  TransportKind::Vxi11,
                                                  TransportKind::UsbTmc,
                                                  TransportKind::Serial}};
    return std::make_shared<ScpiAnalyzerDriver>(info, family.capabilities, family.dialect);
}

DriverPtr makeRigolDsaDriver()
{
    const auto family = familyProfile("Rigol");
    const DriverInfo info{
        .id = "rigol.dsa800",
        .name = family.name,
        .vendor = "Rigol",
        .version = "1.0",
        .origin = "built-in",
        .supportedTransports = {TransportKind::Tcp, TransportKind::Vxi11, TransportKind::UsbTmc}};
    return std::make_shared<ScpiAnalyzerDriver>(info, family.capabilities, family.dialect);
}

} // namespace peakemi::drivers
