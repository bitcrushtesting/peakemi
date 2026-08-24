#pragma once

#include <peakemi/core/Error.hpp>
#include <peakemi/core/SweepParams.hpp>
#include <peakemi/core/Units.hpp>

#include <vector>

namespace peakemi {

/// What an instrument declares it can do (FR-HAL-3). Queried once per
/// connection, cached by the engine, and consulted before any command is sent.
struct Capabilities
{
    FrequencyRange range{hertz(9000), gigahertz(3.0)};
    int minimumPoints{101};
    int maximumPoints{1001};
    std::vector<Detector> detectors{Detector::Peak};
    std::vector<Hertz> resolutionBandwidths;
    std::vector<Hertz> videoBandwidths;
    Decibel minimumAttenuation{0.0};
    Decibel maximumAttenuation{40.0};
    Decibel attenuationStep{5.0};
    Decibel minimumRefLevel{-100.0};
    Decibel maximumRefLevel{30.0};
    bool preamp{false};
    bool trackingGenerator{false};
    bool zeroSpan{true};
    AmplitudeUnit nativeUnit{AmplitudeUnit::dBm};

    [[nodiscard]] bool supports(Detector detector) const;

    /// Nearest supported RBW at or above @p wanted, falling back to the largest
    /// available one. Returns @p wanted unchanged when no discrete list exists.
    [[nodiscard]] Hertz nearestResolutionBandwidth(Hertz wanted) const;
    [[nodiscard]] Hertz nearestVideoBandwidth(Hertz wanted) const;

    /// Reject an unsupported configuration with an actionable message rather
    /// than letting the driver send a command the instrument will refuse.
    [[nodiscard]] Status validate(const SweepParams& params) const;

    /// @return @p params clamped/snapped to what this instrument can actually do.
    [[nodiscard]] SweepParams coerce(const SweepParams& params) const;
};

} // namespace peakemi
