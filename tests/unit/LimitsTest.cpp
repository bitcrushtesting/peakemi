#include <peakemi/core/LimitCatalogue.hpp>
#include <peakemi/core/LimitEvaluator.hpp>
#include <peakemi/core/LimitLine.hpp>
#include <peakemi/core/LimitLineIo.hpp>

#include <QTest>

#include <cmath>

using namespace peakemi;

namespace {

[[nodiscard]] LimitLine makeLine()
{
    LimitLine line;
    line.name = "test";
    line.unit = AmplitudeUnit::dBuV;
    line.points = {
        LimitPoint{kilohertz(150), 66.0, Interpolation::LogFrequency},
        LimitPoint{kilohertz(500), 56.0, Interpolation::Step},
        LimitPoint{megahertz(5), 56.0, Interpolation::Step},
        LimitPoint{megahertz(30), 60.0, Interpolation::LogFrequency},
    };
    return line;
}

} // namespace

class LimitsTest : public QObject
{
    Q_OBJECT

private slots:
    void undefinedOutsideCoverage();
    void logarithmicInterpolationFollowsCispr();
    void stepSegmentsHoldTheirValue();
    void linearInterpolationIsLinear();
    void validationCatchesBadInput();
    void catalogueIsSelfConsistent();
    void evaluatorPicksTheWorstLimit();
    void evaluatorClassifiesVerdicts();
    void worstCasePerBand();
    void csvRoundTrip();
    void jsonRoundTrip();
    void csvRejectsGarbage();
};

