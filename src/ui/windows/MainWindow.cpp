#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/LimitEvaluator.h>
#include <peakemi/core/Logging.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/core/Version.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/hal/DriverRegistry.h>
#include <peakemi/hal/SerialScpiTransport.h>
#include <peakemi/hal/TcpScpiTransport.h>
#include <peakemi/hal/UsbTmcTransport.h>
#include <peakemi/hal/VisaTransport.h>
#include <peakemi/hal/Vxi11Transport.h>
#include <peakemi/reporting/CsvExporter.h>
#include <peakemi/reporting/PdfReportRenderer.h>
#include <peakemi/ui/InstrumentDock.h>
#include <peakemi/ui/LogDock.h>
#include <peakemi/ui/MainWindow.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/RunConfigDock.h>
#include <peakemi/ui/RunController.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <cmath>

namespace peakemi::ui {
namespace {

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] QString phaseName(MeasurementEngine::Phase phase)
{
    switch (phase) {
        case MeasurementEngine::Phase::Idle:
            return QObject::tr("Idle");
        case MeasurementEngine::Phase::Configured:
            return QObject::tr("Configured");
        case MeasurementEngine::Phase::Phase1Sweep:
            return QObject::tr("Phase 1 — scanning");
        case MeasurementEngine::Phase::PeakAnalysis:
            return QObject::tr("Analysing peaks");
        case MeasurementEngine::Phase::Phase2Dwell:
            return QObject::tr("Phase 2 — verifying");
        case MeasurementEngine::Phase::Paused:
            return QObject::tr("Paused");
        case MeasurementEngine::Phase::Finished:
            return QObject::tr("Finished");
        case MeasurementEngine::Phase::Failed:
            return QObject::tr("Failed");
        case MeasurementEngine::Phase::Aborted:
            return QObject::tr("Aborted");
    }
    return {};
}

[[nodiscard]] QVector<QPointF> toPoints(const Trace& trace)
{
    QVector<QPointF> points;
    points.reserve(trace.size());
    for (int i = 0; i < trace.size(); ++i) {
        points.append(QPointF{static_cast<double>(trace.axis.frequencyAt(i).value()),
                              trace.amplitudes[static_cast<std::size_t>(i)]});
    }
    return points;
}

/// Sample a limit line densely enough that a log axis stays smooth.
[[nodiscard]] QVector<QPointF> toPoints(const LimitLine& limit, FrequencyRange span)
{
    QVector<QPointF> points;
    const auto coverage = limit.coverage();
    const auto start = std::max(coverage.start, span.start);
    const auto stop = std::min(coverage.stop, span.stop);
    if (stop <= start) {
        return points;
    }

    constexpr int Samples = 400;
    for (int i = 0; i <= Samples; ++i) {
        const double fraction = static_cast<double>(i) / Samples;
        const auto frequency = start + Hertz{static_cast<std::int64_t>(
                                           fraction * static_cast<double>((stop - start).value()))};
        const double value = limit.evaluateAt(frequency);
        if (std::isfinite(value)) {
            points.append(QPointF{static_cast<double>(frequency.value()), value});
        }
    }
    return points;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow{parent}
{
    setObjectName(QStringLiteral("mainWindow"));
    resize(1400, 900);

    m_controller = new RunController{this};
    m_session = Session::createNew(std::string{ProjectVersion});

    m_plot = new PainterPlotBackend{this};
    setCentralWidget(m_plot);

    createActions();
    createMenus();
    createToolBar();
    createDocks();
    createStatusBar();
    restoreLayout();

    connect(
        m_controller, &RunController::connectionChanged, this, &MainWindow::onConnectionChanged);
    connect(m_controller, &RunController::phaseChanged, this, &MainWindow::onPhaseChanged);
    connect(m_controller, &RunController::traceAcquired, this, &MainWindow::onTraceAcquired);
    connect(m_controller, &RunController::peaksFlagged, this, &MainWindow::onPeaksFlagged);
    connect(m_controller, &RunController::pointMeasured, this, &MainWindow::onPointMeasured);
    connect(m_controller, &RunController::progress, this, &MainWindow::onProgress);
    connect(m_controller, &RunController::runFailed, this, &MainWindow::onRunFailed);
    connect(m_controller, &RunController::runFinished, this, &MainWindow::onRunFinished);
    connect(m_controller, &RunController::logMessage, this, &MainWindow::log);
    connect(m_controller, &RunController::rawResponse, this, [this](const QString& response) {
        m_logDock->appendConsole(response);
    });
    connect(m_controller,
            &RunController::instrumentIdentified,
            this,
            [this](const InstrumentId& identity) {
                m_instrumentStatus->setText(QString::fromStdString(identity.displayName()));
                log(tr("Connected to %1").arg(QString::fromStdString(identity.displayName())));
            });

    connect(
        m_plot, &PainterPlotBackend::cursorMoved, this, [this](double frequency, double amplitude) {
            m_cursorStatus->setText(tr("%1 MHz    %2 %3")
                                        .arg(frequency / 1e6, 0, 'f', 4)
                                        .arg(amplitude, 0, 'f', 1)
                                        .arg(qs(amplitudeUnitKey(AmplitudeUnit::dBuV))));
        });

    m_session.config = m_configDock->configuration();
    m_controller->setConfiguration(m_session.config);
    updateActionStates();
    updateWindowTitle();
    log(tr("PeakEmi %1 started. Connect the simulated analyzer to try a run without hardware.")
            .arg(qs(ProjectVersion)));
}

MainWindow::~MainWindow()
{
    // The run controller is a child object, so it is still alive when ~QObject
    // tears the children down — and its own destructor emits connectionChanged
    // while disconnecting the instrument. By then the MainWindow half of this
    // object is gone, and delivering that signal would call into a destroyed
    // receiver. Sever the wiring first, then stop the run.
    m_controller->disconnect(this);
    m_controller->abort();
}

void MainWindow::createActions()
{
    m_newAction = new QAction{tr("&New session"), this};
    m_newAction->setObjectName(QStringLiteral("actionNewSession"));
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newSession);

    m_openAction = new QAction{tr("&Open session…"), this};
    m_openAction->setObjectName(QStringLiteral("actionOpenSession"));
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openSession);

