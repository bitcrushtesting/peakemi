#include "session/JsonConversions.hpp"

#include <peakemi/core/AtomicFileWriter.hpp>
#include <peakemi/core/Logging.hpp>
#include <peakemi/core/SessionSerializer.hpp>

namespace peakemi {
namespace {

using json_io::Json;

constexpr const char* SchemaName = "peakemi.session";

} // namespace

std::string SessionSerializer::toJson(const Session& session, bool pretty)
{
    Json traces = Json::array();
    for (const auto& trace : session.traces) {
        traces.push_back(json_io::toJson(trace));
    }
    Json results = Json::array();
    for (const auto& result : session.results) {
        results.push_back(json_io::toJson(result));
    }

    const Json document{{"schema", SchemaName},
                        {"schema_version", CurrentSchemaVersion},
                        {"meta", json_io::toJson(session.meta)},
                        {"config", json_io::toJson(session.config)},
                        {"traces", std::move(traces)},
                        {"results", std::move(results)}};
    return pretty ? document.dump(2) : document.dump();
}

Result<Session> SessionSerializer::fromJson(std::string_view text)
{
    Json document = Json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return fail(ErrorCode::ParseFailure, "session file is not valid JSON");
    }
    if (document.value("schema", std::string{}) != SchemaName) {
        return fail(ErrorCode::ParseFailure, "not a PeakEmi session file");
    }

    const int version = document.value("schema_version", 0);
    if (version < MinimumSupportedSchemaVersion) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "session schema version " + std::to_string(version) + " is too old");
    }
    if (version > CurrentSchemaVersion) {
        return fail(ErrorCode::SchemaVersionUnsupported,
                    "session schema version " + std::to_string(version)
                        + " was written by a newer PeakEmi");
    }

    Session session;
    if (document.contains("meta")) {
        session.meta = json_io::sessionMetaFromJson(document.at("meta"));
    }
    if (document.contains("config")) {
        auto config = json_io::runConfigurationFromJson(document.at("config"));
        if (!config) {
            return std::unexpected(config.error());
        }
        session.config = std::move(*config);
    }
    if (document.contains("traces") && document.at("traces").is_array()) {
        for (const auto& trace : document.at("traces")) {
            auto parsed = json_io::traceFromJson(trace);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            session.traces.push_back(std::move(*parsed));
        }
    }
    if (document.contains("results") && document.at("results").is_array()) {
        for (const auto& result : document.at("results")) {
            auto parsed = json_io::measurementPointFromJson(result);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            session.results.push_back(std::move(*parsed));
        }
    }
    return session;
}

Status SessionSerializer::save(const Session& session, const QString& path)
{
    const auto status = writeFileAtomically(path, toJson(session));
    if (status) {
        qCInfo(lcSession) << "session saved to" << path << "-" << session.traces.size()
                          << "traces," << session.results.size() << "results";
    }
    return status;
}

Result<Session> SessionSerializer::load(const QString& path)
{
    auto content = readFile(path);
    if (!content) {
        return std::unexpected(content.error());
    }
    return fromJson(*content);
}

} // namespace peakemi
