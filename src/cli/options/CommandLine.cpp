#include <peakemi/cli/CommandLine.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QRegularExpression>

#include <cmath>
#include <utility>

namespace peakemi::cli {
namespace {

/// Option names, spelled once. A typo between the definition and the read is
/// the classic way a command-line option silently stops working.
namespace name {

const auto Instrument = QStringLiteral("instrument");
const auto Driver = QStringLiteral("driver");
const auto Session = QStringLiteral("session");
const auto Limit = QStringLiteral("limit");
const auto Correction = QStringLiteral("correction");
const auto Start = QStringLiteral("start");
const auto Stop = QStringLiteral("stop");
const auto Points = QStringLiteral("points");
const auto RefLevel = QStringLiteral("ref-level");
const auto Detector = QStringLiteral("detector");
const auto Dwell = QStringLiteral("dwell");
const auto Passes = QStringLiteral("passes");
const auto MaxPeaks = QStringLiteral("max-peaks");
const auto Margin = QStringLiteral("margin");
const auto StartCommand = QStringLiteral("start-command");
const auto StopCommand = QStringLiteral("stop-command");
const auto Eut = QStringLiteral("eut");
const auto EutSerial = QStringLiteral("eut-serial");
const auto Operator = QStringLiteral("operator");
const auto Company = QStringLiteral("company");
const auto Notes = QStringLiteral("notes");
const auto RunId = QStringLiteral("run-id");
const auto OutputDir = QStringLiteral("output-dir");
const auto OutSession = QStringLiteral("out-session");
const auto OutResultsCsv = QStringLiteral("out-results-csv");
const auto OutResultsJson = QStringLiteral("out-results-json");
const auto OutTraceCsv = QStringLiteral("out-trace-csv");
const auto OutReportPdf = QStringLiteral("out-report-pdf");
const auto ReportTemplate = QStringLiteral("report-template");
const auto FailOnOption = QStringLiteral("fail-on");
const auto Summary = QStringLiteral("summary");
const auto Quiet = QStringLiteral("quiet");
const auto Verbose = QStringLiteral("verbose");
const auto LogDir = QStringLiteral("log-dir");
const auto ListDrivers = QStringLiteral("list-drivers");
const auto ListLimits = QStringLiteral("list-limits");

} // namespace name

[[nodiscard]] std::unexpected<Error> usage(const QString& detail)
{
    return fail(ErrorCode::InvalidConfiguration, detail.toStdString());
}

[[nodiscard]] Result<int> parseCount(const QString& option, const QString& text, int minimum)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < minimum) {
        return usage(QStringLiteral("--%1 expects a whole number of at least %2, got '%3'")
                         .arg(option, QString::number(minimum), text));
    }
    return value;
}

[[nodiscard]] Result<double> parseNumber(const QString& option, const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return usage(QStringLiteral("--%1 expects a number, got '%2'").arg(option, text));
    }
    return value;
}

} // namespace

Result<Hertz> parseFrequency(const QString& text)
{
    static const QRegularExpression pattern{QStringLiteral(
        R"(^\s*([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*([kKmMgG]?)(?:[hH][zZ])?\s*$)")};
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return usage(
            QStringLiteral("'%1' is not a frequency; write 30M, 150kHz or 1000000").arg(text));
    }

    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok || !std::isfinite(value) || value < 0.0) {
        return usage(QStringLiteral("'%1' is not a frequency PeakEmi can sweep").arg(text));
    }

    const auto suffix = match.captured(2).toLower();
    if (suffix == QStringLiteral("k")) {
        return kilohertz(value);
    }
    if (suffix == QStringLiteral("m")) {
        return megahertz(value);
    }
    if (suffix == QStringLiteral("g")) {
        return gigahertz(value);
    }
    return Hertz{static_cast<std::int64_t>(value + 0.5)};
}

Result<std::chrono::milliseconds> parseDuration(const QString& text)
{
    static const QRegularExpression pattern{
        QStringLiteral(R"(^\s*(\d+\.?\d*|\.\d+)\s*(ms|s|min|m)?\s*$)")};
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return usage(QStringLiteral("'%1' is not a duration; write 1s, 500ms or 2min").arg(text));
    }

    const double value = match.captured(1).toDouble();
    const auto unit = match.captured(2);
    double milliseconds = value;
    if (unit == QStringLiteral("s")) {
        milliseconds = value * 1e3;
    } else if (unit == QStringLiteral("min") || unit == QStringLiteral("m")) {
        milliseconds = value * 60e3;
    }
    if (milliseconds <= 0.0) {
        return usage(QStringLiteral("'%1' is not a positive duration").arg(text));
    }
    return std::chrono::milliseconds{static_cast<std::int64_t>(milliseconds + 0.5)};
}

