#include <peakemi/core/Logging.h>
#include <peakemi/drivers/InstrumentProfiles.h>
#include <peakemi/drivers/UnitrendFscanDriver.h>
#include <peakemi/hal/Scpi.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <utility>

namespace peakemi::drivers {
namespace {

/// The EMI personality, and the scan-list slot this driver uses.
///
/// The instrument holds ten scan ranges. PeakEmi's engine already splits a run
/// into segments and asks for one span at a time, so the driver keeps a single
/// range and disables the rest rather than mirroring the engine's segmentation
/// into the instrument, where the two could disagree about what was measured.
constexpr auto EmiMode = "EMI";
constexpr int ScanSlot = 1;
constexpr int ScanSlots = 10;
/// The meter used for a dwell. The instrument has three, which could measure
/// peak, quasi-peak and CISPR average in one pass; the engine asks for one
/// detector at a time, so only the first is enabled and the others are left off
/// (they read back the 9.91e+37 "not measuring" sentinel).
constexpr int MeterSlot = 1;
/// What a meter reports when it is switched off: the SCPI "not a number".
constexpr double NotMeasuring = 9.9e37;

[[nodiscard]] std::string scanNode(std::string_view leaf)
{
    return ":SENSe:FSCan:SCAN" + std::to_string(ScanSlot) + std::string{leaf};
}

[[nodiscard]] std::string seconds(std::chrono::milliseconds duration)
{
    return std::to_string(static_cast<double>(duration.count()) / 1000.0);
}

/// Input attenuation this driver will actually set, in dB.
///
/// The EMI personality has no automatic attenuation -- neither the scan list
/// nor the meter offers an AUTO switch, unlike the SA mode's
/// ":SENSe:POWer:RF:ATTenuation:AUTO". So "automatic" cannot be passed on, and
/// the one thing the driver must not do is leave whatever the last operator or
/// the last run happened to set: the instrument does not compensate a wrong
/// attenuation, it just measures its own noise floor that much higher, and the
/// trace looks perfectly plausible while reading tens of dB high. A fixed,
/// documented default is the honest substitute -- 10 dB, which is what the
/// instrument itself powers up with and what SA mode uses.
constexpr int AutomaticAttenuationDb = 10;

[[nodiscard]] int attenuationDb(const SweepParams& params, int maximum, bool evenOnly)
{
    const double wanted = params.automaticAttenuation ? static_cast<double>(AutomaticAttenuationDb)
                                                      : params.attenuation.value();
    int value = static_cast<int>(std::lround(wanted));
    if (evenOnly) {
        // The scan list documents its attenuation as an even number; an odd one
        // is accepted and silently rounded, so it is rounded here instead,
        // where the choice is visible.
        value -= value % 2;
    }
    return std::clamp(value, 0, maximum);
}

} // namespace

UnitrendFscanDriver::UnitrendFscanDriver(DriverInfo info, Capabilities capabilities)
    : m_info{std::move(info)}
    , m_capabilities{std::move(capabilities)}
{}

UnitrendFscanDriver::~UnitrendFscanDriver()
{
    UnitrendFscanDriver::abort();
    UnitrendFscanDriver::close();
}

std::string UnitrendFscanDriver::detectorKeyword(Detector detector)
{
    switch (detector) {
        case Detector::Peak:
            return "POSitive";
        case Detector::QuasiPeak:
            return "QPEak";
        case Detector::Average:
            // CAVerage is the CISPR (EMI) average detector, not the plain
            // linear average the instrument also offers as AVERage. In a mode
            // that exists to make CISPR measurements, and against limit lines
            // whose "average" columns are written for the CISPR average
            // detector, CAVerage is what "average" has to mean -- a plain
            // average here would read low against those limits and pass
            // equipment that does not comply.
            return "CAVerage";
        case Detector::Rms:
        case Detector::Sample:
            // Not offered by the EMI personality; capabilities() omits them, so
            // a sweep asking for one is refused before this is reached.
            return {};
    }
    return {};
}

Status UnitrendFscanDriver::open(TransportPtr transport)
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

    // Switching personality reconfigures the instrument and it stops answering
    // while it does, so the mode change is followed by a wait rather than a
    // query that would be dropped.
    if (auto status = sendValue(":INSTrument:SELect", EmiMode); !status) {
        return status;
    }
    if (auto status = waitUntilResponsive(m_abortToken, std::chrono::seconds{20}); !status) {
        return status;
    }

