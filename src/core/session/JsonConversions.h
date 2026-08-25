#pragma once

#include <peakemi/core/CorrectionTable.h>
#include <peakemi/core/Error.h>
#include <peakemi/core/LimitLine.h>
#include <peakemi/core/MeasurementPoint.h>
#include <peakemi/core/RunConfiguration.h>
#include <peakemi/core/Session.h>
#include <peakemi/core/Trace.h>

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
