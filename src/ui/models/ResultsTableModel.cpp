#include <peakemi/core/Time.h>
#include <peakemi/ui/ResultsTableModel.h>

#include <QBrush>
#include <QColor>
#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace peakemi::ui {
namespace {

[[nodiscard]] QString translatedVerdict(Verdict verdict)
{
    switch (verdict) {
        case Verdict::Pass:
            return QObject::tr("Pass");
        case Verdict::Marginal:
            return QObject::tr("Marginal");
        case Verdict::Fail:
            return QObject::tr("Fail");
        case Verdict::Unknown:
            break;
    }
    return QObject::tr("No limit");
}

[[nodiscard]] QString translatedDetector(Detector detector)
{
    switch (detector) {
        case Detector::Peak:
            return QObject::tr("Peak");
        case Detector::QuasiPeak:
            return QObject::tr("Quasi-peak");
        case Detector::Average:
            return QObject::tr("Average");
        case Detector::Rms:
            return QObject::tr("RMS");
        case Detector::Sample:
            return QObject::tr("Sample");
    }
    return {};
}

/// Background and text for one verdict.
///
/// Both are stated, and both depend on the colour scheme. Setting only the
/// background is what made the table unreadable in dark mode: the pale tints
/// stayed while the theme's text turned near-white, leaving pale text on a pale
/// row. A verdict has to be readable at a glance -- it is the column an
/// operator scans -- so neither colour is left to chance.
struct VerdictColours
{
    QColor background;
    QColor text;
};

/// Whether rows are painted on a dark background.
///
/// The palette is the thing that gets painted, so it is the thing to ask. The
/// colour-scheme hint answers a related but different question -- what the
/// system prefers -- and says nothing about an application palette set by a
/// stylesheet, by a command line switch, or by a screenshot tool.
[[nodiscard]] bool isDarkScheme()
{
    return QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
}

[[nodiscard]] VerdictColours verdictColours(Verdict verdict)
{
    const bool dark = isDarkScheme();
    switch (verdict) {
        case Verdict::Pass:
            return dark ? VerdictColours{QColor{0x15, 0x30, 0x1E}, QColor{0xA8, 0xE6, 0xBC}}
                        : VerdictColours{QColor{0xE6, 0xF4, 0xEA}, QColor{0x0B, 0x40, 0x1E}};
        case Verdict::Marginal:
            return dark ? VerdictColours{QColor{0x3A, 0x30, 0x0E}, QColor{0xF7, 0xD9, 0x7A}}
                        : VerdictColours{QColor{0xFE, 0xF7, 0xE0}, QColor{0x5F, 0x45, 0x00}};
        case Verdict::Fail:
            return dark ? VerdictColours{QColor{0x40, 0x1A, 0x18}, QColor{0xFF, 0xA9, 0xA0}}
                        : VerdictColours{QColor{0xFC, 0xE8, 0xE6}, QColor{0x7A, 0x1A, 0x12}};
        case Verdict::Unknown:
            break;
    }
    // No verdict: the row keeps the theme's own colours.
    return {};
}

[[nodiscard]] QString formatted(double value, int precision)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision) : QStringLiteral("—");
}

} // namespace

ResultsTableModel::ResultsTableModel(QObject* parent) : QAbstractTableModel{parent}
{
    // A table already on screen has to be repainted when the theme changes,
    // rather than waiting for the next measurement to redraw it.
    connect(QGuiApplication::styleHints(),
            &QStyleHints::colorSchemeChanged,
            this,
            [this](Qt::ColorScheme) { refreshColours(); });
    qApp->installEventFilter(this);
}

bool ResultsTableModel::eventFilter(QObject* watched, QEvent* event)
{
    // The palette can change without the colour scheme changing, so both are
    // watched: a stylesheet or a theme switch repaints the verdict columns.
    if (event->type() == QEvent::ApplicationPaletteChange) {
        refreshColours();
    }
    return QAbstractTableModel::eventFilter(watched, event);
}

void ResultsTableModel::refreshColours()
{
    if (m_points.empty()) {
        return;
    }
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(m_points.size()) - 1, ColumnCount - 1),
                     {Qt::BackgroundRole, Qt::ForegroundRole});
}

int ResultsTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_points.size());
}

int ResultsTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

const MeasurementPoint* ResultsTableModel::pointAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_points.size())) {
        return nullptr;
    }
    return &m_points[static_cast<std::size_t>(row)];
}

QVariant ResultsTableModel::data(const QModelIndex& index, int role) const
{
    const auto* point = pointAt(index.row());
    if (point == nullptr) {
        return {};
    }

    if (role == FrequencyRole) {
        const qint64 frequency = point->frequency.value();
        return QVariant::fromValue(frequency);
    }

    if (role == Qt::BackgroundRole) {
        const auto colours = verdictColours(point->verdict);
        return colours.background.isValid() ? QBrush{colours.background} : QVariant{};
    }

    if (role == Qt::ForegroundRole) {
        const auto colours = verdictColours(point->verdict);
        return colours.text.isValid() ? QBrush{colours.text} : QVariant{};
    }

    if (role == Qt::TextAlignmentRole && index.column() != VerdictColumn &&
        index.column() != DetectorColumn && index.column() != UnitColumn)
    {
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    }

    if (role == SortRole) {
        switch (index.column()) {
            case FrequencyColumn:
                return toMegahertz(point->frequency);
            case LevelColumn:
                return point->correctedAmplitude;
            case LimitColumn:
                return point->limitValue;
            case MarginColumn:
                return point->marginDb;
            case DwellColumn: {
                const qint64 dwell = point->dwell.count();
                return dwell;
            }
            case BandwidthColumn: {
                const qint64 bandwidth = point->rbw.value();
                return bandwidth;
            }
            default:
                break;
        }
    }

    if (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != SortRole) {
        return {};
    }

    switch (index.column()) {
        case FrequencyColumn:
            return formatted(toMegahertz(point->frequency), 4);
        case LevelColumn:
            return formatted(point->correctedAmplitude, 2);
        case UnitColumn:
            return QString::fromUtf8(amplitudeUnitKey(point->unit).data(),
                                     static_cast<qsizetype>(amplitudeUnitKey(point->unit).size()));
        case LimitColumn:
            return formatted(point->limitValue, 2);
        case MarginColumn:
            return formatted(point->marginDb, 2);
        case VerdictColumn:
            return translatedVerdict(point->verdict);
        case DetectorColumn:
            return translatedDetector(point->detector);
        case BandwidthColumn:
            return QStringLiteral("%1 kHz").arg(
                static_cast<double>(point->rbw.value()) / 1000.0, 0, 'g', 4);
        case DwellColumn:
            return QStringLiteral("%1 ms").arg(point->dwell.count());
        case TimestampColumn:
            return QString::fromStdString(toIso8601(point->measuredAt));
        default:
            break;
    }
    return {};
}

QVariant ResultsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case FrequencyColumn:
            return tr("Frequency [MHz]");
        case LevelColumn:
            return tr("Level");
        case UnitColumn:
            return tr("Unit");
        case LimitColumn:
            return tr("Limit");
        case MarginColumn:
            return tr("Margin [dB]");
        case VerdictColumn:
            return tr("Verdict");
        case DetectorColumn:
            return tr("Detector");
        case BandwidthColumn:
            return tr("RBW");
        case DwellColumn:
            return tr("Dwell");
        case TimestampColumn:
            return tr("Measured at");
        default:
            break;
    }
    return {};
}

void ResultsTableModel::setPoints(std::vector<MeasurementPoint> points)
{
    beginResetModel();
    m_points = std::move(points);
    endResetModel();
}

void ResultsTableModel::appendPoint(const MeasurementPoint& point)
{
    const auto existing =
        std::find_if(m_points.begin(), m_points.end(), [&](const MeasurementPoint& stored) {
            return stored.frequency == point.frequency;
        });
    if (existing != m_points.end()) {
        *existing = point;
        const int row = static_cast<int>(std::distance(m_points.begin(), existing));
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
        return;
    }

    const int row = static_cast<int>(m_points.size());
    beginInsertRows({}, row, row);
    m_points.push_back(point);
    endInsertRows();
}

void ResultsTableModel::clear()
{
    beginResetModel();
    m_points.clear();
    endResetModel();
}

} // namespace peakemi::ui
