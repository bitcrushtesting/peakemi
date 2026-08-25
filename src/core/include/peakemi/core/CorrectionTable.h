#pragma once

#include <peakemi/core/Trace.h>
#include <peakemi/core/Units.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace peakemi {

/// Kind of element in the measurement chain (FR-LIM-6).
///
/// The kind fixes the sign: losses and transducer factors are added to the
/// instrument reading, gains are subtracted from it.
enum class CorrectionKind : std::uint8_t
{
    AntennaFactor, ///< dB/m; turns a voltage reading into a field strength
    CableLoss,
    LisnFactor,
    Attenuator,
    AmplifierGain,
    Other
};

/// Sign applied to this kind of correction: +1 adds to the reading, -1 subtracts.
[[nodiscard]] double correctionSign(CorrectionKind kind);

[[nodiscard]] std::string_view correctionKindKey(CorrectionKind kind);
[[nodiscard]] std::optional<CorrectionKind> correctionKindFromKey(std::string_view key);

/// One frequency-dependent correction, interpolated linearly over log frequency.
struct CorrectionTable
{
    std::string name;
    CorrectionKind kind{CorrectionKind::Other};
    bool enabled{true};
    std::vector<std::pair<Hertz, double>> points; ///< (frequency, dB)

    /// Interpolated value in dB; clamped to the end points outside the range,
    /// which is the conservative convention for antenna factors.
    [[nodiscard]] double valueAt(Hertz frequency) const;

    /// Signed contribution to the corrected amplitude (gain already negated).
    [[nodiscard]] double contributionAt(Hertz frequency) const;

    void sortPoints();

    friend bool operator==(const CorrectionTable&, const CorrectionTable&) = default;
};

/// One correction as it was applied to a single measurement, for the report.
struct AppliedCorrection
{
    std::string name;
    CorrectionKind kind{CorrectionKind::Other};
    double valueDb{};        ///< table value at that frequency
    double contributionDb{}; ///< signed value actually added to the reading

    friend bool operator==(const AppliedCorrection&, const AppliedCorrection&) = default;
};

[[nodiscard]] std::vector<AppliedCorrection> correctionsAt(Hertz frequency,
                                                           std::span<const CorrectionTable> tables);

[[nodiscard]] double totalCorrectionAt(Hertz frequency, std::span<const CorrectionTable> tables);

/// Pure transformation of the measurement chain: raw trace in, corrected trace
/// out (FR-LIM-6). @p resultUnit overrides the output unit — an antenna factor
/// turns dBuV into dBuV/m, which the caller states explicitly.
[[nodiscard]] Trace applyCorrections(const Trace& trace,
                                     std::span<const CorrectionTable> tables,
                                     std::optional<AmplitudeUnit> resultUnit = std::nullopt);

/// Unit a chain of corrections produces from @p input: a table of antenna
/// factors yields a field strength, everything else keeps the input unit.
[[nodiscard]] AmplitudeUnit resultingUnit(AmplitudeUnit input,
                                          std::span<const CorrectionTable> tables);

} // namespace peakemi