    m_saveAction = new QAction{tr("&Save session"), this};
    m_saveAction->setObjectName(QStringLiteral("actionSaveSession"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this] { (void)saveSession(); });

    m_saveAsAction = new QAction{tr("Save session &as…"), this};
    m_saveAsAction->setObjectName(QStringLiteral("actionSaveSessionAs"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, [this] { (void)saveSessionAs(); });

    m_metadataAction = new QAction{tr("EUT and operator &details…"), this};
    m_metadataAction->setObjectName(QStringLiteral("actionSessionMetadata"));
    connect(m_metadataAction, &QAction::triggered, this, &MainWindow::editSessionMetadata);

    m_exportResultsAction = new QAction{tr("Export &results as CSV…"), this};
    m_exportResultsAction->setObjectName(QStringLiteral("actionExportResults"));
    connect(m_exportResultsAction, &QAction::triggered, this, &MainWindow::exportResultsCsv);

    m_exportTraceAction = new QAction{tr("Export &trace as CSV…"), this};
    m_exportTraceAction->setObjectName(QStringLiteral("actionExportTrace"));
    connect(m_exportTraceAction, &QAction::triggered, this, &MainWindow::exportTraceCsv);

    m_exportPlotAction = new QAction{tr("Export &plot as PNG or SVG…"), this};
    m_exportPlotAction->setObjectName(QStringLiteral("actionExportPlot"));
    connect(m_exportPlotAction, &QAction::triggered, this, &MainWindow::exportPlotImage);

    m_reportAction = new QAction{tr("Generate PDF re&port…"), this};
    m_reportAction->setObjectName(QStringLiteral("actionReport"));
    connect(m_reportAction, &QAction::triggered, this, &MainWindow::exportPdfReport);

    m_quitAction = new QAction{tr("&Quit"), this};
    m_quitAction->setObjectName(QStringLiteral("actionQuit"));
    m_quitAction->setShortcut(QKeySequence::Quit);
    m_quitAction->setMenuRole(QAction::QuitRole);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_startAction = new QAction{tr("&Start run"), this};
    m_startAction->setObjectName(QStringLiteral("actionStartRun"));
    m_startAction->setShortcut(QKeySequence{Qt::Key_F5});
    connect(m_startAction, &QAction::triggered, this, &MainWindow::startRun);

    m_pauseAction = new QAction{tr("&Pause run"), this};
    m_pauseAction->setObjectName(QStringLiteral("actionPauseRun"));
    m_pauseAction->setShortcut(QKeySequence{Qt::Key_F6});
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::togglePause);

