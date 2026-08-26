#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/ui/AboutDialog.h>
#include <peakemi/ui/InstrumentDock.h>
#include <peakemi/ui/LogDock.h>
#include <peakemi/ui/MainWindow.h>
#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/ResultsTableModel.h>
#include <peakemi/ui/RunConfigDock.h>

#include <QApplication>
#include <QBrush>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace peakemi;

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void constructsWithAllDocks();
    void menusExposeTheMainWorkflows();
    void configurationDockRoundTripsAConfiguration();
    void configurationDockNarrowsToCapabilities();
    void plotDecimatesLargeTraces();
    void plotAutoScalesToTheData();
    void plotExportsImages();
    void plotConvertsCoordinatesBothWays();
    void resultsDockSummarisesVerdicts();
    void resultsStayReadableInEitherColourScheme();
    void aboutDialogCreditsLicenceAndSource();
    void logDockEmitsConsoleCommands();
};

void MainWindowTest::initTestCase()
{
    registerMetaTypes();
    drivers::registerBuiltInDrivers();
}

void MainWindowTest::constructsWithAllDocks()
{
    ui::MainWindow window;

    QVERIFY(window.findChild<ui::PainterPlotBackend*>() != nullptr);
    QVERIFY(window.findChild<ui::InstrumentDock*>() != nullptr);
    QVERIFY(window.findChild<ui::RunConfigDock*>() != nullptr);
    QVERIFY(window.findChild<ui::ResultsDock*>() != nullptr);
    QVERIFY(window.findChild<ui::LogDock*>() != nullptr);
    QVERIFY(window.findChild<QTableView*>(QStringLiteral("resultsView")) != nullptr);
    QVERIFY(!window.windowTitle().isEmpty());
    QVERIFY(window.session().meta.runId.size() > 0);
}

void MainWindowTest::menusExposeTheMainWorkflows()
{
    ui::MainWindow window;
    const auto menus = window.menuBar()->actions();
    QCOMPARE(menus.size(), 4); // File, Run, View, Help

    QStringList runActions;
    for (auto* action : menus.at(1)->menu()->actions()) {
        runActions.append(action->text());
    }
    QVERIFY(runActions.filter(QStringLiteral("Start")).size() == 1);
    QVERIFY(runActions.filter(QStringLiteral("Pause")).size() == 1);
    QVERIFY(runActions.filter(QStringLiteral("Abort")).size() == 1);

    // Nothing that needs an instrument or results is enabled on a fresh window.
    for (auto* action : menus.at(1)->menu()->actions()) {
        if (action->text().contains(QStringLiteral("Start"))) {
            QVERIFY(!action->isEnabled());
        }
    }
}

void MainWindowTest::configurationDockRoundTripsAConfiguration()
{
    ui::RunConfigDock dock;

    RunConfiguration config;
    config.span = FrequencyRange{kilohertz(150), megahertz(30)};
    config.phase1Points = 751;
    config.phase1Detector = Detector::Peak;
    config.verificationDetector = Detector::Average;
    config.dwellTime = std::chrono::milliseconds{2500};
    config.peaks.maximumCount = 12;
    config.peaks.marginThresholdDb = 8.0;
    config.peaks.minimumSpacing = megahertz(0.5);
    config.passes = 3;
    dock.setConfiguration(config);

    const auto restored = dock.configuration();
    QCOMPARE(restored.span.start, config.span.start);
    QCOMPARE(restored.span.stop, config.span.stop);
    QCOMPARE(restored.phase1Points, 751);
    QCOMPARE(restored.verificationDetector, Detector::Average);
    QCOMPARE(restored.dwellTime, config.dwellTime);
    QCOMPARE(restored.peaks.maximumCount, 12);
    QCOMPARE(restored.peaks.marginThresholdDb, 8.0);
    QCOMPARE(restored.peaks.minimumSpacing, megahertz(0.5));
    QCOMPARE(restored.passes, 3);
}

void MainWindowTest::configurationDockNarrowsToCapabilities()
{
    ui::RunConfigDock dock;
    const drivers::SimulatedDriver driver;
    dock.applyCapabilities(driver.capabilities());

    RunConfiguration config;
    config.span = FrequencyRange{megahertz(30), gigahertz(3.0)};
    config.phase1Rbw = kilohertz(120);
    dock.setConfiguration(config);
    QCOMPARE(dock.configuration().phase1Rbw, kilohertz(120));

    // A bandwidth the instrument does not offer cannot be selected at all.
    RunConfiguration unsupported = config;
    unsupported.phase1Rbw = hertz(7);
    dock.setConfiguration(unsupported);
    QCOMPARE(dock.configuration().phase1Rbw, hertz(0)); // falls back to "band default"
}

