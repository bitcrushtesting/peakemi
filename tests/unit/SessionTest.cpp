#include <peakemi/core/Capabilities.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/core/SessionSerializer.h>
#include <peakemi/core/Time.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace peakemi;

namespace {

[[nodiscard]] Session makeSession()
{
    Session session = Session::createNew("0.1.0");
    session.meta.eutName = "Widget rev C";
    session.meta.eutSerial = "SN-42";
    session.meta.operatorName = "Test operator";
    session.meta.notes = "Fan running at full speed";

    session.config.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    session.config.phase1Points = 1001;
    session.config.dwellTime = std::chrono::milliseconds{1500};
    session.config.limits = {*builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")};

    CorrectionTable antenna;
    antenna.name = "biconilog";
    antenna.kind = CorrectionKind::AntennaFactor;
    antenna.points = {{megahertz(30), 12.5}, {gigahertz(1.0), 24.0}};
    session.config.corrections = {antenna};

    Trace trace;
    trace.axis = FrequencyAxis::linear(session.config.span, 5);
    trace.amplitudes = {10.0, 20.5, 30.25, 25.0, 12.0};
    trace.unit = AmplitudeUnit::dBuV_per_m;
    trace.detector = Detector::Peak;
    trace.label = "Phase 1";
    trace.corrected = true;
    trace.source = InstrumentId{"PeakEmi",
                                "Simulated Analyzer",
                                "SIM-0001",
                                "1.0",
                                "PeakEmi,Simulated Analyzer,SIM-0001,1.0"};
    trace.acquiredAt = std::chrono::system_clock::now();
    session.traces.push_back(trace);

    MeasurementPoint point;
    point.frequency = megahertz(144);
    point.rawAmplitude = 28.0;
    point.correctedAmplitude = 41.5;
    point.limitValue = 30.0;
    point.marginDb = -11.5;
    point.unit = AmplitudeUnit::dBuV_per_m;
    point.detector = Detector::QuasiPeak;
    point.rbw = kilohertz(120);
    point.dwell = std::chrono::milliseconds{1000};
    point.verdict = Verdict::Fail;
    point.limitName = session.config.limits.front().name;
    point.corrections = {AppliedCorrection{"biconilog", CorrectionKind::AntennaFactor, 13.5, 13.5}};
    point.instrument = trace.source;
    point.measuredAt = std::chrono::system_clock::now();
    session.results.push_back(point);
    return session;
}

} // namespace

class SessionTest : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTripPreservesEverything();
    void newerSchemaIsRejected();
    void garbageIsRejected();
    void missingLimitIsStoredAsNull();
    void savesAtomicallyAndReloads();
    void overallVerdictIsTheWorstOne();
    void segmentPlanningSplitsWideSpans();
    void phase2UsesTheMandatedBandwidth();
    void configurationValidationCatchesMistakes();
    void capabilityValidationIsActionable();
    void capabilityCoercionSnapsToSupportedValues();
    void isoTimestampsRoundTrip();
};

void SessionTest::jsonRoundTripPreservesEverything()
{
    const auto original = makeSession();
    const auto restored = SessionSerializer::fromJson(SessionSerializer::toJson(original));
    QVERIFY2(restored.has_value(), restored.error().message().c_str());

    QCOMPARE(restored->meta.eutName, original.meta.eutName);
    QCOMPARE(restored->meta.runId, original.meta.runId);
    QCOMPARE(restored->config.span, original.config.span);
    QCOMPARE(restored->config.dwellTime, original.config.dwellTime);
    QCOMPARE(restored->config.limits.size(), 1U);
    QCOMPARE(restored->config.limits.front().name, original.config.limits.front().name);
    QCOMPARE(restored->config.corrections.front().points,
             original.config.corrections.front().points);
    QCOMPARE(restored->traces.size(), 1U);
    QCOMPARE(restored->traces.front().amplitudes, original.traces.front().amplitudes);
    QCOMPARE(restored->traces.front().unit, AmplitudeUnit::dBuV_per_m);
    QCOMPARE(restored->results.size(), 1U);
    QCOMPARE(restored->results.front().frequency, original.results.front().frequency);
    QCOMPARE(restored->results.front().verdict, Verdict::Fail);
    QCOMPARE(restored->results.front().corrections.size(), 1U);
}

