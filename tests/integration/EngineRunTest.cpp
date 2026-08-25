#include "TestSupport.h"

#include <peakemi/core/CisprBands.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/MeasurementEngine.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/reporting/CsvExporter.h>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <cmath>
#include <memory>

using namespace peakemi;

namespace {

/// A run configuration that finishes quickly but exercises both phases.
[[nodiscard]] RunConfiguration makeConfiguration()
{
    RunConfiguration config;
    config.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    config.phase1Points = 4001;
    config.phase1Detector = Detector::Peak;
    config.phase1Rbw = kilohertz(120);
    config.refLevel = decibel(80.0);

    config.peaks.marginThresholdDb = 6.0;
    config.peaks.prominenceDb = 3.0;
    config.peaks.minimumSpacing = megahertz(5);
    config.peaks.maximumCount = 5;

    config.verificationDetector = Detector::QuasiPeak;
    config.dwellTime = std::chrono::milliseconds{50};
    config.verificationSpan = kilohertz(400);
    config.verificationPoints = 201;
    config.marginalThresholdDb = 6.0;
    config.autosave = false;

    // Class B radiated at 10 m, with an antenna factor that lifts the simulated
    // emitters over the limit — a bench setup that fails, which is the case
    // worth testing end to end.
    config.limits = {*builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")};

    CorrectionTable antenna;
    antenna.name = "biconilog";
    antenna.kind = CorrectionKind::AntennaFactor;
    antenna.points = {{megahertz(30), 12.0}, {gigahertz(1.0), 24.0}};
    config.corrections = {antenna};
    return config;
}

[[nodiscard]] std::shared_ptr<drivers::SimulatedDriver> makeDriver()
{
    auto driver = std::make_shared<drivers::SimulatedDriver>();
    [[maybe_unused]] const auto opened = driver->open(nullptr);
    return driver;
}

} // namespace

/// End-to-end run of the two-phase loop against the simulated driver, headless
/// (NFR-QUA-3). No hardware, no network, no GUI.
class EngineRunTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void completeRunProducesVerifiedPoints();
    void runWithoutDriverFails();
    void invalidConfigurationFails();
    void abortStopsTheRunAndKeepsPartialResults();
    void pauseAndResumeSurviveARun();
    void autosaveWritesAfterEveryPoint();
    void callerSuppliedRunIdIsKept();
    void multiPassKeepsTheWorstCase();
    void resultsExportToCsv();
};

void EngineRunTest::initTestCase()
{
    registerMetaTypes();
}

void EngineRunTest::completeRunProducesVerifiedPoints()
{
    MeasurementEngine engine;
    engine.setDriver(makeDriver());
    engine.setConfiguration(makeConfiguration());

    QSignalSpy traces{&engine, &MeasurementEngine::traceAcquired};
    QSignalSpy peaks{&engine, &MeasurementEngine::peaksFlagged};
    QSignalSpy points{&engine, &MeasurementEngine::pointMeasured};
    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};
    QSignalSpy failed{&engine, &MeasurementEngine::runFailed};

    engine.start();

    QCOMPARE(failed.count(), 0);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(engine.phase(), MeasurementEngine::Phase::Finished);
    QCOMPARE(traces.count(), 1);
    QCOMPARE(peaks.count(), 1);
    QVERIFY(points.count() > 0);

    const auto session = finished.front().front().value<Session>();
    QCOMPARE(static_cast<int>(session.results.size()), points.count());
    QVERIFY(!session.traces.empty());
    QVERIFY(session.traces.front().corrected);
    QCOMPARE(session.traces.front().unit, AmplitudeUnit::dBuV_per_m);

    for (const auto& point : session.results) {
        // Full provenance is the point of Phase 2 (FR-RUN-4).
        QCOMPARE(point.detector, Detector::QuasiPeak);
        QCOMPARE(point.rbw, mandatedResolutionBandwidth(point.frequency));
        QCOMPARE(point.dwell, std::chrono::milliseconds{50});
        QCOMPARE(point.unit, AmplitudeUnit::dBuV_per_m);
        QVERIFY(!point.corrections.empty());
        QVERIFY(!point.limitName.empty());
        QVERIFY(point.verdict != Verdict::Unknown);
        QVERIFY(point.instrument.model == std::string{"Simulated Analyzer"});
        QVERIFY(std::isfinite(point.marginDb));
        // The corrected level is the raw reading plus the antenna factor.
        QVERIFY(point.correctedAmplitude > point.rawAmplitude);
    }

    // The simulated bench is designed to fail class B: at least one point does.
    QCOMPARE(session.overallVerdict(), Verdict::Fail);
    QVERIFY(session.worstResult().has_value());
}