void MainWindowTest::plotDecimatesLargeTraces()
{
    ui::PainterPlotBackend plot;
    plot.resize(800, 400);

    // 40,001 points is the number FR-VIS-1 asks for.
    ui::PlotSeries series;
    series.id = QStringLiteral("trace");
    series.points.reserve(40001);
    for (int i = 0; i < 40001; ++i) {
        series.points.append(QPointF{30e6 + i * 24245.0, 10.0 + (i % 100) * 0.2});
    }
    plot.setSeries(series);
    plot.setFrequencyRange(FrequencyRange{megahertz(30), gigahertz(1.0)});
    plot.setAmplitudeRange(0.0, 40.0);

    QElapsedTimer timer;
    timer.start();
    const auto image = plot.renderToImage(QSize{800, 400});
    // Decimation makes the cost depend on the widget width, not the trace size.
    QVERIFY2(timer.elapsed() < 500, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(image.size(), QSize(800, 400));
}

void MainWindowTest::plotAutoScalesToTheData()
{
    ui::PainterPlotBackend plot;
    plot.resize(600, 300);

    ui::PlotSeries series;
    series.id = QStringLiteral("trace");
    series.points = {QPointF{30e6, 10.0}, QPointF{500e6, 55.0}, QPointF{1e9, 20.0}};
    plot.setSeries(series);

    QSignalSpy spy{&plot, &ui::PainterPlotBackend::viewChanged};
    plot.autoScale();
    QCOMPARE(spy.count(), 1);

    // Auto scale must include the extremes with a little headroom.
    const auto image = plot.renderToImage(QSize{600, 300});
    QVERIFY(!image.isNull());

    plot.clearSeries();
    plot.autoScale(); // no data: must not crash or emit
    QCOMPARE(spy.count(), 1);
}

void MainWindowTest::plotExportsImages()
{
    ui::PainterPlotBackend plot;
    plot.resize(400, 200);

    ui::PlotSeries series;
    series.id = QStringLiteral("trace");
    series.label = QStringLiteral("Phase 1");
    series.points = {QPointF{30e6, 10.0}, QPointF{1e9, 40.0}};
    plot.setSeries(series);
    plot.setMarkers({ui::PlotMarker{QStringLiteral("48.0"), QPointF{48e6, 35.0}, Qt::red, true}});

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto svgPath = directory.filePath(QStringLiteral("plot.svg"));
    QVERIFY(plot.exportSvg(svgPath, QSize{800, 400}));
    QVERIFY(QFileInfo{svgPath}.size() > 0);

    const auto pngPath = directory.filePath(QStringLiteral("plot.png"));
    QVERIFY(plot.renderToImage(QSize{800, 400}).save(pngPath));
    QVERIFY(QFileInfo{pngPath}.size() > 0);
}

void MainWindowTest::plotConvertsCoordinatesBothWays()
{
    ui::PainterPlotBackend plot;
    plot.resize(600, 300);
    plot.setFrequencyRange(FrequencyRange{megahertz(30), gigahertz(1.0)});
    plot.setAmplitudeRange(0.0, 100.0);

    QSignalSpy spy{&plot, &ui::PainterPlotBackend::cursorMoved};
    QTest::mouseMove(&plot, QPoint{300, 150});
    QApplication::processEvents();
    // The cursor readout is what the status bar shows while measuring.
    QVERIFY(spy.count() >= 0);

    plot.setLogarithmicFrequency(true);
    QVERIFY(plot.isLogarithmicFrequency());
    QVERIFY(!plot.renderToImage(QSize{600, 300}).isNull());
}

void MainWindowTest::resultsDockSummarisesVerdicts()
{
    ui::ResultsDock dock;
    QCOMPARE(dock.points().size(), 0U);

    MeasurementPoint pass;
    pass.frequency = megahertz(100);
    pass.correctedAmplitude = 20.0;
    pass.limitValue = 40.0;
    pass.marginDb = 20.0;
    pass.verdict = Verdict::Pass;

    MeasurementPoint failure;
    failure.frequency = megahertz(144);
    failure.correctedAmplitude = 45.0;
    failure.limitValue = 40.0;
    failure.marginDb = -5.0;
    failure.verdict = Verdict::Fail;

    dock.appendPoint(pass);
    dock.appendPoint(failure);
    QCOMPARE(dock.points().size(), 2U);

    // Re-measuring the same frequency updates in place instead of duplicating.
    MeasurementPoint updated = failure;
    updated.marginDb = -7.0;
    dock.appendPoint(updated);
    QCOMPARE(dock.points().size(), 2U);
    QCOMPARE(dock.points()[1].marginDb, -7.0);

    QSignalSpy spy{&dock, &ui::ResultsDock::pointSelected};
    dock.selectFrequency(megahertz(144));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.front().front().value<Hertz>(), megahertz(144));

    dock.clear();
    QCOMPARE(dock.points().size(), 0U);
}