void SessionTest::newerSchemaIsRejected()
{
    auto text = SessionSerializer::toJson(makeSession());
    text.replace(text.find("\"schema_version\": 1"),
                 std::string{"\"schema_version\": 1"}.size(),
                 "\"schema_version\": 9");

    const auto restored = SessionSerializer::fromJson(text);
    QVERIFY(!restored.has_value());
    QCOMPARE(restored.error().code, ErrorCode::SchemaVersionUnsupported);
}

void SessionTest::garbageIsRejected()
{
    QCOMPARE(SessionSerializer::fromJson("not json at all").error().code, ErrorCode::ParseFailure);
    QCOMPARE(SessionSerializer::fromJson(R"({"schema":"something.else"})").error().code,
             ErrorCode::ParseFailure);
}

void SessionTest::missingLimitIsStoredAsNull()
{
    auto session = makeSession();
    session.results.front().limitValue = std::numeric_limits<double>::quiet_NaN();
    session.results.front().marginDb = std::numeric_limits<double>::quiet_NaN();
    session.results.front().verdict = Verdict::Unknown;

    const auto text = SessionSerializer::toJson(session);
    QVERIFY(text.find("\"limit\": null") != std::string::npos);

    const auto restored = SessionSerializer::fromJson(text);
    QVERIFY(restored.has_value());
    QVERIFY(std::isnan(restored->results.front().limitValue));
    QCOMPARE(restored->results.front().verdict, Verdict::Unknown);
}

void SessionTest::savesAtomicallyAndReloads()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("nested/session.json"));

    const auto session = makeSession();
    QVERIFY(SessionSerializer::save(session, path).has_value());
    QVERIFY(QFile::exists(path));
    // Write-temp-then-rename leaves no stray temporary behind.
    QCOMPARE(QDir{directory.filePath(QStringLiteral("nested"))}
                 .entryList(QDir::Files | QDir::Hidden)
                 .size(),
             1);

    const auto reloaded = SessionSerializer::load(path);
    QVERIFY(reloaded.has_value());
    QCOMPARE(reloaded->results.size(), session.results.size());

    QCOMPARE(
        SessionSerializer::load(directory.filePath(QStringLiteral("missing.json"))).error().code,
        ErrorCode::IoFailure);
}

void SessionTest::overallVerdictIsTheWorstOne()
{
    Session session;
    QCOMPARE(session.overallVerdict(), Verdict::Unknown);
    QVERIFY(!session.worstResult().has_value());

    MeasurementPoint pass;
    pass.verdict = Verdict::Pass;
    pass.marginDb = 12.0;
    pass.frequency = megahertz(100);
    MeasurementPoint marginal;
    marginal.verdict = Verdict::Marginal;
    marginal.marginDb = 3.0;
    marginal.frequency = megahertz(200);

    session.results = {pass, marginal};
    QCOMPARE(session.overallVerdict(), Verdict::Marginal);
    QCOMPARE(session.worstResult()->frequency, megahertz(200));

    MeasurementPoint failure;
    failure.verdict = Verdict::Fail;
    failure.marginDb = -4.0;
    failure.frequency = megahertz(300);
    session.results.push_back(failure);
    QCOMPARE(session.overallVerdict(), Verdict::Fail);
    QCOMPARE(session.worstResult()->frequency, megahertz(300));
}

void SessionTest::segmentPlanningSplitsWideSpans()
{
    RunConfiguration config;
    config.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    config.phase1Points = 1001;
    config.phase1Rbw = kilohertz(120);

    // 1000 bins at 120 kHz cover 120 MHz per sweep, so 970 MHz needs 9 segments.
    const auto segments = config.planSegments();
    QCOMPARE(segments.size(), 9U);
    QCOMPARE(segments.front().start, config.span.start);
    QCOMPARE(segments.back().stop, config.span.stop);
    for (std::size_t i = 1; i < segments.size(); ++i) {
        QCOMPARE(segments[i].start, segments[i - 1].stop);
    }

    config.span = FrequencyRange{megahertz(30), megahertz(60)};
    QCOMPARE(config.planSegments().size(), 1U);

    config.span = FrequencyRange{megahertz(60), megahertz(30)};
    QVERIFY(config.planSegments().empty());
}

