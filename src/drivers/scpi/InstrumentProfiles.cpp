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

/// Resolution and video bandwidths of the UNI-T UTS3000T series: a plain
/// 1-3-10 sequence from 1 Hz to 1 MHz, verified against a UTS3032T+ running
/// firmware V1.04 by setting each value and reading it back.
///
/// The CISPR 16-1-1 bandwidths are absent from this list because they are absent
/// from this mode: asked for 9 kHz it tunes 10 kHz, asked for 120 kHz it tunes
/// 100 kHz, and it reports no error either time. Listing only what it really has
/// is what turns that silent substitution into a refused run with a reason,
/// which is the whole point of declaring bandwidths here.
///
/// The instrument does have the CISPR bandwidths -- exactly 200 Hz, 9 kHz and
/// 120 kHz -- but only in its separate EMI mode (":INSTrument:SELect EMI"),
/// whose settings live under a different command tree and are not reachable
/// from the spectrum-analyzer mode this driver drives. See unitrendCapabilities.
[[nodiscard]] std::vector<Hertz> unitrendBandwidths()
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

/// The UTS3000T answers the standard SCPI spelling for every setting PeakEmi
/// uses, so this is the generic dialect unchanged. Each command in ScpiDialect
/// was sent to a UTS3032T+ on firmware V1.04 and read back, including the ones
/// the other two families had to override: sweep points are writable here, and
/// the trace comes from ":TRACe:DATA? TRACE1" in ASCII.
[[nodiscard]] ScpiDialect unitrendDialect()
{
    // The quasi-peak and RMS keywords inherited from the defaults are never
    // sent: the capabilities below do not offer those detectors, so a sweep
    // asking for one is refused before a command is built.
    return ScpiDialect{};
}

