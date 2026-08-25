#include <peakemi/core/Units.h>

#include <array>
#include <cmath>
#include <utility>

namespace peakemi {
namespace {

template<class Enum, std::size_t N>
[[nodiscard]] std::optional<Enum>
fromKey(const std::array<std::pair<Enum, std::string_view>, N>& table, std::string_view key)
{
    for (const auto& [value, name] : table) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

constexpr std::array<std::pair<Detector, std::string_view>, 5> DetectorKeys{{
    {Detector::Peak, "peak"},
    {Detector::QuasiPeak, "quasi-peak"},
    {Detector::Average, "average"},
    {Detector::Rms, "rms"},
    {Detector::Sample, "sample"},
}};

constexpr std::array<std::pair<AmplitudeUnit, std::string_view>, 4> AmplitudeUnitKeys{{
    {AmplitudeUnit::dBm, "dBm"},
    {AmplitudeUnit::dBuV, "dBuV"},
    {AmplitudeUnit::dBuV_per_m, "dBuV/m"},
    {AmplitudeUnit::dBuA, "dBuA"},
}};

constexpr std::array<std::pair<Verdict, std::string_view>, 4> VerdictKeys{{
    {Verdict::Unknown, "unknown"},
    {Verdict::Pass, "pass"},
    {Verdict::Marginal, "marginal"},
    {Verdict::Fail, "fail"},
}};

/// dBuV = dBm + 90 + 10*log10(Z) ... 107.0 dB for the usual 50 ohm system.
[[nodiscard]] double dBmToDBuVOffset(double impedanceOhms)
{
    return 90.0 + 10.0 * std::log10(impedanceOhms);
}

} // namespace

std::string_view detectorKey(Detector detector)
{
    return DetectorKeys[static_cast<std::size_t>(detector)].second;
}

std::string_view amplitudeUnitKey(AmplitudeUnit unit)
{
    return AmplitudeUnitKeys[static_cast<std::size_t>(unit)].second;
}

std::string_view verdictKey(Verdict verdict)
{
    return VerdictKeys[static_cast<std::size_t>(verdict)].second;
}

std::optional<Detector> detectorFromKey(std::string_view key)
{
    return fromKey(DetectorKeys, key);
}

std::optional<AmplitudeUnit> amplitudeUnitFromKey(std::string_view key)
{
    return fromKey(AmplitudeUnitKeys, key);
}

std::optional<Verdict> verdictFromKey(std::string_view key)
{
    return fromKey(VerdictKeys, key);
}

std::optional<double>
convertAmplitude(double value, AmplitudeUnit from, AmplitudeUnit to, double impedanceOhms)
{
    if (from == to) {
        return value;
    }
    if (from == AmplitudeUnit::dBuV_per_m || to == AmplitudeUnit::dBuV_per_m) {
        return std::nullopt; // needs an antenna factor, see CorrectionTable
    }
    if (impedanceOhms <= 0.0) {
        return std::nullopt;
    }

    // Normalise to dBuV, then out again. dBuA = dBuV - 20*log10(Z).
    const double impedanceDb = 20.0 * std::log10(impedanceOhms);
    double dBuV = value;
    switch (from) {
        case AmplitudeUnit::dBm:
            dBuV = value + dBmToDBuVOffset(impedanceOhms);
            break;
        case AmplitudeUnit::dBuA:
            dBuV = value + impedanceDb;
            break;
        case AmplitudeUnit::dBuV:
            break;
        case AmplitudeUnit::dBuV_per_m:
            return std::nullopt;
    }

    switch (to) {
        case AmplitudeUnit::dBm:
            return dBuV - dBmToDBuVOffset(impedanceOhms);
        case AmplitudeUnit::dBuA:
            return dBuV - impedanceDb;
        case AmplitudeUnit::dBuV:
            return dBuV;
        case AmplitudeUnit::dBuV_per_m:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace peakemi