    auto mode = query(":INSTrument:SELect?", m_abortToken);
    if (!mode) {
        return std::unexpected(mode.error());
    }
    if (mode->find("EMI") == std::string::npos) {
        return fail(ErrorCode::UnsupportedSetting,
                    "the instrument did not enter EMI mode (it reports '" + *mode +
                        "'); the EMI/CISPR option may not be installed");
    }

    if (auto status = send("*CLS"); !status) {
        return status;
    }
    // Stop whatever the front panel was doing and take single-shot control. The
    // scan and the meter have separate run control: INITiate2 is the scan,
    // INITiate1 the meter.
    if (auto status = send(":INITiate:STOP"); !status) {
        return status;
    }
    if (auto status = sendValue(":INITiate2:CONTinuous", "OFF"); !status) {
        return status;
    }
    // The scan trace honours this; the meter does not and always reports dBm,
    // which fetchMeterReading converts.
    return sendValue(":UNIT:POWer", "DBUV");
}

bool UnitrendFscanDriver::isOpen() const
{
    return m_transport && m_transport->isOpen();
}

void UnitrendFscanDriver::close()
{
    if (!m_transport) {
        return;
    }
    // Deliberately *not* left scanning, which is the opposite of what the SA
    // drivers do on close (FR-RUN-5, NFR-UX-2) and is worth explaining.
    //
    // A swept analyzer still answers SCPI between sweeps, so leaving it running
    // costs nothing. The EMI personality does not: while it scans it is deaf as
    // well as mute, and a continuous scan never ends, so there is no gap in
    // which a command can land. Leaving this instrument scanning on disconnect
    // makes it unreachable over the network entirely -- the next run cannot
    // even connect, and it takes the front panel or a power cycle to get it
    // back. A static display is a small price for an instrument that still
    // answers, and the operator can start a scan from the front panel.
    if (auto status = send(":INITiate:STOP"); !status) {
        qCDebug(lcDriver) << "could not stop the scan on close:"
                          << QString::fromStdString(status.error().message());
    }
    if (auto status = sendValue(":INITiate2:CONTinuous", "OFF"); !status) {
        qCDebug(lcDriver) << "could not leave single-scan mode set:"
                          << QString::fromStdString(status.error().message());
    }
    m_transport->close();
    m_transport.reset();
}

Status UnitrendFscanDriver::send(const std::string& command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    return m_transport->write(command);
}

Status UnitrendFscanDriver::sendValue(const std::string& command, const std::string& value)
{
    return send(command + ' ' + value);
}

Result<std::string> UnitrendFscanDriver::query(const std::string& command,
                                               const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    return m_transport->query(command, m_timeout, cancel);
}

Result<InstrumentId> UnitrendFscanDriver::identify()
{
    if (!m_identity.raw.empty()) {
        return m_identity;
    }
    auto response = query("*IDN?", m_abortToken);
    if (!response) {
        return std::unexpected(response.error());
    }
    m_identity = scpi::parseIdn(*response);
    return m_identity;
}

Status UnitrendFscanDriver::waitUntilResponsive(const CancelToken& cancel,
                                                std::chrono::milliseconds limit)
{
    // A scan makes the instrument mute, so "it answered" is the completion
    // signal. Each probe clears the transport first: a reply to an earlier
    // probe that arrived just as the scan ended would otherwise be read as the
    // answer to this one and desynchronise every later query.
    constexpr auto probeTimeout = std::chrono::milliseconds{700};
    // Long enough that a reply already on its way is seen, short enough that
    // draining an empty line costs one of these and no more.
    constexpr auto drainTimeout = std::chrono::milliseconds{250};
    constexpr int maximumDrainReads = 64;
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancel.isCancelled() || m_abortRequested.load()) {
            return fail(ErrorCode::Cancelled, "aborted while waiting for the instrument");
        }
        if (!isOpen()) {
            return fail(ErrorCode::NotConnected, m_info.name);
        }
        m_transport->clear();
        if (m_transport->query("*IDN?", probeTimeout, cancel)) {
            // One probe answering is not yet a clean line. Every probe that
            // timed out while the instrument was mute is still answered, just
            // late, so a backlog of replies is queued behind this one and the
            // caller's next query would read the oldest of them instead of its
            // own answer. Draining has to be done by reading rather than by
            // clear(): clear() only discards what has already been pumped into
            // the socket object, and these replies are still in flight. Reading
            // until a read times out leaves the line genuinely empty.
            // Bounded: one read per probe that could have gone unanswered,
            // plus a margin. An unbounded loop would hang here against an
            // instrument that chatters rather than falling silent.
            for (int drained = 0; drained < maximumDrainReads; ++drained) {
                if (cancel.isCancelled() || m_abortRequested.load()) {
                    return fail(ErrorCode::Cancelled, "aborted while draining the transport");
                }
                if (!m_transport->read(drainTimeout, cancel)) {
                    break;
                }
            }
            m_transport->clear();
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }
    return fail(ErrorCode::Timeout,
                "the instrument did not start answering again within " +
                    std::to_string(limit.count()) + " ms");
}

