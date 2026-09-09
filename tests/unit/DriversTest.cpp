#include "ScriptedTransport.h"
#include "TestSupport.h"

#include <peakemi/core/CisprBands.h>
#include <peakemi/drivers/InstrumentProfiles.h>
#include <peakemi/drivers/ScpiAnalyzerDriver.h>
#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/drivers/UnitrendFscanDriver.h>

#include <QTest>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace peakemi;

class DriversTest : public QObject
{
    Q_OBJECT

private slots:
    void simulatedDriverIsDeterministic();
    void simulatedDriverReportsCapabilities();
    void simulatedDriverRejectsUnsupportedSweeps();
    void simulatedDriverNeedsArmingFirst();
    void simulatedDriverHonoursCancellation();
    void simulatedDriverShowsEmittersAboveTheNoiseFloor();
    void simulatedDetectorsReadBelowPeak();
    void scpiDriverSendsTheExpectedCommands();
    void scpiDriverParsesTheTrace();
    void scpiDriverReportsTransportFailures();
    void scpiDriverDrainsTheErrorQueue();
    void profilesCoverTheSupportedModels();
    void profilesAreSelectedByModel();
    void profilesFallBackToTheFamily();
    void driverNarrowsItsCapabilitiesAfterIdentifying();
    void siglentDoesNotSendAFixedPointCount();
    void unitrendProfileMatchesTheInstrumentOnTheBench();
    void unitrendRefusesWhatItCannotMeasure();
    void unitrendSendsTheUnitBeforeTheReferenceLevel();
    void unitrendDriverCompletesASweep();
    void fscanProfileOffersWhatTheEmiOptionAdds();
    void fscanDriverEntersEmiModeAndRefusesIfItCannot();
    void fscanDriverConfiguresOneScanRange();
    void fscanDriverAlwaysSetsTheAttenuation();
    void fscanDriverDwellsWithTheMeterOnZeroSpan();
    void fscanDriverWaitsOutAMeterThatHasNotSettled();
    void fscanDriverRefusesAMeterThatReportedNothing();
};

void DriversTest::simulatedDriverIsDeterministic()
{
    drivers::SimulatedDriver first;
    drivers::SimulatedDriver second;
    QVERIFY(first.open(nullptr).has_value());
    QVERIFY(second.open(nullptr).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    params.points = 1001;
    params.rbw = kilohertz(120);

    const CancelToken cancel;
    QVERIFY(first.configureSweep(params).has_value());
    QVERIFY(first.armAndTrigger(cancel).has_value());
    const auto a = first.fetchTrace(cancel);

    QVERIFY(second.configureSweep(params).has_value());
    QVERIFY(second.armAndTrigger(cancel).has_value());
    const auto b = second.fetchTrace(cancel);

    QVERIFY(a.has_value());
    QVERIFY(b.has_value());
    QCOMPARE(a->amplitudes, b->amplitudes);
    QCOMPARE(a->size(), 1001);
    QCOMPARE(a->unit, AmplitudeUnit::dBuV);
}

void DriversTest::simulatedDriverReportsCapabilities()
{
    const drivers::SimulatedDriver driver;
    const auto capabilities = driver.capabilities();
    QVERIFY(capabilities.supports(Detector::QuasiPeak));
    QVERIFY(capabilities.preamp);
    QVERIFY(capabilities.maximumPoints >= 40001); // FR-VIS-1 needs 40k points
    QVERIFY(capabilities.range.contains(kilohertz(150)));
    QCOMPARE(driver.info().id, std::string{"peakemi.simulated"});
}

void DriversTest::simulatedDriverRejectsUnsupportedSweeps()
{
    drivers::SimulatedDriver driver;
    QVERIFY(driver.open(nullptr).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), gigahertz(10.0)};
    params.points = 1001;
    const auto status = driver.configureSweep(params);
    QVERIFY(!status.has_value());
    QCOMPARE(status.error().code, ErrorCode::UnsupportedSetting);

    // The rejection is reported through the error queue as well.
    QCOMPARE(driver.lastErrors().size(), 1U);
    QVERIFY(driver.lastErrors().empty()); // draining clears it
}

void DriversTest::simulatedDriverNeedsArmingFirst()
{
    drivers::SimulatedDriver driver;
    const CancelToken cancel;
    QCOMPARE(driver.identify().error().code, ErrorCode::NotConnected);

    QVERIFY(driver.open(nullptr).has_value());
    QVERIFY(driver.identify().has_value());
    QCOMPARE(driver.fetchTrace(cancel).error().code, ErrorCode::ProtocolViolation);
}

