#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/LimitCatalogue.h>
#include <peakemi/reporting/CsvExporter.h>
#include <peakemi/reporting/PdfReportRenderer.h>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

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

QTEST_MAIN(ReportingTest)
#include "ReportingTest.moc"
