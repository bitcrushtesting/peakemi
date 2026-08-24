#pragma once

#include <peakemi/core/IReportRenderer.hpp>

#include <QImage>

namespace peakemi::reporting {

/// PDF report via Qt print support (FR-DAT-4).
///
/// Composes a title block with EUT and operator data, the test configuration,
/// the spectrum plot, the Phase 2 result table with pass/fail colouring, the
/// applied correction tables, operator notes and the mandatory disclaimer.
class PdfReportRenderer final : public IReportRenderer
{
public:
    PdfReportRenderer() = default;

    /// The plot is rendered by the UI (which owns the plot backend) and handed
    /// over as an image, so reporting stays independent of the plot backend.
    void setPlotImage(QImage image) { m_plot = std::move(image); }

    [[nodiscard]] Status render(const Session& session,
                                const ReportTemplate& reportTemplate,
                                const QString& path) override;

private:
    QImage m_plot;
};

} // namespace peakemi::reporting
