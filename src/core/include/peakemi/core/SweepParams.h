#pragma once

#include <peakemi/core/Units.h>

#include <chrono>

namespace peakemi {

/// One instrument configuration. Zero-valued bandwidths and times mean "let the
/// instrument choose"; a NaN attenuation means automatic attenuation.
struct SweepParams
{
    FrequencyRange span{};
    Hertz rbw{0};
    Hertz vbw{0};
    Detector detector{Detector::Peak};
    int points{1001};
    Decibel refLevel{0.0};
    Decibel attenuation{10.0};
    bool automaticAttenuation{true};
    bool preamp{false};
    std::chrono::milliseconds sweepTime{0};

    friend bool operator==(const SweepParams&, const SweepParams&) = default;
};

} // namespace peakemi
