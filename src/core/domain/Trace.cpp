#include <peakemi/core/Trace.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace peakemi {

FrequencyAxis FrequencyAxis::linear(FrequencyRange range, int points)
{
    return FrequencyAxis{.start = range.start, .stop = range.stop, .points = points, .explicitPoints = {}};
}

int FrequencyAxis::size() const
{
    return explicitPoints.empty() ? points : static_cast<int>(explicitPoints.size());
}

Hertz FrequencyAxis::frequencyAt(int index) const
{
    if (!explicitPoints.empty()) {
        if (index < 0 || index >= static_cast<int>(explicitPoints.size())) {
            return Hertz{0};
        }
        return explicitPoints[static_cast<std::size_t>(index)];
    }
    if (points <= 1) {
        return start;
    }
    const double fraction = static_cast<double>(index) / static_cast<double>(points - 1);
    const double width = static_cast<double>((stop - start).value());
    return start + Hertz{static_cast<std::int64_t>(std::llround(fraction * width))};
}

int FrequencyAxis::nearestIndex(Hertz frequency) const
{
    const int count = size();
    if (count <= 0) {
        return -1;
    }
    if (!explicitPoints.empty()) {
        const auto found =
            std::lower_bound(explicitPoints.begin(), explicitPoints.end(), frequency);
        if (found == explicitPoints.begin()) {
            return 0;
        }
        if (found == explicitPoints.end()) {
            return count - 1;
        }
        const auto upper = static_cast<int>(std::distance(explicitPoints.begin(), found));
        const Hertz lowerDistance =
            frequency - explicitPoints[static_cast<std::size_t>(upper) - 1];
        const Hertz upperDistance = explicitPoints[static_cast<std::size_t>(upper)] - frequency;
        return lowerDistance <= upperDistance ? upper - 1 : upper;
    }
    if (count == 1 || stop == start) {
        return 0;
    }
    const double fraction = static_cast<double>((frequency - start).value())
                            / static_cast<double>((stop - start).value());
    const auto index = static_cast<int>(std::llround(fraction * static_cast<double>(count - 1)));
    return std::clamp(index, 0, count - 1);
}

int Trace::maximumIndex() const
{
    if (amplitudes.empty()) {
        return -1;
    }
    const auto found = std::max_element(amplitudes.begin(), amplitudes.end());
    return static_cast<int>(std::distance(amplitudes.begin(), found));
}

double Trace::maximumAmplitude() const
{
    const int index = maximumIndex();
    return index < 0 ? -std::numeric_limits<double>::infinity()
                     : amplitudes[static_cast<std::size_t>(index)];
}

Status Trace::mergeMaxHold(const Trace& other)
{
    if (other.amplitudes.size() != amplitudes.size() || other.axis != axis) {
        return fail(ErrorCode::InvalidConfiguration,
                    "max-hold merge needs two traces on the same frequency axis");
    }
    if (other.unit != unit) {
        return fail(ErrorCode::InvalidConfiguration, "max-hold merge needs matching amplitude units");
    }
    for (std::size_t i = 0; i < amplitudes.size(); ++i) {
        amplitudes[i] = std::max(amplitudes[i], other.amplitudes[i]);
    }
    acquiredAt = std::max(acquiredAt, other.acquiredAt);
    return {};
}

} // namespace peakemi
