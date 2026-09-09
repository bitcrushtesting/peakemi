#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.h>

#include <atomic>
#include <chrono>
#include <string>

namespace peakemi::drivers {

/// The UNI-T UTS3000T series driven through its EMI option rather than its
/// spectrum-analyzer mode.
///
/// The instrument has two personalities, selected with ":INSTrument:SELect".
/// In "SA" it is an ordinary swept analyzer with a 1-3-10 bandwidth ladder and
/// no quasi-peak detector; ScpiAnalyzerDriver drives that one. In "EMI" it is a
/// receiver: the CISPR 16-1-1 bandwidths (200 Hz, 9 kHz, 120 kHz, 1 MHz) and
/// the quasi-peak and CISPR-average detectors, which is what a pre-compliance
/// measurement actually needs. The two modes share almost no commands, so this
/// is a driver of its own rather than another ScpiDialect.
///
/// Two behaviours of the EMI personality shape everything below, and both were
/// measured on a UTS3032T+ running firmware V1.04:
///
/// * **It goes mute while it scans.** Not "busy", not an error -- it answers
///   nothing at all, "*IDN?" included, and commands sent during a scan are
///   discarded rather than queued. "*OPC?" is therefore useless here: issued
///   before the scan it answers immediately and means nothing, issued during
///   one it is dropped and never answers. The scan is complete when the
///   instrument starts answering again, so that is what armAndTrigger() waits
///   for (see waitUntilResponsive).
/// * **A Phase 2 dwell is a different instrument function.** Sweeping is the
///   scan list; dwelling on one frequency is the "meter", a fixed-tuned
///   receiver with its own detector, dwell time and peak hold. A zero-span
///   request is routed to the meter and a swept one to the scan list, which is
///   why configureSweep() branches on the span width.
///
/// That branch decides whether a quasi-peak number means what CISPR says it
/// means, so it is worth being blunt about. The quasi-peak and CISPR-average
/// detectors have charge and discharge time constants measured in hundreds of
/// milliseconds, and the dwell has to be spent *at the frequency* for the
/// reading to settle. The meter does that. A swept range does not: it divides
/// the scan time across its points, so verifying a peak over the engine's
/// default 200 kHz verification span with 101 points spends about a hundredth
/// of the dwell on each one and reads low -- the direction that turns a failing
/// emission into a passing number.
///
/// A run that wants a CISPR-conformant Phase 2 on this instrument therefore
/// wants a verification span of 0, which is what routes the dwell to the meter.
/// The driver does not silently rewrite a swept request into a meter one:
/// measuring something other than what was asked for is the failure this whole
/// class of bug is made of. It measures what it was told to, and this comment
/// is here so the choice is made deliberately.
class UnitrendFscanDriver final : public AbstractAnalyzerDriver
{
public:
    UnitrendFscanDriver(DriverInfo info, Capabilities capabilities);
    ~UnitrendFscanDriver() override;

    [[nodiscard]] DriverInfo info() const override { return m_info; }

    [[nodiscard]] Capabilities capabilities() const override { return m_capabilities; }

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

    /// The keyword this instrument spells the detector with, or empty for one
    /// it does not have. Exposed for the tests, which assert that a CISPR
    /// average is not quietly sent as a plain average.
    [[nodiscard]] static std::string detectorKeyword(Detector detector);

    /// How long after the dwell the meter may take to produce its first real
    /// reading before the point is failed. The default is generous because the
    /// cost of overrunning it is one failed point, while cutting it short means
    /// reporting a level the detector had not finished forming. Worth lowering
    /// only when a caller would rather fail fast than wait.
    void setMeterSettleBudget(std::chrono::milliseconds budget) { m_meterSettleBudget = budget; }

private:
    [[nodiscard]] Status send(const std::string& command);
    [[nodiscard]] Status sendValue(const std::string& command, const std::string& value);
    [[nodiscard]] Result<std::string> query(const std::string& command, const CancelToken& cancel);

    [[nodiscard]] Status configureScan(const SweepParams& params);
    [[nodiscard]] Status configureMeter(const SweepParams& params);

    /// Block until the instrument answers a query again, which is how a
    /// finished scan announces itself. Polls so the token stays effective.
    [[nodiscard]] Status waitUntilResponsive(const CancelToken& cancel,
                                             std::chrono::milliseconds limit);

    [[nodiscard]] Result<Trace> fetchScanTrace(const CancelToken& cancel);
    [[nodiscard]] Result<Trace> fetchMeterReading(const CancelToken& cancel);

    DriverInfo m_info;
    Capabilities m_capabilities;
    TransportPtr m_transport;
    SweepParams m_params;
    InstrumentId m_identity;
    /// True when the configured acquisition is a meter dwell rather than a scan.
    bool m_meterMode{false};
    std::chrono::milliseconds m_timeout{5000};
    /// How long after the dwell the meter is given to produce its first real
    /// reading. Measured: peak takes about a second from the peak-hold reset,
    /// quasi-peak about two and a half. Generous, because overrunning it costs
    /// a failed point while cutting it short would report a level the detector
    /// had not finished forming.
    std::chrono::milliseconds m_meterSettleBudget{15000};
    std::atomic_bool m_abortRequested{false};
    CancelToken m_abortToken;
};

/// The EMI-mode driver for the UNI-T UTS3000T series.
[[nodiscard]] DriverPtr makeUnitrendFscanDriver();

} // namespace peakemi::drivers
