#pragma once

#include <peakemi/core/CorrectionTable.hpp>
#include <peakemi/core/Error.hpp>
#include <peakemi/core/LimitLine.hpp>

#include <QString>

#include <string>
#include <string_view>
#include <vector>

namespace peakemi {

/// Import and export of limit lines and correction tables (FR-LIM-1/6).
///
/// Two documented formats. JSON carries the full metadata; CSV is the format
/// people actually have lying around and is defined as:
///
///     # name: CISPR 32 Class B radiated 10 m
///     # standard: CISPR 32:2015
///     # unit: dBuV/m
///     # detector: quasi-peak
///     frequency_hz,amplitude,interpolation
///     30000000,30.0,step
///
/// `#`-comments carry optional metadata as `key: value`; the header row is
/// optional; the third column defaults to log-frequency interpolation.
namespace limit_io {

[[nodiscard]] Result<LimitLine> fromJsonText(std::string_view text);
[[nodiscard]] std::string toJsonText(const LimitLine& line);

[[nodiscard]] Result<LimitLine> fromCsvText(std::string_view text);
[[nodiscard]] std::string toCsvText(const LimitLine& line);

/// Reads either format, chosen by file extension.
[[nodiscard]] Result<LimitLine> load(const QString& path);
[[nodiscard]] Status save(const LimitLine& line, const QString& path);

} // namespace limit_io

namespace correction_io {

[[nodiscard]] Result<CorrectionTable> fromJsonText(std::string_view text);
[[nodiscard]] std::string toJsonText(const CorrectionTable& table);

/// CSV: `frequency_hz,value_db`, with the same `#`-comment metadata block.
[[nodiscard]] Result<CorrectionTable> fromCsvText(std::string_view text);
[[nodiscard]] std::string toCsvText(const CorrectionTable& table);

[[nodiscard]] Result<CorrectionTable> load(const QString& path);
[[nodiscard]] Status save(const CorrectionTable& table, const QString& path);

} // namespace correction_io

} // namespace peakemi
