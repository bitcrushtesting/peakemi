#include <peakemi/ui/ResultsDock.h>
#include <peakemi/ui/ResultsTableModel.h>

#include <QHeaderView>
#include <QLabel>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace peakemi::ui {

ResultsDock::ResultsDock(QWidget* parent) : QDockWidget{tr("Results"), parent}
{
    setObjectName(QStringLiteral("resultsDock"));

    auto* content = new QWidget{this};
    auto* layout = new QVBoxLayout{content};

    m_model = new ResultsTableModel{this};
    m_proxy = new QSortFilterProxyModel{this};
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(ResultsTableModel::SortRole);

    m_view = new QTableView{content};
    m_view->setModel(m_proxy);
    m_view->setObjectName(QStringLiteral("resultsView"));
    m_view->setSortingEnabled(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAlternatingRowColors(true);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_view->verticalHeader()->setVisible(false);
    layout->addWidget(m_view, 1);

    m_summary = new QLabel{tr("No results yet."), content};
    m_summary->setWordWrap(true);
    layout->addWidget(m_summary);

    setWidget(content);

    connect(m_view->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            [this](const QModelIndex& current) {
                if (!current.isValid()) {
                    return;
                }
                const auto frequency = current.data(ResultsTableModel::FrequencyRole).toLongLong();
                emit pointSelected(hertz(frequency));
            });
}

const std::vector<MeasurementPoint>& ResultsDock::points() const
{
    return m_model->points();
}

void ResultsDock::appendPoint(const MeasurementPoint& point)
{
    m_model->appendPoint(point);
    updateSummary();
}

void ResultsDock::setPoints(std::vector<MeasurementPoint> points)
{
    m_model->setPoints(std::move(points));
    updateSummary();
}

void ResultsDock::clear()
{
    m_model->clear();
    updateSummary();
}

void ResultsDock::selectFrequency(Hertz frequency)
{
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const auto index = m_proxy->index(row, 0);
        if (index.data(ResultsTableModel::FrequencyRole).toLongLong() == frequency.value()) {
            m_view->selectRow(row);
            m_view->scrollTo(index);
            return;
        }
    }
}

void ResultsDock::updateSummary()
{
    const auto& points = m_model->points();
    if (points.empty()) {
        m_summary->setText(tr("No results yet."));
        return;
    }

    int failures = 0;
    int marginal = 0;
    const MeasurementPoint* worst = nullptr;
    for (const auto& point : points) {
        failures += point.verdict == Verdict::Fail ? 1 : 0;
        marginal += point.verdict == Verdict::Marginal ? 1 : 0;
        if (std::isfinite(point.marginDb) && (worst == nullptr || point.marginDb < worst->marginDb))
        {
            worst = &point;
        }
    }

    QString summary = tr("%n point(s) verified", nullptr, static_cast<int>(points.size()));
    summary += tr(" — %1 failing, %2 marginal").arg(failures).arg(marginal);
    if (worst != nullptr) {
        summary += tr("; worst margin %1 dB at %2 MHz")
                       .arg(worst->marginDb, 0, 'f', 1)
                       .arg(toMegahertz(worst->frequency), 0, 'f', 4);
    }
    m_summary->setText(summary);
}

} // namespace peakemi::ui