void DriversTest::simulatedDriverHonoursCancellation()
{
    drivers::SimulatedDriver driver;
    QVERIFY(driver.open(nullptr).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(130)};
    params.points = 1001;
    QVERIFY(driver.configureSweep(params).has_value());

    CancelToken cancel;
    cancel.cancel();
    QCOMPARE(driver.armAndTrigger(cancel).error().code, ErrorCode::Cancelled);
}

void DriversTest::simulatedDriverShowsEmittersAboveTheNoiseFloor()
{
    drivers::SimulatedDriver driver;
    QVERIFY(driver.open(nullptr).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(47), megahertz(49)};
    params.points = 1001;
    params.rbw = kilohertz(120);

    const CancelToken cancel;
    QVERIFY(driver.configureSweep(params).has_value());
    QVERIFY(driver.armAndTrigger(cancel).has_value());
    const auto trace = driver.fetchTrace(cancel);
    QVERIFY(trace.has_value());

    // The 48 MHz emitter of the demo bench sits at 44 dBuV over an 18 dB floor.
    const int peak = trace->maximumIndex();
    QVERIFY(std::abs(toMegahertz(trace->axis.frequencyAt(peak)) - 48.0) < 0.05);
    QVERIFY(trace->maximumAmplitude() > 40.0);
}

void DriversTest::simulatedDetectorsReadBelowPeak()
{
    drivers::SimulatedDriver driver;
    QVERIFY(driver.open(nullptr).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(47), megahertz(49)};
    params.points = 401;
    const CancelToken cancel;

    const auto measure = [&](Detector detector) {
        params.detector = detector;
        [[maybe_unused]] const auto configured = driver.configureSweep(params);
        [[maybe_unused]] const auto armed = driver.armAndTrigger(cancel);
        return driver.fetchTrace(cancel)->maximumAmplitude();
    };

    const double peak = measure(Detector::Peak);
    const double quasiPeak = measure(Detector::QuasiPeak);
    const double average = measure(Detector::Average);
    QVERIFY(quasiPeak < peak);
    QVERIFY(average < quasiPeak);
}

void DriversTest::scpiDriverSendsTheExpectedCommands()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "Siglent,SSA3032X,SN1,1.0");
    transport->setResponse("*OPC?", "1");

    auto driver = drivers::makeSiglentSsaDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    params.points = 751;
    params.rbw = kilohertz(100);
    params.detector = Detector::QuasiPeak;
    params.automaticAttenuation = false;
    params.attenuation = decibel(20.0);
    QVERIFY(driver->configureSweep(params).has_value());

    QVERIFY(transport->sawCommandStartingWith(":SENSe:FREQuency:STARt 30000000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:FREQuency:STOP 1000000000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:BANDwidth:RESolution 100000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:DETector:FUNCtion QPEak"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:POWer:RF:ATTenuation:AUTO OFF"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:POWer:RF:ATTenuation 20.00"));
    QVERIFY(transport->sawCommandStartingWith(":UNIT:POWer DBUV"));

    // Continuous sweep is switched off on connect and back on when closing, so
    // the instrument is never left frozen for the operator (NFR-UX-2).
    QVERIFY(transport->sawCommandStartingWith(":INITiate:CONTinuous OFF"));
    driver->close();
    QVERIFY(transport->sawCommandStartingWith(":INITiate:CONTinuous ON"));
}

void DriversTest::scpiDriverParsesTheTrace()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "Rigol,DSA815,SN2,1.0");
    transport->setResponse("*OPC?", "1");
    transport->setResponse(":TRACe:DATA? TRACE1", "10.0,20.0,30.0,25.0");

    auto driver = drivers::makeRigolDsaDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(230)};
    params.points = 601;
    QVERIFY(driver->configureSweep(params).has_value());

    const CancelToken cancel;
    QVERIFY(driver->armAndTrigger(cancel).has_value());
    const auto trace = driver->fetchTrace(cancel);
    const auto reason = test::errorText(trace);
    QVERIFY2(trace.has_value(), reason.constData());
    QCOMPARE(trace->amplitudes, (std::vector<double>{10.0, 20.0, 30.0, 25.0}));
    QCOMPARE(trace->unit, AmplitudeUnit::dBuV);
    QCOMPARE(trace->source.model, std::string{"DSA815"});
    QCOMPARE(trace->axis.start, megahertz(30));
    QCOMPARE(trace->axis.stop, megahertz(230));
}

