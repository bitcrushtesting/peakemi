#include <peakemi/core/InstrumentId.hpp>

namespace peakemi {

std::string InstrumentId::displayName() const
{
    if (isEmpty()) {
        return raw;
    }
    std::string name = manufacturer;
    if (!model.empty()) {
        if (!name.empty()) {
            name += ' ';
        }
        name += model;
    }
    if (!serial.empty()) {
        name += " (" + serial + ')';
    }
    return name;
}

} // namespace peakemi