    m_abortAction = new QAction{tr("&Abort run"), this};
    m_abortAction->setObjectName(QStringLiteral("actionAbortRun"));
    m_abortAction->setShortcut(QKeySequence{Qt::Key_Escape});
    connect(m_abortAction, &QAction::triggered, this, &MainWindow::abortRun);

    m_disconnectAction = new QAction{tr("&Disconnect instrument"), this};
    m_disconnectAction->setObjectName(QStringLiteral("actionDisconnect"));
    connect(m_disconnectAction, &QAction::triggered, this, [this] {
        m_controller->disconnectInstrument();
    });

    m_logFrequencyAction = new QAction{tr("&Logarithmic frequency axis"), this};
    m_logFrequencyAction->setObjectName(QStringLiteral("actionLogFrequencyAxis"));
    m_logFrequencyAction->setCheckable(true);
    connect(m_logFrequencyAction, &QAction::toggled, this, [this](bool logarithmic) {
        m_plot->setLogarithmicFrequency(logarithmic);
    });

    m_autoScaleAction = new QAction{tr("&Auto scale plot"), this};
    m_autoScaleAction->setObjectName(QStringLiteral("actionAutoScale"));
    m_autoScaleAction->setShortcut(QKeySequence{Qt::Key_Home});
    connect(m_autoScaleAction, &QAction::triggered, this, [this] { m_plot->autoScale(); });

    m_aboutAction = new QAction{tr("&About PeakEmi"), this};
    m_aboutAction->setObjectName(QStringLiteral("actionAbout"));
    m_aboutAction->setMenuRole(QAction::AboutRole);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    m_aboutQtAction = new QAction{tr("About &Qt"), this};
    m_aboutQtAction->setObjectName(QStringLiteral("actionAboutQt"));
    m_aboutQtAction->setMenuRole(QAction::AboutQtRole);
    connect(m_aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::createMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_metadataAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportResultsAction);
    fileMenu->addAction(m_exportTraceAction);
    fileMenu->addAction(m_exportPlotAction);
    fileMenu->addAction(m_reportAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    auto* runMenu = menuBar()->addMenu(tr("&Run"));
    runMenu->addAction(m_startAction);
    runMenu->addAction(m_pauseAction);
    runMenu->addAction(m_abortAction);
    runMenu->addSeparator();
    runMenu->addAction(m_disconnectAction);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_logFrequencyAction);
    viewMenu->addAction(m_autoScaleAction);
    viewMenu->addSeparator();

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(m_aboutAction);
    helpMenu->addAction(m_aboutQtAction);
}

void MainWindow::createToolBar()
{
    auto* toolBar = addToolBar(tr("Run"));
    toolBar->setObjectName(QStringLiteral("runToolBar"));
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->addAction(m_startAction);
    toolBar->addAction(m_pauseAction);
    toolBar->addAction(m_abortAction);
    toolBar->addSeparator();
    toolBar->addAction(m_autoScaleAction);
    toolBar->addAction(m_reportAction);
}