void DriversTest::scpiDriverReportsTransportFailures()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "Siglent,SSA3032X,SN1,1.0");
    auto driver = drivers::makeSiglentSsaDriver();
    QVERIFY(driver->open(transport).has_value());

    transport->failNextWrites(1);
    SweepParams params;
    params.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    params.points = 751;
    const auto status = driver->configureSweep(params);
    QVERIFY(!status.has_value());
    QCOMPARE(status.error().code, ErrorCode::TransportFailure);
}

void DriversTest::scpiDriverDrainsTheErrorQueue()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "Siglent,SSA3032X,SN1,1.0");
    transport->setResponse(":SYSTem:ERRor?", "0,\"No error\"");

    auto driver = drivers::makeSiglentSsaDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->lastErrors().empty());

    transport->setResponse(":SYSTem:ERRor?", "-113,\"Undefined header\"");
    const auto errors = driver->lastErrors();
    QCOMPARE(errors.size(), 16U); // the scripted instrument never clears its queue
    QCOMPARE(errors.front().code, -113);
}

void DriversTest::profilesCoverTheSupportedModels()
{
    // Every profile must be usable: a range that makes sense, bandwidths the
    // CISPR bands need, and the detectors a pre-compliance run uses.
    const auto profiles = drivers::instrumentProfiles();
    QVERIFY(profiles.size() >= 11);

    for (const auto& profile : profiles) {
        QVERIFY2(profile.capabilities.range.isValid(), profile.name.c_str());
        QVERIFY2(!profile.models.empty(), profile.name.c_str());
        QVERIFY2(profile.capabilities.nativeUnit == AmplitudeUnit::dBuV, profile.name.c_str());
        QVERIFY2(profile.capabilities.minimumPoints <= profile.capabilities.maximumPoints,
                 profile.name.c_str());
        QVERIFY2(!profile.capabilities.detectors.empty(), profile.name.c_str());
        QVERIFY2(profile.capabilities.supports(Detector::Peak), profile.name.c_str());

        // A profile that offers the quasi-peak detector is claiming the
        // instrument can make a CISPR-conformant Phase 2 dwell, so it must also
        // have the bandwidths CISPR 16-1-1 mandates for the bands it covers --
        // exactly, not merely nearby. Claiming one without the other is the
        // combination that produces a measurement labelled quasi-peak but taken
        // at whatever bandwidth happened to be closest.
        if (!profile.capabilities.supports(Detector::QuasiPeak)) {
            continue;
        }
        for (const auto& band : cisprBands()) {
            if (band.range.start >= profile.capabilities.range.stop) {
                continue;
            }
            const auto nearest =
                profile.capabilities.nearestResolutionBandwidth(band.resolutionBandwidth);
            QVERIFY2(nearest == band.resolutionBandwidth,
                     (profile.name + " lacks the CISPR bandwidth " +
                      std::to_string(band.resolutionBandwidth.value()) + " Hz")
                         .c_str());
        }
    }
}

void DriversTest::profilesAreSelectedByModel()
{
    const auto ssa3032 = drivers::profileFor("Siglent Technologies", "SSA3032X");
    QVERIFY(ssa3032.has_value());
    QCOMPARE(ssa3032->capabilities.range.stop, gigahertz(3.2));
    QCOMPARE(ssa3032->capabilities.maximumPoints, 751);

    // The same family, a different ceiling.
    QCOMPARE(drivers::profileFor("Siglent", "SSA3021X")->capabilities.range.stop, gigahertz(2.1));
    QCOMPARE(drivers::profileFor("Siglent", "SSA3075X")->capabilities.range.stop, gigahertz(7.5));

    // The tracking-generator variants are the same instrument for our purposes.
    QVERIFY(drivers::profileFor("Siglent", "SSA3032X-TG").has_value());

    QCOMPARE(drivers::profileFor("Rigol Technologies", "DSA815")->capabilities.range.stop,
             gigahertz(1.5));
    QCOMPARE(drivers::profileFor("Rigol", "DSA875")->capabilities.range.stop, gigahertz(7.5));
    QCOMPARE(drivers::profileFor("Rigol", "DSA705")->capabilities.range.stop, megahertz(500));
    // Rigol's point count is settable, Siglent's is not.
    QCOMPARE(drivers::profileFor("Rigol", "DSA832")->capabilities.maximumPoints, 3001);

    QVERIFY(!drivers::profileFor("Keysight", "N9000A").has_value());
    QVERIFY(!drivers::profileFor("Siglent", "SDS1104X").has_value());
}

