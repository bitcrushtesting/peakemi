// Generates the screenshots embedded in README.md.
//
// The images are produced by driving the real main window against the simulated
// analyzer, so they cannot drift away from what the application actually looks
// like: regenerate them with
//
//     cmake --preset debug -DPEAKEMI_BUILD_TOOLS=ON
//     cmake --build --preset debug --target peakemi_screenshots
//     QT_QPA_PLATFORM=offscreen ./build/debug/bin/peakemi_screenshots docs/images
//
// It runs headless, so it works in CI and over SSH.

#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/ui/InstrumentDock.h>
#include <peakemi/ui/LogDock.h>
#include <peakemi/ui/MainWindow.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/RunConfigDock.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QPixmap>
#include <QSettings>
#include <QTabBar>
#include <QTextStream>

#include <functional>

namespace {

QTextStream& out()
{
    static QTextStream stream{stdout};
    return stream;
}

/// Spin the event loop until @p ready reports true or the timeout expires.
bool waitFor(const std::function<bool()>& ready, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready()) {
        if (timer.elapsed() > timeoutMs) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return true;
}

bool save(const QPixmap& pixmap, const QString& path)
{
    if (pixmap.isNull() || !pixmap.save(path, "PNG")) {
        out() << "failed to write " << path << Qt::endl;
        return false;
    }
    out() << "wrote " << path << " (" << pixmap.width() << "x" << pixmap.height() << ")"
          << Qt::endl;
    return true;
}

/// A configuration that shows the tool at its most informative: a radiated
/// class B scan with an antenna factor, which the simulated bench fails.
peakemi::RunConfiguration demoConfiguration()
{
    peakemi::RunConfiguration config;
    config.span = peakemi::FrequencyRange{peakemi::megahertz(30), peakemi::gigahertz(1.0)};
    config.phase1Points = 8001;
    config.phase1Rbw = peakemi::kilohertz(120);
    config.refLevel = peakemi::decibel(80.0);
    config.peaks.marginThresholdDb = 6.0;
    config.peaks.minimumSpacing = peakemi::megahertz(8);
    config.peaks.maximumCount = 8;
    config.verificationDetector = peakemi::Detector::QuasiPeak;
    config.dwellTime = std::chrono::milliseconds{1000};
    config.limits = {*peakemi::builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")};

    peakemi::CorrectionTable antenna;
    antenna.name = "Biconilog antenna factor";
    antenna.kind = peakemi::CorrectionKind::AntennaFactor;
    antenna.points = {{peakemi::megahertz(30), 17.6},
                      {peakemi::megahertz(100), 12.9},
                      {peakemi::megahertz(300), 17.8},
                      {peakemi::megahertz(700), 22.3},
                      {peakemi::gigahertz(1.0), 24.6}};

    peakemi::CorrectionTable cable;
    cable.name = "RG-223 5 m cable loss";
    cable.kind = peakemi::CorrectionKind::CableLoss;
    cable.points = {{peakemi::megahertz(30), 0.6}, {peakemi::gigahertz(1.0), 3.4}};

    config.corrections = {antenna, cable};
    return config;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application{argc, argv};

    // A throwaway settings scope, so the screenshots always show the default
    // dock layout rather than whatever the developer last dragged around.
    QCoreApplication::setOrganizationName(QStringLiteral("PeakEmiScreenshots"));
    QCoreApplication::setApplicationName(QStringLiteral("PeakEmiScreenshots"));
    QSettings{}.clear();

    peakemi::registerMetaTypes();
    peakemi::drivers::registerBuiltInDrivers();

    const QString directory =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("docs/images");
    if (!QDir{}.mkpath(directory)) {
        out() << "cannot create " << directory << Qt::endl;
        return 1;
    }

    peakemi::ui::MainWindow window;
    window.resize(1500, 940);
    window.show();
    QCoreApplication::processEvents();

    auto* instruments = window.findChild<peakemi::ui::InstrumentDock*>();
    auto* config = window.findChild<peakemi::ui::RunConfigDock*>();
    auto* results = window.findChild<peakemi::ui::ResultsDock*>();
    auto* logDock = window.findChild<peakemi::ui::LogDock*>();
    auto* plot = window.findChild<peakemi::ui::PainterPlotBackend*>();
    auto* start = window.findChild<QAction*>(QStringLiteral("actionStartRun"));
    if (instruments == nullptr || config == nullptr || results == nullptr || logDock == nullptr ||
        plot == nullptr || start == nullptr)
    {
        out() << "main window is missing an expected child widget" << Qt::endl;
        return 1;
    }

    config->setConfiguration(demoConfiguration());

    // Connect the simulated analyzer the same way the instrument dock does.
    const peakemi::TransportDescriptor simulated{.kind = peakemi::TransportKind::Simulated,
                                                 .address = "simulated",
                                                 .port = 0,
                                                 .baudRate = 0,
                                                 .terminator = "\n",
                                                 .defaultTimeout = std::chrono::milliseconds{1000}};
    emit instruments->connectRequested(simulated, QString{});
    if (!waitFor([start] { return start->isEnabled(); }, 10000)) {
        out() << "the simulated instrument did not connect" << Qt::endl;
        return 1;
    }

    start->trigger();
    // The run disables the action for its duration and re-enables it at the end.
    if (!waitFor([start] { return !start->isEnabled(); }, 5000)) {
        out() << "the run did not start" << Qt::endl;
        return 1;
    }
    if (!waitFor([start, results] { return start->isEnabled() && !results->points().empty(); },
                 120000))
    {
        out() << "the run did not finish in time" << Qt::endl;
        return 1;
    }
    out() << "run finished with " << results->points().size() << " verified point(s)" << Qt::endl;

    bool ok = save(window.grab(), directory + QStringLiteral("/main-window.png"));
    ok = save(plot->grab(), directory + QStringLiteral("/spectrum-plot.png")) && ok;
    ok = save(results->grab(), directory + QStringLiteral("/results-table.png")) && ok;

    // Raise the log tab to show the application log and the SCPI console.
    logDock->raise();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    ok = save(logDock->grab(), directory + QStringLiteral("/log-console.png")) && ok;

    return ok ? 0 : 1;
}