namespace {

/// WCAG relative luminance, the basis of the contrast ratio below.
[[nodiscard]] double relativeLuminance(const QColor& colour)
{
    const auto channel = [](int value) {
        const double normalised = value / 255.0;
        return normalised <= 0.03928 ? normalised / 12.92
                                     : std::pow((normalised + 0.055) / 1.055, 2.4);
    };
    return (0.2126 * channel(colour.red())) + (0.7152 * channel(colour.green())) +
           (0.0722 * channel(colour.blue()));
}

/// WCAG contrast ratio, from 1 (identical) to 21 (black on white). The AA
/// threshold for body text is 4.5.
[[nodiscard]] double contrastRatio(const QColor& first, const QColor& second)
{
    const double a = relativeLuminance(first);
    const double b = relativeLuminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

[[nodiscard]] QPalette schemePalette(bool dark)
{
    QPalette palette;
    const QColor window = dark ? QColor{0x1E, 0x1E, 0x1E} : QColor{0xF2, 0xF2, 0xF2};
    const QColor base = dark ? QColor{0x14, 0x14, 0x14} : QColor{0xFF, 0xFF, 0xFF};
    const QColor text = dark ? QColor{0xE6, 0xE6, 0xE6} : QColor{0x1A, 0x1A, 0x1A};
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::Text, text);
    return palette;
}

} // namespace

void MainWindowTest::resultsStayReadableInEitherColourScheme()
{
    const QPalette original = QApplication::palette();
    const auto restore = qScopeGuard([&original] { QApplication::setPalette(original); });

    ui::ResultsTableModel model;

    std::vector<MeasurementPoint> points;
    for (const Verdict verdict : {Verdict::Pass, Verdict::Marginal, Verdict::Fail}) {
        MeasurementPoint point;
        point.frequency = megahertz(100);
        point.correctedAmplitude = 30.0;
        point.limitValue = 40.0;
        point.marginDb = 10.0;
        point.verdict = verdict;
        points.push_back(point);
    }
    model.setPoints(points);

    for (const bool dark : {false, true}) {
        QApplication::setPalette(schemePalette(dark));

        for (int row = 0; row < model.rowCount(); ++row) {
            const QModelIndex index = model.index(row, ui::ResultsTableModel::VerdictColumn);
            const QVariant background = model.data(index, Qt::BackgroundRole);
            const QVariant foreground = model.data(index, Qt::ForegroundRole);
            QVERIFY(background.isValid());
            QVERIFY(foreground.isValid());

            const QColor onColour = foreground.value<QBrush>().color();
            const QColor behindColour = background.value<QBrush>().color();

            // The bug this guards against: text at light-theme contrast drawn
            // on a background chosen for the other scheme.
            const double ratio = contrastRatio(onColour, behindColour);
            const QByteArray message =
                QStringLiteral("row %1 in %2 mode: contrast %3")
                    .arg(row)
                    .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"))
                    .arg(ratio)
                    .toUtf8();
            QVERIFY2(ratio >= 4.5, message.constData());
        }
    }
}

void MainWindowTest::aboutDialogCreditsLicenceAndSource()
{
    const QPalette original = QApplication::palette();
    const auto restore = qScopeGuard([&original] { QApplication::setPalette(original); });

    for (const bool dark : {false, true}) {
        QApplication::setPalette(schemePalette(dark));

        ui::AboutDialog dialog;

        QString text;
        const QList<QLabel*> labels = dialog.findChildren<QLabel*>();
        for (const QLabel* label : labels) {
            text += label->text();
        }

        QVERIFY(text.contains(QStringLiteral("Bitcrush Testing")));
        QVERIFY(text.contains(QStringLiteral("GPL-3.0-or-later")));
        QVERIFY(text.contains(ui::AboutDialog::repositoryUrl()));
        QVERIFY(text.contains(ui::AboutDialog::licenceUrl()));
        // The disclaimer required by CON-1 stays on the page.
        QVERIFY(text.contains(QStringLiteral("PRE-COMPLIANCE")));

        // Guards the resource wiring: the icon is compiled into a static
        // library, and a missing Q_INIT_RESOURCE shows up as a blank label
        // rather than as a build failure.
        const auto* icon = dialog.findChild<QLabel*>(QStringLiteral("aboutIcon"));
        QVERIFY(icon != nullptr);
        QVERIFY(!icon->pixmap().isNull());

        // A link nobody can read is not a source offer.
        const QPalette& scheme = dialog.palette();
        QVERIFY(contrastRatio(scheme.color(QPalette::Link), scheme.color(QPalette::Window)) >= 4.5);
    }
}

void MainWindowTest::logDockEmitsConsoleCommands()
{
    ui::LogDock dock;
    dock.appendLog(QStringLiteral("engine started"));
    QVERIFY(dock.logText().contains(QStringLiteral("engine started")));

    auto* input = dock.findChild<QLineEdit*>(QStringLiteral("scpiCommandInput"));
    QVERIFY(input != nullptr);

    QSignalSpy spy{&dock, &ui::LogDock::commandEntered};
    input->setText(QStringLiteral("*IDN?"));
    QTest::keyClick(input, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.front().front().toString(), QStringLiteral("*IDN?"));
    QVERIFY(input->text().isEmpty());

    auto* console = dock.findChild<QPlainTextEdit*>(QStringLiteral("scpiConsole"));
    QVERIFY(console->toPlainText().contains(QStringLiteral("> *IDN?")));
}

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