void DriversTest::profilesFallBackToTheFamily()
{
    // Before *IDN? answers, the driver assumes the widest member of the family:
    // assuming less would reject spans the instrument actually supports.
    const auto siglent = drivers::familyProfile("Siglent");
    QCOMPARE(siglent.capabilities.range.stop, gigahertz(7.5));
    QCOMPARE(siglent.dialect.traceQuery, std::string{":TRACe:DATA? 1"});

    const auto rigol = drivers::familyProfile("Rigol");
    QCOMPARE(rigol.capabilities.range.stop, gigahertz(7.5));
    QCOMPARE(rigol.dialect.traceQuery, std::string{":TRACe:DATA? TRACE1"});
}

void DriversTest::driverNarrowsItsCapabilitiesAfterIdentifying()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "Rigol Technologies,DSA815,DSA8A1234,1.16");

    auto driver = drivers::makeRigolDsaDriver();
    QVERIFY(driver->open(transport).has_value());

    // The family default spans the whole DSA range; a 7 GHz sweep is plausible
    // until the instrument says it is a DSA815.
    SweepParams wide;
    wide.span = FrequencyRange{megahertz(30), gigahertz(3.0)};
    wide.points = 601;
    QVERIFY(driver->capabilities().validate(wide).has_value());

    const auto identity = driver->identify();
    QVERIFY(identity.has_value());
    QCOMPARE(identity->model, std::string{"DSA815"});

    // Now the driver knows better, and refuses the same sweep with a reason.
    const auto rejected = driver->capabilities().validate(wide);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, ErrorCode::UnsupportedSetting);
    QVERIFY(rejected.error().detail.find("instrument range") != std::string::npos);

    SweepParams within = wide;
    within.span = FrequencyRange{megahertz(30), gigahertz(1.4)};
    QVERIFY(driver->capabilities().validate(within).has_value());
}

void DriversTest::siglentDoesNotSendAFixedPointCount()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "Siglent Technologies,SSA3032X,SSA3XABC,1.2.9.5");
    transport->setResponse("*OPC?", "1");

    auto driver = drivers::makeSiglentSsaDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(driver->identify().has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), gigahertz(1.0)};
    params.points = 751;
    QVERIFY(driver->configureSweep(params).has_value());

    // The SSA3000X returns 751 points and refuses to be told otherwise, so the
    // driver must not send the command at all.
    QVERIFY(!transport->sawCommandStartingWith(":SENSe:SWEep:POINts"));
    QVERIFY(transport->sawCommandStartingWith(":TRACe:DATA? 1") ||
            transport->sawCommandStartingWith(":SENSe:FREQuency:STARt"));
}

void DriversTest::unitrendProfileMatchesTheInstrumentOnTheBench()
{
    // *IDN? of the reference unit: "UNI-TREND,UTS3032T+,ASA3726210001,V1.04.0059".
    // The vendor field is the long form, the model carries a '+', and both have
    // to reach the profile for the driver to narrow itself correctly.
    const auto profile = drivers::profileFor("UNI-TREND", "UTS3032T+");
    QVERIFY(profile.has_value());
    QCOMPARE(profile->name, std::string{"UNI-T UTS3032T+"});
    QCOMPARE(profile->capabilities.range.stop, gigahertz(3.2));
    QCOMPARE(profile->capabilities.minimumPoints, 11);
    QCOMPARE(profile->capabilities.maximumPoints, 10001);
    QVERIFY(profile->capabilities.preamp);

    // Measured on the instrument: no quasi-peak and no RMS, both answered with
    // -224 "Illegal parameter value".
    QVERIFY(profile->capabilities.supports(Detector::Peak));
    QVERIFY(profile->capabilities.supports(Detector::Average));
    QVERIFY(profile->capabilities.supports(Detector::Sample));
    QVERIFY(!profile->capabilities.supports(Detector::QuasiPeak));
    QVERIFY(!profile->capabilities.supports(Detector::Rms));

    // A plain 1-3-10 ladder that stops at 1 MHz, with none of the three CISPR
    // bandwidths in it.
    const auto& bandwidths = profile->capabilities.resolutionBandwidths;
    QCOMPARE(bandwidths.front(), hertz(1));
    QCOMPARE(bandwidths.back(), megahertz(1));
    for (const auto missing : {hertz(200), kilohertz(9), kilohertz(120)}) {
        QVERIFY2(std::find(bandwidths.begin(), bandwidths.end(), missing) == bandwidths.end(),
                 std::to_string(missing.value()).c_str());
    }

    // An unlisted model of the series keeps the family defaults.
    QVERIFY(!drivers::profileFor("UNI-TREND", "UTS3021T").has_value());
    const auto family = drivers::familyProfile("UNI-TREND");
    QCOMPARE(family.name, std::string{"UNI-T UTS3000T"});
    QCOMPARE(family.capabilities.range.stop, gigahertz(3.2));
}

