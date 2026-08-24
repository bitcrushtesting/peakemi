#include <peakemi/core/CisprBands.hpp>
#include <peakemi/core/RunConfiguration.hpp>

#include <algorithm>
#include <cmath>

namespace peakemi {

SweepParams RunConfiguration::phase1SweepParams(FrequencyRange segment) const
{
    const Hertz rbw =
        phase1Rbw > Hertz{0} ? phase1Rbw : mandatedResolutionBandwidth(segment.centre());
    return SweepParams{.span = segment,
                       .rbw = rbw,
                       .vbw = phase1Vbw,
                       .detector = phase1Detector,
                       .points = phase1Points,
                       .refLevel = refLevel,
                       .attenuation = attenuation,
                       .automaticAttenuation = automaticAttenuation,
                       .preamp = preamp,
                       .sweepTime = std::chrono::milliseconds{0}};
}

SweepParams RunConfiguration::phase2SweepParams(Hertz frequency) const
{
    const Hertz rbw =
        verificationRbw > Hertz{0} ? verificationRbw : mandatedResolutionBandwidth(frequency);
    const Hertz half = verificationSpan / 2;
    return SweepParams{.span = FrequencyRange{frequency - half, frequency + half},
                       .rbw = rbw,
                       .vbw = Hertz{0},
                       .detector = verificationDetector,
                       .points = verificationPoints,
                       .refLevel = refLevel,
                       .attenuation = attenuation,
                       .automaticAttenuation = automaticAttenuation,
                       .preamp = preamp,
                       .sweepTime = dwellTime};
}

std::vector<FrequencyRange> RunConfiguration::planSegments() const
{
    std::vector<FrequencyRange> segments;
    if (!span.isValid()) {
        return segments;
    }

    const int points = std::max(phase1Points, 2);
    const Hertz rbw = phase1Rbw > Hertz{0} ? phase1Rbw : mandatedResolutionBandwidth(span.centre());

    // A sweep may not step further than the resolution bandwidth between bins,
    // otherwise narrowband emissions fall between the samples.
    const auto binBudget = static_cast<double>(points - 1);
    const auto maximumWidth =
        Hertz{static_cast<std::int64_t>(binBudget * static_cast<double>(rbw.value()))};
    if (maximumWidth <= Hertz{0} || span.width() <= maximumWidth) {
        segments.push_back(span);
        return segments;
    }

    const auto count = static_cast<int>(
        std::ceil(static_cast<double>(span.width().value()) / static_cast<double>(maximumWidth.value())));
    const Hertz step = span.width() / count;
    Hertz start = span.start;
    for (int i = 0; i < count; ++i) {
        const Hertz stop = (i == count - 1) ? span.stop : start + step;
        segments.push_back(FrequencyRange{start, stop});
        start = stop;
    }
    return segments;
}

Status RunConfiguration::validate() const
{
    if (!span.isValid()) {
        return fail(ErrorCode::InvalidConfiguration, "the scan span is empty or inverted");
    }
    if (phase1Points < 2) {
        return fail(ErrorCode::InvalidConfiguration, "phase 1 needs at least two trace points");
    }
    if (passes < 1) {
        return fail(ErrorCode::InvalidConfiguration, "a run needs at least one pass");
    }
    if (maxRetries < 0) {
        return fail(ErrorCode::InvalidConfiguration, "the retry count cannot be negative");
    }
    if (dwellTime.count() <= 0) {
        return fail(ErrorCode::InvalidConfiguration, "the dwell time must be positive");
    }
    if (peaks.maximumCount <= 0) {
        return fail(ErrorCode::InvalidConfiguration, "the maximum peak count must be positive");
    }
    for (const auto& limit : limits) {
        if (auto status = limit.validate(); !status) {
            return status;
        }
    }
    return {};
}

} // namespace peakemi
