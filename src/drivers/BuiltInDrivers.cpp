#include <peakemi/drivers/ScpiAnalyzerDriver.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/drivers/UnitrendFscanDriver.h>
#include <peakemi/hal/DriverRegistry.h>

namespace peakemi::drivers {

void registerBuiltInDrivers()
{
    auto& registry = hal::DriverRegistry::instance();

    registry.registerDriver(
        hal::DriverRegistry::Entry{.info = SimulatedDriver::staticInfo(),
                                   .matcher = hal::makeMatcher("PeakEmi", {"Simulated Analyzer"}),
                                   .factory = [] { return std::make_shared<SimulatedDriver>(); }});

    registry.registerDriver(
        hal::DriverRegistry::Entry{.info = makeSiglentSsaDriver()->info(),
                                   .matcher = hal::makeMatcher("Siglent", {"SSA3*", "SVA1*"}),
                                   .factory = &makeSiglentSsaDriver});

    registry.registerDriver(
        hal::DriverRegistry::Entry{.info = makeRigolDsaDriver()->info(),
                                   .matcher = hal::makeMatcher("Rigol", {"DSA7*", "DSA8*"}),
                                   .factory = &makeRigolDsaDriver});

    // Both UNI-T drivers claim the same box, because it really is two
    // instruments behind one *IDN?. The EMI one names the model exactly and so
    // scores higher, which makes it the automatic choice -- it is the mode with
    // the quasi-peak detector, and this application exists to make CISPR
    // measurements. The spectrum-analyzer mode stays reachable by asking for
    // "unitrend.uts3000t" by name, and remains the automatic choice for a
    // UTS3* this build has not verified, whose EMI option may not be fitted.
    registry.registerDriver(
        hal::DriverRegistry::Entry{.info = makeUnitrendFscanDriver()->info(),
                                   .matcher = hal::makeMatcher("UNI-T", {"UTS3032T+", "UTS3032T"}),
                                   .factory = &makeUnitrendFscanDriver});

    registry.registerDriver(
        hal::DriverRegistry::Entry{.info = makeUnitrendUtsDriver()->info(),
                                   .matcher = hal::makeMatcher("UNI-T", {"UTS3*"}),
                                   .factory = &makeUnitrendUtsDriver});
}

} // namespace peakemi::drivers