Result<TransportDescriptor> parseEndpoint(const QString& text)
{
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return usage(
            QStringLiteral("--instrument needs an endpoint, for example tcp:192.168.1.20"));
    }
    if (trimmed.compare(QStringLiteral("sim"), Qt::CaseInsensitive) == 0 ||
        trimmed.compare(QStringLiteral("simulated"), Qt::CaseInsensitive) == 0)
    {
        return TransportDescriptor{.kind = TransportKind::Simulated};
    }

    const auto separator = trimmed.indexOf(QLatin1Char(':'));
    if (separator <= 0) {
        return usage(
            QStringLiteral("'%1' has no bus; write tcp:%1, vxi11:%1 or serial:%1").arg(trimmed));
    }
    const auto busText = trimmed.left(separator).toLower();
    const auto address = trimmed.mid(separator + 1);
    const auto bus = transportKindFromKey(busText.toStdString());
    if (!bus) {
        return usage(QStringLiteral("'%1' is not a bus PeakEmi speaks; expected one of "
                                    "tcp, vxi11, usbtmc, serial, visa or sim")
                         .arg(busText));
    }
    if (address.isEmpty()) {
        return usage(QStringLiteral("'%1' names a bus but no address").arg(trimmed));
    }

    TransportDescriptor descriptor{.kind = *bus};

    // The trailing ":number" is a port on TCP and a baud rate on serial; a VISA
    // or USBTMC resource string is full of colons and is taken verbatim.
    if (*bus == TransportKind::Tcp || *bus == TransportKind::Vxi11 || *bus == TransportKind::Serial)
    {
        const auto tail = address.lastIndexOf(QLatin1Char(':'));
        bool numeric = false;
        const int number = tail < 0 ? 0 : address.mid(tail + 1).toInt(&numeric);
        if (numeric && number > 0) {
            descriptor.address = address.left(tail).toStdString();
            if (*bus == TransportKind::Serial) {
                descriptor.baudRate = number;
            } else {
                descriptor.port = number;
            }
        } else {
            descriptor.address = address.toStdString();
        }
    } else {
        descriptor.address = address.toStdString();
    }

    if (descriptor.address.empty()) {
        return usage(QStringLiteral("'%1' names a bus but no address").arg(trimmed));
    }
    return descriptor;
}

namespace {

/// Defines the whole option surface. Shared by the parse and by the usage text,
/// so a command line that is refused is described by the same list that would
/// have accepted it.
void defineOptions(QCommandLineParser& parser)
{
    parser.setApplicationDescription(QStringLiteral(
        "Run one EMI pre-compliance measurement without a GUI and report the verdict as the\n"
        "process exit code: 0 within the limits, 1 outside them, 2 a bad command line,\n"
        "3 a run that could not be completed, 4 a run cancelled with Ctrl-C."));
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);

