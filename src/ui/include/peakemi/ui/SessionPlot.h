#pragma once

#include <peakemi/core/LimitLine.h>
#include <peakemi/core/Session.h>
#include <peakemi/core/Trace.h>
#include <peakemi/ui/IPlotBackend.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QSize>
#include <QVector>

namespace peakemi::ui {

/// A session's data as plot geometry.
///
/// One implementation for the live view, for a reopened session and for the
/// image the PDF report embeds, so a report never shows a differently drawn
/// spectrum from the one the operator approved on screen.

/// Series colours that hold up on both a light and a dark plot background. The
/// trace and the limit work on either; the verified-point green does not, and
/// is lightened where the background is dark.
[[nodiscard]] QColor traceColour();
[[nodiscard]] QColor limitColour();
[[nodiscard]] QColor verifiedPointColour();

[[nodiscard]] QVector<QPointF> tracePoints(const Trace& trace);

/// Sampled densely enough that a logarithmic frequency axis stays smooth.
[[nodiscard]] QVector<QPointF> limitPoints(const LimitLine& limit, FrequencyRange span);

/// Draws everything a finished session holds: its last trace, the active limit
/// lines and the verified Phase 2 points.
void showSession(IPlotBackend& plot, const Session& session);

/// Off-screen render of that plot, for a report generated without a window.
///
/// Needs a QApplication -- the backend is a widget -- but no display: the
/// offscreen platform plugin is enough, which is what makes it usable from the
/// headless command line.
[[nodiscard]] QImage renderSessionSpectrum(const Session& session, QSize size);

} // namespace peakemi::ui
