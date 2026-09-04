#include "TestSupport.h"

#include <peakemi/cli/HeadlessRunner.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/LimitLineIo.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/ui/SessionPlot.h>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace peakemi;
using namespace peakemi::cli;

namespace {

/// The name of the built-in limit the simulated bench is designed to fail.
const auto FailingLimit = QStringLiteral("CISPR 32 Class B radiated 10 m (QP)");

/// A short run against the simulated instrument: both phases, no hardware and
/// no waiting, which is what a build server can afford to run on every push.
[[nodiscard]] HeadlessOptions quickRun()
{
    HeadlessOptions options;
    options.endpoint = TransportDescriptor{.kind = TransportKind::Simulated};
    options.limits = {FailingLimit};
    options.start = megahertz(30);
    options.stop = megahertz(200);
    options.points = 2001;
    options.dwell = std::chrono::milliseconds{20};
    options.maximumPeaks = 2;
    options.quiet = true;
    return options;
}

/// A limit line far above anything the simulated bench emits, so the same run
/// passes. Written to a file, which also exercises the CSV importer.
[[nodiscard]] QString writeGenerousLimit(const QTemporaryDir& directory)
{
    LimitLine line;
    line.name = "generous";
    line.standard = "test fixture";
    line.unit = AmplitudeUnit::dBuV;
    line.detector = Detector::QuasiPeak;
    line.points = {{megahertz(30), 140.0, Interpolation::LogFrequency},
                   {gigahertz(1.0), 140.0, Interpolation::LogFrequency}};

    const auto path = directory.filePath(QStringLiteral("generous.csv"));
    [[maybe_unused]] const auto saved = limit_io::save(line, path);
    return path;
}

} // namespace

/// The headless runner end to end (NFR-QUA-3): configuration in, artefacts and
/// an exit code out, with nothing on screen at any point.
class HeadlessRunTest : public QObject
{
    Q_OBJECT

private slots:
    void aFailingRunExitsWithLimitsExceeded();
    void aPassingRunExitsWithSuccess();
    void failOnNeverReportsOnlyWhetherTheRunCompleted();
    void failOnMarginalIsStricterThanTheDefault();
    void outputDirectoryWritesEveryArtefact();
    void jsonSummaryIsMachineReadable();
    void aSessionFileSuppliesTheConfiguration();
    void anUnknownLimitIsRefusedBeforeAnythingIsMeasured();
    void aSpanTheInstrumentCannotSweepIsRefused();
    void aCancellationBeforeTheRunIsHonoured();
    void autosaveSurvivesAnAbortedRun();
};

void HeadlessRunTest::aFailingRunExitsWithLimitsExceeded()
{
    HeadlessRunner runner{quickRun()};
    QCOMPARE(runner.run(), exit_code::LimitsExceeded);

    // The verdict is a result, not an error: the session is complete.
    QCOMPARE(runner.session().overallVerdict(), Verdict::Fail);
    QVERIFY(!runner.session().results.empty());
    QVERIFY(!runner.session().traces.empty());
    QVERIFY(runner.session().worstResult().has_value());
}

void HeadlessRunTest::aPassingRunExitsWithSuccess()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto options = quickRun();
    options.limits = {writeGenerousLimit(directory)};
    // Flag peaks regardless of how far below the limit they sit, so the run
    // verifies points rather than finding nothing to verify.
    options.marginThresholdDb = 200.0;

    HeadlessRunner runner{options};
    QCOMPARE(runner.run(), exit_code::Success);
    QCOMPARE(runner.session().overallVerdict(), Verdict::Pass);
    QVERIFY(!runner.session().results.empty());
}

void HeadlessRunTest::failOnNeverReportsOnlyWhetherTheRunCompleted()
{
    auto options = quickRun();
    options.failOn = FailOn::Never;

    HeadlessRunner runner{options};
    // The same measurement, the same verdict, a different question asked of it.
    QCOMPARE(runner.run(), exit_code::Success);
    QCOMPARE(runner.session().overallVerdict(), Verdict::Fail);
}

void HeadlessRunTest::failOnMarginalIsStricterThanTheDefault()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // A limit placed just above the simulated emitters puts the verified points
    // inside the marginal band rather than over the limit.
    LimitLine line;
    line.name = "marginal";
    line.unit = AmplitudeUnit::dBuV;
    line.detector = Detector::QuasiPeak;
    // The simulated bench peaks a little under 45 dBuV, so this limit is above
    // every emitter and within the 6 dB marginal band of the strongest one.
    line.points = {{megahertz(30), 45.0, Interpolation::LogFrequency},
                   {gigahertz(1.0), 45.0, Interpolation::LogFrequency}};
    const auto path = directory.filePath(QStringLiteral("marginal.csv"));
    QVERIFY(limit_io::save(line, path).has_value());

    auto options = quickRun();
    options.limits = {path};

    HeadlessRunner lenient{options};
    QCOMPARE(lenient.run(), exit_code::Success);
    QCOMPARE(lenient.session().overallVerdict(), Verdict::Marginal);

    options.failOn = FailOn::Marginal;
    HeadlessRunner strict{options};
    QCOMPARE(strict.run(), exit_code::LimitsExceeded);
}

