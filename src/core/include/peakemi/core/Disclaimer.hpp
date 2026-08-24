#pragma once

#include <string_view>

namespace peakemi {

/// The compliance disclaimer required by CON-1.
///
/// Every export path embeds this verbatim. It is a constant with no override,
/// no template placeholder and no configuration switch, precisely so that no UI
/// element or report template can suppress it.
inline constexpr std::string_view ComplianceDisclaimer =
    "PRE-COMPLIANCE DATA ONLY. These results are indicative engineering data produced "
    "with PeakEmi and are NOT an accredited compliance measurement. The measurement "
    "uncertainty of the test setup is not accounted for unless it was entered manually "
    "by the operator. Conformity of the equipment under test can only be established by "
    "a measurement performed by an accredited test laboratory.";

} // namespace peakemi
