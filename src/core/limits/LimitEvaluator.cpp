#include <peakemi/core/LimitEvaluator.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace peakemi {

LimitEvaluator::LimitEvaluator(std::vector<LimitLine> lines, double marginalThresholdDb)
    : m_lines{std::move(lines)}
    , m_marginalThresholdDb{marginalThresholdDb}
{}

void LimitEvaluator::setLimitLines(std::vector<LimitLine> lines)
{
    m_lines = std::move(lines);
}

void LimitEvaluator::setMarginalThreshold(double thresholdDb)
{
    m_marginalThresholdDb = thresholdDb;
}

Verdict LimitEvaluator::classify(double marginDb, bool hasLimit) const
{
    if (!hasLimit || !std::isfinite(marginDb)) {
        return Verdict::Unknown;
    }
    if (marginDb < 0.0) {
        return Verdict::Fail;
    }
    return marginDb < m_marginalThresholdDb ? Verdict::Marginal : Verdict::Pass;
}

MarginResult LimitEvaluator::evaluate(Hertz frequency, double amplitude) const
{
    MarginResult worst{.frequency = frequency,
                       .amplitude = amplitude,
                       .limit = std::numeric_limits<double>::quiet_NaN(),
                       .marginDb = std::numeric_limits<double>::quiet_NaN(),
                       .verdict = Verdict::Unknown,
                       .limitIndex = -1};

    for (std::size_t i = 0; i < m_lines.size(); ++i) {
        const double limit = m_lines[i].evaluateAt(frequency);
        if (!std::isfinite(limit)) {
            continue;
        }
        const double margin = limit - amplitude;
        if (worst.limitIndex < 0 || margin < worst.marginDb) {
            worst.limit = limit;
            worst.marginDb = margin;
            worst.limitIndex = static_cast<int>(i);
        }
    }
    worst.verdict = classify(worst.marginDb, worst.limitIndex >= 0);
    return worst;
}

std::vector<MarginResult> LimitEvaluator::evaluateTrace(const Trace& trace) const
{
    std::vector<MarginResult> results;
    results.reserve(trace.amplitudes.size());
    for (int i = 0; i < trace.size(); ++i) {
        results.push_back(
            evaluate(trace.axis.frequencyAt(i), trace.amplitudes[static_cast<std::size_t>(i)]));
    }
    return results;
}

WorstCase worstCase(std::span<const MarginResult> results)
{
    WorstCase worst;
    for (const auto& result : results) {
        if (result.limitIndex < 0 || !std::isfinite(result.marginDb)) {
            continue;
        }
        if (!worst.valid || result.marginDb < worst.result.marginDb) {
            worst.valid = true;
            worst.result = result;
        }
    }
    return worst;
}

WorstCase worstCaseInBand(std::span<const MarginResult> results, FrequencyRange band)
{
    WorstCase worst;
    for (const auto& result : results) {
        if (!band.contains(result.frequency) || result.limitIndex < 0 ||
            !std::isfinite(result.marginDb))
        {
            continue;
        }
        if (!worst.valid || result.marginDb < worst.result.marginDb) {
            worst.valid = true;
            worst.result = result;
        }
    }
    return worst;
}

} // namespace peakemi
