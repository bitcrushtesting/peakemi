#pragma once

#include <peakemi/core/CorrectionTable.hpp>
#include <peakemi/core/Error.hpp>
#include <peakemi/core/LimitLine.hpp>
#include <peakemi/core/MeasurementPoint.hpp>
#include <peakemi/core/RunConfiguration.hpp>
#include <peakemi/core/Session.hpp>
#include <peakemi/core/Trace.hpp>

#include <nlohmann/json.hpp>

/// Internal JSON mapping shared by the session container and the limit and
/// correction file importers. Not a public header: the JSON library stays an
/// implementation detail of peakemi_core.
namespace peakemi::json_io {

using Json = nlohmann::json;

[[nodiscard]] Json toJson(const InstrumentId& value);
[[nodiscard]] InstrumentId instrumentIdFromJson(const Json& json);

[[nodiscard]] Json toJson(const LimitLine& value);
[[nodiscard]] Result<LimitLine> limitLineFromJson(const Json& json);

[[nodiscard]] Json toJson(const CorrectionTable& value);
[[nodiscard]] Result<CorrectionTable> correctionTableFromJson(const Json& json);

[[nodiscard]] Json toJson(const AppliedCorrection& value);
[[nodiscard]] AppliedCorrection appliedCorrectionFromJson(const Json& json);

[[nodiscard]] Json toJson(const SweepParams& value);
[[nodiscard]] SweepParams sweepParamsFromJson(const Json& json);

[[nodiscard]] Json toJson(const PeakDetectionSettings& value);
[[nodiscard]] PeakDetectionSettings peakSettingsFromJson(const Json& json);

[[nodiscard]] Json toJson(const Trace& value);
[[nodiscard]] Result<Trace> traceFromJson(const Json& json);

[[nodiscard]] Json toJson(const MeasurementPoint& value);
[[nodiscard]] Result<MeasurementPoint> measurementPointFromJson(const Json& json);

[[nodiscard]] Json toJson(const RunConfiguration& value);
[[nodiscard]] Result<RunConfiguration> runConfigurationFromJson(const Json& json);

[[nodiscard]] Json toJson(const SessionMeta& value);
[[nodiscard]] SessionMeta sessionMetaFromJson(const Json& json);

} // namespace peakemi::json_io