/// What a UTS3000T can do, measured rather than read off a datasheet.
///
/// These describe the instrument's spectrum-analyzer mode, which is the mode this
/// driver drives. Two gaps matter for pre-compliance work and are the reason this
/// family gets its own capabilities rather than borrowing another's: there is no
/// quasi-peak detector -- ":SENSe:DETector:FUNCtion QPEak" is answered with -224,
/// "Illegal parameter value" -- and none of the CISPR bandwidths (see
/// unitrendBandwidths). Declaring the gaps is what gets a run refused with an
/// actionable message instead of quietly measured with the wrong detector at the
/// wrong bandwidth.
///
/// The gaps belong to the mode, not to the instrument. Switched to EMI mode with
/// ":INSTrument:SELect EMI", a UTS3032T+ carrying the EMI option offers the CISPR
/// bandwidths and, through "[:SENSe]:FSCan:FINal:DETector<n>", the quasi-peak and
/// CISPR-average detectors with a per-detector dwell of 1 ms to 60 s. That mode is
/// a different command tree -- its own scan table, peak search, final measurement
/// and result fetch -- so driving it means a driver of its own rather than another
/// dialect here. Until that exists, PeakEmi cannot make a CISPR-conformant Phase 2
/// dwell on this instrument, and the capabilities below say so honestly.
[[nodiscard]] Capabilities unitrendCapabilities(Hertz maximumFrequency, bool trackingGenerator)
{
    return Capabilities{.range = FrequencyRange{hertz(9000), maximumFrequency},
                        // Sweep points are writable and the trace honours them;
                        // outside 11 to 10001 the instrument clamps silently.
                        .minimumPoints = 11,
                        .maximumPoints = 10001,
                        // No quasi-peak, and no RMS: both are refused with -224.
                        .detectors = {Detector::Peak, Detector::Average, Detector::Sample},
                        .resolutionBandwidths = unitrendBandwidths(),
                        .videoBandwidths = unitrendBandwidths(),
                        .minimumAttenuation = decibel(0.0),
                        .maximumAttenuation = decibel(51.0),
                        .attenuationStep = decibel(1.0),
                        // The reference level is held in dBm and converted, so in
                        // the dBuV PeakEmi asks for, its -100 to +30 dBm span
                        // lands on 6.99 to 136.99. Rounded inwards, because a
                        // value this list accepts must be one the instrument
                        // takes without clamping it.
                        .minimumRefLevel = decibel(7.0),
                        .maximumRefLevel = decibel(136.9),
                        .preamp = true,
                        .trackingGenerator = trackingGenerator,
                        .zeroSpan = true,
                        .nativeUnit = AmplitudeUnit::dBuV};
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

    // --- UNI-T UTS3000T -----------------------------------------------------
    // Only the model on the bench is listed. The rest of the series differs in
    // its upper frequency, which is the one number that must not be guessed:
    // too high and a sweep is sent that the instrument silently clamps, too low
    // and one it could have made is refused. An unlisted UTS3* keeps the family
    // defaults until someone verifies it against hardware.
    profiles.push_back({.name = "UNI-T UTS3032T+",
                        .vendor = "UNI-T",
                        .models = {"UTS3032T+", "UTS3032T"},
                        .capabilities = unitrendCapabilities(gigahertz(3.2), true),
                        .dialect = unitrendDialect()});

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

InstrumentProfile unitrendEmiProfile()
{
    return {
        .name = "UNI-T UTS3032T+ (EMI)",
        .vendor = "UNI-T",
        .models = {"UTS3032T+", "UTS3032T"},
        .capabilities =
            Capabilities{
                .range = FrequencyRange{hertz(9000), gigahertz(3.2)},
                .minimumPoints = 11,
                .maximumPoints = 10001,
                // Measured on the instrument: POSitive, QPEak and CAVerage
                // are accepted on both the scan traces and the meters.
                // There is no RMS and no sample detector in this mode.
                .detectors = {Detector::Peak, Detector::QuasiPeak, Detector::Average},
                // Exactly the CISPR 16-1-1 set and nothing else -- the EMI
                // personality replaces the SA mode's 1-3-10 ladder with the
                // four bandwidths the standard mandates, one per band:
                // 200 Hz for band A, 9 kHz for B, 120 kHz for C and D,
                // 1 MHz for E. Anything else is snapped to one of these.
                .resolutionBandwidths = {hertz(200), kilohertz(9), kilohertz(120), megahertz(1)},
                // The EMI personality has no video-bandwidth setting at
                // all. An empty list means the engine never validates one,
                // which is right: there is nothing here to get wrong.
                .videoBandwidths = {},
                // The scan list is the binding constraint: it documents an
                // even value from 0 to 50 dB, where the meter takes any
                // integer to 51. The narrower of the two is declared so a
                // configuration is not accepted here and then quietly
                // rounded by the instrument.
                .minimumAttenuation = decibel(0.0),
                .maximumAttenuation = decibel(50.0),
                .attenuationStep = decibel(2.0),
                // Nor is there a reference level: the scan list has no
                // ":RLEVel" and the query is unanswered. The bounds are
                // opened wide rather than left at the defaults so that a
                // configuration carrying a reference level for some other
                // instrument is not rejected over a setting this one does
                // not have and this driver never sends.
                .minimumRefLevel = decibel(-200.0),
                .maximumRefLevel = decibel(200.0),
                .preamp = true,
                // The tracking generator belongs to the SA personality.
                .trackingGenerator = false,
                // Zero span is the meter, which is a better dwell than a
                // zero-span sweep would have been.
                .zeroSpan = true,
                .nativeUnit = AmplitudeUnit::dBuV},
        .dialect = {}};
}

InstrumentProfile familyProfile(std::string_view vendor)
{
    // Before *IDN? has been asked, assume the widest member of the family: a
    // narrower profile takes over as soon as the instrument names itself, and
    // assuming too little here would reject spans the instrument supports.
    const auto reported = QString::fromUtf8(vendor.data(), static_cast<qsizetype>(vendor.size()));
    // "UNI-TREND" is what the instrument puts in *IDN?; "UNI-T" is what the
    // front panel and the manuals say, so both have to lead here.
    if (reported.contains(QStringLiteral("UNI-T"), Qt::CaseInsensitive)) {
        return {.name = "UNI-T UTS3000T",
                .vendor = "UNI-T",
                .models = {"UTS3*"},
                .capabilities = unitrendCapabilities(gigahertz(3.2), true),
                .dialect = unitrendDialect()};
    }
    const bool rigol = reported.contains(QStringLiteral("Rigol"), Qt::CaseInsensitive);
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