    const QList<QCommandLineOption> options{
        {name::Instrument,
         QStringLiteral("Endpoint to measure with: sim, tcp:HOST[:PORT], vxi11:HOST, "
                        "serial:DEVICE[:BAUD], usbtmc:RESOURCE or visa:RESOURCE. Default: sim."),
         QStringLiteral("endpoint")},
        {name::Driver,
         QStringLiteral("Driver id to use instead of matching on the *IDN? reply."),
         QStringLiteral("id")},
        {name::Session,
         QStringLiteral("Session file supplying the run configuration and the EUT metadata."),
         QStringLiteral("file")},
        {name::Limit,
         QStringLiteral("Limit line: a built-in catalogue name or a CSV/JSON file. Repeatable."),
         QStringLiteral("name|file")},
        {name::Correction,
         QStringLiteral("Correction table file (antenna factor, cable loss, ...). Repeatable."),
         QStringLiteral("file")},
        {name::Start,
         QStringLiteral("Start of the scan span, e.g. 30M."),
         QStringLiteral("frequency")},
        {name::Stop, QStringLiteral("End of the scan span, e.g. 1G."), QStringLiteral("frequency")},
        {name::Points, QStringLiteral("Phase 1 trace points."), QStringLiteral("count")},
        {name::RefLevel, QStringLiteral("Reference level in dB."), QStringLiteral("dB")},
        {name::Detector,
         QStringLiteral("Phase 2 detector: peak, quasi-peak, average, rms or sample."),
         QStringLiteral("name")},
        {name::Dwell, QStringLiteral("Phase 2 dwell time, e.g. 1s."), QStringLiteral("duration")},
        {name::Passes,
         QStringLiteral("Max-hold passes over the whole loop."),
         QStringLiteral("count")},
        {name::MaxPeaks,
         QStringLiteral("Upper bound on the Phase 2 dwells."),
         QStringLiteral("count")},
        {name::Margin,
         QStringLiteral("Flag peaks whose margin to the limit is below this, in dB."),
         QStringLiteral("dB")},
        {name::StartCommand,
         QStringLiteral("SCPI command sent before the first sweep. Repeatable, order kept."),
         QStringLiteral("scpi")},
        {name::StopCommand,
         QStringLiteral("SCPI command sent when the run ends, however it ends. Repeatable."),
         QStringLiteral("scpi")},
        {name::Eut, QStringLiteral("Name of the equipment under test."), QStringLiteral("name")},
        {name::EutSerial, QStringLiteral("Serial number of the EUT."), QStringLiteral("serial")},
        {name::Operator,
         QStringLiteral("Operator recorded in the session."),
         QStringLiteral("name")},
        {name::Company,
         QStringLiteral("Company recorded in the session and the report."),
         QStringLiteral("name")},
        {name::Notes,
         QStringLiteral("Free-text note stored with the run."),
         QStringLiteral("text")},
        {name::RunId,
         QStringLiteral("Run identifier repeated in every export. Default: generated."),
         QStringLiteral("id")},
        {name::OutputDir,
         QStringLiteral("Write session.peakemi.json, results.csv, results.json, trace.csv and "
                        "report.pdf into this directory."),
         QStringLiteral("directory")},
        {name::OutSession, QStringLiteral("Write the session file here."), QStringLiteral("file")},
        {name::OutResultsCsv,
         QStringLiteral("Write the result table as CSV here."),
         QStringLiteral("file")},
        {name::OutResultsJson,
         QStringLiteral("Write the result table as JSON here."),
         QStringLiteral("file")},
        {name::OutTraceCsv,
         QStringLiteral("Write the Phase 1 trace as CSV here."),
         QStringLiteral("file")},
        {name::OutReportPdf, QStringLiteral("Write the PDF report here."), QStringLiteral("file")},
        {name::ReportTemplate,
         QStringLiteral("Report branding template. Default: the one saved by the application."),
         QStringLiteral("file")},
        {name::FailOnOption,
         QStringLiteral("Worst verdict that still exits 0: never, fail (default) or marginal."),
         QStringLiteral("verdict")},
        {name::Summary,
         QStringLiteral("Summary written to stdout: text (default) or json."),
         QStringLiteral("format")},
        {name::LogDir,
         QStringLiteral("Also write the rotating log file into this directory."),
         QStringLiteral("directory")},
    };
    parser.addOptions(options);
    parser.addOption({name::Quiet, QStringLiteral("Print the summary and errors, nothing else.")});
    parser.addOption({name::Verbose, QStringLiteral("Log the full SCPI transcript.")});
    parser.addOption(
        {name::ListDrivers, QStringLiteral("List the drivers this build has, then exit.")});
    parser.addOption(
        {name::ListLimits, QStringLiteral("List the built-in limit lines, then exit.")});
}

} // namespace

QString usageText()
{
    QCommandLineParser parser;
    defineOptions(parser);
    parser.addHelpOption();
    parser.addVersionOption();
    return parser.helpText();
}