void DriversTest::unitrendRefusesWhatItCannotMeasure()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "UNI-TREND,UTS3032T+,ASA3726210001,V1.04.0059");

    auto driver = drivers::makeUnitrendUtsDriver();
    QVERIFY(driver->open(transport).has_value());
    QCOMPARE(driver->identify()->model, std::string{"UTS3032T+"});

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(230)};
    params.points = 1001;
    params.refLevel = decibel(107.0);

    // The instrument tunes 100 kHz when asked for the CISPR 120 kHz and reports
    // no error, so the refusal has to come from here or the run would carry a
    // bandwidth the report cannot justify.
    params.rbw = kilohertz(120);
    auto rejected = driver->configureSweep(params);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, ErrorCode::UnsupportedSetting);
    QVERIFY(rejected.error().detail.find("resolution bandwidth") != std::string::npos);

    // Same for the detector the instrument does not have.
    params.rbw = kilohertz(100);
    params.detector = Detector::QuasiPeak;
    rejected = driver->configureSweep(params);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, ErrorCode::UnsupportedSetting);
    QVERIFY(rejected.error().detail.find("detector") != std::string::npos);

    // And for a span past the 3.2 GHz ceiling, which the instrument would
    // silently clamp rather than refuse.
    params.detector = Detector::Peak;
    params.span = FrequencyRange{megahertz(30), gigahertz(4.0)};
    rejected = driver->configureSweep(params);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, ErrorCode::UnsupportedSetting);
}

void DriversTest::unitrendSendsTheUnitBeforeTheReferenceLevel()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "UNI-TREND,UTS3032T+,ASA3726210001,V1.04.0059");

    auto driver = drivers::makeUnitrendUtsDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(230)};
    params.points = 1001;
    params.rbw = kilohertz(100);
    params.refLevel = decibel(107.0);
    QVERIFY(driver->configureSweep(params).has_value());

    // This instrument reads the reference level in whatever unit is selected at
    // the time, so 107 arriving before ":UNIT:POWer DBUV" would be taken as
    // 107 dBm and clamped to the top of the scale -- a wrong reference level
    // with no error to show for it.
    const auto unitIndex = transport->indexOfCommandStartingWith(":UNIT:POWer");
    const auto refLevelIndex =
        transport->indexOfCommandStartingWith(":DISPlay:WINDow:TRACe:Y:SCALe:RLEVel");
    QVERIFY(unitIndex >= 0);
    QVERIFY(refLevelIndex >= 0);
    QVERIFY2(unitIndex < refLevelIndex, "the amplitude unit must precede the reference level");

    // The point count is writable here, unlike on the Siglent.
    QVERIFY(transport->sawCommandStartingWith(":SENSe:SWEep:POINts 1001"));
}

void DriversTest::unitrendDriverCompletesASweep()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "UNI-TREND,UTS3032T+,ASA3726210001,V1.04.0059");
    transport->setResponse("*OPC?", "1");
    transport->setResponse(":TRACe:DATA? TRACE1", "25.9,23.6,21.3,44.5");

    auto driver = drivers::makeUnitrendUtsDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(88), megahertz(108)};
    params.points = 1001;
    params.rbw = kilohertz(100);
    params.refLevel = decibel(107.0);
    QVERIFY(driver->configureSweep(params).has_value());

    const CancelToken cancel;
    QVERIFY(driver->armAndTrigger(cancel).has_value());
    const auto trace = driver->fetchTrace(cancel);
    const auto reason = test::errorText(trace);
    QVERIFY2(trace.has_value(), reason.constData());
    QCOMPARE(trace->amplitudes, (std::vector<double>{25.9, 23.6, 21.3, 44.5}));
    QCOMPARE(trace->unit, AmplitudeUnit::dBuV);
    QCOMPARE(trace->source.manufacturer, std::string{"UNI-TREND"});
    QCOMPARE(trace->source.model, std::string{"UTS3032T+"});
    QCOMPARE(trace->axis.start, megahertz(88));
    QCOMPARE(trace->axis.stop, megahertz(108));
}

