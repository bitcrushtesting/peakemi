#include <peakemi/core/CisprBands.hpp>
#include <peakemi/core/Disclaimer.hpp>
#include <peakemi/core/Logging.hpp>
#include <peakemi/core/Time.hpp>
#include <peakemi/reporting/PdfReportRenderer.hpp>

#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRectF>

#include <array>
#include <cmath>

namespace peakemi::reporting {
namespace {

constexpr int Resolution = 300;                 // dots per inch
constexpr double MarginMm = 15.0;
constexpr double LineHeightMm = 5.0;
constexpr double SmallLineHeightMm = 4.2;

[[nodiscard]] int toDots(double millimetres)
{
    return static_cast<int>(std::lround(millimetres / 25.4 * Resolution));
}

[[nodiscard]] QColor verdictColour(Verdict verdict)
{
    switch (verdict) {
        case Verdict::Pass:     return QColor{0xE6, 0xF4, 0xEA};
        case Verdict::Marginal: return QColor{0xFE, 0xF7, 0xE0};
        case Verdict::Fail:     return QColor{0xFC, 0xE8, 0xE6};
        case Verdict::Unknown:  break;
    }
    return QColor{0xF5, 0xF5, 0xF5};
}

[[nodiscard]] QString qs(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

[[nodiscard]] QString number(double value, int precision = 2)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision)
                                : QStringLiteral("--");
}

/// Sequential page layout: a cursor that knows how to start a new page.
class PageCursor
{
public:
    PageCursor(QPainter& painter, QPdfWriter& writer)
        : m_painter{painter}
        , m_writer{writer}
        , m_left{toDots(MarginMm)}
        , m_right{writer.width() - toDots(MarginMm)}
        , m_bottom{writer.height() - toDots(MarginMm + 8.0)}
        , m_y{toDots(MarginMm)}
    {
    }

    [[nodiscard]] int left() const { return m_left; }
    [[nodiscard]] int right() const { return m_right; }
    [[nodiscard]] int width() const { return m_right - m_left; }
    [[nodiscard]] int y() const { return m_y; }

    void advance(double millimetres) { m_y += toDots(millimetres); }

    void ensure(double millimetresNeeded)
    {
        if (m_y + toDots(millimetresNeeded) > m_bottom) {
            newPage();
        }
    }

    void newPage()
    {
        m_writer.newPage();
        m_y = toDots(MarginMm);
        ++m_pageNumber;
    }

    [[nodiscard]] int pageNumber() const { return m_pageNumber; }

    void text(const QString& value, const QFont& font, double lineHeightMm = LineHeightMm)
    {
        ensure(lineHeightMm);
        m_painter.setFont(font);
        const QRectF box{static_cast<double>(m_left),
                         static_cast<double>(m_y),
                         static_cast<double>(width()),
                         static_cast<double>(toDots(lineHeightMm))};
        m_painter.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, value);
        advance(lineHeightMm);
    }

    /// Word-wrapped paragraph; returns the height it consumed.
    void paragraph(const QString& value, const QFont& font)
    {
        m_painter.setFont(font);
        const QRectF available{static_cast<double>(m_left),
                               static_cast<double>(m_y),
                               static_cast<double>(width()),
                               static_cast<double>(m_bottom - m_y)};
        const QRectF needed =
            m_painter.boundingRect(available, Qt::AlignLeft | Qt::TextWordWrap, value);
        if (needed.height() > available.height()) {
            newPage();
        }
        const QRectF box{static_cast<double>(m_left),
                         static_cast<double>(m_y),
                         static_cast<double>(width()),
                         static_cast<double>(m_bottom - m_y)};
        const QRectF drawn = m_painter.boundingRect(box, Qt::AlignLeft | Qt::TextWordWrap, value);
        m_painter.drawText(box, Qt::AlignLeft | Qt::TextWordWrap, value);
        m_y += static_cast<int>(drawn.height());
        advance(2.0);
    }

private:
    QPainter& m_painter;
    QPdfWriter& m_writer;
    int m_left;
    int m_right;
    int m_bottom;
    int m_y;
    int m_pageNumber{1};
};

void drawKeyValue(QPainter& painter,
                  PageCursor& cursor,
                  const QFont& keyFont,
                  const QFont& valueFont,
                  const QString& key,
                  const QString& value)
{
    cursor.ensure(LineHeightMm);
    const int keyWidth = cursor.width() / 4;
    painter.setFont(keyFont);
    painter.drawText(QRectF{static_cast<double>(cursor.left()),
                            static_cast<double>(cursor.y()),
                            static_cast<double>(keyWidth),
                            static_cast<double>(toDots(LineHeightMm))},
                     Qt::AlignLeft | Qt::AlignVCenter,
                     key);
    painter.setFont(valueFont);
    painter.drawText(QRectF{static_cast<double>(cursor.left() + keyWidth),
                            static_cast<double>(cursor.y()),
                            static_cast<double>(cursor.width() - keyWidth),
                            static_cast<double>(toDots(LineHeightMm))},
                     Qt::AlignLeft | Qt::AlignVCenter,
                     value);
    cursor.advance(LineHeightMm);
}

} // namespace

