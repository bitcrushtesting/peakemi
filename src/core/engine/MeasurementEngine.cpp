#include <peakemi/core/CisprBands.h>
#include <peakemi/core/CorrectionTable.h>
#include <peakemi/core/Logging.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/SessionSerializer.h>

#include <QElapsedTimer>
#include <QMetaType>

#include <algorithm>
#include <cmath>
#include <utility>

namespace peakemi {
namespace {

/// Stitch segment traces into one logical trace with an explicit axis, so a
/// segmented scan behaves exactly like a single wide sweep downstream.
[[nodiscard]] Trace stitch(const std::vector<Trace>& segments)
{
    Trace stitched;
    if (segments.empty()) {
        return stitched;
    }
    stitched = segments.front();
    stitched.axis.explicitPoints.clear();
    stitched.amplitudes.clear();

    for (const auto& segment : segments) {
        for (int i = 0; i < segment.size(); ++i) {
            const Hertz frequency = segment.axis.frequencyAt(i);
            // Segments share their edge frequency; keep the higher reading.
            if (!stitched.axis.explicitPoints.empty() &&
                stitched.axis.explicitPoints.back() == frequency)
            {
                stitched.amplitudes.back() = std::max(
                    stitched.amplitudes.back(), segment.amplitudes[static_cast<std::size_t>(i)]);
                continue;
            }
            stitched.axis.explicitPoints.push_back(frequency);
            stitched.amplitudes.push_back(segment.amplitudes[static_cast<std::size_t>(i)]);
        }
    }
    stitched.axis.start = stitched.axis.explicitPoints.front();
    stitched.axis.stop = stitched.axis.explicitPoints.back();
    stitched.axis.points = static_cast<int>(stitched.axis.explicitPoints.size());
    stitched.params.span = FrequencyRange{stitched.axis.start, stitched.axis.stop};
    return stitched;
}

[[nodiscard]] QString describe(const Error& error)
{
    return QString::fromStdString(error.message());
}

} // namespace

void registerMetaTypes()
{
    qRegisterMetaType<TracePtr>("peakemi::TracePtr");
    qRegisterMetaType<MeasurementPoint>("peakemi::MeasurementPoint");
    qRegisterMetaType<Session>("peakemi::Session");
    qRegisterMetaType<Error>("peakemi::Error");
    qRegisterMetaType<std::vector<PeakCandidate>>("std::vector<peakemi::PeakCandidate>");
    qRegisterMetaType<MeasurementEngine::Phase>("peakemi::MeasurementEngine::Phase");
}

MeasurementEngine::MeasurementEngine(QObject* parent) : QObject{parent} {}

MeasurementEngine::~MeasurementEngine()
{
    requestAbort();
}

void MeasurementEngine::setDriver(DriverPtr driver)
{
    m_driver = std::move(driver);
}

void MeasurementEngine::setConfiguration(RunConfiguration config)
{
    m_config = std::move(config);
    m_evaluator = LimitEvaluator{m_config.limits, m_config.marginalThresholdDb};
    setPhase(Phase::Configured);
}

void MeasurementEngine::setSessionMeta(SessionMeta meta)
{
    m_meta = std::move(meta);
}

bool MeasurementEngine::isRunning() const
{
    return m_running.load();
}

void MeasurementEngine::requestPause()
{
    if (!m_running.load()) {
        return;
    }
    m_pauseRequested.store(true);
}

void MeasurementEngine::requestResume()
{
    {
        const std::lock_guard<std::mutex> lock{m_pauseMutex};
        m_pauseRequested.store(false);
    }
    m_pauseCondition.notify_all();
}

void MeasurementEngine::requestAbort()
{
    m_abortRequested.store(true);
    m_cancel.cancel();
    if (m_driver) {
        m_driver->abort();
    }
    {
        const std::lock_guard<std::mutex> lock{m_pauseMutex};
        m_pauseRequested.store(false);
    }
    m_pauseCondition.notify_all();
}

void MeasurementEngine::setPhase(Phase phase)
{
    const Phase previous = m_phase.exchange(phase);
    if (previous != phase) {
        emit phaseChanged(phase);
    }
}

void MeasurementEngine::emitLog(const QString& message)
{
    qCInfo(lcEngine).noquote() << message;
    emit logMessage(message);
}

bool MeasurementEngine::shouldStop() const
{
    return m_abortRequested.load();
}

bool MeasurementEngine::waitWhilePaused()
{
    if (!m_pauseRequested.load() || m_abortRequested.load()) {
        return !m_abortRequested.load();
    }

    const Phase resumePhase = m_phase.load();
    setPhase(Phase::Paused);
    emitLog(tr("Run paused."));
    {
        std::unique_lock<std::mutex> lock{m_pauseMutex};
        m_pauseCondition.wait(
            lock, [this] { return !m_pauseRequested.load() || m_abortRequested.load(); });
    }
    if (m_abortRequested.load()) {
        return false;
    }
    setPhase(resumePhase);
    emitLog(tr("Run resumed."));
    return true;
}

Result<Trace> MeasurementEngine::acquireSegment(const SweepParams& requested)
{
    const SweepParams params = m_capabilities.coerce(requested);
    if (auto status = m_capabilities.validate(params); !status) {
        return std::unexpected(status.error());
    }

    Error lastError{ErrorCode::None, {}};
    for (int attempt = 0; attempt <= m_config.maxRetries; ++attempt) {
        if (shouldStop()) {
            return fail(ErrorCode::Cancelled, "run aborted");
        }
        if (attempt > 0) {
            emitLog(tr("Retrying sweep (attempt %1 of %2): %3")
                        .arg(attempt + 1)
                        .arg(m_config.maxRetries + 1)
                        .arg(describe(lastError)));
        }

        auto configured = m_driver->configureSweep(params);
        if (!configured) {
            lastError = configured.error();
            continue;
        }
        auto armed = m_driver->armAndTrigger(m_cancel);
        if (!armed) {
            lastError = armed.error();
            if (lastError.code == ErrorCode::Cancelled) {
                return std::unexpected(lastError);
            }
            continue;
        }
        auto trace = m_driver->fetchTrace(m_cancel);
        if (trace) {
            return trace;
        }
        lastError = trace.error();
        if (lastError.code == ErrorCode::Cancelled) {
            return std::unexpected(lastError);
        }
    }
    return std::unexpected(lastError);
}

Result<Trace> MeasurementEngine::runPhase1()
{
    setPhase(Phase::Phase1Sweep);

    const auto segments = m_config.planSegments();
    emitLog(tr("Phase 1: scanning %1 MHz to %2 MHz in %3 segment(s).")
                .arg(toMegahertz(m_config.span.start), 0, 'f', 3)
                .arg(toMegahertz(m_config.span.stop), 0, 'f', 3)
                .arg(segments.size()));

    std::vector<Trace> acquired;
    acquired.reserve(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (!waitWhilePaused()) {
            return fail(ErrorCode::Cancelled, "run aborted");
        }
        auto trace = acquireSegment(m_config.phase1SweepParams(segments[i]));
        if (!trace) {
            return trace;
        }
        acquired.push_back(std::move(*trace));
        emit progress(static_cast<int>(i + 1), static_cast<int>(segments.size()), -1);
    }

    Trace stitched = stitch(acquired);
    stitched.label = tr("Phase 1 peak scan").toStdString();
    return stitched;
}

Result<MeasurementPoint> MeasurementEngine::verifyPeak(const PeakCandidate& candidate, int pass)
{
    const SweepParams params = m_config.phase2SweepParams(candidate.frequency);
    auto trace = acquireSegment(params);
    if (!trace) {
        return std::unexpected(trace.error());
    }

    const double rawAmplitude = trace->maximumAmplitude();
    const auto applied = correctionsAt(candidate.frequency, m_config.corrections);
    const double correction = totalCorrectionAt(candidate.frequency, m_config.corrections);
    const double corrected = rawAmplitude + correction;
    const auto margin = m_evaluator.evaluate(candidate.frequency, corrected);

    MeasurementPoint point;
    point.frequency = candidate.frequency;
    point.rawAmplitude = rawAmplitude;
    point.correctedAmplitude = corrected;
    point.limitValue = margin.limit;
    point.marginDb = margin.marginDb;
    point.rawUnit = trace->unit;
    point.unit = resultingUnit(trace->unit, m_config.corrections);
    point.detector = params.detector;
    point.rbw = m_capabilities.coerce(params).rbw;
    point.vbw = params.vbw;
    point.dwell = m_config.dwellTime;
    point.corrections = applied;
    point.limitName =
        margin.limitIndex >= 0
            ? m_evaluator.limitLines()[static_cast<std::size_t>(margin.limitIndex)].name
            : std::string{};
    point.verdict = margin.verdict;
    point.measuredAt = std::chrono::system_clock::now();
    point.pass = pass;

    auto identity = m_driver->identify();
    if (identity) {
        point.instrument = *identity;
    }
    return point;
}

void MeasurementEngine::autosave(const Session& session)
{
    if (!m_config.autosave || m_config.autosavePath.empty()) {
        return;
    }
    const auto path = QString::fromStdString(m_config.autosavePath);
    if (auto status = SessionSerializer::save(session, path); !status) {
        emitLog(tr("Autosave to %1 failed: %2").arg(path, describe(status.error())));
    }
}

void MeasurementEngine::start()
{
    if (m_running.exchange(true)) {
        return; // already running
    }
    m_abortRequested.store(false);
    m_pauseRequested.store(false);
    m_cancel.reset();

    const auto finish = [this](Phase phase) {
        setPhase(phase);
        m_running.store(false);
    };

    if (!m_driver) {
        emit runFailed(Error{ErrorCode::NotConnected, "no instrument driver is connected"});
        finish(Phase::Failed);
        return;
    }
    if (auto status = m_config.validate(); !status) {
        emit runFailed(status.error());
        finish(Phase::Failed);
        return;
    }

    m_capabilities = m_driver->capabilities();
    m_driver->setTimeout(m_config.operationTimeout);
    m_evaluator = LimitEvaluator{m_config.limits, m_config.marginalThresholdDb};

    Session session = Session::createNew(m_meta.applicationVersion);
    // A run id supplied by the caller wins: the UI derives the autosave path
    // from it, and every export has to name the same run (FR-DAT-6).
    const std::string runId = m_meta.runId.empty() ? session.meta.runId : m_meta.runId;
    session.meta = m_meta;
    session.meta.runId = runId;
    session.meta.createdAt = std::chrono::system_clock::now();
    session.config = m_config;

    QElapsedTimer timer;
    timer.start();

    for (int pass = 1; pass <= m_config.passes; ++pass) {
        if (m_config.passes > 1) {
            emitLog(tr("Pass %1 of %2.").arg(pass).arg(m_config.passes));
        }

        auto scan = runPhase1();
        if (!scan) {
            const Error error = scan.error();
            if (error.code == ErrorCode::Cancelled) {
                emit runFinished(session);
                finish(Phase::Aborted);
                return;
            }
            emit runFailed(error);
            emit runFinished(session);
            finish(Phase::Failed);
            return;
        }

        Trace corrected = applyCorrections(*scan, m_config.corrections);
        corrected.label = tr("Phase 1 corrected (pass %1)").arg(pass).toStdString();
        session.traces.push_back(corrected);
        emit traceAcquired(makeTracePtr(corrected));

        setPhase(Phase::PeakAnalysis);
        auto peakSettings = m_config.peaks;
        peakSettings.marginThresholdDb = m_config.peaks.marginThresholdDb;
        const auto candidates = detectPeaks(corrected, m_evaluator, peakSettings);
        emit peaksFlagged(candidates);
        emitLog(
            tr("Phase 1 complete: %1 peak(s) flagged for verification.").arg(candidates.size()));

        if (candidates.empty()) {
            continue;
        }

        setPhase(Phase::Phase2Dwell);
        const int total = static_cast<int>(candidates.size());
        for (int i = 0; i < total; ++i) {
            if (!waitWhilePaused()) {
                autosave(session);
                emit runFinished(session);
                finish(Phase::Aborted);
                return;
            }
            const auto& candidate = candidates[static_cast<std::size_t>(i)];
            auto point = verifyPeak(candidate, pass);
            if (!point) {
                const Error error = point.error();
                autosave(session);
                if (error.code == ErrorCode::Cancelled) {
                    emit runFinished(session);
                    finish(Phase::Aborted);
                    return;
                }
                emit runFailed(error);
                emit runFinished(session);
                finish(Phase::Failed);
                return;
            }

            // Multi-pass keeps the worst case per frequency (FR-RUN-8).
            const auto existing = std::find_if(session.results.begin(),
                                               session.results.end(),
                                               [&](const MeasurementPoint& stored) {
                                                   return stored.frequency == point->frequency;
                                               });
            if (existing != session.results.end()) {
                if (point->correctedAmplitude > existing->correctedAmplitude) {
                    *existing = *point;
                }
            } else {
                session.results.push_back(*point);
            }

            emit pointMeasured(*point);
            emitLog(tr("%1 MHz: %2 dB margin (%3)")
                        .arg(toMegahertz(point->frequency), 0, 'f', 4)
                        .arg(point->marginDb, 0, 'f', 1)
                        .arg(QString::fromUtf8(
                            verdictKey(point->verdict).data(),
                            static_cast<qsizetype>(verdictKey(point->verdict).size()))));

            const qint64 elapsed = timer.elapsed();
            const qint64 remaining = i + 1 > 0 ? (elapsed / (i + 1)) * (total - i - 1) : -1;
            emit progress(i + 1, total, remaining);

            // Every Phase 2 point is a crash-safe checkpoint (FR-APP-4).
            autosave(session);
        }
    }

    session.meta.modifiedAt = std::chrono::system_clock::now();
    autosave(session);
    emitLog(tr("Run finished: %1 verified point(s) in %2 s.")
                .arg(session.results.size())
                .arg(static_cast<double>(timer.elapsed()) / 1000.0, 0, 'f', 1));
    emit runFinished(session);
    finish(Phase::Finished);
}

} // namespace peakemi
