#pragma once

#include <peakemi/core/Capabilities.h>
#include <peakemi/drivers/ScpiAnalyzerDriver.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace peakemi::drivers {

/// What one instrument model can do, and how it says it.
///
/// The engine validates every sweep against `capabilities` before a command is
/// sent (FR-HAL-3), so these numbers decide whether a run is refused with an
/// actionable message or attempted and rejected by the instrument. They come
/// from the programming guides of the models named in `models`.
struct InstrumentProfile
{
    /// Human readable, e.g. "Siglent SSA3032X".
    std::string name;
    /// Manufacturer as it appears in *IDN?, matched case-insensitively.
    std::string vendor;
    /// Model names this profile covers, matched case-insensitively. A trailing
    /// '*' matches any suffix.
    std::vector<std::string> models;
    Capabilities capabilities;
    ScpiDialect dialect;
};

/// Every profile this build knows, most specific first.
[[nodiscard]] std::span<const InstrumentProfile> instrumentProfiles();

/// The profile for an identified instrument, or nothing when no profile claims
/// it. An exact model name beats a family pattern.
[[nodiscard]] std::optional<InstrumentProfile> profileFor(std::string_view vendor,
                                                          std::string_view model);

/// The profile a driver falls back to before it has identified the instrument:
/// the widest capabilities of the family, so nothing is rejected that a later,
/// narrower profile would have allowed.
[[nodiscard]] InstrumentProfile familyProfile(std::string_view vendor);

} // namespace peakemi::drivers
