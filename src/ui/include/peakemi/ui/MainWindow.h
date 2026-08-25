#pragma once

#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/PeakDetector.h>
#include <peakemi/core/Session.h>

#include <QMainWindow>
#include <QString>

class QAction;
class QLabel;
class QProgressBar;

namespace peakemi::ui {

class InstrumentDock;
class LogDock;
class PainterPlotBackend;
class ResultsDock;
class RunConfigDock;
class RunController;

/// Application main window: the Widgets shell of ADR-1.
///
/// It wires the docks to the run controller and owns the current session. It
/// contains no measurement logic — everything it does is translate a user
/// action into a controller command and a controller signal into a view update.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] const Session& session() const { return m_session; }

    /// Loads a session file at start-up, e.g. from the command line.
    void openSessionFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newSession();
    void openSession();
    bool saveSession();
    bool saveSessionAs();
    void editSessionMetadata();

    void exportResultsCsv();
    void exportTraceCsv();
    void exportPlotImage();
    void exportPdfReport();

    void onConnectRequested(peakemi::TransportDescriptor descriptor, QString driverId);
    void onConnectionChanged(bool connected);
    void onPhaseChanged(peakemi::MeasurementEngine::Phase phase);
    void onTraceAcquired(peakemi::TracePtr trace);
    void onPeaksFlagged(std::vector<peakemi::PeakCandidate> peaks);
    void onPointMeasured(peakemi::MeasurementPoint point);
    void onProgress(int completed, int total, qint64 estimatedRemainingMs);
    void onRunFailed(peakemi::Error error);
    void onRunFinished(peakemi::Session session);

    void startRun();
    void togglePause();
    void abortRun();

    void showAboutDialog();

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createDocks();
    void createStatusBar();
    void restoreLayout();
    void saveLayout();

    void updateActionStates();
    void updateWindowTitle();
    void showLimitOverlays();
    void refreshMarkers();
    void log(const QString& message);
    [[nodiscard]] bool confirmDiscardChanges();
    [[nodiscard]] QString autosavePathFor(const QString& runId) const;

    RunController* m_controller{nullptr};
    PainterPlotBackend* m_plot{nullptr};

    InstrumentDock* m_instrumentDock{nullptr};
    RunConfigDock* m_configDock{nullptr};
    ResultsDock* m_resultsDock{nullptr};
    LogDock* m_logDock{nullptr};

    QAction* m_newAction{nullptr};
    QAction* m_openAction{nullptr};
    QAction* m_saveAction{nullptr};
    QAction* m_saveAsAction{nullptr};
    QAction* m_metadataAction{nullptr};
    QAction* m_exportResultsAction{nullptr};
    QAction* m_exportTraceAction{nullptr};
    QAction* m_exportPlotAction{nullptr};
    QAction* m_reportAction{nullptr};
    QAction* m_quitAction{nullptr};
    QAction* m_startAction{nullptr};
    QAction* m_pauseAction{nullptr};
    QAction* m_abortAction{nullptr};
    QAction* m_disconnectAction{nullptr};
    QAction* m_logFrequencyAction{nullptr};
    QAction* m_autoScaleAction{nullptr};
    QAction* m_aboutAction{nullptr};
    QAction* m_aboutQtAction{nullptr};

    QLabel* m_instrumentStatus{nullptr};
    QLabel* m_cursorStatus{nullptr};
    QLabel* m_phaseStatus{nullptr};
    QProgressBar* m_progress{nullptr};

    std::vector<PeakCandidate> m_peaks;
    Hertz m_selectedFrequency{0};
    Session m_session;
    QString m_sessionPath;
    bool m_dirty{false};
    bool m_connected{false};
    MeasurementEngine::Phase m_phase{MeasurementEngine::Phase::Idle};
};

} // namespace peakemi::ui
