#pragma once

#include <string>

namespace peakemi {

/// Parsed *IDN? response. `raw` is kept verbatim for the transcript and for
/// driver matchers that need the unsplit string.
struct InstrumentId
{
    std::string manufacturer;
    std::string model;
    std::string serial;
    std::string firmware;
    std::string raw;

    [[nodiscard]] bool isEmpty() const { return manufacturer.empty() && model.empty(); }

    /// "Siglent SSA3032X (SSA3XABC1234)" — used in window titles and reports.
    [[nodiscard]] std::string displayName() const;

    friend bool operator==(const InstrumentId&, const InstrumentId&) = default;
};

} // namespace peakemi
