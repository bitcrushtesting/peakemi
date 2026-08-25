#include <peakemi/core/LimitLine.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace peakemi {
namespace {

constexpr std::array<std::pair<Interpolation, std::string_view>, 3> InterpolationKeys{{
    {Interpolation::LogFrequency, "log-frequency"},
    {Interpolation::Linear, "linear"},
    {Interpolation::Step, "step"},
}};

constexpr std::array<std::pair<EmissionKind, std::string_view>, 2> EmissionKindKeys{{
    {EmissionKind::Conducted, "conducted"},
    {EmissionKind::Radiated, "radiated"},
}};

constexpr std::array<std::pair<EquipmentClass, std::string_view>, 3> EquipmentClassKeys{{
    {EquipmentClass::Unspecified, "unspecified"},
    {EquipmentClass::ClassA, "A"},
    {EquipmentClass::ClassB, "B"},
}};

template<class Enum, std::size_t N>
[[nodiscard]] std::optional<Enum>
lookup(const std::array<std::pair<Enum, std::string_view>, N>& table, std::string_view key)
{
    for (const auto& [value, name] : table) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] double interpolate(const LimitPoint& lower, const LimitPoint& upper, Hertz frequency)
{
    switch (lower.interpolationToNext) {
        case Interpolation::Step:
            return lower.amplitude;
        case Interpolation::Linear: {
            const double span = static_cast<double>((upper.frequency - lower.frequency).value());
            if (span <= 0.0) {
                return lower.amplitude;
            }
            const double fraction =
                static_cast<double>((frequency - lower.frequency).value()) / span;
            return lower.amplitude + fraction * (upper.amplitude - lower.amplitude);
        }
        case Interpolation::LogFrequency: {
            const double lowerFrequency = static_cast<double>(lower.frequency.value());
            const double upperFrequency = static_cast<double>(upper.frequency.value());
            if (lowerFrequency <= 0.0 || upperFrequency <= 0.0) {
                return lower.amplitude;
            }
            const double span = std::log10(upperFrequency) - std::log10(lowerFrequency);
            if (span <= 0.0) {
                return lower.amplitude;
            }
            const double fraction =
                (std::log10(static_cast<double>(frequency.value())) - std::log10(lowerFrequency)) /
                span;
            return lower.amplitude + fraction * (upper.amplitude - lower.amplitude);
        }
    }
    return lower.amplitude;
}

} // namespace

FrequencyRange LimitLine::coverage() const
{
    if (points.empty()) {
        return FrequencyRange{};
    }
    return FrequencyRange{.start = points.front().frequency, .stop = points.back().frequency};
}

bool LimitLine::covers(Hertz frequency) const
{
    return !points.empty() && frequency >= points.front().frequency &&
           frequency <= points.back().frequency;
}

double LimitLine::evaluateAt(Hertz frequency) const
{
    if (!covers(frequency)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (points.size() == 1) {
        return points.front().amplitude;
    }

    // Last breakpoint at or below `frequency`; a step at a band edge means the
    // later of two breakpoints sharing a frequency wins.
    std::size_t lower = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (points[i + 1].frequency <= frequency) {
            lower = i + 1;
        } else {
            break;
        }
    }
    if (lower + 1 >= points.size()) {
        return points.back().amplitude;
    }
    return interpolate(points[lower], points[lower + 1], frequency);
}

Status LimitLine::validate() const
{
    if (points.size() < 2) {
        return fail(ErrorCode::InvalidConfiguration,
                    "limit line '" + name + "' needs at least two breakpoints");
    }
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (points[i].frequency <= Hertz{0}) {
            return fail(ErrorCode::InvalidConfiguration,
                        "limit line '" + name + "' has a non-positive frequency");
        }
        if (!std::isfinite(points[i].amplitude)) {
            return fail(ErrorCode::InvalidConfiguration,
                        "limit line '" + name + "' has a non-finite amplitude");
        }
        if (i > 0 && points[i].frequency < points[i - 1].frequency) {
            return fail(ErrorCode::InvalidConfiguration,
                        "limit line '" + name + "' breakpoints are not ordered by frequency");
        }
    }
    return {};
}

void LimitLine::sortPoints()
{
    std::stable_sort(points.begin(), points.end(), [](const LimitPoint& a, const LimitPoint& b) {
        return a.frequency < b.frequency;
    });
}

std::string_view interpolationKey(Interpolation interpolation)
{
    return InterpolationKeys[static_cast<std::size_t>(interpolation)].second;
}

std::optional<Interpolation> interpolationFromKey(std::string_view key)
{
    return lookup(InterpolationKeys, key);
}

std::string_view emissionKindKey(EmissionKind kind)
{
    return EmissionKindKeys[static_cast<std::size_t>(kind)].second;
}

std::optional<EmissionKind> emissionKindFromKey(std::string_view key)
{
    return lookup(EmissionKindKeys, key);
}

std::string_view equipmentClassKey(EquipmentClass equipmentClass)
{
    return EquipmentClassKeys[static_cast<std::size_t>(equipmentClass)].second;
}

std::optional<EquipmentClass> equipmentClassFromKey(std::string_view key)
{
    return lookup(EquipmentClassKeys, key);
}

} // namespace peakemi
