#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>

/// Strong scalar types and the enumerations of the measurement domain.
///
/// Unit confusion (Hz vs kHz, dBm vs dBuV) is the classic bug class in EMI
/// software, so frequencies and levels never travel as bare arithmetic types.
namespace peakemi {

/// Strongly typed scalar: same representation, distinct type per @p Tag.
template<class Tag, class Rep>
class Quantity
{
public:
    using Representation = Rep;

    constexpr Quantity() = default;

    constexpr explicit Quantity(Rep value) : m_value{value} {}

    [[nodiscard]] constexpr Rep value() const { return m_value; }

    friend constexpr auto operator<=>(const Quantity&, const Quantity&) = default;

    constexpr Quantity operator+(Quantity other) const { return Quantity{m_value + other.m_value}; }

    constexpr Quantity operator-(Quantity other) const { return Quantity{m_value - other.m_value}; }

    constexpr Quantity operator-() const { return Quantity{-m_value}; }

    constexpr Quantity operator*(Rep factor) const { return Quantity{m_value * factor}; }

    constexpr Quantity operator/(Rep divisor) const { return Quantity{m_value / divisor}; }

    constexpr Quantity& operator+=(Quantity other)
    {
        m_value += other.m_value;
        return *this;
    }

    constexpr Quantity& operator-=(Quantity other)
    {
        m_value -= other.m_value;
        return *this;
    }

private:
    Rep m_value{};
};

/// Frequency in whole hertz. Integral so that band edges compare exactly.
using Hertz = Quantity<struct HertzTag, std::int64_t>;

/// A level or a difference of levels in decibels.
using Decibel = Quantity<struct DecibelTag, double>;

[[nodiscard]] constexpr Hertz hertz(std::int64_t value)
{
    return Hertz{value};
}

[[nodiscard]] constexpr Hertz kilohertz(double value)
{
    return Hertz{static_cast<std::int64_t>(value * 1e3 + (value < 0.0 ? -0.5 : 0.5))};
}

[[nodiscard]] constexpr Hertz megahertz(double value)
{
    return Hertz{static_cast<std::int64_t>(value * 1e6 + (value < 0.0 ? -0.5 : 0.5))};
}

[[nodiscard]] constexpr Hertz gigahertz(double value)
{
    return Hertz{static_cast<std::int64_t>(value * 1e9 + (value < 0.0 ? -0.5 : 0.5))};
}

/// Hertz from a double that came from outside: a file, a JSON document or the
/// command line.
///
/// The scaling helpers above are for literals, where the author can see that
/// the value fits. A parsed number cannot be trusted that far, and converting
/// one that does not fit into the int64 Hertz holds is undefined behaviour --
/// which is the quiet kind of wrong, because on AArch64 it saturates to
/// INT64_MAX and that then passes every positivity and ordering check
/// downstream. A limit line asking for 1e30 Hz has to be refused at the parse,
/// not turned into a breakpoint at 9.2 EHz.
///
/// Rounds half away from zero, as the scaling helpers do.
[[nodiscard]] inline std::optional<Hertz> checkedHertz(double value)
{
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    const double rounded = value + (value < 0.0 ? -0.5 : 0.5);
    // 2^63 is exactly representable as a double, and the cast truncates towards
    // zero, so the bound is exclusive at both ends.
    constexpr double Bound = 9223372036854775808.0;
    if (rounded >= Bound || rounded <= -Bound) {
        return std::nullopt;
    }
    return Hertz{static_cast<std::int64_t>(rounded)};
}

[[nodiscard]] constexpr Decibel decibel(double value)
{
    return Decibel{value};
}

[[nodiscard]] constexpr double toMegahertz(Hertz frequency)
{
    return static_cast<double>(frequency.value()) / 1e6;
}

/// Half-open-in-spirit frequency span; both edges are inclusive in practice.
struct FrequencyRange
{
    Hertz start{};
    Hertz stop{};

    [[nodiscard]] constexpr bool contains(Hertz frequency) const
    {
        return frequency >= start && frequency <= stop;
    }

    [[nodiscard]] constexpr Hertz width() const { return stop - start; }

    [[nodiscard]] constexpr Hertz centre() const { return start + width() / 2; }

    [[nodiscard]] constexpr bool isValid() const { return start >= Hertz{0} && stop > start; }

    friend constexpr bool operator==(const FrequencyRange&, const FrequencyRange&) = default;
};

/// Detector modes defined by CISPR 16-1-1 plus the vendor-common sample detector.
enum class Detector : std::uint8_t
{
    Peak,
    QuasiPeak,
    Average,
    Rms,
    Sample
};

/// Amplitude units used across the measurement chain.
enum class AmplitudeUnit : std::uint8_t
{
    dBm,
    dBuV,
    dBuV_per_m,
    dBuA
};

/// Outcome of comparing a measurement against a limit line.
enum class Verdict : std::uint8_t
{
    Unknown, ///< No limit line covers this frequency.
    Pass,
    Marginal,
    Fail
};

/// Stable, non-translated keys. The UI maps them to translated text (FR-APP-3).
[[nodiscard]] std::string_view detectorKey(Detector detector);
[[nodiscard]] std::string_view amplitudeUnitKey(AmplitudeUnit unit);
[[nodiscard]] std::string_view verdictKey(Verdict verdict);

[[nodiscard]] std::optional<Detector> detectorFromKey(std::string_view key);
[[nodiscard]] std::optional<AmplitudeUnit> amplitudeUnitFromKey(std::string_view key);
[[nodiscard]] std::optional<Verdict> verdictFromKey(std::string_view key);

/// Convert between the logarithmic amplitude units of the measurement chain.
///
/// dBuV/m is a field strength: it relates to a voltage reading only through an
/// antenna factor, so it converts to nothing but itself and the function
/// returns std::nullopt rather than inventing a number (FR-VIS-3).
[[nodiscard]] std::optional<double>
convertAmplitude(double value, AmplitudeUnit from, AmplitudeUnit to, double impedanceOhms = 50.0);

} // namespace peakemi
