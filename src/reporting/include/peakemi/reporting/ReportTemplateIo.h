#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/IReportRenderer.h>

#include <QString>

#include <string>
#include <string_view>

namespace peakemi::reporting {

/// Persistence for the report branding (FR-DAT-5).
///
/// Templates are plain JSON so a team can keep one in version control and hand
/// it around; the application also keeps one in its config directory as the
/// default for new sessions (FR-APP-2).
namespace report_template {

[[nodiscard]] std::string toJsonText(const ReportTemplate& reportTemplate);
[[nodiscard]] Result<ReportTemplate> fromJsonText(std::string_view text);

[[nodiscard]] Status save(const ReportTemplate& reportTemplate, const QString& path);
[[nodiscard]] Result<ReportTemplate> load(const QString& path);

/// Where the default template lives, under the platform's config directory.
[[nodiscard]] QString defaultPath();

/// The default template, or a blank one when none has been saved yet.
[[nodiscard]] ReportTemplate loadDefault();
[[nodiscard]] Status saveDefault(const ReportTemplate& reportTemplate);

} // namespace report_template

} // namespace peakemi::reporting