void MainWindow::createDocks()
{
    m_instrumentDock = new InstrumentDock{this};
    addDockWidget(Qt::LeftDockWidgetArea, m_instrumentDock);
    connect(
        m_instrumentDock, &InstrumentDock::connectRequested, this, &MainWindow::onConnectRequested);
    connect(m_instrumentDock, &InstrumentDock::disconnectRequested, this, [this] {
        m_controller->disconnectInstrument();
    });
    connect(m_instrumentDock, &InstrumentDock::statusMessage, this, &MainWindow::log);

    m_configDock = new RunConfigDock{this};
    addDockWidget(Qt::LeftDockWidgetArea, m_configDock);
    connect(m_configDock, &RunConfigDock::configurationChanged, this, [this] {
        m_session.config = m_configDock->configuration();
        m_controller->setConfiguration(m_session.config);
        showLimitOverlays();
        m_dirty = true;
        updateWindowTitle();
    });

    m_resultsDock = new ResultsDock{this};
    addDockWidget(Qt::BottomDockWidgetArea, m_resultsDock);
    connect(m_resultsDock, &ResultsDock::pointSelected, this, [this](Hertz frequency) {
        m_selectedFrequency = frequency;
        refreshMarkers();
    });

    m_logDock = new LogDock{this};
    addDockWidget(Qt::BottomDockWidgetArea, m_logDock);
    connect(m_logDock, &LogDock::commandEntered, m_controller, &RunController::sendRawCommand);

    tabifyDockWidget(m_resultsDock, m_logDock);
    m_resultsDock->raise();

    auto* viewMenu = menuBar()->findChild<QMenu*>();
    Q_UNUSED(viewMenu)
    for (auto* action : menuBar()->actions()) {
        if (action->menu() != nullptr && action->text() == tr("&View")) {
            action->menu()->addAction(m_instrumentDock->toggleViewAction());
            action->menu()->addAction(m_configDock->toggleViewAction());
            action->menu()->addAction(m_resultsDock->toggleViewAction());
            action->menu()->addAction(m_logDock->toggleViewAction());
        }
    }
}

void MainWindow::createStatusBar()
{
    m_instrumentStatus = new QLabel{tr("No instrument connected"), this};
    m_phaseStatus = new QLabel{phaseName(MeasurementEngine::Phase::Idle), this};
    m_cursorStatus = new QLabel{QString{}, this};
    m_progress = new QProgressBar{this};
    m_progress->setMaximumWidth(220);
    m_progress->setVisible(false);

    statusBar()->addWidget(m_instrumentStatus, 1);
    statusBar()->addPermanentWidget(m_cursorStatus);
    statusBar()->addPermanentWidget(m_phaseStatus);
    statusBar()->addPermanentWidget(m_progress);
}

void MainWindow::restoreLayout()
{
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
    m_logFrequencyAction->setChecked(
        settings.value(QStringLiteral("plot/logarithmicFrequency"), false).toBool());
}

void MainWindow::saveLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
    settings.setValue(QStringLiteral("plot/logarithmicFrequency"),
                      m_logFrequencyAction->isChecked());
}

void MainWindow::log(const QString& message)
{
    m_logDock->appendLog(message);
    statusBar()->showMessage(message, 6000);
}

void MainWindow::updateActionStates()
{
    const bool running = m_phase == MeasurementEngine::Phase::Phase1Sweep ||
                         m_phase == MeasurementEngine::Phase::PeakAnalysis ||
                         m_phase == MeasurementEngine::Phase::Phase2Dwell ||
                         m_phase == MeasurementEngine::Phase::Paused;

    m_startAction->setEnabled(m_connected && !running);
    m_pauseAction->setEnabled(running);
    m_pauseAction->setText(m_phase == MeasurementEngine::Phase::Paused ? tr("Resu&me run")
                                                                       : tr("&Pause run"));
    m_abortAction->setEnabled(running);
    m_disconnectAction->setEnabled(m_connected && !running);
    m_configDock->setEditingEnabled(!running);
    m_instrumentDock->setConnected(m_connected);

    const bool hasResults = !m_session.results.empty();
    m_exportResultsAction->setEnabled(hasResults);
    m_reportAction->setEnabled(hasResults || !m_session.traces.empty());
    m_exportTraceAction->setEnabled(!m_session.traces.empty());
}

void MainWindow::updateWindowTitle()
{
    const QString name =
        m_sessionPath.isEmpty() ? tr("Untitled session") : QFileInfo{m_sessionPath}.fileName();
    setWindowTitle(tr("%1%2 — PeakEmi %3")
                       .arg(name, m_dirty ? QStringLiteral("*") : QString{}, qs(ProjectVersion)));
}

void MainWindow::showLimitOverlays()
{
    for (const auto& limit : m_session.config.limits) {
        PlotSeries series;
        series.id = QStringLiteral("limit:%1").arg(QString::fromStdString(limit.name));
        series.label = QString::fromStdString(limit.name);
        series.colour = QColor{0xC0, 0x39, 0x2B};
        series.style = PlotStyle::Dashed;
        series.width = 1.5;
        series.points = toPoints(limit, m_session.config.span);
        m_plot->setSeries(series);
    }
    m_plot->setAmplitudeLabel(m_session.config.limits.empty()
                                  ? qs(amplitudeUnitKey(AmplitudeUnit::dBuV))
                                  : qs(amplitudeUnitKey(m_session.config.limits.front().unit)));
}

