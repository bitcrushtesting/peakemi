#pragma once

#include <peakemi/core/LimitLine.h>

#include <optional>
#include <span>
#include <vector>

namespace peakemi {

/// Read-only catalogue of common pre-compliance limit sets (FR-LIM-3).
///
/// The values reproduce the published tables of the cited standards; each entry
/// names its source and edition. They are a convenience for pre-compliance work
/// and do not replace the standard itself.
[[nodiscard]] std::span<const LimitLine> builtInLimitLines();

[[nodiscard]] std::optional<LimitLine> builtInLimitLine(std::string_view name);

[[nodiscard]] std::vector<std::string> builtInLimitLineNames();

} // namespace peakemi