void LimitsTest::undefinedOutsideCoverage()
{
    const auto line = makeLine();
    QVERIFY(std::isnan(line.evaluateAt(kilohertz(100))));
    QVERIFY(std::isnan(line.evaluateAt(megahertz(31))));
    QVERIFY(line.covers(megahertz(1)));
    QCOMPARE(line.coverage().start.value(), 150'000);
    QCOMPARE(line.coverage().stop.value(), 30'000'000);
}

void LimitsTest::logarithmicInterpolationFollowsCispr()
{
    const auto line = makeLine();
    QCOMPARE(line.evaluateAt(kilohertz(150)), 66.0);
    QCOMPARE(line.evaluateAt(kilohertz(500)), 56.0);

    // At the geometric mean of 150 kHz and 500 kHz the limit is exactly halfway
    // between 66 and 56 dBuV, which is what "linear over log f" means.
    const auto geometricMean = hertz(static_cast<std::int64_t>(std::sqrt(150e3 * 500e3)));
    QVERIFY(std::abs(line.evaluateAt(geometricMean) - 61.0) < 0.01);
}

void LimitsTest::stepSegmentsHoldTheirValue()
{
    const auto line = makeLine();
    QCOMPARE(line.evaluateAt(megahertz(1)), 56.0);
    QCOMPARE(line.evaluateAt(megahertz(4)), 56.0);
    QCOMPARE(line.evaluateAt(megahertz(5)), 56.0);
    // Above 5 MHz the last segment interpolates towards 60 dBuV.
    QVERIFY(line.evaluateAt(megahertz(20)) > 56.0);
    QVERIFY(line.evaluateAt(megahertz(20)) < 60.0);
}

void LimitsTest::linearInterpolationIsLinear()
{
    LimitLine line;
    line.points = {LimitPoint{megahertz(30), 30.0, Interpolation::Linear},
                   LimitPoint{megahertz(230), 50.0, Interpolation::Linear}};
    QCOMPARE(line.evaluateAt(megahertz(130)), 40.0);
}

void LimitsTest::validationCatchesBadInput()
{
    LimitLine tooShort;
    tooShort.points = {LimitPoint{megahertz(30), 30.0}};
    QCOMPARE(tooShort.validate().error().code, ErrorCode::InvalidConfiguration);

    LimitLine unordered;
    unordered.points = {LimitPoint{megahertz(230), 30.0}, LimitPoint{megahertz(30), 40.0}};
    QVERIFY(!unordered.validate().has_value());
    unordered.sortPoints();
    QVERIFY(unordered.validate().has_value());

    QVERIFY(makeLine().validate().has_value());
}

void LimitsTest::catalogueIsSelfConsistent()
{
    const auto catalogue = builtInLimitLines();
    QVERIFY(catalogue.size() >= 8);
    for (const auto& line : catalogue) {
        QVERIFY2(line.validate().has_value(), line.name.c_str());
        QVERIFY2(!line.standard.empty(), line.name.c_str());
        QVERIFY(line.builtIn);
    }

    const auto classB = builtInLimitLine("CISPR 32 Class B radiated 10 m (QP)");
    QVERIFY(classB.has_value());
    QCOMPARE(classB->evaluateAt(megahertz(100)), 30.0);
    QCOMPARE(classB->evaluateAt(megahertz(500)), 37.0);
    QCOMPARE(classB->unit, AmplitudeUnit::dBuV_per_m);
    QCOMPARE(classB->measurementDistanceMetres, 10.0);

    QVERIFY(!builtInLimitLine("does not exist").has_value());
    QCOMPARE(builtInLimitLineNames().size(), catalogue.size());
}

void LimitsTest::evaluatorPicksTheWorstLimit()
{
    LimitLine strict;
    strict.name = "strict";
    strict.points = {LimitPoint{megahertz(30), 30.0, Interpolation::Step},
                     LimitPoint{gigahertz(1.0), 30.0}};
    LimitLine relaxed;
    relaxed.name = "relaxed";
    relaxed.points = {LimitPoint{megahertz(30), 40.0, Interpolation::Step},
                      LimitPoint{gigahertz(1.0), 40.0}};

    const LimitEvaluator evaluator{{relaxed, strict}, 6.0};
    const auto result = evaluator.evaluate(megahertz(100), 28.0);
    QCOMPARE(result.limit, 30.0);
    QCOMPARE(result.marginDb, 2.0);
    QCOMPARE(result.limitIndex, 1); // the strict line, not the first one
    QCOMPARE(result.verdict, Verdict::Marginal);
}

void LimitsTest::evaluatorClassifiesVerdicts()
{
    LimitLine line;
    line.points = {LimitPoint{megahertz(30), 40.0, Interpolation::Step},
                   LimitPoint{gigahertz(1.0), 40.0}};
    const LimitEvaluator evaluator{{line}, 6.0};

    QCOMPARE(evaluator.evaluate(megahertz(100), 30.0).verdict, Verdict::Pass);
    QCOMPARE(evaluator.evaluate(megahertz(100), 36.0).verdict, Verdict::Marginal);
    QCOMPARE(evaluator.evaluate(megahertz(100), 41.0).verdict, Verdict::Fail);
    QCOMPARE(evaluator.evaluate(megahertz(10), 41.0).verdict, Verdict::Unknown);
}

void LimitsTest::worstCasePerBand()
{
    LimitLine line;
    line.points = {LimitPoint{megahertz(30), 40.0, Interpolation::Step},
                   LimitPoint{gigahertz(1.0), 40.0}};
    const LimitEvaluator evaluator{{line}, 6.0};

    Trace trace;
    trace.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(1030)}, 101);
    trace.amplitudes.assign(101, 10.0);
    trace.amplitudes[10] = 39.0; // small margin, low band
    trace.amplitudes[90] = 45.0; // failing, high band

    const auto margins = evaluator.evaluateTrace(trace);
    const auto worst = worstCase(margins);
    QVERIFY(worst.valid);
    QCOMPARE(worst.result.marginDb, -5.0);

    const auto lowBand =
        worstCaseInBand(margins, FrequencyRange{megahertz(30), megahertz(300)});
    QVERIFY(lowBand.valid);
    QCOMPARE(lowBand.result.marginDb, 1.0);
}

void LimitsTest::csvRoundTrip()
{
    const auto original = makeLine();
    const auto text = limit_io::toCsvText(original);
    const auto parsed = limit_io::fromCsvText(text);
    QVERIFY2(parsed.has_value(), parsed.error().message().c_str());
    QCOMPARE(parsed->points.size(), original.points.size());
    QCOMPARE(parsed->unit, original.unit);
    QCOMPARE(parsed->evaluateAt(megahertz(1)), original.evaluateAt(megahertz(1)));
    QCOMPARE(parsed->points[1].interpolationToNext, Interpolation::Step);
}

void LimitsTest::jsonRoundTrip()
{
    const auto original = *builtInLimitLine("FCC Part 15B Class B radiated 3 m (QP)");
    const auto parsed = limit_io::fromJsonText(limit_io::toJsonText(original));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->name, original.name);
    QCOMPARE(parsed->standard, original.standard);
    QCOMPARE(parsed->points, original.points);
    QCOMPARE(parsed->evaluateAt(megahertz(100)), 43.5);
}

void LimitsTest::csvRejectsGarbage()
{
    QVERIFY(!limit_io::fromCsvText("# name: nothing here\n").has_value());
    QVERIFY(!limit_io::fromCsvText("1000000,not-a-number\n2000000,30\n").has_value());
    QVERIFY(!limit_io::fromJsonText("{not json").has_value());
}

QTEST_APPLESS_MAIN(LimitsTest)
#include "LimitsTest.moc"
