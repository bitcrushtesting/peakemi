#pragma once

#include <peakemi/core/MeasurementPoint.hpp>

#include <QAbstractTableModel>

#include <vector>

namespace peakemi::ui {

/// Table model over the Phase 2 results (FR-VIS-4, FR-DAT-1).
///
/// Verdict colouring lives in the model's BackgroundRole so the view stays a
/// plain QTableView and sorting/filtering works through a proxy.
class ResultsTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        FrequencyColumn = 0,
        LevelColumn,
        UnitColumn,
        LimitColumn,
        MarginColumn,
        VerdictColumn,
        DetectorColumn,
        BandwidthColumn,
        DwellColumn,
        TimestampColumn,
        ColumnCount
    };

    /// Sort role that keeps numeric columns numeric behind a proxy model.
    static constexpr int SortRole = Qt::UserRole + 1;
    /// Frequency in hertz of the row, for syncing the plot selection.
    static constexpr int FrequencyRole = Qt::UserRole + 2;

    explicit ResultsTableModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const override;

    void setPoints(std::vector<MeasurementPoint> points);
    void appendPoint(const MeasurementPoint& point);
    void clear();

    [[nodiscard]] const std::vector<MeasurementPoint>& points() const { return m_points; }
    [[nodiscard]] const MeasurementPoint* pointAt(int row) const;

private:
    std::vector<MeasurementPoint> m_points;
};

} // namespace peakemi::ui