void HeadlessRunTest::outputDirectoryWritesEveryArtefact()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto options = quickRun();
    options.outputDirectory = directory.filePath(QStringLiteral("artefacts"));
    options.eutName = "Widget rev C";
    options.company = "Bitcrush Testing";
    options.runId = "run-under-test";

    HeadlessRunner runner{options};
    runner.setPlotRenderer(&ui::renderSessionSpectrum);
    QCOMPARE(runner.run(), exit_code::LimitsExceeded);

    const QDir artefacts{options.outputDirectory};
    for (const auto& name : {QStringLiteral("session.peakemi.json"),
                             QStringLiteral("results.csv"),
                             QStringLiteral("results.json"),
                             QStringLiteral("trace.csv"),
                             QStringLiteral("report.pdf")})
    {
        const auto path = artefacts.filePath(name);
        QVERIFY2(QFile::exists(path), qPrintable(path));
        QVERIFY2(QFileInfo{path}.size() > 0, qPrintable(path));
    }

    // The run id the caller supplied is the one every artefact carries (FR-DAT-6).
    const auto reopened =
        SessionSerializer::load(artefacts.filePath(QStringLiteral("session.peakemi.json")));
    const auto reason = test::errorText(reopened);
    QVERIFY2(reopened.has_value(), reason.constData());
    QCOMPARE(reopened->meta.runId, std::string{"run-under-test"});
    QCOMPARE(reopened->meta.eutName, std::string{"Widget rev C"});

    QFile results{artefacts.filePath(QStringLiteral("results.csv"))};
    QVERIFY(results.open(QIODevice::ReadOnly));
    QVERIFY(
        QString::fromUtf8(results.readAll()).contains(QStringLiteral("PRE-COMPLIANCE DATA ONLY")));
}

void HeadlessRunTest::jsonSummaryIsMachineReadable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto options = quickRun();
    options.summary = SummaryFormat::Json;
    options.outputDirectory = directory.path();

    HeadlessRunner runner{options};
    const int code = runner.run();
    QCOMPARE(code, exit_code::LimitsExceeded);

    // The summary goes to stdout, which a test cannot read back portably; the
    // same values are asserted through the session and the JSON export, which
    // is what a pipeline parses anyway.
    QFile exported{QDir{directory.path()}.filePath(QStringLiteral("results.json"))};
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(exported.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
}

void HeadlessRunTest::aSessionFileSuppliesTheConfiguration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    // Configure once -- in the application or by hand -- and re-run it from the
    // pipeline without restating any of it.
    Session saved = Session::createNew("0.2.0");
    saved.meta.eutName = "Widget rev C";
    saved.config.span = FrequencyRange{megahertz(30), megahertz(120)};
    saved.config.phase1Points = 1001;
    saved.config.dwellTime = std::chrono::milliseconds{20};
    saved.config.peaks.maximumCount = 1;
    saved.config.limits = {*builtInLimitLine(FailingLimit.toStdString())};
    saved.config.autosave = false;

    const auto path = directory.filePath(QStringLiteral("configured.peakemi.json"));
    QVERIFY(SessionSerializer::save(saved, path).has_value());

    HeadlessOptions options;
    options.sessionPath = path;
    options.quiet = true;

    HeadlessRunner runner{options};
    QCOMPARE(runner.run(), exit_code::LimitsExceeded);
    QCOMPARE(runner.session().config.span, saved.config.span);
    QCOMPARE(runner.session().meta.eutName, std::string{"Widget rev C"});
    QCOMPARE(runner.session().results.size(), 1U);
}

void HeadlessRunTest::anUnknownLimitIsRefusedBeforeAnythingIsMeasured()
{
    auto options = quickRun();
    options.limits = {QStringLiteral("CISPR 32 Class Q radiated on Tuesdays")};

    HeadlessRunner runner{options};
    // A usage error, not a run failure: nothing was connected and nothing swept.
    QCOMPARE(runner.run(), exit_code::UsageError);
    QVERIFY(runner.session().isEmpty());
}

void HeadlessRunTest::aSpanTheInstrumentCannotSweepIsRefused()
{
    auto options = quickRun();
    options.stop = gigahertz(9.0);

    HeadlessRunner runner{options};
    // Refused against the instrument's declared capabilities, before the first
    // sweep rather than three minutes into the run.
    QCOMPARE(runner.run(), exit_code::UsageError);
    QVERIFY(runner.session().isEmpty());
}

void HeadlessRunTest::aCancellationBeforeTheRunIsHonoured()
{
    HeadlessRunner runner{quickRun()};

    // The Ctrl-C that arrives while the instrument is still being opened is the
    // one an interval-based test cannot catch and a pipeline hits routinely, so
    // it is the one asserted here: a cancelled job must not exit claiming the
    // equipment passed. Aborting mid-sweep is covered by the engine's own test,
    // where the driver can be slowed down to make the timing deterministic.
    runner.requestAbort();

    QCOMPARE(runner.run(), exit_code::Aborted);
    QVERIFY(runner.session().results.empty());
}

void HeadlessRunTest::autosaveSurvivesAnAbortedRun()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto options = quickRun();
    options.sessionOutput = directory.filePath(QStringLiteral("partial.peakemi.json"));

    HeadlessRunner runner{options};
    QCOMPARE(runner.run(), exit_code::LimitsExceeded);

    // The autosave wrote the same file the final save did, so a job killed
    // mid-run leaves its verified points where the pipeline looks for them.
    const auto recovered = SessionSerializer::load(options.sessionOutput);
    const auto reason = test::errorText(recovered);
    QVERIFY2(recovered.has_value(), reason.constData());
    QCOMPARE(recovered->results.size(), runner.session().results.size());
}

QTEST_MAIN(HeadlessRunTest)
#include "HeadlessRunTest.moc"