void EngineRunTest::runWithoutDriverFails()
{
    MeasurementEngine engine;
    engine.setConfiguration(makeConfiguration());

    QSignalSpy failed{&engine, &MeasurementEngine::runFailed};
    engine.start();

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.front().front().value<Error>().code, ErrorCode::NotConnected);
    QCOMPARE(engine.phase(), MeasurementEngine::Phase::Failed);
    QVERIFY(!engine.isRunning());
}

void EngineRunTest::invalidConfigurationFails()
{
    MeasurementEngine engine;
    engine.setDriver(makeDriver());

    auto config = makeConfiguration();
    config.span = FrequencyRange{gigahertz(1.0), megahertz(30)};
    engine.setConfiguration(config);

    QSignalSpy failed{&engine, &MeasurementEngine::runFailed};
    engine.start();

    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.front().front().value<Error>().code, ErrorCode::InvalidConfiguration);
}

void EngineRunTest::abortStopsTheRunAndKeepsPartialResults()
{
    auto driver = makeDriver();
    auto simulatedConfig = driver->config();
    simulatedConfig.timeScale = 1.0; // make the dwell take real time
    driver->setConfig(simulatedConfig);

    MeasurementEngine engine;
    engine.setDriver(driver);
    auto config = makeConfiguration();
    config.dwellTime = std::chrono::milliseconds{400};
    engine.setConfiguration(config);

    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};
    QSignalSpy points{&engine, &MeasurementEngine::pointMeasured};

    // Abort from another thread while the engine is dwelling, which is exactly
    // how the GUI thread's abort button behaves (FR-RUN-5, FR-THR-3).
    auto* aborter = QThread::create([&engine] {
        QThread::msleep(600);
        engine.requestAbort();
    });
    aborter->start();

    engine.start();
    aborter->wait();
    delete aborter;

    QCOMPARE(engine.phase(), MeasurementEngine::Phase::Aborted);
    QCOMPARE(finished.count(), 1);
    const auto session = finished.front().front().value<Session>();
    QVERIFY(session.results.size() < 5U); // stopped before finishing every peak
    QCOMPARE(static_cast<int>(session.results.size()), points.count());
    QVERIFY(!engine.isRunning());
}

void EngineRunTest::pauseAndResumeSurviveARun()
{
    auto driver = makeDriver();
    auto simulatedConfig = driver->config();
    simulatedConfig.timeScale = 1.0;
    driver->setConfig(simulatedConfig);

    MeasurementEngine engine;
    engine.setDriver(driver);
    auto config = makeConfiguration();
    config.dwellTime = std::chrono::milliseconds{200};
    config.peaks.maximumCount = 3;
    engine.setConfiguration(config);

    QSignalSpy phases{&engine, &MeasurementEngine::phaseChanged};
    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};

    auto* controller = QThread::create([&engine] {
        QThread::msleep(400);
        engine.requestPause();
        QThread::msleep(400);
        QVERIFY(engine.phase() == MeasurementEngine::Phase::Paused);
        engine.requestResume();
    });
    controller->start();

    engine.start();
    controller->wait();
    delete controller;

    QCOMPARE(engine.phase(), MeasurementEngine::Phase::Finished);
    QCOMPARE(finished.count(), 1);

    bool sawPaused = false;
    for (const auto& phase : phases) {
        sawPaused = sawPaused || phase.front().value<MeasurementEngine::Phase>() ==
                                     MeasurementEngine::Phase::Paused;
    }
    QVERIFY(sawPaused);
}

