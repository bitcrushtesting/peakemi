#include "ScriptedTransport.h"
#include "TestSupport.h"

#include <peakemi/drivers/ScpiAnalyzerDriver.h>
#include <peakemi/drivers/SimulatedDriver.h>

#include <QTest>

#include <cmath>

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

QTEST_APPLESS_MAIN(DriversTest)
#include "DriversTest.moc"