Status UnitrendFscanDriver::configureSweep(const SweepParams& requested)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    if (auto status = m_capabilities.validate(requested); !status) {
        return status;
    }
    m_params = requested;
    m_meterMode = requested.span.width() == Hertz{0};
    return m_meterMode ? configureMeter(requested) : configureScan(requested);
}

Status UnitrendFscanDriver::configureScan(const SweepParams& params)
{
    // Everything is set up while the instrument is stopped: a scan in progress
    // discards what is sent to it.
    if (auto status = send(":INITiate:STOP"); !status) {
        return status;
    }
    if (auto status = sendValue(":INITiate2:CONTinuous", "OFF"); !status) {
        return status;
    }
    // Only one range is measured at a time, so the other nine are switched off
    // -- an enabled leftover would be scanned too and its points appended to
    // the trace, silently widening the span the caller asked for.
    for (int slot = 1; slot <= ScanSlots; ++slot) {
        if (slot == ScanSlot) {
            continue;
        }
        const auto node = ":SENSe:FSCan:SCAN" + std::to_string(slot) + ":STATe";
        if (auto status = sendValue(node, "OFF"); !status) {
            return status;
        }
    }

    const std::vector<std::pair<std::string, std::string>> steps{
        {scanNode(":STARt"), scpi::formatHertz(params.span.start)},
        {scanNode(":STOP"), scpi::formatHertz(params.span.stop)},
        {scanNode(":POINts"), std::to_string(params.points)},
        {scanNode(":POWer:GAIN:STATe"), params.preamp ? "ON" : "OFF"},
        {":SENSe:FSCan:DETector:TRACe1", detectorKeyword(params.detector)},
        {":SENSe:FSCan:SEQuence", "SCAN"},
    };
    for (const auto& [command, value] : steps) {
        if (auto status = sendValue(command, value); !status) {
            return status;
        }
    }

    if (params.rbw > Hertz{0}) {
        if (auto status = sendValue(scanNode(":BANDwidth:RESolution:AUTO"), "OFF"); !status) {
            return status;
        }
        if (auto status =
                sendValue(scanNode(":BANDwidth:RESolution"), scpi::formatHertz(params.rbw));
            !status)
        {
            return status;
        }
    } else if (auto status = sendValue(scanNode(":BANDwidth:RESolution:AUTO"), "ON"); !status) {
        return status;
    }

    // Always written, never inherited -- see attenuationDb. The scan list takes
    // an even value up to 50 dB.
    if (auto status = sendValue(scanNode(":INPut:ATTenuation"),
                                std::to_string(attenuationDb(params, 50, true)));
        !status)
    {
        return status;
    }

    if (params.sweepTime.count() > 0) {
        if (auto status = sendValue(scanNode(":TIME:AUTO"), "OFF"); !status) {
            return status;
        }
        if (auto status = sendValue(scanNode(":TIME"), seconds(params.sweepTime)); !status) {
            return status;
        }
    }

    // Enabled last: the range is only accepted once its bounds make sense, and
    // enabling it before they are set leaves it switched off again.
    return sendValue(scanNode(":STATe"), "ON");
}

