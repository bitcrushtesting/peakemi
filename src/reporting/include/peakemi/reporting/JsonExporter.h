#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/Session.h>

#include <QString>

#include <string>

namespace peakemi::reporting {

/// JSON export of the Phase 2 result table (FR-DAT-3, FR-DAT-6).
///
/// This is a *report* format, not the session container: it is flat, it carries
/// the disclaimer and the run identity in every file, and it is meant to be
/// read by a spreadsheet, a test-report generator or a script. Sessions are
/// round-tripped by SessionSerializer instead.
namespace json_export {

inline constexpr int SchemaVersion = 1;

[[nodiscard]] std::string resultsToJson(const Session& session, bool pretty = true);
[[nodiscard]] Status writeResults(const Session& session, const QString& path);

} // namespace json_export

} // namespace peakemi::reporting
