#include <peakemi/core/LimitCatalogue.hpp>

#include <algorithm>

namespace peakemi {
namespace {

[[nodiscard]] LimitPoint breakpoint(Hertz frequency,
                                    double amplitude,
                                    Interpolation interpolation = Interpolation::LogFrequency)
{
    return LimitPoint{.frequency = frequency,
                      .amplitude = amplitude,
                      .interpolationToNext = interpolation};
}

[[nodiscard]] std::vector<LimitLine> makeCatalogue()
{
    std::vector<LimitLine> catalogue;

    // --- CISPR 32 / EN 55032 conducted, mains port, 150 kHz - 30 MHz ---------
    catalogue.push_back(LimitLine{
        .name = "CISPR 32 Class B conducted (QP)",
        .standard = "CISPR 32:2015 / EN 55032:2015, Table A.3",
        .note = "Mains port, quasi-peak. 66 dBuV falling to 56 dBuV over 150-500 kHz.",
        .kind = EmissionKind::Conducted,
        .equipmentClass = EquipmentClass::ClassB,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV,
        .measurementDistanceMetres = 0.0,
        .builtIn = true,
        .points = {breakpoint(kilohertz(150), 66.0),
                   breakpoint(kilohertz(500), 56.0, Interpolation::Step),
                   breakpoint(megahertz(5), 56.0, Interpolation::Step),
                   breakpoint(megahertz(30), 60.0)}});

    catalogue.push_back(LimitLine{
        .name = "CISPR 32 Class B conducted (AV)",
        .standard = "CISPR 32:2015 / EN 55032:2015, Table A.3",
        .note = "Mains port, average detector.",
        .kind = EmissionKind::Conducted,
        .equipmentClass = EquipmentClass::ClassB,
        .detector = Detector::Average,
        .unit = AmplitudeUnit::dBuV,
        .measurementDistanceMetres = 0.0,
        .builtIn = true,
        .points = {breakpoint(kilohertz(150), 56.0),
                   breakpoint(kilohertz(500), 46.0, Interpolation::Step),
                   breakpoint(megahertz(5), 46.0, Interpolation::Step),
                   breakpoint(megahertz(30), 50.0)}});

    catalogue.push_back(LimitLine{
        .name = "CISPR 32 Class A conducted (QP)",
        .standard = "CISPR 32:2015 / EN 55032:2015, Table A.6",
        .note = "Mains port, quasi-peak.",
        .kind = EmissionKind::Conducted,
        .equipmentClass = EquipmentClass::ClassA,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV,
        .measurementDistanceMetres = 0.0,
        .builtIn = true,
        .points = {breakpoint(kilohertz(150), 79.0, Interpolation::Step),
                   breakpoint(kilohertz(500), 73.0, Interpolation::Step),
                   breakpoint(megahertz(30), 73.0)}});

    catalogue.push_back(LimitLine{
        .name = "CISPR 32 Class A conducted (AV)",
        .standard = "CISPR 32:2015 / EN 55032:2015, Table A.6",
        .note = "Mains port, average detector.",
        .kind = EmissionKind::Conducted,
        .equipmentClass = EquipmentClass::ClassA,
        .detector = Detector::Average,
        .unit = AmplitudeUnit::dBuV,
        .measurementDistanceMetres = 0.0,
        .builtIn = true,
        .points = {breakpoint(kilohertz(150), 66.0, Interpolation::Step),
                   breakpoint(kilohertz(500), 60.0, Interpolation::Step),
                   breakpoint(megahertz(30), 60.0)}});

    // --- CISPR 32 / EN 55032 radiated, 30 MHz - 1 GHz ------------------------
    catalogue.push_back(LimitLine{
        .name = "CISPR 32 Class B radiated 10 m (QP)",
        .standard = "CISPR 32:2015 / EN 55032:2015, Table A.4",
        .note = "Measurement distance 10 m, quasi-peak.",
        .kind = EmissionKind::Radiated,
        .equipmentClass = EquipmentClass::ClassB,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV_per_m,
        .measurementDistanceMetres = 10.0,
        .builtIn = true,
        .points = {breakpoint(megahertz(30), 30.0, Interpolation::Step),
                   breakpoint(megahertz(230), 37.0, Interpolation::Step),
                   breakpoint(gigahertz(1.0), 37.0)}});

    catalogue.push_back(LimitLine{
        .name = "CISPR 32 Class A radiated 10 m (QP)",
        .standard = "CISPR 32:2015 / EN 55032:2015, Table A.7",
        .note = "Measurement distance 10 m, quasi-peak.",
        .kind = EmissionKind::Radiated,
        .equipmentClass = EquipmentClass::ClassA,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV_per_m,
        .measurementDistanceMetres = 10.0,
        .builtIn = true,
        .points = {breakpoint(megahertz(30), 40.0, Interpolation::Step),
                   breakpoint(megahertz(230), 47.0, Interpolation::Step),
                   breakpoint(gigahertz(1.0), 47.0)}});

    // --- FCC 47 CFR Part 15 subpart B ---------------------------------------
    catalogue.push_back(LimitLine{
        .name = "FCC Part 15B Class B conducted (QP)",
        .standard = "47 CFR 15.107(a)",
        .note = "AC power line conducted, quasi-peak.",
        .kind = EmissionKind::Conducted,
        .equipmentClass = EquipmentClass::ClassB,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV,
        .measurementDistanceMetres = 0.0,
        .builtIn = true,
        .points = {breakpoint(kilohertz(150), 66.0),
                   breakpoint(kilohertz(500), 56.0, Interpolation::Step),
                   breakpoint(megahertz(5), 56.0, Interpolation::Step),
                   breakpoint(megahertz(30), 60.0)}});

    catalogue.push_back(LimitLine{
        .name = "FCC Part 15B Class A conducted (QP)",
        .standard = "47 CFR 15.107(b)",
        .note = "AC power line conducted, quasi-peak.",
        .kind = EmissionKind::Conducted,
        .equipmentClass = EquipmentClass::ClassA,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV,
        .measurementDistanceMetres = 0.0,
        .builtIn = true,
        .points = {breakpoint(kilohertz(150), 79.0, Interpolation::Step),
                   breakpoint(kilohertz(500), 73.0, Interpolation::Step),
                   breakpoint(megahertz(30), 73.0)}});

    catalogue.push_back(LimitLine{
        .name = "FCC Part 15B Class B radiated 3 m (QP)",
        .standard = "47 CFR 15.109(a)",
        .note = "Measurement distance 3 m; field strength limits converted to dBuV/m.",
        .kind = EmissionKind::Radiated,
        .equipmentClass = EquipmentClass::ClassB,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV_per_m,
        .measurementDistanceMetres = 3.0,
        .builtIn = true,
        .points = {breakpoint(megahertz(30), 40.0, Interpolation::Step),
                   breakpoint(megahertz(88), 43.5, Interpolation::Step),
                   breakpoint(megahertz(216), 46.0, Interpolation::Step),
                   breakpoint(megahertz(960), 54.0, Interpolation::Step),
                   breakpoint(gigahertz(1.0), 54.0)}});

    catalogue.push_back(LimitLine{
        .name = "FCC Part 15B Class A radiated 10 m (QP)",
        .standard = "47 CFR 15.109(b)",
        .note = "Measurement distance 10 m; field strength limits converted to dBuV/m.",
        .kind = EmissionKind::Radiated,
        .equipmentClass = EquipmentClass::ClassA,
        .detector = Detector::QuasiPeak,
        .unit = AmplitudeUnit::dBuV_per_m,
        .measurementDistanceMetres = 10.0,
        .builtIn = true,
        .points = {breakpoint(megahertz(30), 39.0, Interpolation::Step),
                   breakpoint(megahertz(88), 43.5, Interpolation::Step),
                   breakpoint(megahertz(216), 46.4, Interpolation::Step),
                   breakpoint(megahertz(960), 49.5, Interpolation::Step),
                   breakpoint(gigahertz(1.0), 49.5)}});

    return catalogue;
}

} // namespace

std::span<const LimitLine> builtInLimitLines()
{
    static const std::vector<LimitLine> catalogue = makeCatalogue();
    return catalogue;
}

std::optional<LimitLine> builtInLimitLine(std::string_view name)
{
    const auto catalogue = builtInLimitLines();
    const auto found = std::find_if(catalogue.begin(), catalogue.end(), [name](const LimitLine& line) {
        return line.name == name;
    });
    if (found == catalogue.end()) {
        return std::nullopt;
    }
    return *found;
}

std::vector<std::string> builtInLimitLineNames()
{
    std::vector<std::string> names;
    for (const auto& line : builtInLimitLines()) {
        names.push_back(line.name);
    }
    return names;
}

} // namespace peakemi