Status UnitrendFscanDriver::configureMeter(const SweepParams& params)
{
    if (auto status = send(":INITiate:STOP"); !status) {
        return status;
    }
    const auto detector = detectorKeyword(params.detector);
    const std::vector<std::pair<std::string, std::string>> steps{
        {":SENSe:FREQuency:CENTer", scpi::formatHertz(params.span.start)},
        {":SENSe:METer" + std::to_string(MeterSlot) + ":DETector", detector},
        {":DISPlay:METer" + std::to_string(MeterSlot) + ":STATe", "ON"},
    };
    for (const auto& [command, value] : steps) {
        if (auto status = sendValue(command, value); !status) {
            return status;
        }
    }
    // The other two meters are switched off so the reading this driver takes
    // cannot be confused with a leftover from an earlier configuration.
    for (int slot = 1; slot <= 3; ++slot) {
        if (slot == MeterSlot) {
            continue;
        }
        if (auto status = sendValue(":DISPlay:METer" + std::to_string(slot) + ":STATe", "OFF");
            !status)
        {
            return status;
        }
    }
    // The engine carries a Phase 2 dwell in sweepTime -- there is no separate
    // dwell field on SweepParams -- and for the meter that is exactly what it
    // means: how long the detector integrates before the reading is taken.
    // The meter has its own front end, set separately from the scan list's and
    // over the slightly wider 0 to 51 dB with no even-value restriction.
    if (auto status = sendValue(":SENSe:POWer:RF:ATTenuation",
                                std::to_string(attenuationDb(params, 51, false)));
        !status)
    {
        return status;
    }
    if (auto status = sendValue(":SENSe:POWer:RF:GAIN", params.preamp ? "ON" : "OFF"); !status) {
        return status;
    }
    if (params.sweepTime.count() > 0) {
        if (auto status = sendValue(":SENSe:METer:DETector:DWELl", seconds(params.sweepTime));
            !status)
        {
            return status;
        }
    }
    return {};
}

Status UnitrendFscanDriver::armAndTrigger(const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    m_abortRequested.store(false);

    if (m_meterMode) {
        // Clear the peak hold first: it is what the reading is taken from, and
        // an unreset one would report the previous point's level at this one.
        if (auto status = send(":SENSe:METer:PHOLd:RESet"); !status) {
            return status;
        }
        if (auto status = sendValue(":INITiate1:CONTinuous", "ON"); !status) {
            return status;
        }
        // The meter integrates for the dwell; there is nothing to poll, so the
        // wait is the dwell itself, sliced so the token still takes effect.
        const auto dwell =
            m_params.sweepTime.count() > 0 ? m_params.sweepTime : std::chrono::milliseconds{1000};
        const auto deadline = std::chrono::steady_clock::now() + dwell;
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancel.isCancelled() || m_abortRequested.load()) {
                return fail(ErrorCode::Cancelled, "dwell aborted");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        return {};
    }

    if (auto status = send(":INITiate2:IMMediate"); !status) {
        return status;
    }
    // A scan can take a long time at a narrow bandwidth over a wide span, and
    // the instrument is mute for all of it, so the budget is generous: the
    // engine's own timeout is what bounds a run that is genuinely stuck.
    const auto budget = m_params.sweepTime.count() > 0
                            ? m_params.sweepTime + std::chrono::minutes{5}
                            : std::chrono::minutes{5};
    if (auto status = waitUntilResponsive(cancel, budget); !status) {
        return status;
    }
    if (m_abortRequested.load()) {
        return fail(ErrorCode::Cancelled, "scan aborted");
    }
    return {};
}

Result<Trace> UnitrendFscanDriver::fetchTrace(const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_info.name);
    }
    auto trace = m_meterMode ? fetchMeterReading(cancel) : fetchScanTrace(cancel);
    if (!trace) {
        return trace;
    }
    trace->unit = m_capabilities.nativeUnit;
    trace->detector = m_params.detector;
    trace->params = m_params;
    trace->acquiredAt = std::chrono::system_clock::now();
    if (auto identity = identify()) {
        trace->source = *identity;
    }
    return trace;
}

Result<Trace> UnitrendFscanDriver::fetchScanTrace(const CancelToken& cancel)
{
    if (auto status = sendValue(":FORMat:TRACe:DATA", "ASCii"); !status) {
        return std::unexpected(status.error());
    }
    auto response = query(":TRACe:DATA? TRACE1", cancel);
    if (!response) {
        return std::unexpected(response.error());
    }
    auto values = scpi::parseAsciiTrace(*response);
    if (!values) {
        return std::unexpected(values.error());
    }
    if (values->empty()) {
        return fail(ErrorCode::ProtocolViolation, "the instrument returned an empty scan trace");
    }

    Trace trace;
    trace.axis = FrequencyAxis::linear(m_params.span, static_cast<int>(values->size()));
    trace.amplitudes = std::move(*values);
    return trace;
}

