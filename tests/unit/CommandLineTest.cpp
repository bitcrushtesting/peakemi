#include "TestSupport.h"

#include <peakemi/cli/CommandLine.h>

#include <QTest>

using namespace peakemi;
using namespace peakemi::cli;

namespace {

/// Every parse needs argv[0] and something to measure against, so the two
/// constants that every case would otherwise repeat live here.
[[nodiscard]] QStringList commandLine(const QStringList& arguments)
{
    return QStringList{QStringLiteral("peakemi-cli"),
                       QStringLiteral("--limit"),
                       QStringLiteral("CISPR 32 Class B radiated 10 m (QP)")} +
           arguments;
}

} // namespace

/// The command line is the interface a pipeline is written against, so it is
/// tested as one: what it accepts, what it refuses, and what it leaves alone.
class CommandLineTest : public QObject
{
    Q_OBJECT

private slots:
    void frequenciesTakeTheUsualSuffixes();
    void frequenciesRejectNonsense();
    void durationsTakeTheUsualSuffixes();
    void endpointsCarryTheirBus();
    void endpointsRejectAnUnknownBus();
    void defaultsMeasureTheSimulatedInstrument();
    void overridesAreOnlySetWhenGiven();
    void repeatableOptionsKeepTheirOrder();
    void informationalActionsSkipTheRunChecks();
    void aRunWithoutALimitIsRefused();
    void anInvertedSpanIsRefused();
    void failOnAndSummaryTakeKnownWordsOnly();
    void positionalArgumentsAreRefused();
};

void CommandLineTest::frequenciesTakeTheUsualSuffixes()
{
    QCOMPARE(parseFrequency(QStringLiteral("30M")).value(), megahertz(30));
    QCOMPARE(parseFrequency(QStringLiteral("30MHz")).value(), megahertz(30));
    QCOMPARE(parseFrequency(QStringLiteral("150k")).value(), kilohertz(150));
    QCOMPARE(parseFrequency(QStringLiteral("1G")).value(), gigahertz(1.0));
    QCOMPARE(parseFrequency(QStringLiteral("1e6")).value(), megahertz(1));
    // A bare number is hertz, not "whatever the last option used".
    QCOMPARE(parseFrequency(QStringLiteral("1000000")).value(), megahertz(1));
    QCOMPARE(parseFrequency(QStringLiteral(" 9 kHz ")).value(), kilohertz(9));
}

void CommandLineTest::frequenciesRejectNonsense()
{
    QVERIFY(!parseFrequency(QStringLiteral("thirty")).has_value());
    QVERIFY(!parseFrequency(QStringLiteral("30 MHz wide")).has_value());
    QVERIFY(!parseFrequency(QStringLiteral("-30M")).has_value());
    QVERIFY(!parseFrequency(QString{}).has_value());
}

void CommandLineTest::durationsTakeTheUsualSuffixes()
{
    QCOMPARE(parseDuration(QStringLiteral("500ms")).value(), std::chrono::milliseconds{500});
    QCOMPARE(parseDuration(QStringLiteral("1s")).value(), std::chrono::milliseconds{1000});
    QCOMPARE(parseDuration(QStringLiteral("2min")).value(), std::chrono::milliseconds{120000});
    QCOMPARE(parseDuration(QStringLiteral("250")).value(), std::chrono::milliseconds{250});
    QVERIFY(!parseDuration(QStringLiteral("0")).has_value());
    QVERIFY(!parseDuration(QStringLiteral("soon")).has_value());
}

