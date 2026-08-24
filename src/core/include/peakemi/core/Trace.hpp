#pragma once

#include <peakemi/core/Error.hpp>
#include <peakemi/core/InstrumentId.hpp>
#include <peakemi/core/SweepParams.hpp>
#include <peakemi/core/Time.hpp>
#include <peakemi/core/Units.hpp>

#include <memory>
#include <string>
#include <vector>

namespace peakemi {

/// Frequency axis of a trace. Evenly spaced sweeps carry start/stop/points;
/// stitched segmented sweeps carry the explicit frequency of every bin.
struct FrequencyAxis
{
    Hertz start{};
    Hertz stop{};
    int points{0};
    std::vector<Hertz> explicitPoints;

    [[nodiscard]] static FrequencyAxis linear(FrequencyRange range, int points);

    [[nodiscard]] int size() const;
    [[nodiscard]] Hertz frequencyAt(int index) const;

    /// Index of the bin whose centre is closest to @p frequency, or -1 if empty.
    [[nodiscard]] int nearestIndex(Hertz frequency) const;

    friend bool operator==(const FrequencyAxis&, const FrequencyAxis&) = default;
};

/// One acquisition. Immutable by convention: acquisition hands out
/// `shared_ptr<const Trace>` and every consumer copies the pointer, not the data
/// (FR-THR-2, ADR-4).
struct Trace
{
    FrequencyAxis axis;
    std::vector<double> amplitudes;
    AmplitudeUnit unit{AmplitudeUnit::dBuV};
    Detector detector{Detector::Peak};
    SweepParams params{};
    InstrumentId source;
    TimePoint acquiredAt{};
    std::string label;
    bool corrected{false}; ///< true once applyCorrections() has been folded in.

    [[nodiscard]] int size() const { return static_cast<int>(amplitudes.size()); }

    [[nodiscard]] bool isEmpty() const { return amplitudes.empty(); }

    /// Index of the largest amplitude, or -1 for an empty trace.
    [[nodiscard]] int maximumIndex() const;

    [[nodiscard]] double maximumAmplitude() const;

    /// Element-wise maximum with @p other, for max-hold (FR-RUN-8).
    [[nodiscard]] Status mergeMaxHold(const Trace& other);

    friend bool operator==(const Trace&, const Trace&) = default;
};

using TracePtr = std::shared_ptr<const Trace>;

[[nodiscard]] inline TracePtr makeTracePtr(Trace trace)
{
    return std::make_shared<const Trace>(std::move(trace));
}

} // namespace peakemi
