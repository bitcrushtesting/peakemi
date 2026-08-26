#include <peakemi/core/Disclaimer.h>
#include <peakemi/reporting/ReportTemplateIo.h>
#include <peakemi/ui/ReportTemplateDialog.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <string_view>
#include <utility>

namespace peakemi::ui {
namespace {

constexpr int LogoPreviewHeight = 48;

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

ReportTemplateDialog::ReportTemplateDialog(const ReportTemplate& reportTemplate, QWidget* parent)
    : QDialog{parent}
{
    setWindowTitle(tr("Report template"));
    setObjectName(QStringLiteral("reportTemplateDialog"));
    resize(620, 640);

    auto* layout = new QVBoxLayout{this};

    auto* branding = new QGroupBox{tr("Branding"), this};
    auto* brandingForm = new QFormLayout{branding};

    m_companyName = new QLineEdit{branding};
    m_companyName->setObjectName(QStringLiteral("templateCompany"));
    brandingForm->addRow(tr("Company"), m_companyName);

    m_address = new QLineEdit{branding};
    brandingForm->addRow(tr("Address"), m_address);

    m_title = new QLineEdit{branding};
    brandingForm->addRow(tr("Report title"), m_title);

    auto* logoRow = new QHBoxLayout;
    m_logoPath = new QLineEdit{branding};
    m_logoPath->setPlaceholderText(tr("No logo"));
    m_logoPath->setReadOnly(true);
    auto* chooseLogoButton = new QPushButton{tr("Choose…"), branding};
    auto* clearLogoButton = new QPushButton{tr("Clear"), branding};
    logoRow->addWidget(m_logoPath, 1);
    logoRow->addWidget(chooseLogoButton);
    logoRow->addWidget(clearLogoButton);
    brandingForm->addRow(tr("Logo"), logoRow);

    m_logoPreview = new QLabel{branding};
    m_logoPreview->setFixedHeight(LogoPreviewHeight);
    brandingForm->addRow(QString{}, m_logoPreview);
    layout->addWidget(branding);

    auto* text = new QGroupBox{tr("Free text"), this};
    auto* textLayout = new QFormLayout{text};
    m_introduction = new QPlainTextEdit{text};
    m_introduction->setPlaceholderText(tr("Shown after the title block, before the results."));
    m_conclusion = new QPlainTextEdit{text};
    m_conclusion->setPlaceholderText(tr("Shown at the end of the report."));
    textLayout->addRow(tr("Introduction"), m_introduction);
    textLayout->addRow(tr("Conclusion"), m_conclusion);
    layout->addWidget(text, 1);

    auto* sections = new QGroupBox{tr("Sections"), this};
    auto* sectionsLayout = new QVBoxLayout{sections};
    m_includePlot = new QCheckBox{tr("Include the spectrum plot"), sections};
    m_includeCorrections = new QCheckBox{tr("Include the correction tables"), sections};
    m_includeNotes = new QCheckBox{tr("Include the operator notes"), sections};
    sectionsLayout->addWidget(m_includePlot);
    sectionsLayout->addWidget(m_includeCorrections);
    sectionsLayout->addWidget(m_includeNotes);

    auto* notice = new QLabel{tr("Measured values and the pre-compliance notice are always "
                                 "included and cannot be switched off."),
                              sections};
    notice->setWordWrap(true);
    notice->setEnabled(false);
    notice->setToolTip(qs(ComplianceDisclaimer));
    sectionsLayout->addWidget(notice);
    layout->addWidget(sections);

    m_makeDefault = new QCheckBox{tr("Use this template for new sessions"), this};
    layout->addWidget(m_makeDefault);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};
    auto* importButton = buttons->addButton(tr("Import…"), QDialogButtonBox::ActionRole);
    auto* exportButton = buttons->addButton(tr("Export…"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(importButton, &QPushButton::clicked, this, &ReportTemplateDialog::importTemplate);
    connect(exportButton, &QPushButton::clicked, this, &ReportTemplateDialog::exportTemplate);
    connect(chooseLogoButton, &QPushButton::clicked, this, &ReportTemplateDialog::chooseLogo);
    connect(clearLogoButton, &QPushButton::clicked, this, &ReportTemplateDialog::clearLogo);
    layout->addWidget(buttons);

    applyToWidgets(reportTemplate);
}

void ReportTemplateDialog::applyToWidgets(const ReportTemplate& reportTemplate)
{
    m_companyName->setText(QString::fromStdString(reportTemplate.companyName));
    m_address->setText(QString::fromStdString(reportTemplate.address));
    m_title->setText(QString::fromStdString(reportTemplate.title));
    m_logoPath->setText(QString::fromStdString(reportTemplate.logoPath));
    m_introduction->setPlainText(QString::fromStdString(reportTemplate.introduction));
    m_conclusion->setPlainText(QString::fromStdString(reportTemplate.conclusion));
    m_includePlot->setChecked(reportTemplate.includeTracePlot);
    m_includeCorrections->setChecked(reportTemplate.includeCorrectionTables);
    m_includeNotes->setChecked(reportTemplate.includeNotes);
    updateLogoPreview();
}

ReportTemplate ReportTemplateDialog::reportTemplate() const
{
    ReportTemplate reportTemplate;
    reportTemplate.companyName = m_companyName->text().toStdString();
    reportTemplate.address = m_address->text().toStdString();
    reportTemplate.logoPath = m_logoPath->text().toStdString();
    if (!m_title->text().trimmed().isEmpty()) {
        reportTemplate.title = m_title->text().toStdString();
    }
    reportTemplate.introduction = m_introduction->toPlainText().toStdString();
    reportTemplate.conclusion = m_conclusion->toPlainText().toStdString();
    reportTemplate.includeTracePlot = m_includePlot->isChecked();
    reportTemplate.includeCorrectionTables = m_includeCorrections->isChecked();
    reportTemplate.includeNotes = m_includeNotes->isChecked();
    return reportTemplate;
}

bool ReportTemplateDialog::saveAsDefault() const
{
    return m_makeDefault->isChecked();
}

void ReportTemplateDialog::updateLogoPreview()
{
    const QString path = m_logoPath->text();
    if (path.isEmpty()) {
        m_logoPreview->clear();
        return;
    }
    const QPixmap logo{path};
    if (logo.isNull()) {
        m_logoPreview->setText(tr("The logo could not be read."));
        return;
    }
    m_logoPreview->setPixmap(logo.scaledToHeight(LogoPreviewHeight, Qt::SmoothTransformation));
}

void ReportTemplateDialog::chooseLogo()
{
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Choose a logo"), {}, tr("Images (*.png *.jpg *.jpeg *.svg)"));
    if (path.isEmpty()) {
        return;
    }
    m_logoPath->setText(path);
    updateLogoPreview();
}

void ReportTemplateDialog::clearLogo()
{
    m_logoPath->clear();
    updateLogoPreview();
}

void ReportTemplateDialog::importTemplate()
{
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Import a report template"), {}, tr("Report templates (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    auto imported = reporting::report_template::load(path);
    if (!imported) {
        QMessageBox::warning(
            this, tr("Import failed"), QString::fromStdString(imported.error().message()));
        return;
    }
    applyToWidgets(*imported);
}

void ReportTemplateDialog::exportTemplate()
{
    const auto path = QFileDialog::getSaveFileName(this,
                                                   tr("Export the report template"),
                                                   QStringLiteral("report-template.json"),
                                                   tr("Report templates (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    if (auto status = reporting::report_template::save(reportTemplate(), path); !status) {
        QMessageBox::warning(
            this, tr("Export failed"), QString::fromStdString(status.error().message()));
    }
}

} // namespace peakemi::ui
