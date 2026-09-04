#include <peakemi/ui/PainterPlotBackend.h>
#include <peakemi/ui/SessionPlot.h>

#include <QGuiApplication>
#include <QPalette>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace peakemi::ui {
// The colours live here rather than in the window, so the image the report
// embeds is drawn with the same palette the operator approved on screen.

QColor traceColour()
{
    return QColor{0x1A, 0x73, 0xE8};
}

QColor limitColour()
{
    return QColor{0xC0, 0x39, 0x2B};
}

QColor verifiedPointColour()
{
    const bool dark = QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
    return dark ? QColor{0x4C, 0xD9, 0x8C} : QColor{0x0B, 0x80, 0x43};
}

QVector<QPointF> tracePoints(const Trace& trace)
{
    QVector<QPointF> points;
    points.reserve(trace.size());
    for (int i = 0; i < trace.size(); ++i) {
        points.append(QPointF{static_cast<double>(trace.axis.frequencyAt(i).value()),
                              trace.amplitudes[static_cast<std::size_t>(i)]});
    }
    return points;
}

QVector<QPointF> limitPoints(const LimitLine& limit, FrequencyRange span)
{
    QVector<QPointF> points;
    const auto coverage = limit.coverage();
    const auto start = std::max(coverage.start, span.start);
    const auto stop = std::min(coverage.stop, span.stop);
    if (stop <= start) {
        return points;
    }

    constexpr int Samples = 400;
    for (int i = 0; i <= Samples; ++i) {
        const double fraction = static_cast<double>(i) / Samples;
        const auto frequency = start + Hertz{static_cast<std::int64_t>(
                                           fraction * static_cast<double>((stop - start).value()))};
        const double value = limit.evaluateAt(frequency);
        if (std::isfinite(value)) {
            points.append(QPointF{static_cast<double>(frequency.value()), value});
        }
    }
    return points;
}

void showSession(IPlotBackend& plot, const Session& session)
{
    plot.clearSeries();

    for (const auto& limit : session.config.limits) {
        PlotSeries series;
        series.id = QStringLiteral("limit:%1").arg(QString::fromStdString(limit.name));
        series.label = QString::fromStdString(limit.name);
        series.colour = limitColour();
        series.style = PlotStyle::Dashed;
        series.width = 1.5;
        series.points = limitPoints(limit, session.config.span);
        plot.setSeries(series);
    }

    AmplitudeUnit unit =
        session.config.limits.empty() ? AmplitudeUnit::dBuV : session.config.limits.front().unit;
    if (!session.traces.empty()) {
        const auto& trace = session.traces.back();
        unit = trace.unit;

        PlotSeries series;
        series.id = QStringLiteral("trace:live");
        series.label = QString::fromStdString(trace.label);
        series.colour = traceColour();
        series.points = tracePoints(trace);
        plot.setSeries(series);
    }
    plot.setAmplitudeLabel(QString::fromUtf8(
        amplitudeUnitKey(unit).data(), static_cast<qsizetype>(amplitudeUnitKey(unit).size())));

    if (!session.results.empty()) {
        PlotSeries verified;
        verified.id = QStringLiteral("phase2");
        verified.colour = verifiedPointColour();
        verified.style = PlotStyle::Points;
        verified.width = 7.0;
        verified.points.reserve(static_cast<qsizetype>(session.results.size()));
        for (const auto& point : session.results) {
            verified.points.append(
                QPointF{static_cast<double>(point.frequency.value()), point.correctedAmplitude});
        }
        plot.setSeries(verified);
    }

    plot.setFrequencyRange(session.config.span);
    plot.autoScale();
}

QImage renderSessionSpectrum(const Session& session, QSize size)
{
    // Never shown: the widget is resized to the target size and painted into an
    // image, so nothing about this needs a window or a compositor.
    PainterPlotBackend plot;
    plot.resize(size);
    showSession(plot, session);
    return plot.renderToImage(size);
}

} // namespace peakemi::ui
