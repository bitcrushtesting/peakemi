#pragma once

#include <peakemi/core/LimitLine.h>
#include <peakemi/core/Trace.h>

#include <span>
#include <vector>

namespace peakemi {

/// Margin of one measurement against the worst-case active limit (FR-LIM-5).
struct MarginResult
{
    Hertz frequency{};
    double amplitude{}; ///< corrected measurement, in the trace unit
    double limit{};     ///< NaN when no active limit covers this frequency
    double marginDb{};  ///< limit - amplitude; positive means below the limit
    Verdict verdict{Verdict::Unknown};
    int limitIndex{-1}; ///< index into the evaluator's limit lines, -1 if none
};

/// Worst case over a set of margins, with the frequency where it occurred.
struct WorstCase
{
    bool valid{false};
    MarginResult result{};
};

/// Evaluates traces against the currently active limit lines (FR-LIM-4/5).
///
/// Several limits may be active at once; the worst (smallest) margin wins, so a
/// point is only "pass" when it passes every applicable limit.
class LimitEvaluator
{
public:
    LimitEvaluator() = default;
    explicit LimitEvaluator(std::vector<LimitLine> lines, double marginalThresholdDb = 6.0);

    void setLimitLines(std::vector<LimitLine> lines);
    void setMarginalThreshold(double thresholdDb);

    [[nodiscard]] std::span<const LimitLine> limitLines() const { return m_lines; }

    [[nodiscard]] double marginalThreshold() const { return m_marginalThresholdDb; }

    [[nodiscard]] bool hasLimits() const { return !m_lines.empty(); }

    /// Worst-case margin of one measured point across all active limits.
    [[nodiscard]] MarginResult evaluate(Hertz frequency, double amplitude) const;

    /// Per-point margins for a whole trace; the trace must already be corrected.
    [[nodiscard]] std::vector<MarginResult> evaluateTrace(const Trace& trace) const;

    /// Verdict for a margin, using the configured marginal threshold.
    [[nodiscard]] Verdict classify(double marginDb, bool hasLimit) const;

private:
    std::vector<LimitLine> m_lines;
    double m_marginalThresholdDb{6.0};
};

/// Smallest margin in @p results, i.e. the point closest to (or furthest past)
/// the limit. Points without a limit are ignored.
[[nodiscard]] WorstCase worstCase(std::span<const MarginResult> results);

/// Worst case restricted to @p band — used for the per-band summary of FR-LIM-5.
[[nodiscard]] WorstCase worstCaseInBand(std::span<const MarginResult> results, FrequencyRange band);

} // namespace peakemi
