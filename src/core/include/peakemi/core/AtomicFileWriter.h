#pragma once

#include <peakemi/core/Error.h>

#include <QString>

#include <string_view>

namespace peakemi {

/// Write @p content to @p path as write-temp-then-rename, so an interrupted
/// write can never truncate an existing session or settings file (NFR-REL-3).
[[nodiscard]] Status writeFileAtomically(const QString& path, std::string_view content);

[[nodiscard]] Result<std::string> readFile(const QString& path);

} // namespace peakemi
