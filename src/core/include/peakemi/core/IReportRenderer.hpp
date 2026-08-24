#pragma once

#include <peakemi/core/Error.hpp>
#include <peakemi/core/Session.hpp>

#include <QString>

namespace peakemi {

/// Branding of a generated report (FR-DAT-5). Everything here is presentation:
/// no renderer may use it to alter measured values or to omit the disclaimer.
struct ReportTemplate
{
    std::string companyName;
    std::string address;
    std::string logoPath;
    std::string title{"EMI pre-compliance measurement report"};
    std::string introduction;
    std::string conclusion;
    bool includeCorrectionTables{true};
    bool includeTracePlot{true};
    bool includeNotes{true};
};

/// Port implemented by the reporting adapters (architecture.md 8).
class IReportRenderer
{
public:
    IReportRenderer() = default;
    virtual ~IReportRenderer() = default;

    IReportRenderer(const IReportRenderer&) = delete;
    IReportRenderer& operator=(const IReportRenderer&) = delete;
    IReportRenderer(IReportRenderer&&) = delete;
    IReportRenderer& operator=(IReportRenderer&&) = delete;

    [[nodiscard]] virtual Status render(const Session& session,
                                        const ReportTemplate& reportTemplate,
                                        const QString& path) = 0;
};

} // namespace peakemi