Result<Trace> UnitrendFscanDriver::fetchMeterReading(const CancelToken& cancel)
{
    // The meter answers "-inf" until its detector has produced a first result,
    // and how long that takes is a property of the detector rather than of the
    // dwell: peak is ready within about a second of the peak hold being reset,
    // quasi-peak needs about two and a half. Reading once when the dwell timer
    // expires therefore returns -inf for exactly the detectors this driver
    // exists to provide, so the reading is polled until the instrument has one.
    //
    // The value taken is the peak hold, which accumulates from the reset in
    // armAndTrigger, so waiting longer can only extend the window the maximum
    // was taken over -- never shorten it below the dwell that was asked for.
    const auto deadline = std::chrono::steady_clock::now() + m_meterSettleBudget;
    for (;;) {
        if (cancel.isCancelled() || m_abortRequested.load()) {
            return fail(ErrorCode::Cancelled, "dwell aborted");
        }
        auto response = query(":CALCulate:METer:POWer:PEAK?", cancel);
        if (!response) {
            return std::unexpected(response.error());
        }
        auto values = scpi::parseAsciiTrace(*response);
        if (!values || values->empty()) {
            return fail(ErrorCode::ProtocolViolation,
                        "could not read the meter: '" + *response + "'");
        }

        // One field per meter; only the first is enabled. A disabled meter
        // reports 9.91e+37, the SCPI "not a number", and one that has not
        // measured yet reports -inf. Neither is a level, and neither may reach
        // a report as though it were.
        const double reading = values->front();
        if (std::isfinite(reading) && reading < NotMeasuring) {
            // The meter always answers in dBm, whatever :UNIT:POWer says --
            // unlike the scan trace, which honours it. Converting here keeps
            // every trace this driver returns in the unit capabilities()
            // advertises.
            const auto converted =
                convertAmplitude(reading, AmplitudeUnit::dBm, m_capabilities.nativeUnit);
            if (!converted) {
                return fail(ErrorCode::InvalidConfiguration,
                            "the meter reads dBm, which does not convert to the unit this "
                            "driver advertises");
            }
            Trace trace;
            trace.axis = FrequencyAxis::linear(m_params.span, 1);
            trace.amplitudes = {*converted};
            return trace;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            return fail(ErrorCode::Timeout,
                        "the meter produced no reading within " +
                            std::to_string(m_meterSettleBudget.count()) +
                            " ms of the dwell ending; it may not have been measuring");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
    }
}

void UnitrendFscanDriver::abort()
{
    m_abortRequested.store(true);
    m_abortToken.cancel();
    if (!m_transport) {
        return;
    }
    // Best effort, and worth being honest about how weak it is: a scan in
    // progress makes the instrument deaf as well as mute, so this command is
    // discarded unless it happens to land between scans. The instrument
    // finishes the scan it started regardless -- an aborted run can leave it
    // sweeping for as long as that takes, and it answers nothing until it is
    // done. The flags above are what actually stops this driver; the command is
    // here for the case where it can still be heard.
    if (auto status = m_transport->write(":INITiate:STOP"); !status) {
        qCDebug(lcDriver) << "abort: could not ask the instrument to stop:"
                          << QString::fromStdString(status.error().message());
    }
    m_transport->clear();
}

std::vector<InstrumentError> UnitrendFscanDriver::lastErrors()
{
    std::vector<InstrumentError> errors;
    if (!isOpen()) {
        return errors;
    }
    for (int i = 0; i < 16; ++i) {
        auto response = query(":SYSTem:ERRor?", m_abortToken);
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

void UnitrendFscanDriver::setTimeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
}

DriverPtr makeUnitrendFscanDriver()
{
    const auto profile = unitrendEmiProfile();
    const DriverInfo info{
        .id = "unitrend.uts3000t.emi",
        .name = profile.name,
        .vendor = "UNI-T",
        .version = "1.0",
        .origin = "built-in",
        .supportedTransports = {TransportKind::Tcp, TransportKind::Vxi11, TransportKind::UsbTmc}};
    return std::make_shared<UnitrendFscanDriver>(info, profile.capabilities);
}

} // namespace peakemi::drivers
