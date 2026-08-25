#pragma once

#include <peakemi/core/Error.hpp>
#include <peakemi/core/Units.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace peakemi {

/// How the limit behaves between two breakpoints (FR-LIM-2).
enum class Interpolation : std::uint8_t
{
    /// Linear amplitude over logarithmic frequency — the CISPR convention.
    LogFrequency,
    /// Linear amplitude over linear frequency.
    Linear,
    /// Hold the left value up to the next breakpoint: a band-edge step.
    Step
};

enum class EmissionKind : std::uint8_t
{
    Conducted,
    Radiated
};

enum class EquipmentClass : std::uint8_t
{
    Unspecified,
    ClassA,
    ClassB
};

struct LimitPoint
{
    Hertz frequency{};
    double amplitude{};
    Interpolation interpolationToNext{Interpolation::LogFrequency};

    friend bool operator==(const LimitPoint&, const LimitPoint&) = default;
};

/// A regulatory limit as an ordered breakpoint list plus the metadata a report
/// has to cite (FR-LIM-1/3).
struct LimitLine
{
    std::string name;
    std::string standard;  ///< e.g. "CISPR 32:2015 / EN 55032"
    std::string note;      ///< edition, table reference, provenance
    EmissionKind kind{EmissionKind::Radiated};
    EquipmentClass equipmentClass{EquipmentClass::Unspecified};
    Detector detector{Detector::QuasiPeak};
    AmplitudeUnit unit{AmplitudeUnit::dBuV_per_m};
    double measurementDistanceMetres{0.0}; ///< 0 for conducted limits
    bool builtIn{false};                   ///< read-only catalogue entry (FR-LIM-3)
    std::vector<LimitPoint> points;

    [[nodiscard]] FrequencyRange coverage() const;
    [[nodiscard]] bool covers(Hertz frequency) const;

    /// Limit value at @p frequency, or NaN outside the covered range.
    [[nodiscard]] double evaluateAt(Hertz frequency) const;

    /// Breakpoints ordered, at least two of them, finite amplitudes.
    [[nodiscard]] Status validate() const;

    /// Sort breakpoints by frequency; keeps a hand-written file usable.
    void sortPoints();

    friend bool operator==(const LimitLine&, const LimitLine&) = default;
};

[[nodiscard]] std::string_view interpolationKey(Interpolation interpolation);
[[nodiscard]] std::optional<Interpolation> interpolationFromKey(std::string_view key);
[[nodiscard]] std::string_view emissionKindKey(EmissionKind kind);
[[nodiscard]] std::optional<EmissionKind> emissionKindFromKey(std::string_view key);
[[nodiscard]] std::string_view equipmentClassKey(EquipmentClass equipmentClass);
[[nodiscard]] std::optional<EquipmentClass> equipmentClassFromKey(std::string_view key);

} // namespace peakemi
