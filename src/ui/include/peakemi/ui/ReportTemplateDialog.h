#pragma once

#include <peakemi/core/IReportRenderer.h>

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace peakemi::ui {

/// Editor for the report branding (FR-DAT-5).
///
/// Company, address, logo and the free-text sections are the parts a team wants
/// to set once; what the report must always contain — the measured values and
/// the compliance notice of CON-1 — is deliberately not editable here.
class ReportTemplateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ReportTemplateDialog(const ReportTemplate& reportTemplate, QWidget* parent = nullptr);

    [[nodiscard]] ReportTemplate reportTemplate() const;

    /// True when the user asked for this template to become the default for new
    /// sessions.
    [[nodiscard]] bool saveAsDefault() const;

private slots:
    void chooseLogo();
    void clearLogo();
    void importTemplate();
    void exportTemplate();

private:
    void applyToWidgets(const ReportTemplate& reportTemplate);
    void updateLogoPreview();

    QLineEdit* m_companyName{nullptr};
    QLineEdit* m_address{nullptr};
    QLineEdit* m_title{nullptr};
    QLineEdit* m_logoPath{nullptr};
    QLabel* m_logoPreview{nullptr};
    QPlainTextEdit* m_introduction{nullptr};
    QPlainTextEdit* m_conclusion{nullptr};
    QCheckBox* m_includePlot{nullptr};
    QCheckBox* m_includeCorrections{nullptr};
    QCheckBox* m_includeNotes{nullptr};
    QCheckBox* m_makeDefault{nullptr};
};

} // namespace peakemi::ui
