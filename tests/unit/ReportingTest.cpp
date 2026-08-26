#include "TestSupport.h"

#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/reporting/CsvExporter.h>
#include <peakemi/reporting/JsonExporter.h>
#include <peakemi/reporting/PdfReportRenderer.h>
#include <peakemi/reporting/ReportTemplateIo.h>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <nlohmann/json.hpp>

#include <limits>

using namespace peakemi;

namespace {

[[nodiscard]] Session makeSession()
{
    Session session = Session::createNew("0.1.0");
    session.meta.eutName = "Widget rev C";
    session.meta.operatorName = "Operator";
    session.meta.notes = "Chamber at 3 m";
    session.config.limits = {*builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)")};

    CorrectionTable antenna;
    antenna.name = "biconilog";
    antenna.kind = CorrectionKind::AntennaFactor;
    antenna.points = {{megahertz(30), 12.5}, {gigahertz(1.0), 24.0}};
    session.config.corrections = {antenna};

    Trace trace;
    trace.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(1030)}, 4);
    trace.amplitudes = {10.0, 20.0, 30.0, 15.0};
    trace.unit = AmplitudeUnit::dBuV_per_m;
    trace.label = "Phase 1";
    trace.corrected = true;
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
    session.results.push_back(point);
    return session;
}

} // namespace

class ReportingTest : public QObject
{
    Q_OBJECT

private slots:
    void resultCsvCarriesContextAndDisclaimer();
    void traceCsvListsEveryPoint();
    void csvQuotesEmbeddedSeparators();
    void pdfReportIsWritten();
    void pdfReportRefusesAnUnwritablePath();
    void jsonResultsCarryTheDisclaimerAndProvenance();
    void jsonResultsStoreAMissingLimitAsNull();
    void jsonResultsAreWrittenToDisk();
    void reportTemplateRoundTrips();
    void reportTemplateKeepsItsTitleWhenBlank();
    void reportTemplateRejectsForeignFiles();
};

void ReportingTest::resultCsvCarriesContextAndDisclaimer()
{
    const auto csv = reporting::csv::resultsToCsv(makeSession());

    QVERIFY(csv.find("# PeakEmi measurement export") != std::string::npos);
    QVERIFY(csv.find("# eut: Widget rev C") != std::string::npos);
    QVERIFY(csv.find("# correction: biconilog") != std::string::npos);
    QVERIFY(csv.find("# limit: CISPR 32 Class B radiated 10 m (QP)") != std::string::npos);
    QVERIFY(csv.find("run_id") != std::string::npos);
    QVERIFY(csv.find("144.000000") != std::string::npos);
    QVERIFY(csv.find("fail") != std::string::npos);
    QVERIFY(csv.find(",C,") != std::string::npos); // CISPR band of 144 MHz

    // The disclaimer is wrapped over several comment lines but must be complete.
    // Everything before it is `# key: value` metadata or the export title.
    std::string disclaimerText;
    for (const auto& line : QString::fromStdString(csv).split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("# PeakEmi measurement export"))) {
            continue;
        }
        if (line.startsWith(QStringLiteral("# ")) && !line.contains(QLatin1Char(':'))) {
            if (!disclaimerText.empty()) {
                disclaimerText += ' ';
            }
            disclaimerText += line.mid(2).toStdString();
        }
    }
    QCOMPARE(disclaimerText, std::string{ComplianceDisclaimer});
}

void ReportingTest::traceCsvListsEveryPoint()
{
    const auto session = makeSession();
    const auto csv = reporting::csv::traceToCsv(session, session.traces.front());

    QVERIFY(csv.find("# trace: Phase 1") != std::string::npos);
    QVERIFY(csv.find("# unit: dBuV/m") != std::string::npos);
    QVERIFY(csv.find("# corrections_applied: yes") != std::string::npos);
    QVERIFY(csv.find("frequency_hz,amplitude_dBuV/m") != std::string::npos);
    QVERIFY(csv.find("30000000,10.00") != std::string::npos);
    QVERIFY(csv.find("1030000000,15.00") != std::string::npos);
}

void ReportingTest::csvQuotesEmbeddedSeparators()
{
    auto session = makeSession();
    session.results.front().corrections.push_back(
        AppliedCorrection{"cable, long", CorrectionKind::CableLoss, 2.0, 2.0});

    const auto csv = reporting::csv::resultsToCsv(session);
    QVERIFY(csv.find("\"biconilog=13.50 dB; cable, long=2.00 dB\"") != std::string::npos);
}

void ReportingTest::pdfReportIsWritten()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("report.pdf"));

    QImage plot{400, 200, QImage::Format_ARGB32};
    plot.fill(Qt::white);

    ReportTemplate reportTemplate;
    reportTemplate.companyName = "Bitcrush";
    reportTemplate.introduction = "Pre-compliance scan of the prototype.";

    reporting::PdfReportRenderer renderer;
    renderer.setPlotImage(plot);
    QVERIFY(renderer.render(makeSession(), reportTemplate, path).has_value());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto content = file.readAll();
    QVERIFY(content.startsWith("%PDF"));
    QVERIFY(content.size() > 1000);
}

