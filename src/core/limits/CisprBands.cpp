#include <peakemi/core/CisprBands.hpp>

#include <array>

namespace peakemi {
namespace {

constexpr std::array<CisprBandInfo, 5> Bands{{
    {CisprBand::A, {kilohertz(9), kilohertz(150)}, hertz(200)},
    {CisprBand::B, {kilohertz(150), megahertz(30)}, kilohertz(9)},
    {CisprBand::C, {megahertz(30), megahertz(300)}, kilohertz(120)},
    {CisprBand::D, {megahertz(300), gigahertz(1.0)}, kilohertz(120)},
    {CisprBand::E, {gigahertz(1.0), gigahertz(18.0)}, megahertz(1)},
}};

constexpr std::array<std::string_view, 5> BandKeys{"A", "B", "C", "D", "E"};

} // namespace

std::span<const CisprBandInfo> cisprBands()
{
    return Bands;
}

CisprBandInfo cisprBandFor(Hertz frequency)
{
    for (const auto& band : Bands) {
        if (frequency < band.range.stop) {
            return band;
        }
    }
    return Bands.back();
}

Hertz mandatedResolutionBandwidth(Hertz frequency)
{
    return cisprBandFor(frequency).resolutionBandwidth;
}

std::string_view cisprBandKey(CisprBand band)
{
    return BandKeys[static_cast<std::size_t>(band)];
}

} // namespace peakemi
