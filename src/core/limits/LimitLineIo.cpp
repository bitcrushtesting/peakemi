#include "session/JsonConversions.h"

#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/LimitLineIo.h>

#include <QFileInfo>

#include <cstddef>
#include <exception>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace peakemi {
namespace {

using json_io::Json;

struct CsvDocument
{
    std::map<std::string, std::string> metadata; ///< from the `# key: value` block
    std::vector<std::vector<std::string>> rows;  ///< data rows, header stripped
};

[[nodiscard]] std::string trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

[[nodiscard]] bool looksNumeric(std::string_view text)
{
    const auto trimmed = trim(text);
    if (trimmed.empty()) {
        return false;
    }
    const char first = trimmed.front();
    return (first >= '0' && first <= '9') || first == '-' || first == '+' || first == '.';
}

[[nodiscard]] std::vector<std::string> splitFields(std::string_view line)
{
    std::vector<std::string> fields;
    std::string current;
    for (const char character : line) {
        if (character == ',' || character == ';' || character == '\t') {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    fields.push_back(trim(current));
    return fields;
}

[[nodiscard]] CsvDocument parseCsv(std::string_view text)
{
    CsvDocument document;
    std::istringstream stream{std::string{text}};
    std::string line;
    while (std::getline(stream, line)) {
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }
        if (trimmed.front() == '#') {
            const auto body = trim(trimmed.substr(1));
            const auto colon = body.find(':');
            if (colon != std::string::npos) {
                document.metadata.emplace(trim(body.substr(0, colon)),
                                          trim(body.substr(colon + 1)));
            }
            continue;
        }
        auto fields = splitFields(trimmed);
        if (fields.empty() || !looksNumeric(fields.front())) {
            continue; // header row or stray text
        }
        document.rows.push_back(std::move(fields));
    }
    return document;
}

[[nodiscard]] std::string metadataValue(const CsvDocument& document, const std::string& key)
{
    const auto found = document.metadata.find(key);
    return found == document.metadata.end() ? std::string{} : found->second;
}

[[nodiscard]] std::optional<double> parseNumber(const std::string& text)
{
    try {
        std::size_t consumed = 0;
        const double value = std::stod(text, &consumed);
        return consumed > 0 ? std::optional<double>{value} : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::string formatDouble(double value)
{
    std::ostringstream stream;
    stream.precision(6);
    stream << value;
    return stream.str();
}

[[nodiscard]] bool hasSuffix(const QString& path, const char* suffix)
{
    return QFileInfo{path}.suffix().compare(QString::fromUtf8(suffix), Qt::CaseInsensitive) == 0;
}

} // namespace

namespace limit_io {

Result<LimitLine> fromJsonText(std::string_view text)
{
    Json document = Json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        return fail(ErrorCode::ParseFailure, "limit file is not valid JSON");
    }
    return json_io::limitLineFromJson(document);
}

std::string toJsonText(const LimitLine& line)
{
    return json_io::toJson(line).dump(2);
}

Result<LimitLine> fromCsvText(std::string_view text)
{
    const auto document = parseCsv(text);
    if (document.rows.empty()) {
        return fail(ErrorCode::ParseFailure, "limit CSV contains no breakpoints");
    }

    LimitLine line;
    line.name = metadataValue(document, "name");
    line.standard = metadataValue(document, "standard");
    line.note = metadataValue(document, "note");
    if (const auto unit = amplitudeUnitFromKey(metadataValue(document, "unit"))) {
        line.unit = *unit;
    }
    if (const auto detector = detectorFromKey(metadataValue(document, "detector"))) {
        line.detector = *detector;
    }
    if (const auto kind = emissionKindFromKey(metadataValue(document, "kind"))) {
        line.kind = *kind;
    }
    if (const auto equipmentClass = equipmentClassFromKey(metadataValue(document, "class"))) {
        line.equipmentClass = *equipmentClass;
    }
    if (const auto distance = parseNumber(metadataValue(document, "distance_m"))) {
        line.measurementDistanceMetres = *distance;
    }

    for (const auto& row : document.rows) {
        if (row.size() < 2) {
            return fail(ErrorCode::ParseFailure, "limit CSV row needs frequency and amplitude");
        }
        const auto frequency = parseNumber(row[0]);
        const auto amplitude = parseNumber(row[1]);
        if (!frequency || !amplitude) {
            return fail(ErrorCode::ParseFailure, "limit CSV row '" + row[0] + "' is not numeric");
        }
        Interpolation interpolation = Interpolation::LogFrequency;
        if (row.size() > 2) {
            interpolation = interpolationFromKey(row[2]).value_or(interpolation);
        }
        line.points.push_back(LimitPoint{.frequency = hertz(static_cast<std::int64_t>(*frequency)),
                                         .amplitude = *amplitude,
                                         .interpolationToNext = interpolation});
    }

    line.sortPoints();
    if (auto status = line.validate(); !status) {
        return std::unexpected(status.error());
    }
    return line;
}

std::string toCsvText(const LimitLine& line)
{
    std::ostringstream stream;
    stream << "# name: " << line.name << '\n'
           << "# standard: " << line.standard << '\n'
           << "# note: " << line.note << '\n'
           << "# kind: " << emissionKindKey(line.kind) << '\n'
           << "# class: " << equipmentClassKey(line.equipmentClass) << '\n'
           << "# detector: " << detectorKey(line.detector) << '\n'
           << "# unit: " << amplitudeUnitKey(line.unit) << '\n'
           << "# distance_m: " << formatDouble(line.measurementDistanceMetres) << '\n'
           << "frequency_hz,amplitude,interpolation\n";
    for (const auto& point : line.points) {
        stream << point.frequency.value() << ',' << formatDouble(point.amplitude) << ','
               << interpolationKey(point.interpolationToNext) << '\n';
    }
    return stream.str();
}

Result<LimitLine> load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return hasSuffix(path, "json") ? fromJsonText(*content) : fromCsvText(*content);
}

Status save(const LimitLine& line, const QString& path)
{
    return writeFileAtomically(path, hasSuffix(path, "json") ? toJsonText(line) : toCsvText(line));
}

} // namespace limit_io

namespace correction_io {

Result<CorrectionTable> fromJsonText(std::string_view text)
{
    Json document = Json::parse(text, nullptr, false);
    if (document.is_discarded()) {
        return fail(ErrorCode::ParseFailure, "correction file is not valid JSON");
    }
    return json_io::correctionTableFromJson(document);
}

std::string toJsonText(const CorrectionTable& table)
{
    return json_io::toJson(table).dump(2);
}

Result<CorrectionTable> fromCsvText(std::string_view text)
{
    const auto document = parseCsv(text);
    if (document.rows.empty()) {
        return fail(ErrorCode::ParseFailure, "correction CSV contains no points");
    }

    CorrectionTable table;
    table.name = metadataValue(document, "name");
    if (const auto kind = correctionKindFromKey(metadataValue(document, "kind"))) {
        table.kind = *kind;
    }
    for (const auto& row : document.rows) {
        if (row.size() < 2) {
            return fail(ErrorCode::ParseFailure, "correction CSV row needs frequency and value");
        }
        const auto frequency = parseNumber(row[0]);
        const auto value = parseNumber(row[1]);
        if (!frequency || !value) {
            return fail(ErrorCode::ParseFailure,
                        "correction CSV row '" + row[0] + "' is not numeric");
        }
        table.points.emplace_back(hertz(static_cast<std::int64_t>(*frequency)), *value);
    }
    table.sortPoints();
    return table;
}

std::string toCsvText(const CorrectionTable& table)
{
    std::ostringstream stream;
    stream << "# name: " << table.name << '\n'
           << "# kind: " << correctionKindKey(table.kind) << '\n'
           << "frequency_hz,value_db\n";
    for (const auto& [frequency, value] : table.points) {
        stream << frequency.value() << ',' << formatDouble(value) << '\n';
    }
    return stream.str();
}

Result<CorrectionTable> load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return hasSuffix(path, "json") ? fromJsonText(*content) : fromCsvText(*content);
}

Status save(const CorrectionTable& table, const QString& path)
{
    return writeFileAtomically(path,
                               hasSuffix(path, "json") ? toJsonText(table) : toCsvText(table));
}

} // namespace correction_io

} // namespace peakemi
