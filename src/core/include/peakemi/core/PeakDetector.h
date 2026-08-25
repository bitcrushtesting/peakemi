#pragma once

#include <peakemi/core/LimitEvaluator.h>
#include <peakemi/core/Trace.h>

#include <vector>

namespace peakemi {

/// One Phase 1 candidate: a local maximum close enough to a limit to deserve a
/// quasi-peak dwell in Phase 2 (FR-RUN-1).
struct PeakCandidate
{
    int index{-1};
    Hertz frequency{};
    double amplitude{};
    double limit{};
    double marginDb{};
    double prominenceDb{};
    Verdict verdict{Verdict::Unknown};

    friend bool operator==(const PeakCandidate&, const PeakCandidate&) = default;
};

/// Tuning of the Phase 1 peak search (FR-RUN-1/2).
struct PeakDetectionSettings
{
    /// How far a peak must rise above its surrounding valleys to count.
    double prominenceDb{3.0};
    /// Keep peaks whose margin to the limit is below this (default 6 dB).
    double marginThresholdDb{6.0};
    /// Minimum distance between two kept peaks; the stronger one wins.
    Hertz minimumSpacing{megahertz(1)};
    /// Upper bound on the number of Phase 2 dwells a run will perform.
    int maximumCount{20};
    /// Drop points that no active limit covers instead of ranking them last.
    bool requireLimit{true};
    /// Absolute floor: ignore anything below this amplitude, in the trace unit.
    double amplitudeFloor{-200.0};

    friend bool operator==(const PeakDetectionSettings&, const PeakDetectionSettings&) = default;
};

/// Find Phase 2 candidates in a corrected trace.
///
/// Pure function: local maxima by prominence, filtered by proximity to the
/// limit, de-duplicated by minimum spacing, ranked by smallest margin and
/// truncated to `maximumCount`. The result is ordered by ascending margin,
/// i.e. the most critical emission first.
[[nodiscard]] std::vector<PeakCandidate> detectPeaks(const Trace& trace,
                                                     const LimitEvaluator& evaluator,
                                                     const PeakDetectionSettings& settings);

} // namespace peakemi
