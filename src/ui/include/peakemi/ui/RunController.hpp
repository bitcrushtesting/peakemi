#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.hpp>
#include <peakemi/core/MeasurementEngine.hpp>
#include <peakemi/core/RunConfiguration.hpp>
#include <peakemi/core/Session.hpp>

#include <QObject>
#include <QString>

class QThread;

namespace peakemi::ui {

/// Owns the acquisition thread and everything that must not touch the GUI
/// thread: the measurement engine, the connected driver and its transport
/// (FR-THR-1). Widgets talk to this object only.
class RunController : public QObject
{
    Q_OBJECT

public:
    explicit RunController(QObject* parent = nullptr);
    ~RunController() override;

    /// Hands the driver and its transport to the worker thread. Any previous
    /// connection is closed first.
    void connectInstrument(DriverPtr driver, TransportPtr transport);
    void disconnectInstrument();

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] InstrumentId instrumentId() const { return m_instrument; }
    [[nodiscard]] Capabilities capabilities() const;

    void setConfiguration(RunConfiguration config);
    void setSessionMeta(SessionMeta meta);

    [[nodiscard]] const RunConfiguration& configuration() const { return m_config; }

public slots:
    void start();
    void pause();
    void resume();
    void abort();

    /// Raw SCPI console (FR-COM-6). Refused while a run is in progress, so the
    /// console can never interleave with the engine's own traffic.
    void sendRawCommand(const QString& command);

signals:
    void connectionChanged(bool connected);
    void instrumentIdentified(peakemi::InstrumentId identity);
    void phaseChanged(peakemi::MeasurementEngine::Phase phase);
    void traceAcquired(peakemi::TracePtr trace);
    void peaksFlagged(std::vector<peakemi::PeakCandidate> peaks);
    void pointMeasured(peakemi::MeasurementPoint point);
    void progress(int completed, int total, qint64 estimatedRemainingMs);
    void runFailed(peakemi::Error error);
    void runFinished(peakemi::Session session);
    void logMessage(QString message);
    void rawResponse(QString response);

private:
    QThread* m_thread{nullptr};
    MeasurementEngine* m_engine{nullptr};
    DriverPtr m_driver;
    TransportPtr m_transport;
    InstrumentId m_instrument;
    RunConfiguration m_config;
};

} // namespace peakemi::ui
