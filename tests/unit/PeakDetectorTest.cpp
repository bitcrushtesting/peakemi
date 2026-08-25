#include <peakemi/core/PeakDetector.h>

#include <QTest>

#include <cmath>

using namespace peakemi;

namespace {

/// Flat noise floor at 10 dB with narrow emitters injected at chosen bins.
[[nodiscard]] Trace makeTrace(const std::vector<std::pair<int, double>>& emitters,
                              int points = 1001)
{
    Trace trace;
    trace.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(1030)}, points);
    trace.amplitudes.assign(static_cast<std::size_t>(points), 10.0);
    for (const auto& [index, amplitude] : emitters) {
        trace.amplitudes[static_cast<std::size_t>(index)] = amplitude;
    }
    trace.corrected = true;
    return trace;
}

[[nodiscard]] LimitEvaluator flatLimit(double value)
{
    LimitLine line;
    line.name = "flat";
    line.points = {LimitPoint{megahertz(1), value, Interpolation::Step},
                   LimitPoint{gigahertz(3.0), value}};
    return LimitEvaluator{{line}, 6.0};
}

} // namespace

class PeakDetectorTest : public QObject
{
    Q_OBJECT

private slots:
    void findsPeaksNearTheLimit();
    void ignoresPeaksWellBelowTheLimit();
    void respectsProminence();
    void deduplicatesByMinimumSpacing();
    void ranksByMarginAndTruncates();
    void requiresALimitWhenAsked();
    void handlesDegenerateTraces();
    void reportsVerdictPerCandidate();
};

void PeakDetectorTest::findsPeaksNearTheLimit()
{
    const auto trace = makeTrace({{100, 38.0}, {500, 42.0}});
    PeakDetectionSettings settings;
    settings.minimumSpacing = megahertz(1);

    const auto peaks = detectPeaks(trace, flatLimit(40.0), settings);
    QCOMPARE(peaks.size(), 2U);
    // Ranked by smallest margin: the 42 dB emitter fails and comes first.
    QCOMPARE(peaks[0].index, 500);
    QCOMPARE(peaks[0].marginDb, -2.0);
    QCOMPARE(peaks[1].index, 100);
    QCOMPARE(peaks[1].marginDb, 2.0);
}

void PeakDetectorTest::ignoresPeaksWellBelowTheLimit()
{
    const auto trace = makeTrace({{100, 20.0}, {500, 39.0}});
    PeakDetectionSettings settings;
    settings.marginThresholdDb = 6.0;
    settings.minimumSpacing = megahertz(1);

    const auto peaks = detectPeaks(trace, flatLimit(40.0), settings);
    QCOMPARE(peaks.size(), 1U);
    QCOMPARE(peaks[0].index, 500);
}

void PeakDetectorTest::respectsProminence()
{
    // A broad Gaussian hump carrying small ripples: one real emission, plus
    // measurement grass that must not each become a Phase 2 dwell.
    Trace trace = makeTrace({});
    for (int i = 0; i < trace.size(); ++i) {
        const double offset = (i - 500) / 40.0;
        const double hump = 28.0 * std::exp(-offset * offset);
        const double ripple = 0.6 * std::sin(i * 1.7);
        trace.amplitudes[static_cast<std::size_t>(i)] = 10.0 + hump + ripple;
    }

    PeakDetectionSettings settings;
    settings.prominenceDb = 3.0;
    settings.minimumSpacing = hertz(0); // isolate the prominence filter
    const auto prominentOnly = detectPeaks(trace, flatLimit(40.0), settings);
    QCOMPARE(prominentOnly.size(), 1U);
    QCOMPARE(prominentOnly.front().index, 500);

    settings.prominenceDb = 0.1;
    QVERIFY(detectPeaks(trace, flatLimit(40.0), settings).size() > 5U);
}

void PeakDetectorTest::deduplicatesByMinimumSpacing()
{
    // Two emitters 2 MHz apart (1001 points over 1000 MHz = 1 MHz per bin).
    const auto trace = makeTrace({{100, 38.0}, {102, 39.0}, {500, 37.0}});
    PeakDetectionSettings settings;
    settings.minimumSpacing = megahertz(10);

    const auto peaks = detectPeaks(trace, flatLimit(40.0), settings);
    QCOMPARE(peaks.size(), 2U);
    // Of the close pair only the more critical one survives.
    QCOMPARE(peaks[0].index, 102);
    QCOMPARE(peaks[1].index, 500);
}

void PeakDetectorTest::ranksByMarginAndTruncates()
{
    const auto trace = makeTrace({{100, 36.0}, {300, 39.0}, {500, 41.0}, {700, 38.0}});
    PeakDetectionSettings settings;
    settings.minimumSpacing = megahertz(1);
    settings.maximumCount = 2;

    const auto peaks = detectPeaks(trace, flatLimit(40.0), settings);
    QCOMPARE(peaks.size(), 2U);
    QCOMPARE(peaks[0].index, 500);
    QCOMPARE(peaks[1].index, 300);
    QVERIFY(peaks[0].marginDb < peaks[1].marginDb);
}

void PeakDetectorTest::requiresALimitWhenAsked()
{
    const auto trace = makeTrace({{100, 60.0}});
    LimitLine line;
    line.name = "narrow";
    line.points = {LimitPoint{megahertz(500), 40.0, Interpolation::Step},
                   LimitPoint{gigahertz(1.0), 40.0}};
    const LimitEvaluator evaluator{{line}, 6.0};

    PeakDetectionSettings settings;
    settings.minimumSpacing = megahertz(1);
    settings.requireLimit = true;
    QVERIFY(detectPeaks(trace, evaluator, settings).empty());

    settings.requireLimit = false;
    const auto peaks = detectPeaks(trace, evaluator, settings);
    QCOMPARE(peaks.size(), 1U);
    QVERIFY(std::isinf(peaks[0].marginDb));
    QCOMPARE(peaks[0].verdict, Verdict::Unknown);
}

void PeakDetectorTest::handlesDegenerateTraces()
{
    Trace empty;
    QVERIFY(detectPeaks(empty, flatLimit(40.0), {}).empty());

    Trace tiny;
    tiny.axis = FrequencyAxis::linear(FrequencyRange{megahertz(30), megahertz(31)}, 2);
    tiny.amplitudes = {10.0, 50.0};
    QVERIFY(detectPeaks(tiny, flatLimit(40.0), {}).empty());
}

void PeakDetectorTest::reportsVerdictPerCandidate()
{
    const auto trace = makeTrace({{100, 45.0}, {500, 37.0}});
    PeakDetectionSettings settings;
    settings.minimumSpacing = megahertz(1);

    const auto peaks = detectPeaks(trace, flatLimit(40.0), settings);
    QCOMPARE(peaks.size(), 2U);
    QCOMPARE(peaks[0].verdict, Verdict::Fail);
    QCOMPARE(peaks[1].verdict, Verdict::Marginal);
    QCOMPARE(peaks[0].frequency.value(), trace.axis.frequencyAt(100).value());
    QVERIFY(peaks[0].prominenceDb > 30.0);
}

QTEST_APPLESS_MAIN(PeakDetectorTest)
#include "PeakDetectorTest.moc"
