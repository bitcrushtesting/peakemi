#include <peakemi/drivers/ScpiAnalyzerDriver.hpp>
#include <peakemi/drivers/SimulatedDriver.hpp>
#include <peakemi/hal/DriverRegistry.hpp>

namespace peakemi::drivers {

void registerBuiltInDrivers()
{
    auto& registry = hal::DriverRegistry::instance();

    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = SimulatedDriver::staticInfo(),
        .matcher = hal::makeMatcher("PeakEmi", {"Simulated Analyzer"}),
        .factory = [] { return std::make_shared<SimulatedDriver>(); }});

    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = makeSiglentSsaDriver()->info(),
        .matcher = hal::makeMatcher("Siglent", {"SSA3*", "SVA1*"}),
        .factory = &makeSiglentSsaDriver});

    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = makeRigolDsaDriver()->info(),
        .matcher = hal::makeMatcher("Rigol", {"DSA7*", "DSA8*"}),
        .factory = &makeRigolDsaDriver});
}

} // namespace peakemi::drivers