namespace {

/// A scripted instrument that behaves like the EMI personality: it names
/// itself, reports the mode, and answers the error queue.
[[nodiscard]] std::shared_ptr<test::ScriptedTransport> emiTransport()
{
    auto transport = std::make_shared<test::ScriptedTransport>();
    transport->setResponse("*IDN?", "UNI-TREND,UTS3032T+,ASA3726210001,V1.04.0059");
    transport->setResponse(":INSTrument:SELect?", "EMI");
    transport->setResponse(":SYSTem:ERRor?", "0,\"No error\"");
    return transport;
}

} // namespace

void DriversTest::fscanProfileOffersWhatTheEmiOptionAdds()
{
    const auto profile = drivers::unitrendEmiProfile();
    const auto& capabilities = profile.capabilities;

    // The whole point of this driver: the detectors the SA mode does not have.
    QVERIFY(capabilities.supports(Detector::QuasiPeak));
    QVERIFY(capabilities.supports(Detector::Peak));
    QVERIFY(capabilities.supports(Detector::Average));
    QVERIFY(!capabilities.supports(Detector::Rms));
    QVERIFY(!capabilities.supports(Detector::Sample));

    // Exactly the CISPR 16-1-1 bandwidths, and exactly those: this mode has no
    // 1-3-10 ladder at all, so an instrument-shaped bandwidth like 100 kHz is
    // refused here rather than snapped to 120 kHz by the instrument.
    QCOMPARE(capabilities.resolutionBandwidths,
             (std::vector<Hertz>{hertz(200), kilohertz(9), kilohertz(120), megahertz(1)}));
    for (const auto& band : cisprBands()) {
        if (band.range.start >= capabilities.range.stop) {
            continue;
        }
        QCOMPARE(capabilities.nearestResolutionBandwidth(band.resolutionBandwidth),
                 band.resolutionBandwidth);
    }

    // "Average" has to reach the CISPR average detector, not the plain one:
    // the (AV) limit lines are written for CAVerage.
    QCOMPARE(drivers::UnitrendFscanDriver::detectorKeyword(Detector::Average),
             std::string{"CAVerage"});
    QCOMPARE(drivers::UnitrendFscanDriver::detectorKeyword(Detector::QuasiPeak),
             std::string{"QPEak"});
    QVERIFY(drivers::UnitrendFscanDriver::detectorKeyword(Detector::Rms).empty());
}

void DriversTest::fscanDriverEntersEmiModeAndRefusesIfItCannot()
{
    auto transport = emiTransport();
    auto driver = drivers::makeUnitrendFscanDriver();
    QVERIFY(driver->open(transport).has_value());
    QVERIFY(transport->sawCommandStartingWith(":INSTrument:SELect EMI"));
    // The scan trace honours the unit; asking for dBuV is what keeps the
    // amplitudes in the unit the limit lines are written in.
    QVERIFY(transport->sawCommandStartingWith(":UNIT:POWer DBUV"));

    // Closing must leave the instrument stopped, not scanning. A continuous EMI
    // scan never yields the bus, so an instrument left running that way cannot
    // be reached again over the network at all.
    driver->close();
    QVERIFY(transport->sawCommandStartingWith(":INITiate:STOP"));
    QVERIFY(transport->sawCommandStartingWith(":INITiate2:CONTinuous OFF"));
    QVERIFY(!transport->sawCommandStartingWith(":INITiate2:CONTinuous ON"));

    // An instrument without the EMI option stays in SA, and that has to be a
    // refusal with a reason rather than a run measured in the wrong mode.
    auto plain = std::make_shared<test::ScriptedTransport>();
    plain->setResponse("*IDN?", "UNI-TREND,UTS3032T+,ASA3726210001,V1.04.0059");
    plain->setResponse(":INSTrument:SELect?", "SA");
    auto refused = drivers::makeUnitrendFscanDriver();
    const auto status = refused->open(plain);
    QVERIFY(!status.has_value());
    QCOMPARE(status.error().code, ErrorCode::UnsupportedSetting);
    QVERIFY(status.error().detail.find("EMI") != std::string::npos);
}

