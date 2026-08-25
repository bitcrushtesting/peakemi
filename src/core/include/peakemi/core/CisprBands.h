#pragma once

#include <peakemi/core/Units.h>

#include <cstdint>
#include <span>

namespace peakemi {

/// Measurement bands of CISPR 16-1-1, which fix the resolution bandwidth the
/// Phase 2 dwell has to use (FR-RUN-3).
enum class CisprBand : std::uint8_t
{
    A, ///<   9 kHz -  150 kHz, 200 Hz RBW
    B, ///< 150 kHz -   30 MHz,   9 kHz RBW
    C, ///<  30 MHz -  300 MHz, 120 kHz RBW
    D, ///< 300 MHz -    1 GHz, 120 kHz RBW
    E  ///<   1 GHz -   18 GHz,   1 MHz RBW
};

struct CisprBandInfo
{
    CisprBand band{CisprBand::B};
    FrequencyRange range{};
    Hertz resolutionBandwidth{};
};

[[nodiscard]] std::span<const CisprBandInfo> cisprBands();

/// Band containing @p frequency. Frequencies below 9 kHz clamp to band A and
/// frequencies above 18 GHz to band E, so the caller always gets a usable RBW.
[[nodiscard]] CisprBandInfo cisprBandFor(Hertz frequency);

/// Resolution bandwidth the standard mandates at @p frequency.
[[nodiscard]] Hertz mandatedResolutionBandwidth(Hertz frequency);

[[nodiscard]] std::string_view cisprBandKey(CisprBand band);

} // namespace peakemi
