#include <peakemi/core/CorrectionTable.hpp>
#include <peakemi/core/LimitLineIo.hpp>

#include <QTest>

#include <cmath>

using namespace peakemi;

class CorrectionsTest : public QObject
{
    Q_OBJECT

private slots:
    void interpolatesOverLogFrequency();
    void clampsOutsideItsRange();
    void gainSubtractsAndLossAdds();
    void disabledTablesContributeNothing();
    void applyCorrectionsTransformsTheTrace();
    void antennaFactorProducesFieldStrength();
    void appliedCorrectionsAreReportable();
    void csvRoundTrip();
};

void CorrectionsTest::interpolatesOverLogFrequency()
{
    CorrectionTable table;
    table.kind = CorrectionKind::AntennaFactor;
    table.points = {{megahertz(30), 10.0}, {megahertz(300), 20.0}};

    QCOMPARE(table.valueAt(megahertz(30)), 10.0);
    QCOMPARE(table.valueAt(megahertz(300)), 20.0);
    // One decade spans 10 dB, so the half-decade point sits at 15 dB.
    const auto midpoint = hertz(static_cast<std::int64_t>(std::sqrt(30e6 * 300e6)));
    QVERIFY(std::abs(table.valueAt(midpoint) - 15.0) < 0.01);
}

void CorrectionsTest::clampsOutsideItsRange()
{
    CorrectionTable table;
    table.points = {{megahertz(30), 10.0}, {megahertz(300), 20.0}};
    QCOMPARE(table.valueAt(megahertz(1)), 10.0);
    QCOMPARE(table.valueAt(gigahertz(3.0)), 20.0);

    const CorrectionTable empty;
    QCOMPARE(empty.valueAt(megahertz(100)), 0.0);
}

void CorrectionsTest::gainSubtractsAndLossAdds()
{
    QCOMPARE(correctionSign(CorrectionKind::AmplifierGain), -1.0);
    QCOMPARE(correctionSign(CorrectionKind::CableLoss), 1.0);
    QCOMPARE(correctionSign(CorrectionKind::AntennaFactor), 1.0);

    CorrectionTable gain;
    gain.kind = CorrectionKind::AmplifierGain;
    gain.points = {{megahertz(30), 20.0}, {gigahertz(1.0), 20.0}};
    QCOMPARE(gain.contributionAt(megahertz(100)), -20.0);

    CorrectionTable cable;
    cable.kind = CorrectionKind::CableLoss;
    cable.points = {{megahertz(30), 1.5}, {gigahertz(1.0), 3.0}};
    QVERIFY(cable.contributionAt(megahertz(100)) > 0.0);
}

void CorrectionsTest::disabledTablesContributeNothing()
{
    CorrectionTable table;
    table.kind = CorrectionKind::CableLoss;
    table.enabled = false;
    table.points = {{megahertz(30), 5.0}, {gigahertz(1.0), 5.0}};
    QCOMPARE(table.contributionAt(megahertz(100)), 0.0);
    QCOMPARE(totalCorrectionAt(megahertz(100), std::vector<CorrectionTable>{table}), 0.0);
    QVERIFY(correctionsAt(megahertz(100), std::vector<CorrectionTable>{table}).empty());
}

void CorrectionsTest::applyCorrectionsTransformsTheTrace()
{
    Trace trace;
    trace.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(1030)}, 3);
    trace.amplitudes = {40.0, 40.0, 40.0};
    trace.unit = AmplitudeUnit::dBuV;

    CorrectionTable antenna;
    antenna.name = "biconical";
    antenna.kind = CorrectionKind::AntennaFactor;
    antenna.points = {{megahertz(30), 10.0}, {megahertz(1030), 10.0}};

    CorrectionTable amplifier;
    amplifier.name = "LNA";
    amplifier.kind = CorrectionKind::AmplifierGain;
    amplifier.points = {{megahertz(30), 25.0}, {megahertz(1030), 25.0}};

    const std::vector<CorrectionTable> chain{antenna, amplifier};
    const auto corrected = applyCorrections(trace, chain);

    // 40 dBuV + 10 dB antenna factor - 25 dB gain = 25 dBuV/m.
    QCOMPARE(corrected.amplitudes[0], 25.0);
    QVERIFY(corrected.corrected);
    QVERIFY(!trace.corrected); // the input is untouched: this is a pure function
    QCOMPARE(trace.amplitudes[0], 40.0);
}

void CorrectionsTest::antennaFactorProducesFieldStrength()
{
    CorrectionTable antenna;
    antenna.kind = CorrectionKind::AntennaFactor;
    antenna.points = {{megahertz(30), 10.0}, {gigahertz(1.0), 10.0}};
    CorrectionTable cable;
    cable.kind = CorrectionKind::CableLoss;
    cable.points = {{megahertz(30), 1.0}, {gigahertz(1.0), 2.0}};

    QCOMPARE(resultingUnit(AmplitudeUnit::dBuV, std::vector<CorrectionTable>{antenna}),
             AmplitudeUnit::dBuV_per_m);
    QCOMPARE(resultingUnit(AmplitudeUnit::dBuV, std::vector<CorrectionTable>{cable}),
             AmplitudeUnit::dBuV);
    QCOMPARE(resultingUnit(AmplitudeUnit::dBuV, {}), AmplitudeUnit::dBuV);
}

void CorrectionsTest::appliedCorrectionsAreReportable()
{
    CorrectionTable antenna;
    antenna.name = "antenna";
    antenna.kind = CorrectionKind::AntennaFactor;
    antenna.points = {{megahertz(30), 12.0}, {gigahertz(1.0), 12.0}};

    const auto applied = correctionsAt(megahertz(100), std::vector<CorrectionTable>{antenna});
    QCOMPARE(applied.size(), 1U);
    QCOMPARE(applied[0].name, std::string{"antenna"});
    QCOMPARE(applied[0].valueDb, 12.0);
    QCOMPARE(applied[0].contributionDb, 12.0);
}

void CorrectionsTest::csvRoundTrip()
{
    CorrectionTable table;
    table.name = "cable";
    table.kind = CorrectionKind::CableLoss;
    table.points = {{megahertz(30), 1.2}, {gigahertz(1.0), 3.4}};

    const auto parsed = correction_io::fromCsvText(correction_io::toCsvText(table));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->name, table.name);
    QCOMPARE(parsed->kind, table.kind);
    QCOMPARE(parsed->points.size(), 2U);
    QCOMPARE(parsed->valueAt(megahertz(30)), 1.2);

    const auto json = correction_io::fromJsonText(correction_io::toJsonText(table));
    QVERIFY(json.has_value());
    QCOMPARE(json->points, table.points);
}

QTEST_APPLESS_MAIN(CorrectionsTest)
#include "CorrectionsTest.moc"