void DriversTest::fscanDriverConfiguresOneScanRange()
{
    auto transport = emiTransport();
    auto driver = drivers::makeUnitrendFscanDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(230)};
    params.points = 1001;
    params.rbw = kilohertz(120);
    params.detector = Detector::QuasiPeak;
    QVERIFY(driver->configureSweep(params).has_value());

    QVERIFY(transport->sawCommandStartingWith(":SENSe:FSCan:SCAN1:STARt 30000000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:FSCan:SCAN1:STOP 230000000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:FSCan:SCAN1:POINts 1001"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:FSCan:SCAN1:BANDwidth:RESolution 120000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:FSCan:DETector:TRACe1 QPEak"));

    // Every other slot is switched off: one left enabled would be scanned too
    // and its points appended, widening the span without saying so.
    for (int slot = 2; slot <= 10; ++slot) {
        const auto command = ":SENSe:FSCan:SCAN" + std::to_string(slot) + ":STATe OFF";
        QVERIFY2(transport->sawCommandStartingWith(command), command.c_str());
    }

    // The range is enabled last -- enabling it before its bounds are set leaves
    // it switched off again on the real instrument.
    const auto enabled = transport->indexOfCommandStartingWith(":SENSe:FSCan:SCAN1:STATe ON");
    const auto stop = transport->indexOfCommandStartingWith(":SENSe:FSCan:SCAN1:STOP");
    QVERIFY(enabled >= 0);
    QVERIFY2(stop < enabled, "the scan range must be enabled after its bounds are set");

    // A bandwidth this mode does not have is refused rather than snapped.
    params.rbw = kilohertz(100);
    const auto rejected = driver->configureSweep(params);
    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, ErrorCode::UnsupportedSetting);
}

void DriversTest::fscanDriverAlwaysSetsTheAttenuation()
{
    // The EMI mode has no auto-attenuation. Leaving the instrument's previous
    // value in place is what made an open input read forty decibels high, so
    // the driver must write one every time, including when the caller asked
    // for "automatic".
    auto transport = emiTransport();
    auto driver = drivers::makeUnitrendFscanDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(30), megahertz(230)};
    params.points = 1001;
    params.rbw = kilohertz(120);
    params.automaticAttenuation = true;
    QVERIFY(driver->configureSweep(params).has_value());
    QVERIFY(transport->sawCommandStartingWith(":SENSe:FSCan:SCAN1:INPut:ATTenuation 10"));

    // An explicit odd value is rounded to the even step the scan list documents,
    // here rather than silently by the instrument.
    auto second = emiTransport();
    auto rounding = drivers::makeUnitrendFscanDriver();
    QVERIFY(rounding->open(second).has_value());
    params.automaticAttenuation = false;
    params.attenuation = decibel(11.0);
    QVERIFY(rounding->configureSweep(params).has_value());
    QVERIFY(second->sawCommandStartingWith(":SENSe:FSCan:SCAN1:INPut:ATTenuation 10"));
}