void EngineRunTest::autosaveWritesAfterEveryPoint()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("autosave.json"));

    MeasurementEngine engine;
    engine.setDriver(makeDriver());
    auto config = makeConfiguration();
    config.autosave = true;
    config.autosavePath = path.toStdString();
    engine.setConfiguration(config);

    engine.start();

    // Crash safety: the file on disk holds the results without an explicit save.
    const auto recovered = SessionSerializer::load(path);
    const auto reason = test::errorText(recovered);
    QVERIFY2(recovered.has_value(), reason.constData());
    QVERIFY(!recovered->results.empty());
    QVERIFY(!recovered->traces.empty());
    QCOMPARE(recovered->config.span, config.span);
}

void EngineRunTest::callerSuppliedRunIdIsKept()
{
    MeasurementEngine engine;
    engine.setDriver(makeDriver());
    auto config = makeConfiguration();
    config.peaks.maximumCount = 1;
    engine.setConfiguration(config);

    SessionMeta meta;
    meta.applicationVersion = "0.1.0";
    meta.runId = "run-under-test";
    meta.eutName = "Widget rev C";
    engine.setSessionMeta(meta);

    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};
    engine.start();

    QCOMPARE(finished.count(), 1);
    const auto session = finished.front().front().value<Session>();
    QCOMPARE(session.meta.runId, std::string{"run-under-test"});
    QCOMPARE(session.meta.eutName, std::string{"Widget rev C"});

    // Without one, the engine generates an identifier of its own.
    MeasurementEngine generated;
    generated.setDriver(makeDriver());
    generated.setConfiguration(config);
    QSignalSpy generatedFinished{&generated, &MeasurementEngine::runFinished};
    generated.start();
    QCOMPARE(generatedFinished.count(), 1);
    QVERIFY(!generatedFinished.front().front().value<Session>().meta.runId.empty());
}

void EngineRunTest::multiPassKeepsTheWorstCase()
{
    MeasurementEngine engine;
    engine.setDriver(makeDriver());
    auto config = makeConfiguration();
    config.passes = 2;
    config.peaks.maximumCount = 3;
    engine.setConfiguration(config);

    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};
    engine.start();

    QCOMPARE(finished.count(), 1);
    const auto session = finished.front().front().value<Session>();
    // Two passes over the same deterministic spectrum still yield one row per
    // frequency: the worst case is kept, not appended (FR-RUN-8).
    QVERIFY(session.results.size() <= 3U);
    for (std::size_t i = 1; i < session.results.size(); ++i) {
        QVERIFY(session.results[i].frequency != session.results[i - 1].frequency);
    }
}

void EngineRunTest::resultsExportToCsv()
{
    MeasurementEngine engine;
    engine.setDriver(makeDriver());
    auto config = makeConfiguration();
    config.peaks.maximumCount = 2;
    engine.setConfiguration(config);

    QSignalSpy finished{&engine, &MeasurementEngine::runFinished};
    engine.start();
    QCOMPARE(finished.count(), 1);

    const auto session = finished.front().front().value<Session>();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("results.csv"));
    QVERIFY(reporting::csv::writeResults(session, path).has_value());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto content = QString::fromUtf8(file.readAll());
    QVERIFY(content.contains(QStringLiteral("PRE-COMPLIANCE DATA ONLY")));
    QVERIFY(content.contains(QStringLiteral("quasi-peak")));
    QCOMPARE(content.count(QLatin1Char('\n')) > static_cast<int>(session.results.size()), true);
}

QTEST_APPLESS_MAIN(EngineRunTest)
#include "EngineRunTest.moc"
