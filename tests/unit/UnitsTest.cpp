#include <peakemi/core/CisprBands.h>
#include <peakemi/core/Trace.h>
#include <peakemi/core/Units.h>

#include <QTest>

using namespace peakemi;

class UnitsTest : public QObject
{
    Q_OBJECT

private slots:
    void strongTypesRejectMixing();
    void frequencyHelpersRound();
    void amplitudeConversionRoundTrips();
    void fieldStrengthDoesNotConvert();
    void enumKeysRoundTrip();
    void cisprBandsMandateBandwidths();
    void linearAxisInterpolates();
    void explicitAxisFindsNearestIndex();
    void maxHoldKeepsTheHigherValue();
    void maxHoldRejectsMismatchedAxes();
};

void UnitsTest::strongTypesRejectMixing()
{
    // Hertz and Decibel share a representation but are distinct types; this is
    // the property the whole domain relies on. It is a compile-time guarantee,
    // asserted here so the intent is documented.
    static_assert(!std::is_same_v<Hertz, Decibel>);
    static_assert(!std::is_convertible_v<Hertz, Decibel>);
    static_assert(!std::is_convertible_v<std::int64_t, Hertz>);

    QCOMPARE((megahertz(30) + megahertz(70)).value(), 100'000'000);
    QCOMPARE((megahertz(100) / 4).value(), 25'000'000);
    QVERIFY(kilohertz(150) < megahertz(30));
}

void UnitsTest::frequencyHelpersRound()
{
    QCOMPARE(kilohertz(9).value(), 9'000);
    QCOMPARE(megahertz(0.15).value(), 150'000);
    QCOMPARE(gigahertz(1.0).value(), 1'000'000'000);
    QCOMPARE(toMegahertz(hertz(48'000'000)), 48.0);
}

void UnitsTest::amplitudeConversionRoundTrips()
{
    // 0 dBm is 107 dBuV in a 50 ohm system.
    const auto dBuV = convertAmplitude(0.0, AmplitudeUnit::dBm, AmplitudeUnit::dBuV);
    QVERIFY(dBuV.has_value());
    QVERIFY(std::abs(*dBuV - 106.9897) < 0.01);

    const auto back = convertAmplitude(*dBuV, AmplitudeUnit::dBuV, AmplitudeUnit::dBm);
    QVERIFY(back.has_value());
    QVERIFY(std::abs(*back) < 1e-9);

    const auto dBuA = convertAmplitude(60.0, AmplitudeUnit::dBuV, AmplitudeUnit::dBuA);
    QVERIFY(dBuA.has_value());
    QVERIFY(std::abs(*dBuA - (60.0 - 33.979)) < 0.01);
}

void UnitsTest::fieldStrengthDoesNotConvert()
{
    // dBuV/m needs an antenna factor, so a direct conversion must be refused
    // rather than silently inventing a number.
    QVERIFY(!convertAmplitude(40.0, AmplitudeUnit::dBuV_per_m, AmplitudeUnit::dBuV).has_value());
    QVERIFY(!convertAmplitude(40.0, AmplitudeUnit::dBm, AmplitudeUnit::dBuV_per_m).has_value());
    QCOMPARE(convertAmplitude(40.0, AmplitudeUnit::dBuV_per_m, AmplitudeUnit::dBuV_per_m).value(),
             40.0);
}

void UnitsTest::enumKeysRoundTrip()
{
    for (const auto detector :
         {Detector::Peak, Detector::QuasiPeak, Detector::Average, Detector::Rms, Detector::Sample})
    {
        QCOMPARE(detectorFromKey(detectorKey(detector)).value(), detector);
    }
    for (const auto unit :
         {AmplitudeUnit::dBm, AmplitudeUnit::dBuV, AmplitudeUnit::dBuV_per_m, AmplitudeUnit::dBuA})
    {
        QCOMPARE(amplitudeUnitFromKey(amplitudeUnitKey(unit)).value(), unit);
    }
    for (const auto verdict : {Verdict::Unknown, Verdict::Pass, Verdict::Marginal, Verdict::Fail}) {
        QCOMPARE(verdictFromKey(verdictKey(verdict)).value(), verdict);
    }
    QVERIFY(!detectorFromKey("not-a-detector").has_value());
}

void UnitsTest::cisprBandsMandateBandwidths()
{
    QCOMPARE(mandatedResolutionBandwidth(kilohertz(20)).value(), 200);
    QCOMPARE(mandatedResolutionBandwidth(megahertz(1)).value(), 9'000);
    QCOMPARE(mandatedResolutionBandwidth(megahertz(100)).value(), 120'000);
    QCOMPARE(mandatedResolutionBandwidth(megahertz(500)).value(), 120'000);
    QCOMPARE(mandatedResolutionBandwidth(gigahertz(2.4)).value(), 1'000'000);

    // Band edges belong to the upper band: 150 kHz is band B, not A.
    QCOMPARE(cisprBandFor(kilohertz(150)).band, CisprBand::B);
    QCOMPARE(cisprBandFor(kilohertz(149)).band, CisprBand::A);
    QCOMPARE(cisprBandFor(megahertz(30)).band, CisprBand::C);
}

void UnitsTest::linearAxisInterpolates()
{
    const auto axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(1030)}, 1001);
    QCOMPARE(axis.size(), 1001);
    QCOMPARE(axis.frequencyAt(0).value(), 30'000'000);
    QCOMPARE(axis.frequencyAt(1000).value(), 1'030'000'000);
    QCOMPARE(axis.frequencyAt(500).value(), 530'000'000);
    QCOMPARE(axis.nearestIndex(megahertz(530)), 500);
    QCOMPARE(axis.nearestIndex(hertz(0)), 0);
    QCOMPARE(axis.nearestIndex(gigahertz(3.0)), 1000);
}

void UnitsTest::explicitAxisFindsNearestIndex()
{
    FrequencyAxis axis;
    axis.explicitPoints = {megahertz(30), megahertz(40), megahertz(80)};
    axis.start = megahertz(30);
    axis.stop = megahertz(80);
    axis.points = 3;

    QCOMPARE(axis.size(), 3);
    QCOMPARE(axis.frequencyAt(1).value(), 40'000'000);
    QCOMPARE(axis.nearestIndex(megahertz(41)), 1);
    QCOMPARE(axis.nearestIndex(megahertz(70)), 2);
    QCOMPARE(axis.frequencyAt(99).value(), 0);
}

void UnitsTest::maxHoldKeepsTheHigherValue()
{
    Trace first;
    first.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(40)}, 3);
    first.amplitudes = {10.0, 20.0, 30.0};

    Trace second = first;
    second.amplitudes = {15.0, 5.0, 30.0};

    QVERIFY(first.mergeMaxHold(second).has_value());
    QCOMPARE(first.amplitudes, (std::vector<double>{15.0, 20.0, 30.0}));
    QCOMPARE(first.maximumIndex(), 2);
    QCOMPARE(first.maximumAmplitude(), 30.0);
}

void UnitsTest::maxHoldRejectsMismatchedAxes()
{
    Trace first;
    first.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(40)}, 3);
    first.amplitudes = {10.0, 20.0, 30.0};

    Trace other;
    other.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(50)}, 3);
    other.amplitudes = {1.0, 2.0, 3.0};

    const auto merged = first.mergeMaxHold(other);
    QVERIFY(!merged.has_value());
    QCOMPARE(merged.error().code, ErrorCode::InvalidConfiguration);
}

QTEST_APPLESS_MAIN(UnitsTest)
#include "UnitsTest.moc"
