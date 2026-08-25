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
    static const std::string identification =
        std::string{ProjectName} + ' ' + std::string{ProjectVersion} + std::string{Configuration};
    return identification;
}

} // namespace peakemi
