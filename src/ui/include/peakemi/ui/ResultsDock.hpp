#pragma once

#include <peakemi/core/MeasurementPoint.hpp>

#include <QDockWidget>

class QLabel;
class QSortFilterProxyModel;
class QTableView;

namespace peakemi::ui {

class ResultsTableModel;

/// Sortable, verdict-coloured table of the Phase 2 results, kept in sync with
/// the plot selection (architecture.md 7).
class ResultsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit ResultsDock(QWidget* parent = nullptr);

    void appendPoint(const MeasurementPoint& point);
    void setPoints(std::vector<MeasurementPoint> points);
    void clear();

    [[nodiscard]] const std::vector<MeasurementPoint>& points() const;

    /// Select the row whose frequency matches, e.g. after a plot marker click.
    void selectFrequency(Hertz frequency);

signals:
    /// The user selected a row; the plot highlights the matching marker.
    void pointSelected(peakemi::Hertz frequency);

private:
    void updateSummary();

    ResultsTableModel* m_model{nullptr};
    QSortFilterProxyModel* m_proxy{nullptr};
    QTableView* m_view{nullptr};
    QLabel* m_summary{nullptr};
};

} // namespace peakemi::ui