Result<CommandLine> parseCommandLine(const QStringList& arguments)
{
    QCommandLineParser parser;
    defineOptions(parser);
    const auto helpOption = parser.addHelpOption();
    const auto versionOption = parser.addVersionOption();

    CommandLine result;
    result.helpText = parser.helpText();

    if (!parser.parse(arguments)) {
        return usage(parser.errorText());
    }
    if (parser.isSet(helpOption)) {
        result.action = Action::ShowHelp;
        return result;
    }
    if (parser.isSet(versionOption)) {
        result.action = Action::ShowVersion;
        return result;
    }
    if (parser.isSet(name::ListDrivers)) {
        result.action = Action::ListDrivers;
        return result;
    }
    if (parser.isSet(name::ListLimits)) {
        result.action = Action::ListLimits;
        return result;
    }
    if (!parser.positionalArguments().isEmpty()) {
        return usage(QStringLiteral("unexpected argument '%1'; every input is a named option")
                         .arg(parser.positionalArguments().first()));
    }

    auto& out = result.options;

    if (parser.isSet(name::Instrument)) {
        auto endpoint = parseEndpoint(parser.value(name::Instrument));
        if (!endpoint) {
            return std::unexpected(endpoint.error());
        }
        out.endpoint = *endpoint;
    }
    out.driverId = parser.value(name::Driver).toStdString();
    out.sessionPath = parser.value(name::Session);
    out.limits = parser.values(name::Limit);
    out.corrections = parser.values(name::Correction);

    // An option nobody passed stays std::nullopt, so it cannot overwrite what a
    // session file already said.
    if (parser.isSet(name::Start)) {
        auto value = parseFrequency(parser.value(name::Start));
        if (!value) {
            return std::unexpected(value.error());
        }
        out.start = *value;
    }
    if (parser.isSet(name::Stop)) {
        auto value = parseFrequency(parser.value(name::Stop));
        if (!value) {
            return std::unexpected(value.error());
        }
        out.stop = *value;
    }
    if (parser.isSet(name::Dwell)) {
        auto value = parseDuration(parser.value(name::Dwell));
        if (!value) {
            return std::unexpected(value.error());
        }
        out.dwell = *value;
    }
    if (parser.isSet(name::Points)) {
        auto value = parseCount(name::Points, parser.value(name::Points), 2);
        if (!value) {
            return std::unexpected(value.error());
        }
        out.points = *value;
    }
    if (parser.isSet(name::Passes)) {
        auto value = parseCount(name::Passes, parser.value(name::Passes), 1);
        if (!value) {
            return std::unexpected(value.error());
        }
        out.passes = *value;
    }
    if (parser.isSet(name::MaxPeaks)) {
        auto value = parseCount(name::MaxPeaks, parser.value(name::MaxPeaks), 1);
        if (!value) {
            return std::unexpected(value.error());
        }
        out.maximumPeaks = *value;
    }

    if (parser.isSet(name::RefLevel)) {
        auto value = parseNumber(name::RefLevel, parser.value(name::RefLevel));
        if (!value) {
            return std::unexpected(value.error());
        }
        out.refLevel = decibel(*value);
    }
    if (parser.isSet(name::Margin)) {
        auto value = parseNumber(name::Margin, parser.value(name::Margin));
        if (!value) {
            return std::unexpected(value.error());
        }
        out.marginThresholdDb = *value;
    }
    if (parser.isSet(name::Detector)) {
        const auto key = parser.value(name::Detector).toLower().toStdString();
        const auto detector = detectorFromKey(key);
        if (!detector) {
            return usage(QStringLiteral("'%1' is not a detector; expected peak, quasi-peak, "
                                        "average, rms or sample")
                             .arg(parser.value(name::Detector)));
        }
        out.verificationDetector = *detector;
    }

    for (const auto& command : parser.values(name::StartCommand)) {
        out.startCommands.push_back(command.toStdString());
    }
    for (const auto& command : parser.values(name::StopCommand)) {
        out.stopCommands.push_back(command.toStdString());
    }

    out.eutName = parser.value(name::Eut).toStdString();
    out.eutSerial = parser.value(name::EutSerial).toStdString();
    out.operatorName = parser.value(name::Operator).toStdString();
    out.company = parser.value(name::Company).toStdString();
    out.notes = parser.value(name::Notes).toStdString();
    out.runId = parser.value(name::RunId).toStdString();

    out.outputDirectory = parser.value(name::OutputDir);
    out.sessionOutput = parser.value(name::OutSession);
    out.resultsCsvOutput = parser.value(name::OutResultsCsv);
    out.resultsJsonOutput = parser.value(name::OutResultsJson);
    out.traceCsvOutput = parser.value(name::OutTraceCsv);
    out.reportPdfOutput = parser.value(name::OutReportPdf);
    out.reportTemplatePath = parser.value(name::ReportTemplate);
    out.logDirectory = parser.value(name::LogDir);

    if (parser.isSet(name::FailOnOption)) {
        const auto value = parser.value(name::FailOnOption).toLower();
        if (value == QStringLiteral("never")) {
            out.failOn = FailOn::Never;
        } else if (value == QStringLiteral("fail")) {
            out.failOn = FailOn::Fail;
        } else if (value == QStringLiteral("marginal")) {
            out.failOn = FailOn::Marginal;
        } else {
            return usage(QStringLiteral("--fail-on expects never, fail or marginal, got '%1'")
                             .arg(parser.value(name::FailOnOption)));
        }
    }
    if (parser.isSet(name::Summary)) {
        const auto value = parser.value(name::Summary).toLower();
        if (value == QStringLiteral("text")) {
            out.summary = SummaryFormat::Text;
        } else if (value == QStringLiteral("json")) {
            out.summary = SummaryFormat::Json;
        } else {
            return usage(QStringLiteral("--summary expects text or json, got '%1'")
                             .arg(parser.value(name::Summary)));
        }
    }
    out.quiet = parser.isSet(name::Quiet);
    out.verbose = parser.isSet(name::Verbose);

    if (out.start && out.stop && *out.stop <= *out.start) {
        return usage(QStringLiteral("--start must be below --stop"));
    }
    if (out.sessionPath.isEmpty() && out.limits.isEmpty()) {
        return usage(QStringLiteral(
            "nothing to measure against: pass --limit, or --session with a configured one. "
            "--list-limits shows the built-in catalogue."));
    }

    return result;
}

} // namespace peakemi::cli
