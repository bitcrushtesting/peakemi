#include "session/JsonConversions.h"

#include <peakemi/core/Time.h>

#include <cmath>
#include <limits>

namespace peakemi::json_io {
namespace {

template<class T>
[[nodiscard]] T get(const Json& json, const char* key, T fallback)
{
    if (!json.is_object() || !json.contains(key) || json.at(key).is_null()) {
        return fallback;
    }
    try {
        return json.at(key).get<T>();
    } catch (const Json::exception&) {
        return fallback;
    }
}

[[nodiscard]] Hertz hertzFrom(const Json& json, const char* key, Hertz fallback = Hertz{0})
{
    return hertz(get<std::int64_t>(json, key, fallback.value()));
}

[[nodiscard]] Decibel
decibelFrom(const Json& json, const char* key, Decibel fallback = Decibel{0.0})
{
    return decibel(get<double>(json, key, fallback.value()));
}

[[nodiscard]] double amplitudeFrom(const Json& json, const char* key)
{
    if (!json.contains(key) || json.at(key).is_null()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return get<double>(json, key, std::numeric_limits<double>::quiet_NaN());
}

/// NaN is not representable in JSON; a missing limit is stored as null.
[[nodiscard]] Json amplitudeToJson(double value)
{
    return std::isfinite(value) ? Json(value) : Json(nullptr);
}

[[nodiscard]] Json timeToJson(TimePoint value)
{
    return Json(toIso8601(value));
}

[[nodiscard]] TimePoint timeFromJson(const Json& json, const char* key)
{
    const auto text = get<std::string>(json, key, std::string{});
    if (text.empty()) {
        return TimePoint{};
    }
    return fromIso8601(text).value_or(TimePoint{});
}

template<class Enum>
[[nodiscard]] Enum enumFrom(const Json& json,
                            const char* key,
                            std::optional<Enum> (*parse)(std::string_view),
                            Enum fallback)
{
    const auto text = get<std::string>(json, key, std::string{});
    if (text.empty()) {
        return fallback;
    }
    return parse(text).value_or(fallback);
}

} // namespace

Json toJson(const InstrumentId& value)
{
    return Json{{"manufacturer", value.manufacturer},
                {"model", value.model},
                {"serial", value.serial},
                {"firmware", value.firmware},
                {"idn", value.raw}};
}

InstrumentId instrumentIdFromJson(const Json& json)
{
    return InstrumentId{.manufacturer = get<std::string>(json, "manufacturer", {}),
                        .model = get<std::string>(json, "model", {}),
                        .serial = get<std::string>(json, "serial", {}),
                        .firmware = get<std::string>(json, "firmware", {}),
                        .raw = get<std::string>(json, "idn", {})};
}

Json toJson(const LimitLine& value)
{
    Json points = Json::array();
    for (const auto& point : value.points) {
        points.push_back(Json{{"frequency_hz", point.frequency.value()},
                              {"amplitude", point.amplitude},
                              {"interpolation", interpolationKey(point.interpolationToNext)}});
    }
    return Json{{"name", value.name},
                {"standard", value.standard},
                {"note", value.note},
                {"kind", emissionKindKey(value.kind)},
                {"class", equipmentClassKey(value.equipmentClass)},
                {"detector", detectorKey(value.detector)},
                {"unit", amplitudeUnitKey(value.unit)},
                {"distance_m", value.measurementDistanceMetres},
                {"built_in", value.builtIn},
                {"points", points}};
}

Result<LimitLine> limitLineFromJson(const Json& json)
{
    if (!json.is_object()) {
        return fail(ErrorCode::ParseFailure, "limit line must be a JSON object");
    }
    LimitLine line;
    line.name = get<std::string>(json, "name", {});
    line.standard = get<std::string>(json, "standard", {});
    line.note = get<std::string>(json, "note", {});
    line.kind = enumFrom(json, "kind", &emissionKindFromKey, EmissionKind::Radiated);
    line.equipmentClass =
        enumFrom(json, "class", &equipmentClassFromKey, EquipmentClass::Unspecified);
    line.detector = enumFrom(json, "detector", &detectorFromKey, Detector::QuasiPeak);
    line.unit = enumFrom(json, "unit", &amplitudeUnitFromKey, AmplitudeUnit::dBuV_per_m);
    line.measurementDistanceMetres = get<double>(json, "distance_m", 0.0);
    line.builtIn = get<bool>(json, "built_in", false);

    if (!json.contains("points") || !json.at("points").is_array()) {
        return fail(ErrorCode::ParseFailure, "limit line '" + line.name + "' has no points array");
    }
    for (const auto& point : json.at("points")) {
        line.points.push_back(LimitPoint{
            .frequency = hertzFrom(point, "frequency_hz"),
            .amplitude = get<double>(point, "amplitude", 0.0),
            .interpolationToNext = enumFrom(
                point, "interpolation", &interpolationFromKey, Interpolation::LogFrequency)});
    }
    line.sortPoints();
    if (auto status = line.validate(); !status) {
        return std::unexpected(status.error());
    }
    return line;
}

Json toJson(const CorrectionTable& value)
{
    Json points = Json::array();
    for (const auto& [frequency, amplitude] : value.points) {
        points.push_back(Json{{"frequency_hz", frequency.value()}, {"value_db", amplitude}});
    }
    return Json{{"name", value.name},
                {"kind", correctionKindKey(value.kind)},
                {"enabled", value.enabled},
                {"points", points}};
}

Result<CorrectionTable> correctionTableFromJson(const Json& json)
{
    if (!json.is_object()) {
        return fail(ErrorCode::ParseFailure, "correction table must be a JSON object");
    }
    CorrectionTable table;
    table.name = get<std::string>(json, "name", {});
    table.kind = enumFrom(json, "kind", &correctionKindFromKey, CorrectionKind::Other);
    table.enabled = get<bool>(json, "enabled", true);
    if (json.contains("points") && json.at("points").is_array()) {
        for (const auto& point : json.at("points")) {
            table.points.emplace_back(hertzFrom(point, "frequency_hz"),
                                      get<double>(point, "value_db", 0.0));
        }
    }
    table.sortPoints();
    return table;
}

Json toJson(const AppliedCorrection& value)
{
    return Json{{"name", value.name},
                {"kind", correctionKindKey(value.kind)},
                {"value_db", value.valueDb},
                {"contribution_db", value.contributionDb}};
}

AppliedCorrection appliedCorrectionFromJson(const Json& json)
{
    return AppliedCorrection{
        .name = get<std::string>(json, "name", {}),
        .kind = enumFrom(json, "kind", &correctionKindFromKey, CorrectionKind::Other),
        .valueDb = get<double>(json, "value_db", 0.0),
        .contributionDb = get<double>(json, "contribution_db", 0.0)};
}

Json toJson(const SweepParams& value)
{
    return Json{{"start_hz", value.span.start.value()},
                {"stop_hz", value.span.stop.value()},
                {"rbw_hz", value.rbw.value()},
                {"vbw_hz", value.vbw.value()},
                {"detector", detectorKey(value.detector)},
                {"points", value.points},
                {"ref_level_db", value.refLevel.value()},
                {"attenuation_db", value.attenuation.value()},
                {"automatic_attenuation", value.automaticAttenuation},
                {"preamp", value.preamp},
                {"sweep_time_ms", value.sweepTime.count()}};
}

SweepParams sweepParamsFromJson(const Json& json)
{
    return SweepParams{
        .span = FrequencyRange{hertzFrom(json, "start_hz"), hertzFrom(json, "stop_hz")},
        .rbw = hertzFrom(json, "rbw_hz"),
        .vbw = hertzFrom(json, "vbw_hz"),
        .detector = enumFrom(json, "detector", &detectorFromKey, Detector::Peak),
        .points = get<int>(json, "points", 1001),
        .refLevel = decibelFrom(json, "ref_level_db"),
        .attenuation = decibelFrom(json, "attenuation_db", decibel(10.0)),
        .automaticAttenuation = get<bool>(json, "automatic_attenuation", true),
        .preamp = get<bool>(json, "preamp", false),
        .sweepTime = std::chrono::milliseconds{get<std::int64_t>(json, "sweep_time_ms", 0)}};
}

Json toJson(const PeakDetectionSettings& value)
{
    return Json{{"prominence_db", value.prominenceDb},
                {"margin_threshold_db", value.marginThresholdDb},
                {"minimum_spacing_hz", value.minimumSpacing.value()},
                {"maximum_count", value.maximumCount},
                {"require_limit", value.requireLimit},
                {"amplitude_floor", value.amplitudeFloor}};
}

PeakDetectionSettings peakSettingsFromJson(const Json& json)
{
    PeakDetectionSettings settings;
    settings.prominenceDb = get<double>(json, "prominence_db", settings.prominenceDb);
    settings.marginThresholdDb =
        get<double>(json, "margin_threshold_db", settings.marginThresholdDb);
    settings.minimumSpacing = hertzFrom(json, "minimum_spacing_hz", settings.minimumSpacing);
    settings.maximumCount = get<int>(json, "maximum_count", settings.maximumCount);
    settings.requireLimit = get<bool>(json, "require_limit", settings.requireLimit);
    settings.amplitudeFloor = get<double>(json, "amplitude_floor", settings.amplitudeFloor);
    return settings;
}

Json toJson(const Trace& value)
{
    Json axis{{"start_hz", value.axis.start.value()},
              {"stop_hz", value.axis.stop.value()},
              {"points", value.axis.points}};
    if (!value.axis.explicitPoints.empty()) {
        Json frequencies = Json::array();
        for (const auto& frequency : value.axis.explicitPoints) {
            frequencies.push_back(frequency.value());
        }
        axis["frequencies_hz"] = std::move(frequencies);
    }
    return Json{{"label", value.label},
                {"axis", std::move(axis)},
                {"amplitudes", value.amplitudes},
                {"unit", amplitudeUnitKey(value.unit)},
                {"detector", detectorKey(value.detector)},
                {"corrected", value.corrected},
                {"params", toJson(value.params)},
                {"source", toJson(value.source)},
                {"acquired_at", timeToJson(value.acquiredAt)}};
}

Result<Trace> traceFromJson(const Json& json)
{
    if (!json.is_object() || !json.contains("amplitudes")) {
        return fail(ErrorCode::ParseFailure, "trace must be an object with an amplitudes array");
    }
    Trace trace;
    trace.label = get<std::string>(json, "label", {});
    trace.amplitudes = get<std::vector<double>>(json, "amplitudes", {});
    trace.unit = enumFrom(json, "unit", &amplitudeUnitFromKey, AmplitudeUnit::dBuV);
    trace.detector = enumFrom(json, "detector", &detectorFromKey, Detector::Peak);
    trace.corrected = get<bool>(json, "corrected", false);
    trace.acquiredAt = timeFromJson(json, "acquired_at");
    if (json.contains("params")) {
        trace.params = sweepParamsFromJson(json.at("params"));
    }
    if (json.contains("source")) {
        trace.source = instrumentIdFromJson(json.at("source"));
    }

    const Json axis = json.contains("axis") ? json.at("axis") : Json::object();
    trace.axis.start = hertzFrom(axis, "start_hz");
    trace.axis.stop = hertzFrom(axis, "stop_hz");
    trace.axis.points = get<int>(axis, "points", static_cast<int>(trace.amplitudes.size()));
    if (axis.contains("frequencies_hz") && axis.at("frequencies_hz").is_array()) {
        for (const auto& frequency : axis.at("frequencies_hz")) {
            trace.axis.explicitPoints.push_back(hertz(frequency.get<std::int64_t>()));
        }
    }
    if (trace.axis.size() != static_cast<int>(trace.amplitudes.size())) {
        return fail(ErrorCode::ParseFailure,
                    "trace '" + trace.label + "' axis and amplitude counts disagree");
    }
    return trace;
}

Json toJson(const MeasurementPoint& value)
{
    Json corrections = Json::array();
    for (const auto& correction : value.corrections) {
        corrections.push_back(toJson(correction));
    }
    return Json{{"frequency_hz", value.frequency.value()},
                {"raw_amplitude", value.rawAmplitude},
                {"corrected_amplitude", value.correctedAmplitude},
                {"limit", amplitudeToJson(value.limitValue)},
                {"margin_db", amplitudeToJson(value.marginDb)},
                {"raw_unit", amplitudeUnitKey(value.rawUnit)},
                {"unit", amplitudeUnitKey(value.unit)},
                {"detector", detectorKey(value.detector)},
                {"rbw_hz", value.rbw.value()},
                {"vbw_hz", value.vbw.value()},
                {"dwell_ms", value.dwell.count()},
                {"corrections", std::move(corrections)},
                {"limit_name", value.limitName},
                {"verdict", verdictKey(value.verdict)},
                {"instrument", toJson(value.instrument)},
                {"measured_at", timeToJson(value.measuredAt)},
                {"pass", value.pass}};
}

Result<MeasurementPoint> measurementPointFromJson(const Json& json)
{
    if (!json.is_object()) {
        return fail(ErrorCode::ParseFailure, "measurement point must be a JSON object");
    }
    MeasurementPoint point;
    point.frequency = hertzFrom(json, "frequency_hz");
    point.rawAmplitude = get<double>(json, "raw_amplitude", 0.0);
    point.correctedAmplitude = get<double>(json, "corrected_amplitude", 0.0);
    point.limitValue = amplitudeFrom(json, "limit");
    point.marginDb = amplitudeFrom(json, "margin_db");
    point.rawUnit = enumFrom(json, "raw_unit", &amplitudeUnitFromKey, AmplitudeUnit::dBuV);
    point.unit = enumFrom(json, "unit", &amplitudeUnitFromKey, AmplitudeUnit::dBuV);
    point.detector = enumFrom(json, "detector", &detectorFromKey, Detector::QuasiPeak);
    point.rbw = hertzFrom(json, "rbw_hz");
    point.vbw = hertzFrom(json, "vbw_hz");
    point.dwell = std::chrono::milliseconds{get<std::int64_t>(json, "dwell_ms", 0)};
    if (json.contains("corrections") && json.at("corrections").is_array()) {
        for (const auto& correction : json.at("corrections")) {
            point.corrections.push_back(appliedCorrectionFromJson(correction));
        }
    }
    point.limitName = get<std::string>(json, "limit_name", {});
    point.verdict = enumFrom(json, "verdict", &verdictFromKey, Verdict::Unknown);
    if (json.contains("instrument")) {
        point.instrument = instrumentIdFromJson(json.at("instrument"));
    }
    point.measuredAt = timeFromJson(json, "measured_at");
    point.pass = get<int>(json, "pass", 1);
    return point;
}

Json toJson(const RunConfiguration& value)
{
    Json limits = Json::array();
    for (const auto& limit : value.limits) {
        limits.push_back(toJson(limit));
    }
    Json corrections = Json::array();
    for (const auto& correction : value.corrections) {
        corrections.push_back(toJson(correction));
    }
    return Json{{"start_hz", value.span.start.value()},
                {"stop_hz", value.span.stop.value()},
                {"phase1_points", value.phase1Points},
                {"phase1_detector", detectorKey(value.phase1Detector)},
                {"phase1_rbw_hz", value.phase1Rbw.value()},
                {"phase1_vbw_hz", value.phase1Vbw.value()},
                {"ref_level_db", value.refLevel.value()},
                {"attenuation_db", value.attenuation.value()},
                {"automatic_attenuation", value.automaticAttenuation},
                {"preamp", value.preamp},
                {"maximum_points_per_sweep", value.maximumPointsPerSweep},
                {"peaks", toJson(value.peaks)},
                {"verification_detector", detectorKey(value.verificationDetector)},
                {"dwell_ms", value.dwellTime.count()},
                {"verification_span_hz", value.verificationSpan.value()},
                {"verification_points", value.verificationPoints},
                {"verification_rbw_hz", value.verificationRbw.value()},
                {"passes", value.passes},
                {"max_retries", value.maxRetries},
                {"operation_timeout_ms", value.operationTimeout.count()},
                {"marginal_threshold_db", value.marginalThresholdDb},
                {"start_commands", value.startCommands},
                {"stop_commands", value.stopCommands},
                {"command_timeout_ms", value.commandTimeout.count()},
                {"autosave", value.autosave},
                {"autosave_path", value.autosavePath},
                {"limits", std::move(limits)},
                {"corrections", std::move(corrections)}};
}

Result<RunConfiguration> runConfigurationFromJson(const Json& json)
{
    if (!json.is_object()) {
        return fail(ErrorCode::ParseFailure, "run configuration must be a JSON object");
    }
    RunConfiguration config;
    config.span = FrequencyRange{hertzFrom(json, "start_hz", config.span.start),
                                 hertzFrom(json, "stop_hz", config.span.stop)};
    config.phase1Points = get<int>(json, "phase1_points", config.phase1Points);
    config.phase1Detector =
        enumFrom(json, "phase1_detector", &detectorFromKey, config.phase1Detector);
    config.phase1Rbw = hertzFrom(json, "phase1_rbw_hz", config.phase1Rbw);
    config.phase1Vbw = hertzFrom(json, "phase1_vbw_hz", config.phase1Vbw);
    config.refLevel = decibelFrom(json, "ref_level_db", config.refLevel);
    config.attenuation = decibelFrom(json, "attenuation_db", config.attenuation);
    config.automaticAttenuation =
        get<bool>(json, "automatic_attenuation", config.automaticAttenuation);
    config.preamp = get<bool>(json, "preamp", config.preamp);
    config.maximumPointsPerSweep =
        get<int>(json, "maximum_points_per_sweep", config.maximumPointsPerSweep);
    if (json.contains("peaks")) {
        config.peaks = peakSettingsFromJson(json.at("peaks"));
    }
    config.verificationDetector =
        enumFrom(json, "verification_detector", &detectorFromKey, config.verificationDetector);
    config.dwellTime =
        std::chrono::milliseconds{get<std::int64_t>(json, "dwell_ms", config.dwellTime.count())};
    config.verificationSpan = hertzFrom(json, "verification_span_hz", config.verificationSpan);
    config.verificationPoints = get<int>(json, "verification_points", config.verificationPoints);
    config.verificationRbw = hertzFrom(json, "verification_rbw_hz", config.verificationRbw);
    config.passes = get<int>(json, "passes", config.passes);
    config.maxRetries = get<int>(json, "max_retries", config.maxRetries);
    config.operationTimeout = std::chrono::milliseconds{
        get<std::int64_t>(json, "operation_timeout_ms", config.operationTimeout.count())};
    config.marginalThresholdDb =
        get<double>(json, "marginal_threshold_db", config.marginalThresholdDb);
    config.startCommands = get<std::vector<std::string>>(json, "start_commands", {});
    config.stopCommands = get<std::vector<std::string>>(json, "stop_commands", {});
    config.commandTimeout = std::chrono::milliseconds{
        get<std::int64_t>(json, "command_timeout_ms", config.commandTimeout.count())};
    config.autosave = get<bool>(json, "autosave", config.autosave);
    config.autosavePath = get<std::string>(json, "autosave_path", {});

    if (json.contains("limits") && json.at("limits").is_array()) {
        for (const auto& limit : json.at("limits")) {
            auto parsed = limitLineFromJson(limit);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.limits.push_back(std::move(*parsed));
        }
    }
    if (json.contains("corrections") && json.at("corrections").is_array()) {
        for (const auto& correction : json.at("corrections")) {
            auto parsed = correctionTableFromJson(correction);
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            config.corrections.push_back(std::move(*parsed));
        }
    }
    return config;
}

Json toJson(const SessionMeta& value)
{
    return Json{{"eut_name", value.eutName},
                {"eut_serial", value.eutSerial},
                {"eut_operating_mode", value.eutOperatingMode},
                {"test_setup", value.testSetup},
                {"operator", value.operatorName},
                {"company", value.company},
                {"notes", value.notes},
                {"application_version", value.applicationVersion},
                {"run_id", value.runId},
                {"created_at", timeToJson(value.createdAt)},
                {"modified_at", timeToJson(value.modifiedAt)}};
}

SessionMeta sessionMetaFromJson(const Json& json)
{
    SessionMeta meta;
    meta.eutName = get<std::string>(json, "eut_name", {});
    meta.eutSerial = get<std::string>(json, "eut_serial", {});
    meta.eutOperatingMode = get<std::string>(json, "eut_operating_mode", {});
    meta.testSetup = get<std::string>(json, "test_setup", {});
    meta.operatorName = get<std::string>(json, "operator", {});
    meta.company = get<std::string>(json, "company", {});
    meta.notes = get<std::string>(json, "notes", {});
    meta.applicationVersion = get<std::string>(json, "application_version", {});
    meta.runId = get<std::string>(json, "run_id", {});
    meta.createdAt = timeFromJson(json, "created_at");
    meta.modifiedAt = timeFromJson(json, "modified_at");
    return meta;
}

} // namespace peakemi::json_io
