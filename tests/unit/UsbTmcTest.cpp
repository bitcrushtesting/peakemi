#include "TestSupport.h"

#include <peakemi/hal/UsbTmcProtocol.h>
#include <peakemi/hal/UsbTmcTransport.h>

#include <QTest>

#include <array>
#include <cstdint>

using namespace peakemi;
using namespace peakemi::hal;

namespace {

[[nodiscard]] std::span<const std::byte> bytesOf(const QByteArray& data)
{
    return {reinterpret_cast<const std::byte*>(data.constData()),
            static_cast<std::size_t>(data.size())};
}

/// A bulk-in transfer as an instrument would send it.
[[nodiscard]] QByteArray makeBulkIn(std::uint8_t tag, const QByteArray& payload, bool endOfMessage)
{
    QByteArray transfer;
    transfer.append(static_cast<char>(2)); // DEV_DEP_MSG_IN
    transfer.append(static_cast<char>(tag));
    transfer.append(static_cast<char>(static_cast<std::uint8_t>(~tag)));
    transfer.append('\0');
    const auto size = static_cast<std::uint32_t>(payload.size());
    transfer.append(static_cast<char>(size & 0xFF));
    transfer.append(static_cast<char>((size >> 8) & 0xFF));
    transfer.append(static_cast<char>((size >> 16) & 0xFF));
    transfer.append(static_cast<char>((size >> 24) & 0xFF));
    transfer.append(static_cast<char>(endOfMessage ? usbtmc::AttributeEndOfMessage : 0));
    transfer.append(3, '\0');
    transfer.append(payload);
    return transfer;
}

} // namespace

class UsbTmcTest : public QObject
{
    Q_OBJECT

private slots:
    void tagsSkipZero();
    void messageOutHasTheStandardHeader();
    void messageOutPadsToFourBytes();
    void requestInCarriesTheTerminator();
    void bulkInHeaderIsUnpacked();
    void bulkInRejectsAMismatchedTag();
    void bulkInRejectsABrokenComplement();
    void bulkInRejectsAShortTransfer();
    void addressesRoundTrip();
    void addressesRejectGarbage();
    void unsupportedBuildsReportItClearly();
};

void UsbTmcTest::tagsSkipZero()
{
    // bTag 0 is reserved, so the sequence wraps from 255 back to 1.
    QCOMPARE(usbtmc::nextTag(0), std::uint8_t{1});
    QCOMPARE(usbtmc::nextTag(1), std::uint8_t{2});
    QCOMPARE(usbtmc::nextTag(254), std::uint8_t{255});
    QCOMPARE(usbtmc::nextTag(255), std::uint8_t{1});
}

void UsbTmcTest::messageOutHasTheStandardHeader()
{
    const QByteArray transfer = usbtmc::encodeMessageOut(7, "*IDN?\n");
    QCOMPARE(static_cast<std::uint8_t>(transfer[0]), std::uint8_t{1}); // DEV_DEP_MSG_OUT
    QCOMPARE(static_cast<std::uint8_t>(transfer[1]), std::uint8_t{7});
    QCOMPARE(static_cast<std::uint8_t>(transfer[2]), static_cast<std::uint8_t>(~7));
    QCOMPARE(static_cast<std::uint8_t>(transfer[3]), std::uint8_t{0});
    QCOMPARE(static_cast<std::uint8_t>(transfer[4]), std::uint8_t{6}); // little-endian size
    QCOMPARE(static_cast<std::uint8_t>(transfer[5]), std::uint8_t{0});
    QCOMPARE(static_cast<std::uint8_t>(transfer[8]), usbtmc::AttributeEndOfMessage);
    QCOMPARE(transfer.mid(usbtmc::HeaderSize, 6), QByteArray{"*IDN?\n"});
}

void UsbTmcTest::messageOutPadsToFourBytes()
{
    QCOMPARE(usbtmc::paddingFor(0), 0);
    QCOMPARE(usbtmc::paddingFor(1), 3);
    QCOMPARE(usbtmc::paddingFor(4), 0);
    QCOMPARE(usbtmc::paddingFor(6), 2);

    // Six payload bytes plus two of padding keeps the next header aligned.
    QCOMPARE(usbtmc::encodeMessageOut(1, "*IDN?\n").size(), usbtmc::HeaderSize + 8);
    QCOMPARE(usbtmc::encodeMessageOut(1, "ABCD").size(), usbtmc::HeaderSize + 4);

    const QByteArray partial = usbtmc::encodeMessageOut(3, "x", false);
    QCOMPARE(static_cast<std::uint8_t>(partial[8]), std::uint8_t{0}); // no EOM
}

