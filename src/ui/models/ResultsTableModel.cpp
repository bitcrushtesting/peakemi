#include <peakemi/core/Time.h>
#include <peakemi/ui/ResultsTableModel.h>

#include <QBrush>
#include <QColor>

#include <algorithm>
#include <cmath>
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

[[nodiscard]] QColor verdictColour(Verdict verdict)
{
    switch (verdict) {
        case Verdict::Pass:
            return QColor{0xE6, 0xF4, 0xEA};
        case Verdict::Marginal:
            return QColor{0xFE, 0xF7, 0xE0};
        case Verdict::Fail:
            return QColor{0xFC, 0xE8, 0xE6};
        case Verdict::Unknown:
            break;
    }
    return {};
}

[[nodiscard]] QString formatted(double value, int precision)
{
    return std::isfinite(value) ? QString::number(value, 'f', precision) : QStringLiteral("—");
}

} // namespace

ResultsTableModel::ResultsTableModel(QObject* parent) : QAbstractTableModel{parent} {}

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
        return QVariant::fromValue(static_cast<qint64>(point->frequency.value()));
    }

    if (role == Qt::BackgroundRole) {
        const QColor colour = verdictColour(point->verdict);
        return colour.isValid() ? QBrush{colour} : QVariant{};
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
            case DwellColumn:
                return static_cast<qint64>(point->dwell.count());
            case BandwidthColumn:
                return static_cast<qint64>(point->rbw.value());
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
