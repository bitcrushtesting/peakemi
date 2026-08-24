#pragma once

#include <peakemi/core/Units.hpp>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

class QWidget;

namespace peakemi::ui {

/// How a series is drawn. The backend picks the concrete pen.
enum class PlotStyle
{
    Line,
    Dashed,
    Points
};

/// One curve in the plot: a live trace, a max-hold, a reference or a limit line.
struct PlotSeries
{
    QString id;    ///< stable identity; setSeries() replaces a series with the same id
    QString label; ///< legend text
    QColor colour{Qt::blue};
    PlotStyle style{PlotStyle::Line};
    bool visible{true};
    qreal width{1.0};
    /// x = frequency in hertz, y = amplitude in the plot's unit.
    QVector<QPointF> points;
};

/// A flagged peak or a user marker.
struct PlotMarker
{
    QString label;
    QPointF position; ///< (frequency in hertz, amplitude)
    QColor colour{Qt::red};
    bool selected{false};
};

/// The plot abstraction of CON-3 / FR-VIS-1.
///
/// Deliberately narrow: series, axes, markers and image export. That is the
/// whole contract, so the open benchmark question about the rendering backend
/// (requirements 6, Q2) can be settled by writing one more implementation
/// instead of touching application logic (ADR-5).
class IPlotBackend
{
public:
    IPlotBackend() = default;
    virtual ~IPlotBackend() = default;

    IPlotBackend(const IPlotBackend&) = delete;
    IPlotBackend& operator=(const IPlotBackend&) = delete;
    IPlotBackend(IPlotBackend&&) = delete;
    IPlotBackend& operator=(IPlotBackend&&) = delete;

    /// Add or replace a series by id.
    virtual void setSeries(const PlotSeries& series) = 0;
    virtual void removeSeries(const QString& id) = 0;
    virtual void clearSeries() = 0;
    virtual void setSeriesVisible(const QString& id, bool visible) = 0;

    virtual void setFrequencyRange(FrequencyRange range) = 0;
    virtual void setAmplitudeRange(double minimum, double maximum) = 0;
    virtual void setLogarithmicFrequency(bool logarithmic) = 0;
    virtual void setAmplitudeLabel(const QString& label) = 0;
    virtual void autoScale() = 0;

    virtual void setMarkers(const QVector<PlotMarker>& markers) = 0;

    /// Off-screen render for the PDF report and the PNG export (FR-VIS-5).
    [[nodiscard]] virtual QImage renderToImage(QSize size) const = 0;
    [[nodiscard]] virtual bool exportSvg(const QString& path, QSize size) const = 0;

    /// The widget to embed. Owned by the backend.
    [[nodiscard]] virtual QWidget* widget() = 0;
};

} // namespace peakemi::ui