void CommandLineTest::endpointsCarryTheirBus()
{
    const auto simulated = parseEndpoint(QStringLiteral("sim"));
    QVERIFY(simulated.has_value());
    QCOMPARE(simulated->kind, TransportKind::Simulated);

    const auto tcp = parseEndpoint(QStringLiteral("tcp:192.168.1.20"));
    QVERIFY(tcp.has_value());
    QCOMPARE(tcp->kind, TransportKind::Tcp);
    QCOMPARE(tcp->address, std::string{"192.168.1.20"});
    QCOMPARE(tcp->port, 5025); // the descriptor's default is left in place

    const auto tcpPort = parseEndpoint(QStringLiteral("tcp:192.168.1.20:5555"));
    QVERIFY(tcpPort.has_value());
    QCOMPARE(tcpPort->port, 5555);

    const auto serial = parseEndpoint(QStringLiteral("serial:/dev/ttyUSB0:9600"));
    QVERIFY(serial.has_value());
    QCOMPARE(serial->kind, TransportKind::Serial);
    QCOMPARE(serial->address, std::string{"/dev/ttyUSB0"});
    QCOMPARE(serial->baudRate, 9600);

    // A VISA resource string is full of colons and none of them is a port.
    const auto visa = parseEndpoint(QStringLiteral("visa:TCPIP0::10.0.0.5::inst0::INSTR"));
    QVERIFY(visa.has_value());
    QCOMPARE(visa->kind, TransportKind::Visa);
    QCOMPARE(visa->address, std::string{"TCPIP0::10.0.0.5::inst0::INSTR"});
}

void CommandLineTest::endpointsRejectAnUnknownBus()
{
    QVERIFY(!parseEndpoint(QStringLiteral("gpib:7")).has_value());
    QVERIFY(!parseEndpoint(QStringLiteral("192.168.1.20")).has_value());
    QVERIFY(!parseEndpoint(QStringLiteral("tcp:")).has_value());
    QVERIFY(!parseEndpoint(QString{}).has_value());
}

void CommandLineTest::defaultsMeasureTheSimulatedInstrument()
{
    const auto parsed = parseCommandLine(commandLine({}));
    const auto reason = test::errorText(parsed);
    QVERIFY2(parsed.has_value(), reason.constData());
    QCOMPARE(parsed->action, Action::Run);
    QCOMPARE(parsed->options.endpoint.kind, TransportKind::Simulated);
    QCOMPARE(parsed->options.failOn, FailOn::Fail);
    QCOMPARE(parsed->options.summary, SummaryFormat::Text);
    QVERIFY(!parsed->options.quiet);
    QVERIFY(!parsed->helpText.isEmpty());
}

void CommandLineTest::overridesAreOnlySetWhenGiven()
{
    const auto bare = parseCommandLine(commandLine({}));
    QVERIFY(bare.has_value());
    // Nothing was said about the span, so nothing may overwrite a session file.
    QVERIFY(!bare->options.start.has_value());
    QVERIFY(!bare->options.stop.has_value());
    QVERIFY(!bare->options.dwell.has_value());
    QVERIFY(!bare->options.points.has_value());

    const auto set = parseCommandLine(commandLine({QStringLiteral("--start"),
                                                   QStringLiteral("150k"),
                                                   QStringLiteral("--stop"),
                                                   QStringLiteral("30M"),
                                                   QStringLiteral("--dwell"),
                                                   QStringLiteral("2s"),
                                                   QStringLiteral("--points"),
                                                   QStringLiteral("2001"),
                                                   QStringLiteral("--detector"),
                                                   QStringLiteral("average")}));
    const auto reason = test::errorText(set);
    QVERIFY2(set.has_value(), reason.constData());
    QCOMPARE(set->options.start.value(), kilohertz(150));
    QCOMPARE(set->options.stop.value(), megahertz(30));
    QCOMPARE(set->options.dwell.value(), std::chrono::milliseconds{2000});
    QCOMPARE(set->options.points.value(), 2001);
    QCOMPARE(set->options.verificationDetector.value(), Detector::Average);
}

