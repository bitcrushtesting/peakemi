#include <peakemi/core/Disclaimer.h>
#include <peakemi/core/Version.h>
#include <peakemi/ui/AboutDialog.h>

#include <QApplication>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QVBoxLayout>

#include <string_view>

/// Registers the compiled-in resources.
///
/// At global scope on purpose, and not in an anonymous namespace:
/// Q_INIT_RESOURCE declares the generated symbol in whatever namespace it is
/// expanded in, while the generated one lives in none. The call is needed at
/// all because the resource is compiled into a static library, from which the
/// linker drops anything that nothing references.
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
static void initPeakEmiResources()
{
    Q_INIT_RESOURCE(peakemi);
}

namespace peakemi::ui {
namespace {

constexpr int IconSize = 96;
constexpr int DialogWidth = 560;
constexpr int NoticeMargin = 10;

/// A link colour that stays readable on the background it is drawn on.
///
/// Several desktop dark themes leave QPalette::Link at its light-theme navy,
/// which all but disappears on a dark window, so the dialog picks its own.
[[nodiscard]] QColor linkColour(const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    return dark ? QColor{0x7F, 0xB6, 0xFF} : QColor{0x11, 0x4E, 0xA8};
}

/// Lets the layout ask a wrapped label how tall it needs to be at a given
/// width. Without this the dialog is sized from the label's first guess and
/// the last line of a paragraph ends up cut off.
void wrapAndGrow(QLabel* label)
{
    label->setWordWrap(true);
    QSizePolicy policy = label->sizePolicy();
    policy.setHeightForWidth(true);
    label->setSizePolicy(policy);
}

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

QString AboutDialog::repositoryUrl()
{
    return QStringLiteral("https://github.com/bitcrushtesting/peakemi");
}

QString AboutDialog::licenceUrl()
{
    return repositoryUrl() + QStringLiteral("/blob/main/LICENSE");
}

AboutDialog::AboutDialog(QWidget* parent) : QDialog{parent}
{
    initPeakEmiResources();

    setWindowTitle(tr("About PeakEmi"));
    setObjectName(QStringLiteral("aboutDialog"));

    QPalette scheme = palette();
    scheme.setColor(QPalette::Link, linkColour(scheme));
    scheme.setColor(QPalette::LinkVisited, linkColour(scheme));
    setPalette(scheme);

    auto* layout = new QVBoxLayout{this};

    auto* header = new QHBoxLayout;
    header->setSpacing(16);
    auto* icon = new QLabel{this};
    icon->setObjectName(QStringLiteral("aboutIcon"));
    icon->setPixmap(QIcon{QStringLiteral(":/peakemi/icon.png")}.pixmap(IconSize, IconSize));
    icon->setFixedSize(IconSize, IconSize);
    header->addWidget(icon);

    auto* title = new QLabel{this};
    title->setObjectName(QStringLiteral("aboutTitle"));
    title->setTextFormat(Qt::RichText);
    title->setText(tr("<h2 style='margin-bottom:2px'>PeakEmi %1</h2>"
                      "<p style='margin-top:0'>EMI pre-compliance measurement suite<br>"
                      "Developed by Bitcrush Testing</p>")
                       .arg(qs(ProjectVersion)));
    header->addWidget(title, 1);
    layout->addLayout(header);

    auto* details = new QLabel{this};
    details->setObjectName(QStringLiteral("aboutDetails"));
    details->setTextFormat(Qt::RichText);
    wrapAndGrow(details);
    // Links open in the user's browser rather than inside a measurement tool.
    details->setOpenExternalLinks(true);
    details->setTextInteractionFlags(Qt::TextBrowserInteraction);
    details->setText(tr("<p><b>Licence:</b> GPL-3.0-or-later — "
                        "<a href=\"%1\">read the licence</a>.<br>"
                        "PeakEmi is free software: you may use, study, share and modify it "
                        "under the terms of the GNU General Public License, version 3 or "
                        "any later version. It comes with no warranty.</p>"
                        "<p><b>Source:</b> <a href=\"%2\">%2</a></p>")
                         .arg(licenceUrl(), repositoryUrl()));
    layout->addWidget(details);

    // The notice reads as a notice: framed, and never quieter than the rest.
    // The frame is a widget of its own rather than QLabel::setFrameShape so
    // that its layout accounts for the border when the text wraps.
    auto* notice = new QFrame{this};
    notice->setFrameShape(QFrame::StyledPanel);
    auto* noticeLayout = new QVBoxLayout{notice};
    noticeLayout->setContentsMargins(NoticeMargin, NoticeMargin, NoticeMargin, NoticeMargin);

    auto* disclaimer = new QLabel{notice};
    disclaimer->setObjectName(QStringLiteral("aboutDisclaimer"));
    wrapAndGrow(disclaimer);
    disclaimer->setText(qs(ComplianceDisclaimer));
    noticeLayout->addWidget(disclaimer);
    layout->addWidget(notice);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close, this};
    auto* aboutQt = buttons->addButton(tr("About Qt"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(aboutQt, &QPushButton::clicked, qApp, &QApplication::aboutQt);
    layout->addWidget(buttons);

    // Fix the width first: a wrapped paragraph only knows how tall it is once
    // it knows how wide it is, so the height has to be asked for at that width.
    setMinimumWidth(DialogWidth);
    layout->activate();
    resize(DialogWidth, layout->totalHeightForWidth(DialogWidth));
}

} // namespace peakemi::ui