void UsbTmcTest::requestInCarriesTheTerminator()
{
    const QByteArray withTerminator = usbtmc::encodeRequestIn(9, 4096, '\n', true);
    QCOMPARE(withTerminator.size(), usbtmc::HeaderSize);
    QCOMPARE(static_cast<std::uint8_t>(withTerminator[0]), std::uint8_t{2});
    QCOMPARE(static_cast<std::uint8_t>(withTerminator[4]), std::uint8_t{0x00});
    QCOMPARE(static_cast<std::uint8_t>(withTerminator[5]), std::uint8_t{0x10}); // 4096
    QCOMPARE(static_cast<std::uint8_t>(withTerminator[8]), usbtmc::AttributeTerminationCharacter);
    QCOMPARE(withTerminator[9], '\n');

    const QByteArray without = usbtmc::encodeRequestIn(9, 4096, '\n', false);
    QCOMPARE(static_cast<std::uint8_t>(without[8]), std::uint8_t{0});
    QCOMPARE(without[9], '\0');
}

void UsbTmcTest::bulkInHeaderIsUnpacked()
{
    const QByteArray transfer = makeBulkIn(5, QByteArray{"Rigol,DSA815,1,1.0"}, true);
    const auto header = usbtmc::parseMessageIn(bytesOf(transfer), 5);
    QVERIFY2(header.has_value(), test::errorText(header).constData());
    QCOMPARE(header->tag, std::uint8_t{5});
    QCOMPARE(header->transferSize, 18U);
    QVERIFY(header->endOfMessage);

    const QByteArray partial = makeBulkIn(6, QByteArray{"partial"}, false);
    QVERIFY(!usbtmc::parseMessageIn(bytesOf(partial), 6)->endOfMessage);
}

void UsbTmcTest::bulkInRejectsAMismatchedTag()
{
    const QByteArray transfer = makeBulkIn(5, QByteArray{"data"}, true);
    const auto header = usbtmc::parseMessageIn(bytesOf(transfer), 6);
    QVERIFY(!header.has_value());
    QCOMPARE(header.error().code, ErrorCode::ProtocolViolation);
    QVERIFY(header.error().detail.find("expected 6") != std::string::npos);
}

void UsbTmcTest::bulkInRejectsABrokenComplement()
{
    QByteArray transfer = makeBulkIn(5, QByteArray{"data"}, true);
    transfer[2] = static_cast<char>(0x00); // corrupt the tag complement
    const auto header = usbtmc::parseMessageIn(bytesOf(transfer), 5);
    QVERIFY(!header.has_value());
    QVERIFY(header.error().detail.find("complement") != std::string::npos);
}

void UsbTmcTest::bulkInRejectsAShortTransfer()
{
    const QByteArray truncated{"12345"};
    QCOMPARE(usbtmc::parseMessageIn(bytesOf(truncated), 1).error().code,
             ErrorCode::ProtocolViolation);
}

void UsbTmcTest::addressesRoundTrip()
{
    const UsbTmcDevice device{.vendorId = 0x1AB1,
                              .productId = 0x0960,
                              .serial = "DSA8A1234",
                              .manufacturer = "Rigol",
                              .product = "DSA815"};
    QCOMPARE(device.address(), std::string{"1ab1:0960:DSA8A1234"});

    const auto parsed = parseUsbAddress(device.address());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->vendorId, device.vendorId);
    QCOMPARE(parsed->productId, device.productId);
    QCOMPARE(parsed->serial, device.serial);

    // The serial is optional: without it the first matching device wins.
    const auto anySerial = parseUsbAddress("1ab1:0960");
    QVERIFY(anySerial.has_value());
    QVERIFY(anySerial->serial.empty());
}

void UsbTmcTest::addressesRejectGarbage()
{
    QCOMPARE(parseUsbAddress("nonsense").error().code, ErrorCode::InvalidConfiguration);
    QCOMPARE(parseUsbAddress("zzzz:0960").error().code, ErrorCode::InvalidConfiguration);
    QVERIFY(parseUsbAddress("1ab1:zzzz").error().detail.find("hexadecimal") != std::string::npos);
}

void UsbTmcTest::unsupportedBuildsReportItClearly()
{
    // Whichever way this build was configured, the answer must be coherent: a
    // build without USB support refuses every operation with a clear reason
    // rather than pretending the bus is empty (NFR-BLD-5).
    if (UsbTmcTransport::isSupported()) {
        const auto devices = UsbTmcTransport::enumerate();
        QVERIFY2(devices.has_value(), test::errorText(devices).constData());
    } else {
        const auto devices = UsbTmcTransport::enumerate();
        QVERIFY(!devices.has_value());
        QCOMPARE(devices.error().code, ErrorCode::NotImplemented);

        TransportDescriptor descriptor{.kind = TransportKind::UsbTmc,
                                       .address = "1ab1:0960",
                                       .port = 0,
                                       .baudRate = 0,
                                       .terminator = "\n",
                                       .defaultTimeout = std::chrono::milliseconds{1000}};
        UsbTmcTransport transport{descriptor};
        QCOMPARE(transport.open().error().code, ErrorCode::NotImplemented);
        QVERIFY(!transport.isOpen());
    }
}

QTEST_APPLESS_MAIN(UsbTmcTest)
#include "UsbTmcTest.moc"
