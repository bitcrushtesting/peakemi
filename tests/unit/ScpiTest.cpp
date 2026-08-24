#include <peakemi/hal/DriverRegistry.hpp>
#include <peakemi/hal/Scpi.hpp>

#include <QTest>

#include <cstring>

using namespace peakemi;

class ScpiTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesIdnResponses();
    void parsesPartialIdn();
    void parsesDefiniteLengthBlocks();
    void rejectsMalformedBlocks();
    void parsesAsciiTraces();
    void rejectsNonNumericTrace();
    void parsesBinaryPayloads();
    void parsesErrorQueue();
    void formatsScpiValues();
    void registryScoresMatches();
    void registryReportsAmbiguity();
    void registryCreatesByIdentifier();
};

void ScpiTest::parsesIdnResponses()
{
    const auto id = scpi::parseIdn("Siglent Technologies,SSA3032X,SSA3XABC1234,1.2.9.5\n");
    QCOMPARE(id.manufacturer, std::string{"Siglent Technologies"});
    QCOMPARE(id.model, std::string{"SSA3032X"});
    QCOMPARE(id.serial, std::string{"SSA3XABC1234"});
    QCOMPARE(id.firmware, std::string{"1.2.9.5"});
    QVERIFY(!id.isEmpty());
    QCOMPARE(id.displayName(),
             std::string{"Siglent Technologies SSA3032X (SSA3XABC1234)"});
}

void ScpiTest::parsesPartialIdn()
{
    const auto id = scpi::parseIdn("Rigol,DSA815");
    QCOMPARE(id.manufacturer, std::string{"Rigol"});
    QCOMPARE(id.model, std::string{"DSA815"});
    QVERIFY(id.serial.empty());
    QCOMPARE(id.raw, std::string{"Rigol,DSA815"});

    const auto empty = scpi::parseIdn("   ");
    QVERIFY(empty.isEmpty());
}

void ScpiTest::parsesDefiniteLengthBlocks()
{
    const std::string block = "#3012abcdefghijkl";
    const auto payload = scpi::parseDefiniteLengthBlock(block);
    QVERIFY(payload.has_value());
    QCOMPARE(payload->size(), 12U);
    QCOMPARE(static_cast<char>(payload->front()), 'a');
    QCOMPARE(static_cast<char>(payload->back()), 'l');

    const auto header = scpi::parseBlockHeader(block);
    QVERIFY(header.has_value());
    QCOMPARE(header->headerSize, 5U);
    QCOMPARE(header->payloadSize, 12U);
}

void ScpiTest::rejectsMalformedBlocks()
{
    QCOMPARE(scpi::parseDefiniteLengthBlock("no block here").error().code,
             ErrorCode::ProtocolViolation);
    QCOMPARE(scpi::parseDefiniteLengthBlock("#0stream").error().code,
             ErrorCode::ProtocolViolation);
    QCOMPARE(scpi::parseDefiniteLengthBlock("#3012short").error().code,
             ErrorCode::ProtocolViolation);
    QCOMPARE(scpi::parseDefiniteLengthBlock("#X12abc").error().code,
             ErrorCode::ProtocolViolation);
}

void ScpiTest::parsesAsciiTraces()
{
    const auto values = scpi::parseAsciiTrace("-71.2,-70.8,-72.0,-69.55\n");
    QVERIFY(values.has_value());
    QCOMPARE(values->size(), 4U);
    QCOMPARE(values->front(), -71.2);
    QCOMPARE(values->back(), -69.55);

    // Trailing separators are tolerated: instruments differ on this.
    QCOMPARE(scpi::parseAsciiTrace("1.0,2.0,")->size(), 2U);
    QCOMPARE(scpi::parseAsciiTrace("  3.5  ")->size(), 1U);
}

void ScpiTest::rejectsNonNumericTrace()
{
    QCOMPARE(scpi::parseAsciiTrace("1.0,oops,3.0").error().code, ErrorCode::ParseFailure);
    QCOMPARE(scpi::parseAsciiTrace("").error().code, ErrorCode::ParseFailure);
}