QString MainWindow::autosavePathFor(const QString& runId) const
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           QStringLiteral("/autosave");
    QDir{}.mkpath(directory);
    return directory + QStringLiteral("/run-") + runId + QStringLiteral(".peakemi.json");
}

void MainWindow::onConnectRequested(const TransportDescriptor& descriptor, const QString& driverId)
{
    TransportPtr transport;
    DriverPtr driver;

    switch (descriptor.kind) {
        case TransportKind::Simulated: {
            auto simulated = std::make_shared<drivers::SimulatedDriver>();
            auto config = simulated->config();
            config.timeScale = 0.15; // lifelike pacing without a 20 minute demo
            simulated->setConfig(config);
            driver = simulated;
            break;
        }
        case TransportKind::Tcp:
            transport = std::make_shared<hal::TcpScpiTransport>(descriptor);
            break;
        case TransportKind::Vxi11:
            transport = std::make_shared<hal::Vxi11Transport>(descriptor);
            break;
        case TransportKind::Serial:
            transport = std::make_shared<hal::SerialScpiTransport>(descriptor);
            break;
        case TransportKind::UsbTmc:
            if (!hal::UsbTmcTransport::isSupported()) {
                QMessageBox::information(
                    this,
                    tr("USB support not built in"),
                    tr("This build has no USBTMC transport. Configure PeakEmi with "
                       "-DPEAKEMI_WITH_USBTMC=ON to talk to USB instruments."));
                return;
            }
            transport = std::make_shared<hal::UsbTmcTransport>(descriptor);
            break;
        case TransportKind::Visa:
            if (!hal::VisaTransport::isAvailable()) {
                QMessageBox::information(
                    this,
                    tr("No VISA runtime"),
                    tr("No VISA runtime was found on this machine. PeakEmi talks to "
                       "instruments directly over TCP, VXI-11, USB and serial without it."));
                return;
            }
            transport = std::make_shared<hal::VisaTransport>(descriptor);
            break;
    }

    if (!driver) {
        auto& registry = hal::DriverRegistry::instance();
        auto created = driverId.isEmpty() ? registry.createBestMatch(InstrumentId{})
                                          : registry.create(driverId.toStdString());
        if (!created && driverId.isEmpty()) {
            // No identity yet: probe the endpoint, then match on the response.
            auto identity = hal::identifyEndpoint(descriptor, descriptor.defaultTimeout);
            if (!identity) {
                QMessageBox::warning(this,
                                     tr("Connection failed"),
                                     tr("%1 did not answer *IDN?:\n%2")
                                         .arg(QString::fromStdString(descriptor.displayName()),
                                              QString::fromStdString(identity.error().message())));
                return;
            }
            created = registry.createBestMatch(*identity);
            if (!created) {
                QMessageBox::warning(this,
                                     tr("Select a driver"),
                                     tr("%1\n\nChoose a driver manually in the instrument dock.")
                                         .arg(QString::fromStdString(created.error().message())));
                return;
            }
        } else if (!created) {
            QMessageBox::warning(
                this, tr("Driver unavailable"), QString::fromStdString(created.error().message()));
            return;
        }
        driver = *created;
    }

    m_controller->connectInstrument(driver, transport);
}

void MainWindow::onConnectionChanged(bool connected)
{
    m_connected = connected;
    if (connected) {
        m_configDock->applyCapabilities(m_controller->capabilities());
    } else {
        m_instrumentStatus->setText(tr("No instrument connected"));
    }
    updateActionStates();
}

void MainWindow::onPhaseChanged(MeasurementEngine::Phase phase)
{
    m_phase = phase;
    m_phaseStatus->setText(phaseName(phase));
    updateActionStates();
}

