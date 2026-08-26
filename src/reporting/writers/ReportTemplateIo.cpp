#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/Logging.h>
#include <peakemi/reporting/ReportTemplateIo.h>

#include <QDir>
#include <QStandardPaths>

#include <nlohmann/json.hpp>

namespace peakemi::reporting::report_template {
namespace {

using Json = nlohmann::json;

constexpr const char* SchemaName = "peakemi.report-template";
constexpr int SchemaVersion = 1;

[[nodiscard]] std::string stringField(const Json& json, const char* key)
{
    if (!json.contains(key) || !json.at(key).is_string()) {
        return {};
    }
    return json.at(key).get<std::string>();
}

[[nodiscard]] bool boolField(const Json& json, const char* key, bool fallback)
{
    if (!json.contains(key) || !json.at(key).is_boolean()) {
        return fallback;
    }
    return json.at(key).get<bool>();
}

} // namespace

std::string toJsonText(const ReportTemplate& reportTemplate)
{
    const Json document{{"schema", SchemaName},
                        {"schema_version", SchemaVersion},
                        {"company_name", reportTemplate.companyName},
                        {"address", reportTemplate.address},
                        {"logo_path", reportTemplate.logoPath},
                        {"title", reportTemplate.title},
                        {"introduction", reportTemplate.introduction},
                        {"conclusion", reportTemplate.conclusion},
                        {"include_correction_tables", reportTemplate.includeCorrectionTables},
                        {"include_trace_plot", reportTemplate.includeTracePlot},
                        {"include_notes", reportTemplate.includeNotes}};
    return document.dump(2);
}

Result<ReportTemplate> fromJsonText(std::string_view text)
{
    Json document = Json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail(ErrorCode::ParseFailure, "report template is not valid JSON");
    }
    if (document.value("schema", std::string{}) != SchemaName) {
        return fail(ErrorCode::ParseFailure, "not a PeakEmi report template");
    }
    if (document.value("schema_version", 0) > SchemaVersion) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "the template was written by a newer PeakEmi");
    }

    ReportTemplate reportTemplate;
    reportTemplate.companyName = stringField(document, "company_name");
    reportTemplate.address = stringField(document, "address");
    reportTemplate.logoPath = stringField(document, "logo_path");
    reportTemplate.introduction = stringField(document, "introduction");
    reportTemplate.conclusion = stringField(document, "conclusion");
    // An empty title would produce an untitled report; keep the default instead.
    if (const auto title = stringField(document, "title"); !title.empty()) {
        reportTemplate.title = title;
    }
    reportTemplate.includeCorrectionTables =
        boolField(document, "include_correction_tables", reportTemplate.includeCorrectionTables);
    reportTemplate.includeTracePlot =
        boolField(document, "include_trace_plot", reportTemplate.includeTracePlot);
    reportTemplate.includeNotes = boolField(document, "include_notes", reportTemplate.includeNotes);
    return reportTemplate;
}

Status save(const ReportTemplate& reportTemplate, const QString& path)
{
    return writeFileAtomically(path, toJsonText(reportTemplate));
}

Result<ReportTemplate> load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return fromJsonText(*content);
}

QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/report-template.json");
}

ReportTemplate loadDefault()
{
    auto stored = load(defaultPath());
    if (stored) {
        return *stored;
    }
    // A missing file is the normal first-run case and not worth reporting; a
    // malformed one is worth a log line before falling back.
    if (stored.error().code != ErrorCode::IoFailure) {
        qCWarning(lcReport) << "ignoring the stored report template:"
                            << QString::fromStdString(stored.error().message());
    }
    return ReportTemplate{};
}

Status saveDefault(const ReportTemplate& reportTemplate)
{
    return save(reportTemplate, defaultPath());
}

} // namespace peakemi::reporting::report_template
