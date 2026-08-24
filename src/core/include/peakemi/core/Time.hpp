#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace peakemi {

using TimePoint = std::chrono::system_clock::time_point;

/// ISO-8601 in UTC with millisecond resolution, e.g. "2026-08-25T09:13:44.120Z".
/// Every timestamp that reaches a file or a report goes through these two.
[[nodiscard]] std::string toIso8601(TimePoint timePoint);
[[nodiscard]] std::optional<TimePoint> fromIso8601(std::string_view text);

} // namespace peakemi
