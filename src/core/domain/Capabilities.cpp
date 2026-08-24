#include <peakemi/core/Capabilities.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace peakemi {
namespace {

[[nodiscard]] std::string describe(Hertz frequency)
{
    return std::to_string(frequency.value()) + " Hz";
}

[[nodiscard]] Hertz nearestAtOrAbove(const std::vector<Hertz>& available, Hertz wanted)
{
    if (available.empty()) {
        return wanted;
    }
    auto sorted = available;
    std::sort(sorted.begin(), sorted.end());
    const auto found = std::lower_bound(sorted.begin(), sorted.end(), wanted);
    return found != sorted.end() ? *found : sorted.back();
}

} // namespace

bool Capabilities::supports(Detector detector) const
{
    return std::find(detectors.begin(), detectors.end(), detector) != detectors.end();
}

Hertz Capabilities::nearestResolutionBandwidth(Hertz wanted) const
{
    return nearestAtOrAbove(resolutionBandwidths, wanted);
}

Hertz Capabilities::nearestVideoBandwidth(Hertz wanted) const
{
    return nearestAtOrAbove(videoBandwidths, wanted);
}

Status Capabilities::validate(const SweepParams& params) const
{
    const bool zeroSpanRequested = params.span.width() == Hertz{0};
    if (zeroSpanRequested && !zeroSpan) {
        return fail(ErrorCode::UnsupportedSetting, "instrument has no zero-span mode");
    }
    if (!zeroSpanRequested && !params.span.isValid()) {
        return fail(ErrorCode::InvalidConfiguration, "stop frequency must exceed start frequency");
    }
    if (params.span.start < range.start || params.span.stop > range.stop) {
        return fail(ErrorCode::UnsupportedSetting,
                    "span " + describe(params.span.start) + " to " + describe(params.span.stop)
                        + " leaves the instrument range " + describe(range.start) + " to "
                        + describe(range.stop));
    }
    if (params.points < minimumPoints || params.points > maximumPoints) {
        return fail(ErrorCode::UnsupportedSetting,
                    "trace point count " + std::to_string(params.points) + " outside "
                        + std::to_string(minimumPoints) + " to " + std::to_string(maximumPoints));
    }
    if (!supports(params.detector)) {
        return fail(ErrorCode::UnsupportedSetting,
                    "detector " + std::string{detectorKey(params.detector)} + " not available");
    }
    if (params.rbw > Hertz{0} && !resolutionBandwidths.empty()) {
        const bool exact = std::find(resolutionBandwidths.begin(),
                                     resolutionBandwidths.end(),
                                     params.rbw)
                           != resolutionBandwidths.end();
        if (!exact) {
            return fail(ErrorCode::UnsupportedSetting,
                        "resolution bandwidth " + describe(params.rbw) + " not available");
        }
    }
    if (params.vbw > Hertz{0} && !videoBandwidths.empty()) {
        const bool exact =
            std::find(videoBandwidths.begin(), videoBandwidths.end(), params.vbw)
            != videoBandwidths.end();
        if (!exact) {
            return fail(ErrorCode::UnsupportedSetting,
                        "video bandwidth " + describe(params.vbw) + " not available");
        }
    }
    if (params.preamp && !preamp) {
        return fail(ErrorCode::UnsupportedSetting, "instrument has no pre-amplifier");
    }
    if (!params.automaticAttenuation
        && (params.attenuation < minimumAttenuation || params.attenuation > maximumAttenuation)) {
        return fail(ErrorCode::UnsupportedSetting,
                    "attenuation " + std::to_string(params.attenuation.value())
                        + " dB outside the supported range");
    }
    if (params.refLevel < minimumRefLevel || params.refLevel > maximumRefLevel) {
        return fail(ErrorCode::UnsupportedSetting,
                    "reference level " + std::to_string(params.refLevel.value())
                        + " dB outside the supported range");
    }
    return {};
}

SweepParams Capabilities::coerce(const SweepParams& params) const
{
    SweepParams coerced = params;
    coerced.span.start = std::max(coerced.span.start, range.start);
    coerced.span.stop = std::min(coerced.span.stop, range.stop);
    coerced.points = std::clamp(coerced.points, minimumPoints, maximumPoints);
    if (!supports(coerced.detector) && !detectors.empty()) {
        coerced.detector = detectors.front();
    }
    if (coerced.rbw > Hertz{0}) {
        coerced.rbw = nearestResolutionBandwidth(coerced.rbw);
    }
    if (coerced.vbw > Hertz{0}) {
        coerced.vbw = nearestVideoBandwidth(coerced.vbw);
    }
    coerced.preamp = coerced.preamp && preamp;
    if (!coerced.automaticAttenuation) {
        coerced.attenuation =
            std::clamp(coerced.attenuation, minimumAttenuation, maximumAttenuation);
    }
    coerced.refLevel = std::clamp(coerced.refLevel, minimumRefLevel, maximumRefLevel);
    return coerced;
}

} // namespace peakemi