void MainWindow::onTraceAcquired(const TracePtr& trace)
{
    if (!trace) {
        return;
    }
    m_session.traces.push_back(*trace);

    PlotSeries series;
    series.id = QStringLiteral("trace:live");
    series.label = tr("Phase 1 scan");
    series.colour = QColor{0x1A, 0x73, 0xE8};
    series.points = toPoints(*trace);
    m_plot->setSeries(series);
    m_plot->setAmplitudeLabel(qs(amplitudeUnitKey(trace->unit)));
    m_plot->autoScale();

    m_dirty = true;
    updateActionStates();
    updateWindowTitle();
}

void MainWindow::onPeaksFlagged(std::vector<PeakCandidate> peaks)
{
    m_peaks = std::move(peaks);
    refreshMarkers();
    if (!m_peaks.empty()) {
        log(tr("%n peak(s) flagged for Phase 2 verification.",
               nullptr,
               static_cast<int>(m_peaks.size())));
    }
}

void MainWindow::refreshMarkers()
{
    QVector<PlotMarker> markers;
    markers.reserve(static_cast<qsizetype>(m_peaks.size()));
    for (const auto& peak : m_peaks) {
        markers.append(PlotMarker{
            .label = QStringLiteral("%1").arg(toMegahertz(peak.frequency), 0, 'f', 2),
            .position = QPointF{static_cast<double>(peak.frequency.value()), peak.amplitude},
            .colour =
                peak.verdict == Verdict::Fail ? QColor{0xC0, 0x39, 0x2B} : QColor{0xF9, 0xAB, 0x00},
            .selected = peak.frequency == m_selectedFrequency});
    }
    m_plot->setMarkers(markers);
}

void MainWindow::onPointMeasured(const MeasurementPoint& point)
{
    m_resultsDock->appendPoint(point);

    // Verified points are drawn on top of the scan as discrete markers.
    PlotSeries verified;
    verified.id = QStringLiteral("phase2");
    verified.label = tr("Verified points");
    verified.colour = QColor{0x0B, 0x80, 0x43};
    verified.style = PlotStyle::Points;
    for (const auto& stored : m_resultsDock->points()) {
        verified.points.append(
            QPointF{static_cast<double>(stored.frequency.value()), stored.correctedAmplitude});
    }
    m_plot->setSeries(verified);

    m_dirty = true;
    updateActionStates();
    updateWindowTitle();
}

void MainWindow::onProgress(int completed, int total, qint64 estimatedRemainingMs)
{
    m_progress->setVisible(total > 0);
    m_progress->setRange(0, std::max(total, 1));
    m_progress->setValue(completed);
    if (estimatedRemainingMs > 0) {
        m_progress->setFormat(
            tr("%p%  ~%1 s left")
                .arg(static_cast<double>(estimatedRemainingMs) / 1000.0, 0, 'f', 0));
    } else {
        m_progress->setFormat(QStringLiteral("%p%"));
    }
}

void MainWindow::onRunFailed(const Error& error)
{
    m_progress->setVisible(false);
    const auto message = QString::fromStdString(error.message());
    log(tr("Run failed: %1").arg(message));
    QMessageBox::warning(this, tr("Run failed"), message);
}

void MainWindow::onRunFinished(const Session& session)
{
    m_progress->setVisible(false);
    m_session.results = session.results;
    m_session.meta = session.meta;
    m_resultsDock->setPoints(session.results);
    m_dirty = true;
    updateActionStates();
    updateWindowTitle();
}

void MainWindow::startRun()
{
    m_session.config = m_configDock->configuration();
    if (auto status = m_session.config.validate(); !status) {
        QMessageBox::warning(
            this, tr("Configuration incomplete"), QString::fromStdString(status.error().message()));
        return;
    }
    if (m_session.config.limits.empty() &&
        QMessageBox::question(this,
                              tr("No limit line selected"),
                              tr("Without a limit line no peak can be flagged and Phase 2 "
                                 "will not run. Continue anyway?")) != QMessageBox::Yes)
    {
        return;
    }

    if (m_session.meta.runId.empty()) {
        m_session.meta = Session::createNew(std::string{ProjectVersion}).meta;
    }
    m_session.config.autosave = true;
    m_session.config.autosavePath =
        autosavePathFor(QString::fromStdString(m_session.meta.runId)).toStdString();

    m_resultsDock->clear();
    m_session.results.clear();
    m_peaks.clear();
    m_selectedFrequency = Hertz{0};
    m_plot->setMarkers({});
    m_plot->removeSeries(QStringLiteral("phase2"));
    showLimitOverlays();

    m_controller->setConfiguration(m_session.config);
    m_controller->setSessionMeta(m_session.meta);
    m_controller->start();
    log(tr("Run started."));
}