void DriversTest::fscanDriverDwellsWithTheMeterOnZeroSpan()
{
    auto transport = emiTransport();
    // Three meters answer; only the first is enabled, the others report the
    // "not measuring" sentinel.
    transport->setResponse(":CALCulate:METer:POWer:PEAK?",
                           "-8.233855e+01,9.910000e+37,9.910000e+37");
    auto driver = drivers::makeUnitrendFscanDriver();
    QVERIFY(driver->open(transport).has_value());

    SweepParams params;
    params.span = FrequencyRange{megahertz(98), megahertz(98)}; // zero span: a dwell
    // The point count belongs to the scan list and means nothing to the meter,
    // but it is still validated, and the engine coerces it up to the minimum
    // before the driver ever sees it -- so this is what actually arrives.
    params.points = 11;
    params.rbw = kilohertz(120);
    params.detector = Detector::QuasiPeak;
    params.sweepTime = std::chrono::milliseconds{50}; // the engine carries the dwell here
    QVERIFY(driver->configureSweep(params).has_value());

    QVERIFY(transport->sawCommandStartingWith(":SENSe:FREQuency:CENTer 98000000"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:METer1:DETector QPEak"));
    QVERIFY(transport->sawCommandStartingWith(":SENSe:METer:DETector:DWELl"));
    QVERIFY(transport->sawCommandStartingWith(":DISPlay:METer1:STATe ON"));
    // Nothing from the scan list: a dwell is a different instrument function.
    QVERIFY(!transport->sawCommandStartingWith(":SENSe:FSCan:SCAN1:STARt"));

    const CancelToken cancel;
    QVERIFY(driver->armAndTrigger(cancel).has_value());
    // The peak hold is what the reading is taken from, so it has to be cleared
    // or this point reports the previous one's level.
    QVERIFY(transport->sawCommandStartingWith(":SENSe:METer:PHOLd:RESet"));

    const auto trace = driver->fetchTrace(cancel);
    const auto reason = test::errorText(trace);
    QVERIFY2(trace.has_value(), reason.constData());
    QCOMPARE(trace->size(), 1);
    // The meter answers in dBm whatever :UNIT:POWer says, so -82.34 dBm has to
    // come back as the dBuV the driver advertises: +107 dB in 50 ohms.
    QCOMPARE(trace->unit, AmplitudeUnit::dBuV);
    QVERIFY(std::abs(trace->amplitudes.front() - 24.66) < 0.02);
    QCOMPARE(trace->detector, Detector::QuasiPeak);
}

void DriversTest::fscanDriverWaitsOutAMeterThatHasNotSettled()
{
    auto transport = emiTransport();
    // What the instrument really answers for the first second or two after the
    // peak hold is reset, before the detector has formed a result.
    transport->setResponse(":CALCulate:METer:POWer:PEAK?", "-inf,9.910000e+37,9.910000e+37");
    auto driver = drivers::makeUnitrendFscanDriver();
    QVERIFY(driver->open(transport).has_value());
    auto* fscan = static_cast<drivers::UnitrendFscanDriver*>(driver.get());
    fscan->setMeterSettleBudget(std::chrono::seconds{5});

    SweepParams params;
    params.span = FrequencyRange{megahertz(98), megahertz(98)};
    params.points = 11;
    params.detector = Detector::QuasiPeak;
    params.sweepTime = std::chrono::milliseconds{20};
    QVERIFY(driver->configureSweep(params).has_value());

    const CancelToken cancel;
    QVERIFY(driver->armAndTrigger(cancel).has_value());

    // Once the detector settles the reading is taken -- -inf must never be
    // mistaken for a level, and the quasi-peak detector reports it for about
    // two seconds after every reset.
    transport->setResponse(":CALCulate:METer:POWer:PEAK?",
                           "-8.233855e+01,9.910000e+37,9.910000e+37");
    const auto trace = driver->fetchTrace(cancel);
    const auto reason = test::errorText(trace);
    QVERIFY2(trace.has_value(), reason.constData());
    QVERIFY(std::abs(trace->amplitudes.front() - 24.66) < 0.02);
}

void DriversTest::fscanDriverRefusesAMeterThatReportedNothing()
{
    auto transport = emiTransport();
    // Every meter disabled: 9.91e+37 is the SCPI "not a number", and it must
    // never reach a report as though it were a level.
    transport->setResponse(":CALCulate:METer:POWer:PEAK?",
                           "9.910000e+37,9.910000e+37,9.910000e+37");
    auto driver = drivers::makeUnitrendFscanDriver();
    QVERIFY(driver->open(transport).has_value());
    // Fail fast: the point of this test is the refusal, not the waiting.
    std::static_pointer_cast<drivers::UnitrendFscanDriver>(driver)->setMeterSettleBudget(
        std::chrono::milliseconds{300});

    SweepParams params;
    params.span = FrequencyRange{megahertz(98), megahertz(98)};
    params.points = 11;
    params.detector = Detector::Peak;
    params.sweepTime = std::chrono::milliseconds{20};
    QVERIFY(driver->configureSweep(params).has_value());

    const CancelToken cancel;
    QVERIFY(driver->armAndTrigger(cancel).has_value());
    const auto trace = driver->fetchTrace(cancel);
    QVERIFY(!trace.has_value());
    // A meter that never produces a value is a timeout, not a level: neither
    // the 9.91e+37 sentinel nor the -inf it reports before its first result may
    // ever be returned as an amplitude.
    QCOMPARE(trace.error().code, ErrorCode::Timeout);
}

QTEST_APPLESS_MAIN(DriversTest)
#include "DriversTest.moc"