Status PdfReportRenderer::render(const Session& session,
                                 const ReportTemplate& reportTemplate,
                                 const QString& path)
{
    QPdfWriter writer{path};
    writer.setPageSize(QPageSize{QPageSize::A4});
    writer.setResolution(Resolution);
    writer.setTitle(qs(reportTemplate.title));
    writer.setCreator(QStringLiteral("PeakEmi %1")
                          .arg(QString::fromStdString(session.meta.applicationVersion)));

    QPainter painter;
    if (!painter.begin(&writer)) {
        return fail(ErrorCode::IoFailure, "cannot write the PDF to " + path.toStdString());
    }

    QFont titleFont = painter.font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    QFont headingFont = painter.font();
    headingFont.setPointSize(12);
    headingFont.setBold(true);
    QFont bodyFont = painter.font();
    bodyFont.setPointSize(9);
    QFont keyFont = bodyFont;
    keyFont.setBold(true);
    QFont tableFont = painter.font();
    tableFont.setPointSize(8);
    QFont smallFont = painter.font();
    smallFont.setPointSize(7);

    PageCursor cursor{painter, writer};

    // --- Title block --------------------------------------------------------
    if (!reportTemplate.logoPath.empty()) {
        const QImage logo{QString::fromStdString(reportTemplate.logoPath)};
        if (!logo.isNull()) {
            const int height = toDots(15.0);
            const QImage scaled = logo.scaledToHeight(height, Qt::SmoothTransformation);
            painter.drawImage(QPoint{cursor.left(), cursor.y()}, scaled);
            cursor.advance(18.0);
        }
    }
    cursor.text(qs(reportTemplate.title), titleFont, 9.0);
    if (!reportTemplate.companyName.empty()) {
        cursor.text(QString::fromStdString(reportTemplate.companyName), headingFont);
    }
    if (!reportTemplate.address.empty()) {
        cursor.text(QString::fromStdString(reportTemplate.address), bodyFont);
    }
    cursor.advance(3.0);

    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Equipment under test"),
                 QString::fromStdString(session.meta.eutName));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Serial number"),
                 QString::fromStdString(session.meta.eutSerial));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Operating mode"),
                 QString::fromStdString(session.meta.eutOperatingMode));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Test setup"),
                 QString::fromStdString(session.meta.testSetup));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Operator"),
                 QString::fromStdString(session.meta.operatorName));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Run identifier"),
                 QString::fromStdString(session.meta.runId));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Date"),
                 QString::fromStdString(toIso8601(session.meta.createdAt)));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Application"),
                 QStringLiteral("PeakEmi %1")
                     .arg(QString::fromStdString(session.meta.applicationVersion)));
    if (!session.results.empty()) {
        drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Overall result"),
                     qs(verdictKey(session.overallVerdict())).toUpper());
    }
    cursor.advance(4.0);

    if (!reportTemplate.introduction.empty()) {
        cursor.paragraph(QString::fromStdString(reportTemplate.introduction), bodyFont);
    }

    // --- Disclaimer (CON-1), never optional --------------------------------
    cursor.advance(2.0);
    cursor.text(QObject::tr("Compliance notice"), headingFont);
    cursor.paragraph(qs(ComplianceDisclaimer), bodyFont);

    // --- Test configuration -------------------------------------------------
    cursor.advance(3.0);
    cursor.text(QObject::tr("Test configuration"), headingFont);
    const auto& config = session.config;
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Scan span"),
                 QStringLiteral("%1 MHz - %2 MHz")
                     .arg(number(toMegahertz(config.span.start), 4),
                          number(toMegahertz(config.span.stop), 4)));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Phase 1 detector"),
                 qs(detectorKey(config.phase1Detector)));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Phase 2 detector"),
                 qs(detectorKey(config.verificationDetector)));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Dwell time"),
                 QStringLiteral("%1 ms").arg(config.dwellTime.count()));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Peak threshold"),
                 QStringLiteral("%1 dB to limit").arg(number(config.peaks.marginThresholdDb, 1)));
    drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Marginal threshold"),
                 QStringLiteral("%1 dB").arg(number(config.marginalThresholdDb, 1)));
    for (const auto& limit : config.limits) {
        drawKeyValue(painter, cursor, keyFont, bodyFont, QObject::tr("Limit line"),
                     QStringLiteral("%1 (%2)")
                         .arg(QString::fromStdString(limit.name),
                              QString::fromStdString(limit.standard)));
    }

    // --- Plot ---------------------------------------------------------------
    if (reportTemplate.includeTracePlot && !m_plot.isNull()) {
        cursor.advance(3.0);
        cursor.text(QObject::tr("Spectrum"), headingFont);
        const QImage scaled = m_plot.scaledToWidth(cursor.width(), Qt::SmoothTransformation);
        cursor.ensure(static_cast<double>(scaled.height()) / Resolution * 25.4);
        painter.drawImage(QPoint{cursor.left(), cursor.y()}, scaled);
        cursor.advance(static_cast<double>(scaled.height()) / Resolution * 25.4 + 3.0);
    }

    // --- Result table -------------------------------------------------------
    cursor.advance(3.0);
    cursor.text(QObject::tr("Verified measurement points"), headingFont);

    const std::array<QString, 7> headers{QObject::tr("Frequency [MHz]"),
                                         QObject::tr("Level"),
                                         QObject::tr("Unit"),
                                         QObject::tr("Limit"),
                                         QObject::tr("Margin [dB]"),
                                         QObject::tr("Detector"),
                                         QObject::tr("Verdict")};
    const std::array<double, 7> weights{0.20, 0.14, 0.10, 0.13, 0.15, 0.14, 0.14};

    const auto drawRow = [&](const std::array<QString, 7>& cells,
                             const QColor& background,
                             const QFont& font) {
        cursor.ensure(SmallLineHeightMm);
        const int rowHeight = toDots(SmallLineHeightMm);
        int x = cursor.left();
        painter.setFont(font);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const int cellWidth = static_cast<int>(weights[i] * cursor.width());
            const QRectF cell{static_cast<double>(x),
                              static_cast<double>(cursor.y()),
                              static_cast<double>(cellWidth),
                              static_cast<double>(rowHeight)};
            painter.fillRect(cell, background);
            painter.setPen(QColor{0xCC, 0xCC, 0xCC});
            painter.drawRect(cell);
            painter.setPen(Qt::black);
            painter.drawText(cell.adjusted(toDots(1.0), 0, -toDots(1.0), 0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             cells[i]);
            x += cellWidth;
        }
        cursor.advance(SmallLineHeightMm);
    };

    drawRow(headers, QColor{0xEE, 0xEE, 0xEE}, keyFont);
    if (session.results.empty()) {
        cursor.text(QObject::tr("No Phase 2 measurements were recorded."), bodyFont);
    }
    for (const auto& point : session.results) {
        drawRow({number(toMegahertz(point.frequency), 4),
                 number(point.correctedAmplitude),
                 qs(amplitudeUnitKey(point.unit)),
                 number(point.limitValue),
                 number(point.marginDb, 1),
                 qs(detectorKey(point.detector)),
                 qs(verdictKey(point.verdict)).toUpper()},
                verdictColour(point.verdict),
                tableFont);
    }

    // --- Corrections --------------------------------------------------------
    if (reportTemplate.includeCorrectionTables && !config.corrections.empty()) {
        cursor.advance(4.0);
        cursor.text(QObject::tr("Applied corrections"), headingFont);
        for (const auto& correction : config.corrections) {
            cursor.text(QStringLiteral("%1 - %2 (%3)")
                            .arg(QString::fromStdString(correction.name),
                                 qs(correctionKindKey(correction.kind)),
                                 correction.enabled ? QObject::tr("applied")
                                                    : QObject::tr("disabled")),
                        keyFont,
                        SmallLineHeightMm);
            for (const auto& [frequency, value] : correction.points) {
                cursor.text(QStringLiteral("    %1 MHz: %2 dB")
                                .arg(number(toMegahertz(frequency), 4), number(value)),
                            smallFont,
                            SmallLineHeightMm);
            }
        }
    }

    // --- Notes --------------------------------------------------------------
    if (reportTemplate.includeNotes && !session.meta.notes.empty()) {
        cursor.advance(4.0);
        cursor.text(QObject::tr("Operator notes"), headingFont);
        cursor.paragraph(QString::fromStdString(session.meta.notes), bodyFont);
    }
    if (!reportTemplate.conclusion.empty()) {
        cursor.advance(3.0);
        cursor.paragraph(QString::fromStdString(reportTemplate.conclusion), bodyFont);
    }

    painter.end();
    qCInfo(lcReport) << "report written to" << path;
    return {};
}

} // namespace peakemi::reporting