void CommandLineTest::repeatableOptionsKeepTheirOrder()
{
    const auto parsed = parseCommandLine(commandLine({QStringLiteral("--start-command"),
                                                      QStringLiteral(":LISN:LINE L1"),
                                                      QStringLiteral("--start-command"),
                                                      QStringLiteral(":LISN:CLAMP ON"),
                                                      QStringLiteral("--stop-command"),
                                                      QStringLiteral(":LISN:LINE OFF")}));
    QVERIFY(parsed.has_value());
    // Order is the whole point: a clamp closed before the line is selected is a
    // different setup from one closed after it.
    QCOMPARE(parsed->options.startCommands,
             (std::vector<std::string>{":LISN:LINE L1", ":LISN:CLAMP ON"}));
    QCOMPARE(parsed->options.stopCommands, (std::vector<std::string>{":LISN:LINE OFF"}));
}

void CommandLineTest::informationalActionsSkipTheRunChecks()
{
    // --list-limits is how you find out what --limit accepts, so it cannot be
    // the option that demands a --limit first.
    const auto limits =
        parseCommandLine({QStringLiteral("peakemi-cli"), QStringLiteral("--list-limits")});
    QVERIFY(limits.has_value());
    QCOMPARE(limits->action, Action::ListLimits);

    const auto drivers =
        parseCommandLine({QStringLiteral("peakemi-cli"), QStringLiteral("--list-drivers")});
    QVERIFY(drivers.has_value());
    QCOMPARE(drivers->action, Action::ListDrivers);

    const auto help = parseCommandLine({QStringLiteral("peakemi-cli"), QStringLiteral("--help")});
    QVERIFY(help.has_value());
    QCOMPARE(help->action, Action::ShowHelp);
    QVERIFY(help->helpText.contains(QStringLiteral("--fail-on")));
}

void CommandLineTest::aRunWithoutALimitIsRefused()
{
    const auto parsed = parseCommandLine({QStringLiteral("peakemi-cli")});
    QVERIFY(!parsed.has_value());
    QCOMPARE(parsed.error().code, ErrorCode::InvalidConfiguration);
    QVERIFY(!usageText().isEmpty());
}

void CommandLineTest::anInvertedSpanIsRefused()
{
    const auto parsed = parseCommandLine(commandLine({QStringLiteral("--start"),
                                                      QStringLiteral("1G"),
                                                      QStringLiteral("--stop"),
                                                      QStringLiteral("30M")}));
    QVERIFY(!parsed.has_value());
}

void CommandLineTest::failOnAndSummaryTakeKnownWordsOnly()
{
    const auto marginal =
        parseCommandLine(commandLine({QStringLiteral("--fail-on"), QStringLiteral("marginal")}));
    QVERIFY(marginal.has_value());
    QCOMPARE(marginal->options.failOn, FailOn::Marginal);

    const auto json =
        parseCommandLine(commandLine({QStringLiteral("--summary"), QStringLiteral("json")}));
    QVERIFY(json.has_value());
    QCOMPARE(json->options.summary, SummaryFormat::Json);

    QVERIFY(
        !parseCommandLine(commandLine({QStringLiteral("--fail-on"), QStringLiteral("sometimes")}))
             .has_value());
    QVERIFY(!parseCommandLine(commandLine({QStringLiteral("--summary"), QStringLiteral("yaml")}))
                 .has_value());
    QVERIFY(!parseCommandLine(commandLine({QStringLiteral("--detector"), QStringLiteral("cispr")}))
                 .has_value());
    QVERIFY(!parseCommandLine(commandLine({QStringLiteral("--points"), QStringLiteral("1")}))
                 .has_value());
}

void CommandLineTest::positionalArgumentsAreRefused()
{
    // A stray word is almost always a mistyped option or a shell quoting slip,
    // and guessing what it meant is how a pipeline measures the wrong thing.
    const auto parsed = parseCommandLine(commandLine({QStringLiteral("run.json")}));
    QVERIFY(!parsed.has_value());
}

QTEST_APPLESS_MAIN(CommandLineTest)
#include "CommandLineTest.moc"
