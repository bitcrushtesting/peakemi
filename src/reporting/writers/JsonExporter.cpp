#include <peakemi/core/AtomicFileWriter.h>
#include <peakemi/core/CisprBands.h>
#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/Time.h>
#include <peakemi/reporting/JsonExporter.h>

#include <nlohmann/json.hpp>

#include <cmath>

namespace peakemi::reporting::json_export {
namespace {

using Json = nlohmann::json;

/// NaN has no JSON representation; a missing limit is null, not zero.
[[nodiscard]] Json numberOrNull(double value)
{
    return std::isfinite(value) ? Json(value) : Json(nullptr);
}

[[nodiscard]] Json toJson(const AppliedCorrection& correction)
{
    return Json{{"name", correction.name},
                {"kind", correctionKindKey(correction.kind)},
                {"value_db", correction.valueDb},
                {"contribution_db", correction.contributionDb}};
}

[[nodiscard]] Json toJson(const MeasurementPoint& point)
{
    Json corrections = Json::array();
    for (const auto& correction : point.corrections) {
        corrections.push_back(toJson(correction));
    }

    return Json{{"frequency_hz", point.frequency.value()},
                {"frequency_mhz", toMegahertz(point.frequency)},
                {"cispr_band", cisprBandKey(cisprBandFor(point.frequency).band)},
                {"raw_amplitude", point.rawAmplitude},
                {"raw_unit", amplitudeUnitKey(point.rawUnit)},
                {"corrected_amplitude", point.correctedAmplitude},
                {"unit", amplitudeUnitKey(point.unit)},
                {"limit", numberOrNull(point.limitValue)},
                {"limit_name", point.limitName},
                {"margin_db", numberOrNull(point.marginDb)},
                {"verdict", verdictKey(point.verdict)},
                {"detector", detectorKey(point.detector)},
                {"rbw_hz", point.rbw.value()},
                {"vbw_hz", point.vbw.value()},
                {"dwell_ms", point.dwell.count()},
                {"pass", point.pass},
                {"corrections", std::move(corrections)},
                {"measured_at", toIso8601(point.measuredAt)}};
}

} // namespace

std::string resultsToJson(const Session& session, bool pretty)
{
    Json results = Json::array();
    for (const auto& point : session.results) {
        results.push_back(toJson(point));
    }

    Json limits = Json::array();
    for (const auto& limit : session.config.limits) {
        limits.push_back(Json{{"name", limit.name},
                              {"standard", limit.standard},
                              {"unit", amplitudeUnitKey(limit.unit)},
                              {"class", equipmentClassKey(limit.equipmentClass)},
                              {"kind", emissionKindKey(limit.kind)},
                              {"distance_m", limit.measurementDistanceMetres}});
    }

    Json corrections = Json::array();
    for (const auto& correction : session.config.corrections) {
        corrections.push_back(Json{{"name", correction.name},
                                   {"kind", correctionKindKey(correction.kind)},
                                   {"enabled", correction.enabled},
                                   {"points", correction.points.size()}});
    }

    const InstrumentId instrument =
        session.results.empty() ? InstrumentId{} : session.results.front().instrument;

    const Json document{
        {"schema", "peakemi.results"},
        {"schema_version", SchemaVersion},
        {"disclaimer", ComplianceDisclaimer},
        {"application_version", session.meta.applicationVersion},
        {"run_id", session.meta.runId},
        {"exported_at", toIso8601(std::chrono::system_clock::now())},
        {"eut",
         Json{{"name", session.meta.eutName},
              {"serial", session.meta.eutSerial},
              {"operating_mode", session.meta.eutOperatingMode},
              {"test_setup", session.meta.testSetup}}},
        {"operator", session.meta.operatorName},
        {"company", session.meta.company},
        {"instrument",
         Json{{"manufacturer", instrument.manufacturer},
              {"model", instrument.model},
              {"serial", instrument.serial},
              {"firmware", instrument.firmware}}},
        {"configuration",
         Json{{"start_hz", session.config.span.start.value()},
              {"stop_hz", session.config.span.stop.value()},
              {"phase1_detector", detectorKey(session.config.phase1Detector)},
              {"verification_detector", detectorKey(session.config.verificationDetector)},
              {"dwell_ms", session.config.dwellTime.count()},
              {"marginal_threshold_db", session.config.marginalThresholdDb},
              {"passes", session.config.passes}}},
        {"limits", std::move(limits)},
        {"corrections", std::move(corrections)},
        {"overall_verdict", verdictKey(session.overallVerdict())},
        {"results", std::move(results)}};

    return pretty ? document.dump(2) : document.dump();
}

Status writeResults(const Session& session, const QString& path)
{
    return writeFileAtomically(path, resultsToJson(session));
}

} // namespace peakemi::reporting::json_export