void MainWindow::togglePause()
{
    if (m_phase == MeasurementEngine::Phase::Paused) {
        m_controller->resume();
    } else {
        m_controller->pause();
    }
}

void MainWindow::abortRun()
{
    if (m_phase != MeasurementEngine::Phase::Phase1Sweep &&
        m_phase != MeasurementEngine::Phase::Phase2Dwell &&
        m_phase != MeasurementEngine::Phase::PeakAnalysis &&
        m_phase != MeasurementEngine::Phase::Paused)
    {
        return;
    }
    if (QMessageBox::question(this,
                              tr("Abort run"),
                              tr("Abort the running measurement? Results collected so far are "
                                 "kept.")) != QMessageBox::Yes)
    {
        return;
    }
    m_controller->abort();
    log(tr("Abort requested."));
}

void MainWindow::newSession()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    m_session = Session::createNew(std::string{ProjectVersion});
    m_session.config = m_configDock->configuration();
    m_sessionPath.clear();
    m_dirty = false;
    m_resultsDock->clear();
    m_plot->clearSeries();
    showLimitOverlays();
    updateActionStates();
    updateWindowTitle();
}

void MainWindow::openSession()
{
    if (!confirmDiscardChanges()) {
        return;
    }
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open session"), {}, tr("PeakEmi sessions (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        openSessionFile(path);
    }
}

void MainWindow::openSessionFile(const QString& path)
{
    auto session = SessionSerializer::load(path);
    if (!session) {
        QMessageBox::warning(
            this, tr("Cannot open session"), QString::fromStdString(session.error().message()));
        return;
    }

    m_session = std::move(*session);
    m_sessionPath = path;
    m_dirty = false;

    m_configDock->setConfiguration(m_session.config);
    m_resultsDock->setPoints(m_session.results);
    m_plot->clearSeries();
    showLimitOverlays();
    if (!m_session.traces.empty()) {
        const auto& trace = m_session.traces.back();
        PlotSeries series;
        series.id = QStringLiteral("trace:live");
        series.label = QString::fromStdString(trace.label);
        series.colour = QColor{0x1A, 0x73, 0xE8};
        series.points = toPoints(trace);
        m_plot->setSeries(series);
        m_plot->setAmplitudeLabel(qs(amplitudeUnitKey(trace.unit)));
        m_plot->autoScale();
    }
    updateActionStates();
    updateWindowTitle();
    log(tr("Opened %1").arg(path));
}

bool MainWindow::saveSession()
{
    if (m_sessionPath.isEmpty()) {
        return saveSessionAs();
    }
    m_session.meta.modifiedAt = std::chrono::system_clock::now();
    if (auto status = SessionSerializer::save(m_session, m_sessionPath); !status) {
        QMessageBox::warning(
            this, tr("Cannot save session"), QString::fromStdString(status.error().message()));
        return false;
    }
    m_dirty = false;
    updateWindowTitle();
    log(tr("Session saved to %1").arg(m_sessionPath));
    return true;
}

bool MainWindow::saveSessionAs()
{
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Save session"), QStringLiteral("session.json"), tr("PeakEmi sessions (*.json)"));
    if (path.isEmpty()) {
        return false;
    }
    m_sessionPath = path;
    return saveSession();
}

