#include <peakemi/drivers/InstrumentProfiles.h>

#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <array>
#include <vector>

namespace peakemi::drivers {
namespace {

/// Resolution bandwidths of the Siglent SSA3000X and SVA1000X families: the
/// 1-3-10 sequence of the base instrument, plus the three CISPR 16-1-1
/// bandwidths.
///
/// Those three -- 200 Hz, 9 kHz and 120 kHz -- and the quasi-peak detector come
/// with the vendor's EMI option. They are listed because this application
/// exists to make CISPR measurements, and a Phase 2 dwell cannot be configured
/// without them. On an instrument that lacks the option, the command is refused
/// and the run stops with the instrument's own error rather than with silently
/// wrong numbers measured at the nearest available bandwidth.
[[nodiscard]] std::vector<Hertz> siglentResolutionBandwidths()
{
    return {hertz(1),
            hertz(3),
            hertz(10),
            hertz(30),
            hertz(100),
            hertz(200),
            hertz(300),
            kilohertz(1),
            kilohertz(3),
            kilohertz(9),
            kilohertz(10),
            kilohertz(30),
            kilohertz(100),
            kilohertz(120),
            kilohertz(300),
            megahertz(1)};
}

/// The DSA800 and DSA700 series share this list; the same EMI option applies.
[[nodiscard]] std::vector<Hertz> rigolResolutionBandwidths()
{
    return {hertz(10),
            hertz(30),
            hertz(100),
            hertz(200),
            hertz(300),
            kilohertz(1),
            kilohertz(3),
            kilohertz(9),
            kilohertz(10),
            kilohertz(30),
            kilohertz(100),
            kilohertz(120),
            kilohertz(300),
            megahertz(1)};
}

[[nodiscard]] std::vector<Hertz> videoBandwidths()
{
    return {hertz(1),
            hertz(3),
            hertz(10),
            hertz(30),
            hertz(100),
            hertz(300),
            kilohertz(1),
            kilohertz(3),
            kilohertz(10),
            kilohertz(30),
            kilohertz(100),
            kilohertz(300),
            megahertz(1),
            megahertz(3)};
}

/// Siglent instruments answer the standard SCPI spelling; the differences from
/// the generic dialect are in how a trace is asked for and how it comes back.
[[nodiscard]] ScpiDialect siglentDialect()
{
    ScpiDialect dialect;
    // The SSA/SVA return the trace of a numbered trace register.
    dialect.traceQuery = ":TRACe:DATA? 1";
    dialect.traceFormat = ":FORMat:TRACe:DATA";
    dialect.traceFormatAscii = "ASCii";
    // The sweep points setting is fixed on these models; setting it is refused,
    // so the driver never sends it (see pointsAreFixed below).
    dialect.sweepPoints.clear();
    return dialect;
}

/// Rigol's DSA series wants the trace named and the format set separately, and
/// spells the quasi-peak detector without the abbreviation.
[[nodiscard]] ScpiDialect rigolDialect()
{
    ScpiDialect dialect;
    dialect.traceQuery = ":TRACe:DATA? TRACE1";
    dialect.traceFormat = ":FORMat:TRACe:DATA";
    dialect.traceFormatAscii = "ASCii";
    dialect.quasiPeakDetector = "QPEak";
    dialect.sweepPoints = ":SENSe:SWEep:POINts";
    return dialect;
}

[[nodiscard]] Capabilities siglentCapabilities(Hertz maximumFrequency, bool trackingGenerator)
{
    return Capabilities{.range = FrequencyRange{hertz(9000), maximumFrequency},
                        // The SSA3000X returns 751 points and the setting is not writable.
                        .minimumPoints = 751,
                        .maximumPoints = 751,
                        .detectors = {Detector::Peak,
                                      Detector::QuasiPeak,
                                      Detector::Average,
                                      Detector::Rms,
                                      Detector::Sample},
                        .resolutionBandwidths = siglentResolutionBandwidths(),
                        .videoBandwidths = videoBandwidths(),
                        .minimumAttenuation = decibel(0.0),
                        .maximumAttenuation = decibel(51.0),
                        .attenuationStep = decibel(1.0),
                        .minimumRefLevel = decibel(-100.0),
                        .maximumRefLevel = decibel(30.0),
                        .preamp = true,
                        .trackingGenerator = trackingGenerator,
                        .zeroSpan = true,
                        // The instrument can report in dBm or dBuV; PeakEmi asks for dBuV
                        // because that is the unit CISPR and FCC limits are written in, so
                        // corrections and limit evaluation need no conversion per trace.
                        .nativeUnit = AmplitudeUnit::dBuV};
}

[[nodiscard]] Capabilities rigolCapabilities(Hertz maximumFrequency, bool trackingGenerator)
{
    return Capabilities{.range = FrequencyRange{hertz(9000), maximumFrequency},
                        // DSA800 sweep points are settable from 101 to 3001.
                        .minimumPoints = 101,
                        .maximumPoints = 3001,
                        .detectors = {Detector::Peak,
                                      Detector::QuasiPeak,
                                      Detector::Average,
                                      Detector::Rms,
                                      Detector::Sample},
                        .resolutionBandwidths = rigolResolutionBandwidths(),
                        .videoBandwidths = videoBandwidths(),
                        .minimumAttenuation = decibel(0.0),
                        .maximumAttenuation = decibel(50.0),
                        .attenuationStep = decibel(1.0),
                        .minimumRefLevel = decibel(-100.0),
                        .maximumRefLevel = decibel(30.0),
                        .preamp = true,
                        .trackingGenerator = trackingGenerator,
                        .zeroSpan = true,
                        .nativeUnit = AmplitudeUnit::dBuV};
}

[[nodiscard]] std::vector<InstrumentProfile> makeProfiles()
{
    std::vector<InstrumentProfile> profiles;

    // --- Siglent SSA3000X ---------------------------------------------------
    profiles.push_back({.name = "Siglent SSA3021X",
                        .vendor = "Siglent",
                        .models = {"SSA3021X", "SSA3021X-TG"},
                        .capabilities = siglentCapabilities(gigahertz(2.1), false),
                        .dialect = siglentDialect()});
    profiles.push_back({.name = "Siglent SSA3032X",
                        .vendor = "Siglent",
                        .models = {"SSA3032X", "SSA3032X-TG"},
                        .capabilities = siglentCapabilities(gigahertz(3.2), false),
                        .dialect = siglentDialect()});
    profiles.push_back({.name = "Siglent SSA3075X",
                        .vendor = "Siglent",
                        .models = {"SSA3075X", "SSA3075X-TG"},
                        .capabilities = siglentCapabilities(gigahertz(7.5), false),
                        .dialect = siglentDialect()});

    // --- Siglent SVA1000X: a spectrum analyzer with a VNA in the same box ---
    profiles.push_back({.name = "Siglent SVA1015X",
                        .vendor = "Siglent",
                        .models = {"SVA1015X"},
                        .capabilities = siglentCapabilities(gigahertz(1.5), true),
                        .dialect = siglentDialect()});
    profiles.push_back({.name = "Siglent SVA1032X",
                        .vendor = "Siglent",
                        .models = {"SVA1032X"},
                        .capabilities = siglentCapabilities(gigahertz(3.2), true),
                        .dialect = siglentDialect()});
    profiles.push_back({.name = "Siglent SVA1075X",
                        .vendor = "Siglent",
                        .models = {"SVA1075X"},
                        .capabilities = siglentCapabilities(gigahertz(7.5), true),
                        .dialect = siglentDialect()});

    // --- Rigol DSA800 -------------------------------------------------------
    profiles.push_back({.name = "Rigol DSA815",
                        .vendor = "Rigol",
                        .models = {"DSA815", "DSA815-TG"},
                        .capabilities = rigolCapabilities(gigahertz(1.5), false),
                        .dialect = rigolDialect()});
    profiles.push_back({.name = "Rigol DSA832",
                        .vendor = "Rigol",
                        .models = {"DSA832", "DSA832-TG", "DSA832E"},
                        .capabilities = rigolCapabilities(gigahertz(3.2), false),
                        .dialect = rigolDialect()});
    profiles.push_back({.name = "Rigol DSA875",
                        .vendor = "Rigol",
                        .models = {"DSA875", "DSA875-TG"},
                        .capabilities = rigolCapabilities(gigahertz(7.5), false),
                        .dialect = rigolDialect()});

    // --- Rigol DSA700 -------------------------------------------------------
    profiles.push_back({.name = "Rigol DSA705",
                        .vendor = "Rigol",
                        .models = {"DSA705"},
                        .capabilities = rigolCapabilities(megahertz(500), false),
                        .dialect = rigolDialect()});
    profiles.push_back({.name = "Rigol DSA710",
                        .vendor = "Rigol",
                        .models = {"DSA710"},
                        .capabilities = rigolCapabilities(gigahertz(1.0), false),
                        .dialect = rigolDialect()});

    return profiles;
}

[[nodiscard]] bool matchesModel(std::string_view pattern, std::string_view model)
{
    const auto expression =
        QString::fromUtf8(pattern.data(), static_cast<qsizetype>(pattern.size()));
    const auto candidate = QString::fromUtf8(model.data(), static_cast<qsizetype>(model.size()));
    if (expression.endsWith(QLatin1Char('*'))) {
        return candidate.startsWith(expression.chopped(1), Qt::CaseInsensitive);
    }
    return candidate.compare(expression, Qt::CaseInsensitive) == 0;
}

} // namespace

std::span<const InstrumentProfile> instrumentProfiles()
{
    static const std::vector<InstrumentProfile> profiles = makeProfiles();
    return profiles;
}

std::optional<InstrumentProfile> profileFor(std::string_view vendor, std::string_view model)
{
    const auto reported = QString::fromUtf8(vendor.data(), static_cast<qsizetype>(vendor.size()));
    for (const auto& profile : instrumentProfiles()) {
        const auto expected = QString::fromStdString(profile.vendor);
        if (!vendor.empty() && !reported.contains(expected, Qt::CaseInsensitive)) {
            continue;
        }
        const bool claims = std::any_of(
            profile.models.begin(), profile.models.end(), [model](const std::string& pattern) {
                return matchesModel(pattern, model);
            });
        if (claims) {
            return profile;
        }
    }
    return std::nullopt;
}

InstrumentProfile familyProfile(std::string_view vendor)
{
    // Before *IDN? has been asked, assume the widest member of the family: a
    // narrower profile takes over as soon as the instrument names itself, and
    // assuming too little here would reject spans the instrument supports.
    const bool rigol = QString::fromUtf8(vendor.data(), static_cast<qsizetype>(vendor.size()))
                           .contains(QStringLiteral("Rigol"), Qt::CaseInsensitive);
    if (rigol) {
        return {.name = "Rigol DSA700/DSA800",
                .vendor = "Rigol",
                .models = {"DSA7*", "DSA8*"},
                .capabilities = rigolCapabilities(gigahertz(7.5), false),
                .dialect = rigolDialect()};
    }
    return {.name = "Siglent SSA3000X/SVA1000X",
            .vendor = "Siglent",
            .models = {"SSA3*", "SVA1*"},
            .capabilities = siglentCapabilities(gigahertz(7.5), true),
            .dialect = siglentDialect()};
}

} // namespace peakemi::drivers