void ScpiTest::parsesBinaryPayloads()
{
    const std::vector<float> source{1.5F, -2.25F, 3.0F};
    std::vector<std::byte> bytes(source.size() * sizeof(float));
    std::memcpy(bytes.data(), source.data(), bytes.size());

    const auto values = scpi::parseReal32(bytes);
    QCOMPARE(values.size(), 3U);
    QCOMPARE(values[0], 1.5);
    QCOMPARE(values[1], -2.25);

    const std::vector<double> wide{10.5, -20.25};
    std::vector<std::byte> wideBytes(wide.size() * sizeof(double));
    std::memcpy(wideBytes.data(), wide.data(), wideBytes.size());
    QCOMPARE(scpi::parseReal64(wideBytes), wide);
}

void ScpiTest::parsesErrorQueue()
{
    const auto entry = scpi::parseErrorQueueEntry("-113,\"Undefined header\"");
    QVERIFY(entry.has_value());
    QCOMPARE(entry->first, -113);
    QCOMPARE(entry->second, std::string{"Undefined header"});

    const auto none = scpi::parseErrorQueueEntry("0,\"No error\"");
    QCOMPARE(none->first, 0);
    QCOMPARE(scpi::parseErrorQueueEntry("garbage").error().code, ErrorCode::ParseFailure);
}

void ScpiTest::formatsScpiValues()
{
    QCOMPARE(scpi::formatHertz(megahertz(30)), std::string{"30000000"});
    QCOMPARE(scpi::formatDecibel(decibel(-10.5)), std::string{"-10.50"});
    QCOMPARE(scpi::trim("  \tvalue \r\n"), std::string{"value"});
}

void ScpiTest::registryScoresMatches()
{
    hal::DriverRegistry registry;
    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = DriverInfo{.id = "siglent", .name = "Siglent", .vendor = "Siglent"},
        .matcher = hal::makeMatcher("Siglent", {"SSA3032X", "SSA3*"}),
        .factory = {}});
    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = DriverInfo{.id = "rigol", .name = "Rigol", .vendor = "Rigol"},
        .matcher = hal::makeMatcher("Rigol", {"DSA8*"}),
        .factory = {}});

    const auto exact = registry.match(scpi::parseIdn("Siglent Technologies,SSA3032X,X,1"));
    QCOMPARE(exact.size(), 1U);
    QCOMPARE(exact.front().score, static_cast<int>(hal::MatchScore::ExactModel));

    const auto family = registry.match(scpi::parseIdn("Siglent,SSA3075X,X,1"));
    QCOMPARE(family.front().score, static_cast<int>(hal::MatchScore::ModelFamily));

    const auto vendorOnly = registry.match(scpi::parseIdn("Siglent,SVA1032X,X,1"));
    QCOMPARE(vendorOnly.front().score, static_cast<int>(hal::MatchScore::Vendor));

    QVERIFY(registry.match(scpi::parseIdn("Keysight,N9000A,X,1")).empty());
    QCOMPARE(registry.drivers().size(), 2U);
    QVERIFY(registry.contains("rigol"));
}

void ScpiTest::registryReportsAmbiguity()
{
    hal::DriverRegistry registry;
    const auto matcher = [](const InstrumentId&) { return 50; };
    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = DriverInfo{.id = "a", .name = "A"}, .matcher = matcher, .factory = {}});
    registry.registerDriver(hal::DriverRegistry::Entry{
        .info = DriverInfo{.id = "b", .name = "B"}, .matcher = matcher, .factory = {}});

    const auto created = registry.createBestMatch(scpi::parseIdn("Vendor,Model,S,1"));
    QVERIFY(!created.has_value());
    QCOMPARE(created.error().code, ErrorCode::NoDriverMatch);
    QVERIFY(created.error().detail.find("select one manually") != std::string::npos);

    hal::DriverRegistry empty;
    QCOMPARE(empty.createBestMatch(InstrumentId{}).error().code, ErrorCode::NoDriverMatch);
}

void ScpiTest::registryCreatesByIdentifier()
{
    hal::DriverRegistry registry;
    QCOMPARE(registry.create("nothing").error().code, ErrorCode::NoDriverMatch);
    registry.clear();
    QVERIFY(registry.drivers().empty());
}

QTEST_APPLESS_MAIN(ScpiTest)
#include "ScpiTest.moc"