void MainWindow::editSessionMetadata()
{
    QDialog dialog{this};
    dialog.setWindowTitle(tr("EUT and operator details"));
    auto* form = new QFormLayout{&dialog};

    auto* eutName = new QLineEdit{QString::fromStdString(m_session.meta.eutName), &dialog};
    auto* eutSerial = new QLineEdit{QString::fromStdString(m_session.meta.eutSerial), &dialog};
    auto* mode = new QLineEdit{QString::fromStdString(m_session.meta.eutOperatingMode), &dialog};
    auto* setup = new QLineEdit{QString::fromStdString(m_session.meta.testSetup), &dialog};
    auto* operatorName =
        new QLineEdit{QString::fromStdString(m_session.meta.operatorName), &dialog};
    auto* company = new QLineEdit{QString::fromStdString(m_session.meta.company), &dialog};
    auto* notes = new QPlainTextEdit{QString::fromStdString(m_session.meta.notes), &dialog};

    form->addRow(tr("Equipment under test"), eutName);
    form->addRow(tr("Serial number"), eutSerial);
    form->addRow(tr("Operating mode"), mode);
    form->addRow(tr("Test setup"), setup);
    form->addRow(tr("Operator"), operatorName);
    form->addRow(tr("Company"), company);
    form->addRow(tr("Notes"), notes);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_session.meta.eutName = eutName->text().toStdString();
    m_session.meta.eutSerial = eutSerial->text().toStdString();
    m_session.meta.eutOperatingMode = mode->text().toStdString();
    m_session.meta.testSetup = setup->text().toStdString();
    m_session.meta.operatorName = operatorName->text().toStdString();
    m_session.meta.company = company->text().toStdString();
    m_session.meta.notes = notes->toPlainText().toStdString();
    m_controller->setSessionMeta(m_session.meta);
    m_dirty = true;
    updateWindowTitle();
}

void MainWindow::exportResultsCsv()
{
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export results"), QStringLiteral("results.csv"), tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    m_session.results = m_resultsDock->points();
    if (auto status = reporting::csv::writeResults(m_session, path); !status) {
        QMessageBox::warning(
            this, tr("Export failed"), QString::fromStdString(status.error().message()));
        return;
    }
    log(tr("Results exported to %1").arg(path));
}

void MainWindow::exportTraceCsv()
{
    if (m_session.traces.empty()) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Export trace"), QStringLiteral("trace.csv"), tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    if (auto status = reporting::csv::writeTrace(m_session, m_session.traces.back(), path); !status)
    {
        QMessageBox::warning(
            this, tr("Export failed"), QString::fromStdString(status.error().message()));
        return;
    }
    log(tr("Trace exported to %1").arg(path));
}

void MainWindow::exportPlotImage()
{
    const auto path = QFileDialog::getSaveFileName(this,
                                                   tr("Export plot"),
                                                   QStringLiteral("spectrum.png"),
                                                   tr("PNG image (*.png);;SVG drawing (*.svg)"));
    if (path.isEmpty()) {
        return;
    }

    const QSize size{1920, 1080};
    const bool ok = path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)
                        ? m_plot->exportSvg(path, size)
                        : m_plot->renderToImage(size).save(path);
    if (!ok) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(path));
        return;
    }
    log(tr("Plot exported to %1").arg(path));
}

void MainWindow::exportPdfReport()
{
    const auto path = QFileDialog::getSaveFileName(
        this, tr("Generate report"), QStringLiteral("report.pdf"), tr("PDF documents (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }

    m_session.results = m_resultsDock->points();
    ReportTemplate reportTemplate;
    reportTemplate.companyName = m_session.meta.company;

    reporting::PdfReportRenderer renderer;
    renderer.setPlotImage(m_plot->renderToImage(QSize{1600, 900}));
    if (auto status = renderer.render(m_session, reportTemplate, path); !status) {
        QMessageBox::warning(
            this, tr("Report failed"), QString::fromStdString(status.error().message()));
        return;
    }
    log(tr("Report written to %1").arg(path));
}

bool MainWindow::confirmDiscardChanges()
{
    if (!m_dirty) {
        return true;
    }
    const auto answer =
        QMessageBox::question(this,
                              tr("Unsaved measurements"),
                              tr("The current session has unsaved changes. Save it first?"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) {
        return false;
    }
    if (answer == QMessageBox::Save) {
        return saveSession();
    }
    return true;
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this,
                       tr("About PeakEmi"),
                       tr("<h3>PeakEmi %1</h3>"
                          "<p>Cross-platform EMI pre-compliance measurement suite.</p>"
                          "<p>%2</p>")
                           .arg(qs(ProjectVersion), qs(ComplianceDisclaimer)));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_controller->isRunning()) {
        if (QMessageBox::question(
                this,
                tr("Run in progress"),
                tr("A measurement run is still in progress. Abort it and quit?")) !=
            QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_controller->abort();
    }
    if (!confirmDiscardChanges()) {
        event->ignore();
        return;
    }
    saveLayout();
    event->accept();
}

} // namespace peakemi::ui
