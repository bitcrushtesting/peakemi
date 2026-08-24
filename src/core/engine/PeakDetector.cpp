#include <peakemi/core/PeakDetector.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace peakemi {
namespace {

/// Prominence of the maximum at @p peak: its height above the higher of the two
/// lowest valleys between it and the nearest higher sample on either side.
[[nodiscard]] double prominence(const std::vector<double>& values, std::size_t peak)
{
    const double height = values[peak];

    double leftValley = height;
    for (std::size_t i = peak; i-- > 0;) {
        if (values[i] > height) {
            break;
        }
        leftValley = std::min(leftValley, values[i]);
    }

    double rightValley = height;
    for (std::size_t i = peak + 1; i < values.size(); ++i) {
        if (values[i] > height) {
            break;
        }
        rightValley = std::min(rightValley, values[i]);
    }

    return height - std::max(leftValley, rightValley);
}

[[nodiscard]] bool isLocalMaximum(const std::vector<double>& values, std::size_t index)
{
    const double value = values[index];
    if (index > 0 && values[index - 1] > value) {
        return false;
    }
    if (index + 1 < values.size() && values[index + 1] > value) {
        return false;
    }
    // On a plateau only the first sample is reported.
    return !(index > 0 && values[index - 1] == value);
}

} // namespace

std::vector<PeakCandidate> detectPeaks(const Trace& trace,
                                       const LimitEvaluator& evaluator,
                                       const PeakDetectionSettings& settings)
{
    std::vector<PeakCandidate> candidates;
    if (trace.size() < 3) {
        return candidates;
    }

    const auto& values = trace.amplitudes;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i]) || values[i] < settings.amplitudeFloor) {
            continue;
        }
        if (!isLocalMaximum(values, i)) {
            continue;
        }
        const double peakProminence = prominence(values, i);
        if (peakProminence < settings.prominenceDb) {
            continue;
        }

        const int index = static_cast<int>(i);
        const auto margin = evaluator.evaluate(trace.axis.frequencyAt(index), values[i]);
        const bool hasLimit = margin.limitIndex >= 0;
        if (settings.requireLimit && !hasLimit) {
            continue;
        }
        if (hasLimit && margin.marginDb > settings.marginThresholdDb) {
            continue;
        }

        candidates.push_back(PeakCandidate{.index = index,
                                           .frequency = margin.frequency,
                                           .amplitude = margin.amplitude,
                                           .limit = margin.limit,
                                           .marginDb = hasLimit
                                                           ? margin.marginDb
                                                           : std::numeric_limits<double>::infinity(),
                                           .prominenceDb = peakProminence,
                                           .verdict = margin.verdict});
    }

    // Rank by criticality: smallest margin first, strongest signal as tie break.
    std::stable_sort(candidates.begin(),
                     candidates.end(),
                     [](const PeakCandidate& a, const PeakCandidate& b) {
                         if (a.marginDb != b.marginDb) {
                             return a.marginDb < b.marginDb;
                         }
                         return a.amplitude > b.amplitude;
                     });

    // De-duplicate by minimum spacing, keeping the more critical peak (FR-RUN-2).
    std::vector<PeakCandidate> kept;
    kept.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        const bool tooClose =
            std::any_of(kept.begin(), kept.end(), [&](const PeakCandidate& accepted) {
                const Hertz distance = accepted.frequency > candidate.frequency
                                           ? accepted.frequency - candidate.frequency
                                           : candidate.frequency - accepted.frequency;
                return distance < settings.minimumSpacing;
            });
        if (tooClose) {
            continue;
        }
        kept.push_back(candidate);
        if (settings.maximumCount > 0 && static_cast<int>(kept.size()) >= settings.maximumCount) {
            break;
        }
    }
    return kept;
}

} // namespace peakemi
