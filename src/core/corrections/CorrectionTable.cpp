#include <peakemi/core/CorrectionTable.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace peakemi {
namespace {

constexpr std::array<std::pair<CorrectionKind, std::string_view>, 6> CorrectionKindKeys{{
    {CorrectionKind::AntennaFactor, "antenna-factor"},
    {CorrectionKind::CableLoss, "cable-loss"},
    {CorrectionKind::LisnFactor, "lisn-factor"},
    {CorrectionKind::Attenuator, "attenuator"},
    {CorrectionKind::AmplifierGain, "amplifier-gain"},
    {CorrectionKind::Other, "other"},
}};

} // namespace

double correctionSign(CorrectionKind kind)
{
    return kind == CorrectionKind::AmplifierGain ? -1.0 : 1.0;
}

std::string_view correctionKindKey(CorrectionKind kind)
{
    return CorrectionKindKeys[static_cast<std::size_t>(kind)].second;
}

std::optional<CorrectionKind> correctionKindFromKey(std::string_view key)
{
    for (const auto& [value, name] : CorrectionKindKeys) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

double CorrectionTable::valueAt(Hertz frequency) const
{
    if (points.empty()) {
        return 0.0;
    }
    if (points.size() == 1 || frequency <= points.front().first) {
        return points.front().second;
    }
    if (frequency >= points.back().first) {
        return points.back().second;
    }

    const auto upper = std::lower_bound(
        points.begin(),
        points.end(),
        frequency,
        [](const std::pair<Hertz, double>& point, Hertz value) { return point.first < value; });
    if (upper == points.begin()) {
        return upper->second;
    }
    const auto lower = std::prev(upper);

    const double lowerFrequency = static_cast<double>(lower->first.value());
    const double upperFrequency = static_cast<double>(upper->first.value());
    if (lowerFrequency <= 0.0 || upperFrequency <= 0.0) {
        return lower->second;
    }
    const double span = std::log10(upperFrequency) - std::log10(lowerFrequency);
    if (span <= 0.0) {
        return lower->second;
    }
    const double fraction =
        (std::log10(static_cast<double>(frequency.value())) - std::log10(lowerFrequency)) / span;
    return lower->second + fraction * (upper->second - lower->second);
}

double CorrectionTable::contributionAt(Hertz frequency) const
{
    return enabled ? correctionSign(kind) * valueAt(frequency) : 0.0;
}

void CorrectionTable::sortPoints()
{
    std::stable_sort(points.begin(),
                     points.end(),
                     [](const std::pair<Hertz, double>& a, const std::pair<Hertz, double>& b) {
                         return a.first < b.first;
                     });
}

std::vector<AppliedCorrection> correctionsAt(Hertz frequency,
                                             std::span<const CorrectionTable> tables)
{
    std::vector<AppliedCorrection> applied;
    applied.reserve(tables.size());
    for (const auto& table : tables) {
        if (!table.enabled || table.points.empty()) {
            continue;
        }
        applied.push_back(AppliedCorrection{.name = table.name,
                                            .kind = table.kind,
                                            .valueDb = table.valueAt(frequency),
                                            .contributionDb = table.contributionAt(frequency)});
    }
    return applied;
}

double totalCorrectionAt(Hertz frequency, std::span<const CorrectionTable> tables)
{
    double total = 0.0;
    for (const auto& table : tables) {
        total += table.contributionAt(frequency);
    }
    return total;
}

AmplitudeUnit resultingUnit(AmplitudeUnit input, std::span<const CorrectionTable> tables)
{
    const bool hasAntennaFactor =
        std::any_of(tables.begin(), tables.end(), [](const CorrectionTable& table) {
            return table.enabled && table.kind == CorrectionKind::AntennaFactor &&
                   !table.points.empty();
        });
    return hasAntennaFactor ? AmplitudeUnit::dBuV_per_m : input;
}

Trace applyCorrections(const Trace& trace,
                       std::span<const CorrectionTable> tables,
                       std::optional<AmplitudeUnit> resultUnit)
{
    Trace corrected = trace;
    corrected.unit = resultUnit.value_or(resultingUnit(trace.unit, tables));
    corrected.corrected = true;
    if (tables.empty()) {
        return corrected;
    }
    for (int i = 0; i < corrected.size(); ++i) {
        const auto index = static_cast<std::size_t>(i);
        corrected.amplitudes[index] += totalCorrectionAt(corrected.axis.frequencyAt(i), tables);
    }
    return corrected;
}

} // namespace peakemi
