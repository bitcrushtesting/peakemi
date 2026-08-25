#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/Session.h>

#include <QString>

#include <string>

namespace peakemi {

/// Versioned JSON container for sessions (FR-DAT-2).
///
/// Compatibility rule: a newer application reads every older schema version. A
/// file written by a *newer* application is rejected with
/// ErrorCode::SchemaVersionUnsupported rather than silently misread.
class SessionSerializer
{
public:
    static constexpr int CurrentSchemaVersion = 1;
    static constexpr int MinimumSupportedSchemaVersion = 1;

    [[nodiscard]] static std::string toJson(const Session& session, bool pretty = true);
    [[nodiscard]] static Result<Session> fromJson(std::string_view text);

    /// Atomic write (NFR-REL-3) and matching read.
    [[nodiscard]] static Status save(const Session& session, const QString& path);
    [[nodiscard]] static Result<Session> load(const QString& path);
};

} // namespace peakemi
