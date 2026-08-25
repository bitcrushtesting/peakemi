#pragma once

#include <peakemi/ui/IPlotBackend.h>

#include <QPoint>
#include <QWidget>

#include <optional>
#include <vector>

class QPainter;

namespace peakemi::ui {

/// QPainter implementation of IPlotBackend (path B of architecture.md 7.1).
///
/// Traces are decimated to a min/max envelope per pixel column before drawing,
/// which is what keeps a 40,001-point trace interactive: the cost of a repaint
/// depends on the widget width, not on the trace length (NFR-PERF-2). The full
/// arrays stay untouched in the session.
class PainterPlotBackend
    : public QWidget
    , public IPlotBackend
{
    Q_OBJECT

public:
    explicit PainterPlotBackend(QWidget* parent = nullptr);
    ~PainterPlotBackend() override;

    void setSeries(const PlotSeries& series) override;
    void removeSeries(const QString& id) override;
    void clearSeries() override;
    void setSeriesVisible(const QString& id, bool visible) override;

    void setFrequencyRange(FrequencyRange range) override;
    void setAmplitudeRange(double minimum, double maximum) override;
    void setLogarithmicFrequency(bool logarithmic) override;
    void setAmplitudeLabel(const QString& label) override;
    void autoScale() override;

    void setMarkers(const QVector<PlotMarker>& markers) override;

    [[nodiscard]] QImage renderToImage(QSize size) const override;
    [[nodiscard]] bool exportSvg(const QString& path, QSize size) const override;

    [[nodiscard]] QWidget* widget() override { return this; }

    [[nodiscard]] bool isLogarithmicFrequency() const { return m_logarithmic; }

signals:
    /// Cursor position in data coordinates, for the status bar readout.
    void cursorMoved(double frequencyHz, double amplitude);
    /// The user clicked near a marker; the index refers to setMarkers().
    void markerActivated(int index);
    void viewChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct Layout
    {
        QRectF plotArea;
        double frequencyStart{0.0};
        double frequencyStop{1.0};
        double amplitudeMinimum{0.0};
        double amplitudeMaximum{1.0};
    };

    [[nodiscard]] Layout layoutFor(QSize size) const;
    [[nodiscard]] double toX(const Layout& layout, double frequency) const;
    [[nodiscard]] double toY(const Layout& layout, double amplitude) const;
    [[nodiscard]] double fromX(const Layout& layout, double x) const;
    [[nodiscard]] double fromY(const Layout& layout, double y) const;

    void render(QPainter& painter, QSize size) const;
    void drawGrid(QPainter& painter, const Layout& layout) const;
    void drawSeries(QPainter& painter, const Layout& layout, const PlotSeries& series) const;
    void drawMarkers(QPainter& painter, const Layout& layout) const;
    void drawLegend(QPainter& painter, const Layout& layout) const;
    void drawCursor(QPainter& painter, const Layout& layout) const;

    void zoomFrequency(double factor, double anchorFrequency);

    std::vector<PlotSeries> m_series;
    QVector<PlotMarker> m_markers;
    QString m_amplitudeLabel{QStringLiteral("dBuV")};

    double m_frequencyStart{30e6};
    double m_frequencyStop{1e9};
    double m_amplitudeMinimum{0.0};
    double m_amplitudeMaximum{80.0};
    bool m_logarithmic{false};

    std::optional<QPointF> m_cursor;
    std::optional<QPoint> m_dragOrigin;
    double m_dragStartFrequency{0.0};
    double m_dragStopFrequency{0.0};
};

} // namespace peakemi::ui