void ReportingTest::pdfReportRefusesAnUnwritablePath()
{
    reporting::PdfReportRenderer renderer;
    const auto status = renderer.render(
        makeSession(), ReportTemplate{}, QStringLiteral("/definitely/not/a/directory/report.pdf"));
    QVERIFY(!status.has_value());
    QCOMPARE(status.error().code, ErrorCode::IoFailure);
}

void ReportingTest::jsonResultsCarryTheDisclaimerAndProvenance()
{
    const auto session = makeSession();
    const auto document = nlohmann::json::parse(reporting::json_export::resultsToJson(session));

    QCOMPARE(document.at("schema").get<std::string>(), std::string{"peakemi.results"});
    QCOMPARE(document.at("schema_version").get<int>(), reporting::json_export::SchemaVersion);
    QCOMPARE(document.at("disclaimer").get<std::string>(), std::string{ComplianceDisclaimer});
    QCOMPARE(document.at("run_id").get<std::string>(), session.meta.runId);
    QCOMPARE(document.at("eut").at("name").get<std::string>(), std::string{"Widget rev C"});
    QCOMPARE(document.at("overall_verdict").get<std::string>(), std::string{"fail"});
    QCOMPARE(document.at("limits").size(), 1U);
    QCOMPARE(document.at("corrections").size(), 1U);

    QCOMPARE(document.at("results").size(), 1U);
    const auto& point = document.at("results").front();
    QCOMPARE(point.at("frequency_hz").get<std::int64_t>(), 144'000'000);
    QCOMPARE(point.at("cispr_band").get<std::string>(), std::string{"C"});
    QCOMPARE(point.at("verdict").get<std::string>(), std::string{"fail"});
    QCOMPARE(point.at("detector").get<std::string>(), std::string{"quasi-peak"});
    QCOMPARE(point.at("unit").get<std::string>(), std::string{"dBuV/m"});
    QCOMPARE(point.at("corrections").size(), 1U);
}

void ReportingTest::jsonResultsStoreAMissingLimitAsNull()
{
    auto session = makeSession();
    session.results.front().limitValue = std::numeric_limits<double>::quiet_NaN();
    session.results.front().marginDb = std::numeric_limits<double>::quiet_NaN();

    // NaN has no JSON spelling; the value must be null, never a silent zero.
    const auto document = nlohmann::json::parse(reporting::json_export::resultsToJson(session));
    const auto& point = document.at("results").front();
    QVERIFY(point.at("limit").is_null());
    QVERIFY(point.at("margin_db").is_null());
}

void ReportingTest::jsonResultsAreWrittenToDisk()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("results.json"));

    const auto status = reporting::json_export::writeResults(makeSession(), path);
    QVERIFY2(status.has_value(), test::errorText(status).constData());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(nlohmann::json::accept(file.readAll().toStdString()));
}

void ReportingTest::reportTemplateRoundTrips()
{
    ReportTemplate original;
    original.companyName = "Bitcrush";
    original.address = "Somewhere 1";
    original.logoPath = "/tmp/logo.png";
    original.title = "Radiated pre-compliance";
    original.introduction = "Scan of the prototype.";
    original.conclusion = "Three emissions need attention.";
    original.includeCorrectionTables = false;
    original.includeNotes = false;

    const auto restored =
        reporting::report_template::fromJsonText(reporting::report_template::toJsonText(original));
    QVERIFY2(restored.has_value(), test::errorText(restored).constData());
    QCOMPARE(restored->companyName, original.companyName);
    QCOMPARE(restored->address, original.address);
    QCOMPARE(restored->logoPath, original.logoPath);
    QCOMPARE(restored->title, original.title);
    QCOMPARE(restored->introduction, original.introduction);
    QCOMPARE(restored->conclusion, original.conclusion);
    QCOMPARE(restored->includeCorrectionTables, false);
    QCOMPARE(restored->includeNotes, false);
    QCOMPARE(restored->includeTracePlot, true);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("template.json"));
    QVERIFY(reporting::report_template::save(original, path).has_value());
    QCOMPARE(reporting::report_template::load(path)->companyName, original.companyName);
}

void ReportingTest::reportTemplateKeepsItsTitleWhenBlank()
{
    // An empty title would produce an untitled report, so the default stands.
    const auto restored = reporting::report_template::fromJsonText(
        R"({"schema":"peakemi.report-template","schema_version":1,"title":""})");
    QVERIFY(restored.has_value());
    QCOMPARE(restored->title, ReportTemplate{}.title);
}

void ReportingTest::reportTemplateRejectsForeignFiles()
{
    QCOMPARE(reporting::report_template::fromJsonText("not json").error().code,
             ErrorCode::ParseFailure);
    QCOMPARE(
        reporting::report_template::fromJsonText(R"({"schema":"something.else"})").error().code,
        ErrorCode::ParseFailure);
    QCOMPARE(reporting::report_template::fromJsonText(
                 R"({"schema":"peakemi.report-template","schema_version":99})")
                 .error()
                 .code,
             ErrorCode::SchemaVersionUnsupported);
}

QTEST_MAIN(ReportingTest)
#include "ReportingTest.moc"