void SessionTest::phase2UsesTheMandatedBandwidth()
{
    RunConfiguration config;
    config.verificationSpan = kilohertz(200);
    config.dwellTime = std::chrono::milliseconds{1000};

    const auto conducted = config.phase2SweepParams(megahertz(1));
    QCOMPARE(conducted.rbw, kilohertz(9));
    QCOMPARE(conducted.span.centre(), megahertz(1));
    QCOMPARE(conducted.span.width(), kilohertz(200));
    QCOMPARE(conducted.detector, Detector::QuasiPeak);
    QCOMPARE(conducted.sweepTime, std::chrono::milliseconds{1000});

    QCOMPARE(config.phase2SweepParams(megahertz(100)).rbw, kilohertz(120));
    QCOMPARE(config.phase2SweepParams(gigahertz(2.0)).rbw, megahertz(1));

    config.verificationRbw = kilohertz(10);
    QCOMPARE(config.phase2SweepParams(megahertz(100)).rbw, kilohertz(10));
}

void SessionTest::configurationValidationCatchesMistakes()
{
    RunConfiguration config;
    QVERIFY(config.validate().has_value());

    config.span = FrequencyRange{megahertz(100), megahertz(30)};
    QCOMPARE(config.validate().error().code, ErrorCode::InvalidConfiguration);

    config = RunConfiguration{};
    config.dwellTime = std::chrono::milliseconds{0};
    QVERIFY(!config.validate().has_value());

    config = RunConfiguration{};
    config.passes = 0;
    QVERIFY(!config.validate().has_value());
}

void SessionTest::capabilityValidationIsActionable()
{
    Capabilities capabilities;
    capabilities.range = FrequencyRange{hertz(9000), gigahertz(1.5)};
    capabilities.minimumPoints = 601;
    capabilities.maximumPoints = 601;
    capabilities.detectors = {Detector::Peak, Detector::QuasiPeak};
    capabilities.resolutionBandwidths = {kilohertz(10), kilohertz(100)};
    capabilities.preamp = false;

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), gigahertz(3.0)};
    params.points = 601;
    auto status = capabilities.validate(params);
    QVERIFY(!status.has_value());
    QCOMPARE(status.error().code, ErrorCode::UnsupportedSetting);
    QVERIFY(status.error().detail.find("instrument range") != std::string::npos);

    params.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    params.points = 1001;
    QVERIFY(!capabilities.validate(params).has_value());

    params.points = 601;
    params.detector = Detector::Average;
    QVERIFY(!capabilities.validate(params).has_value());

    params.detector = Detector::Peak;
    params.rbw = kilohertz(9);
    QVERIFY(!capabilities.validate(params).has_value());

    params.rbw = kilohertz(10);
    params.preamp = true;
    QVERIFY(!capabilities.validate(params).has_value());

    params.preamp = false;
    QVERIFY(capabilities.validate(params).has_value());
}

void SessionTest::capabilityCoercionSnapsToSupportedValues()
{
    Capabilities capabilities;
    capabilities.range = FrequencyRange{hertz(9000), gigahertz(1.5)};
    capabilities.minimumPoints = 601;
    capabilities.maximumPoints = 601;
    capabilities.detectors = {Detector::Peak};
    capabilities.resolutionBandwidths = {kilohertz(10), kilohertz(100), megahertz(1)};

    SweepParams params;
    params.span = FrequencyRange{hertz(1000), gigahertz(3.0)};
    params.points = 40001;
    params.detector = Detector::QuasiPeak;
    params.rbw = kilohertz(120);
    params.preamp = true;

    const auto coerced = capabilities.coerce(params);
    QCOMPARE(coerced.span.start, hertz(9000));
    QCOMPARE(coerced.span.stop, gigahertz(1.5));
    QCOMPARE(coerced.points, 601);
    QCOMPARE(coerced.detector, Detector::Peak);
    QCOMPARE(coerced.rbw, megahertz(1)); // nearest supported at or above 120 kHz
    QVERIFY(!coerced.preamp);
    QVERIFY(capabilities.validate(coerced).has_value());
}

void SessionTest::isoTimestampsRoundTrip()
{
    const auto now =
        std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const auto text = toIso8601(now);
    QVERIFY(text.ends_with("Z") || text.find('+') != std::string::npos);

    const auto parsed = fromIso8601(text);
    QVERIFY(parsed.has_value());
    QCOMPARE(std::chrono::time_point_cast<std::chrono::milliseconds>(*parsed), now);
    QVERIFY(!fromIso8601("yesterday").has_value());
}

QTEST_APPLESS_MAIN(SessionTest)
#include "SessionTest.moc"
