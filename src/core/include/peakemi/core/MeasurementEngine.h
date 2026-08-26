#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.h>
#include <peakemi/core/CancelToken.h>
#include <peakemi/core/LimitEvaluator.h>
#include <peakemi/core/MeasurementPoint.h>
#include <peakemi/core/PeakDetector.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/core/Session.h>

#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace peakemi {

/// Register the value types used in queued signal connections. Call once from
/// main() before any cross-thread connection is made.
void registerMetaTypes();

/// Owns the scan -> detect -> verify state machine (architecture.md 5).
///
/// Threading: the object lives on a dedicated worker thread and `start()` runs
/// the whole loop inside one invocation. `requestPause()`, `requestResume()` and
/// `requestAbort()` are therefore *not* slots — they are thread-safe methods
/// that flip an atomic the running loop polls, so they work while the worker's
/// event loop is busy (FR-THR-1/3, FR-RUN-5).
class MeasurementEngine : public QObject
{
    Q_OBJECT

public:
    enum class Phase : std::uint8_t
    {
        Idle,
        Configured,
        Phase1Sweep,
        PeakAnalysis,
        Phase2Dwell,
        Paused,
        Finished,
        Failed,
        Aborted
    };
    Q_ENUM(Phase)

    explicit MeasurementEngine(QObject* parent = nullptr);
    ~MeasurementEngine() override;

    /// Sends one operator-supplied command and reports what happened.
    ///
    /// The engine does not know how to reach auxiliary equipment and does not
    /// want to: whoever owns the transport supplies this, and the engine only
    /// decides when the commands are sent and what a failure means.
    using CommandSender = std::function<Status(const std::string& command)>;

    /// Both are read only while the engine is idle.
    void setDriver(DriverPtr driver);

    /// Without a sender, a configuration carrying commands fails the run rather
    /// than starting a measurement whose setup was never switched.
    void setCommandSender(CommandSender sender);
    void setConfiguration(RunConfiguration config);
    void setSessionMeta(SessionMeta meta);

    [[nodiscard]] Phase phase() const { return m_phase.load(); }

    [[nodiscard]] bool isRunning() const;

    /// Thread-safe run control; safe to call from the GUI thread mid-sweep.
    void requestPause();
    void requestResume();
    void requestAbort();

public slots:
    /// Runs the complete two-phase loop. Invoke queued on the worker thread.
    void start();

signals:
    void phaseChanged(peakemi::MeasurementEngine::Phase phase);
    void traceAcquired(peakemi::TracePtr trace);
    void peaksFlagged(std::vector<peakemi::PeakCandidate> peaks);
    void pointMeasured(peakemi::MeasurementPoint point);
    void progress(int completed, int total, qint64 estimatedRemainingMs);
    void runFailed(peakemi::Error error);
    void runFinished(peakemi::Session session);
    void logMessage(QString message);

private:
    /// Send the start or stop hooks, in order.
    [[nodiscard]] Status sendCommands(const std::vector<std::string>& commands,
                                      std::string_view phase);

    /// @return false when the run must stop (abort requested).
    [[nodiscard]] bool waitWhilePaused();
    [[nodiscard]] bool shouldStop() const;
    void setPhase(Phase phase);
    void emitLog(const QString& message);

    [[nodiscard]] Result<Trace> acquireSegment(const SweepParams& params);
    [[nodiscard]] Result<Trace> runPhase1();
    [[nodiscard]] Result<MeasurementPoint> verifyPeak(const PeakCandidate& candidate, int pass);
    void autosave(const Session& session);

    DriverPtr m_driver;
    CommandSender m_commandSender;
    RunConfiguration m_config;
    SessionMeta m_meta;
    LimitEvaluator m_evaluator;
    Capabilities m_capabilities;

    std::atomic<Phase> m_phase{Phase::Idle};
    std::atomic_bool m_running{false};
    std::atomic_bool m_pauseRequested{false};
    std::atomic_bool m_abortRequested{false};
    CancelToken m_cancel;

    std::mutex m_pauseMutex;
    std::condition_variable m_pauseCondition;
};

} // namespace peakemi

Q_DECLARE_METATYPE(peakemi::TracePtr)
Q_DECLARE_METATYPE(peakemi::MeasurementPoint)
Q_DECLARE_METATYPE(peakemi::Session)
Q_DECLARE_METATYPE(peakemi::Error)
Q_DECLARE_METATYPE(std::vector<peakemi::PeakCandidate>)
