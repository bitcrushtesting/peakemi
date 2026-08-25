#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/Session.h>
#include <peakemi/core/Trace.h>

#include <QString>

#include <string>

namespace peakemi::reporting {

/// CSV export of traces and of the Phase 2 result table (FR-DAT-3).
///
/// Both formats start with a `#` header block documenting units, corrections,
/// instrument identity, application version and run id (FR-DAT-6), followed by
/// the mandatory disclaimer, so a file that leaves the tool carries its context.
namespace csv {

[[nodiscard]] std::string traceToCsv(const Session& session, const Trace& trace);
[[nodiscard]] std::string resultsToCsv(const Session& session);

[[nodiscard]] Status writeTrace(const Session& session, const Trace& trace, const QString& path);
[[nodiscard]] Status writeResults(const Session& session, const QString& path);

} // namespace csv

} // namespace peakemi::reporting
