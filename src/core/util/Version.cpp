#include <peakemi/core/Version.h>

#include <string>

namespace peakemi {

std::string_view buildIdentification()
{
#ifdef PEAKEMI_DEBUG
    static constexpr std::string_view Configuration = " (debug)";
#else
    static constexpr std::string_view Configuration = "";
#endif
    // The full version rather than the bare one: pre-compliance data is only
    // worth as much as the ability to say which build produced it.
    static const std::string identification = std::string{ProjectName} + ' ' +
                                              std::string{ProjectVersionFull} +
                                              std::string{Configuration};
    return identification;
}

} // namespace peakemi
