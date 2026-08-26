#include "TestSupport.h"

#include <peakemi/hal/Vxi11Protocol.h>
#include <peakemi/hal/Vxi11Transport.h>

#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>

#include <atomic>
#include <cstdint>

using namespace peakemi;
using namespace peakemi::hal;

namespace {

/// Read a big-endian 32-bit word out of an encoded message.
[[nodiscard]] std::uint32_t wordAt(const QByteArray& data, int index)
{
    return qFromBigEndian<std::uint32_t>(data.constData() + index * 4);
}

/// A minimal VXI-11 instrument: portmapper on one socket, core channel on
/// another, answering create_link, device_write, device_read and destroy_link.
/// Enough to drive the real transport end to end without hardware.
class FakeInstrument : public QObject
{
    Q_OBJECT

public:
    explicit FakeInstrument(QObject* parent = nullptr) : QObject{parent}
    {
        m_core.listen(QHostAddress::LocalHost, 0);
        connect(&m_core, &QTcpServer::newConnection, this, &FakeInstrument::serveCore);
    }

    [[nodiscard]] quint16 corePort() const { return m_core.serverPort(); }

    /// Called from the test thread while the instrument runs in its own.
    [[nodiscard]] QByteArray received() const
    {
        const QMutexLocker locker{&m_mutex};
        return m_received;
    }

private slots:

    void serveCore()
    {
        auto* socket = m_core.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    void handle(QTcpSocket* socket)
    {
        m_buffer.append(socket->readAll());
        while (m_buffer.size() >= 4) {
            const auto mark = vxi11::parseRecordMark(m_buffer.left(4));
            if (!mark) {
                return;
            }
            const auto total = 4 + static_cast<qsizetype>(mark->length);
            if (m_buffer.size() < total) {
                return;
            }
            const QByteArray message = m_buffer.mid(4, static_cast<qsizetype>(mark->length));
            m_buffer.remove(0, total);
            reply(socket, message);
        }
    }

    void reply(QTcpSocket* socket, const QByteArray& call)
    {
        const auto transactionId = wordAt(call, 0);
        const auto procedure = wordAt(call, 5);

        vxi11::XdrWriter body;
        switch (static_cast<vxi11::CoreProcedure>(procedure)) {
            case vxi11::CoreProcedure::CreateLink:
                body.putUnsigned(0);    // error
                body.putUnsigned(42);   // link id
                body.putUnsigned(9099); // abort port
                body.putUnsigned(4096); // maximum receive size
                break;
            case vxi11::CoreProcedure::DeviceWrite: {
                // The written payload is the last opaque field of the call.
                vxi11::XdrReader reader{call.mid(10 * 4)};
                (void)reader.takeUnsigned(); // link
                (void)reader.takeUnsigned(); // io timeout
                (void)reader.takeUnsigned(); // lock timeout
                (void)reader.takeUnsigned(); // flags
                {
                    const QMutexLocker locker{&m_mutex};
                    m_received = reader.takeOpaque();
                }
                body.putUnsigned(0);
                body.putUnsigned(static_cast<std::uint32_t>(received().size()));
                break;
            }
            case vxi11::CoreProcedure::DeviceRead:
                body.putUnsigned(0);
                body.putUnsigned(vxi11::ReasonEnd);
                body.putOpaque(std::string_view{m_response.constData(),
                                                static_cast<std::size_t>(m_response.size())});
                break;
            default:
                body.putUnsigned(0);
                break;
        }

        vxi11::XdrWriter header;
        header.putUnsigned(transactionId);
        header.putUnsigned(1); // REPLY
        header.putUnsigned(0); // MSG_ACCEPTED
        header.putUnsigned(0); // verifier flavour
        header.putUnsigned(0); // verifier length
        header.putUnsigned(0); // SUCCESS

        QByteArray message = header.take();
        message.append(body.take());
        socket->write(vxi11::addRecordMark(message));
        socket->flush();
    }

    mutable QMutex m_mutex;
    QTcpServer m_core;
    QByteArray m_buffer;
    QByteArray m_received;
    QByteArray m_response{"PeakEmi,Fake VXI-11,SN9,1.0\n"};
};

/// The instrument needs its own event loop: the transport blocks the calling
/// thread while it waits for the connection and the reply, exactly as it does
/// on an acquisition worker, so a server sharing that thread would never accept.
class InstrumentThread : public QThread
{
public:
    ~InstrumentThread() override
    {
        quit();
        wait();
    }

    /// Blocks until the instrument is listening, then reports its port.
    [[nodiscard]] quint16 start()
    {
        QThread::start();
        m_ready.acquire();
        return m_port;
    }

    [[nodiscard]] QByteArray received() const
    {
        auto* instrument = m_instrument.load();
        return instrument != nullptr ? instrument->received() : QByteArray{};
    }

protected:
    void run() override
    {
        FakeInstrument instrument;
        m_instrument = &instrument;
        m_port = instrument.corePort();
        m_ready.release();
        exec();
        m_instrument = nullptr;
    }

private:
    QSemaphore m_ready;
    std::atomic<FakeInstrument*> m_instrument{nullptr};
    quint16 m_port{0};
};

} // namespace

class Vxi11Test : public QObject
{
    Q_OBJECT

private slots:
    void xdrWritesBigEndianAndPads();
    void xdrReaderStopsAtTheEnd();
    void recordMarkRoundTrips();
    void callHasAuthNullCredentials();
    void replyRejectsAForeignTransactionId();
    void replyReportsAnUnacceptedCall();
    void portmapperRequestMatchesTheStandard();
    void portmapperRejectsPortZero();
    void createLinkRoundTrips();
    void deviceWriteCarriesTheEndFlag();
    void deviceReadRequestsTheTerminator();
    void errorCodesMapOntoTheTaxonomy();
    void transportTalksToAFakeInstrument();
};

void Vxi11Test::xdrWritesBigEndianAndPads()
{
    vxi11::XdrWriter writer;
    writer.putUnsigned(0x01020304U);
    const QByteArray value = writer.take();
    QCOMPARE(value.size(), 4);
    QCOMPARE(static_cast<unsigned char>(value[0]), 0x01U);
    QCOMPARE(static_cast<unsigned char>(value[3]), 0x04U);

    vxi11::XdrWriter opaque;
    opaque.putOpaque("abc"); // 4 length bytes + 3 payload + 1 pad
    QCOMPARE(opaque.take().size(), 8);

    vxi11::XdrWriter aligned;
    aligned.putOpaque("abcd"); // already a multiple of four: no padding
    QCOMPARE(aligned.take().size(), 8);
}

void Vxi11Test::xdrReaderStopsAtTheEnd()
{
    vxi11::XdrWriter writer;
    writer.putUnsigned(7);
    writer.putOpaque("hello");

    vxi11::XdrReader reader{writer.take()};
    QCOMPARE(reader.takeUnsigned(), 7U);
    QCOMPARE(reader.takeOpaque(), QByteArray{"hello"});
    QVERIFY(reader.ok());

    (void)reader.takeUnsigned(); // past the end
    QVERIFY(!reader.ok());
}

void Vxi11Test::recordMarkRoundTrips()
{
    const QByteArray framed = vxi11::addRecordMark(QByteArray{"payload"});
    QCOMPARE(framed.size(), 11);

    const auto mark = vxi11::parseRecordMark(framed.left(4));
    QVERIFY(mark.has_value());
    QCOMPARE(mark->length, 7U);
    QVERIFY(mark->last);

    QCOMPARE(vxi11::parseRecordMark(QByteArray{"ab"}).error().code, ErrorCode::ProtocolViolation);
}

void Vxi11Test::callHasAuthNullCredentials()
{
    const QByteArray call =
        vxi11::encodeCall(0x1234, vxi11::CoreProgram, vxi11::CoreVersion, 10, {});
    QCOMPARE(call.size(), 40);
    QCOMPARE(wordAt(call, 0), 0x1234U);
    QCOMPARE(wordAt(call, 1), 0U); // CALL
    QCOMPARE(wordAt(call, 2), 2U); // RPC version 2
    QCOMPARE(wordAt(call, 3), vxi11::CoreProgram);
    QCOMPARE(wordAt(call, 4), vxi11::CoreVersion);
    QCOMPARE(wordAt(call, 5), 10U);
    // AUTH_NULL credentials and verifier: four zero words.
    for (int i = 6; i < 10; ++i) {
        QCOMPARE(wordAt(call, i), 0U);
    }
}

void Vxi11Test::replyRejectsAForeignTransactionId()
{
    vxi11::XdrWriter writer;
    writer.putUnsigned(99); // transaction id
    writer.putUnsigned(1);
    writer.putUnsigned(0);
    writer.putUnsigned(0);
    writer.putUnsigned(0);
    writer.putUnsigned(0);

    const auto decoded = vxi11::decodeReply(writer.take(), 1);
    QVERIFY(!decoded.has_value());
    QCOMPARE(decoded.error().code, ErrorCode::ProtocolViolation);
    QVERIFY(decoded.error().detail.find("transaction id") != std::string::npos);
}

void Vxi11Test::replyReportsAnUnacceptedCall()
{
    vxi11::XdrWriter writer;
    writer.putUnsigned(1);
    writer.putUnsigned(1); // REPLY
    writer.putUnsigned(0); // MSG_ACCEPTED
    writer.putUnsigned(0);
    writer.putUnsigned(0);
    writer.putUnsigned(3); // PROC_UNAVAIL

    const auto decoded = vxi11::decodeReply(writer.take(), 1);
    QVERIFY(!decoded.has_value());
    QVERIFY(decoded.error().detail.find("procedure unavailable") != std::string::npos);
}

void Vxi11Test::portmapperRequestMatchesTheStandard()
{
    const QByteArray request =
        vxi11::encodeGetPort(vxi11::CoreProgram, vxi11::CoreVersion, vxi11::PortmapProtocolTcp);
    QCOMPARE(request.size(), 16);
    QCOMPARE(wordAt(request, 0), 0x0607AFU); // the VXI-11 core program number
    QCOMPARE(wordAt(request, 1), 1U);
    QCOMPARE(wordAt(request, 2), 6U); // IPPROTO_TCP
}

void Vxi11Test::portmapperRejectsPortZero()
{
    vxi11::XdrWriter writer;
    writer.putUnsigned(0);
    const auto port = vxi11::decodeGetPort(writer.take());
    QVERIFY(!port.has_value());
    QVERIFY(port.error().detail.find("no VXI-11 core channel") != std::string::npos);
}

void Vxi11Test::createLinkRoundTrips()
{
    const QByteArray request = vxi11::encodeCreateLink(7, 0, "inst0");
    QCOMPARE(wordAt(request, 0), 7U);
    QCOMPARE(wordAt(request, 1), 0U); // never asks for an exclusive lock

    vxi11::XdrWriter writer;
    writer.putUnsigned(0);
    writer.putUnsigned(11);
    writer.putUnsigned(9099);
    writer.putUnsigned(2048);

    const auto reply = vxi11::decodeCreateLink(writer.take());
    QVERIFY(reply.has_value());
    QCOMPARE(reply->linkId, 11U);
    QCOMPARE(reply->maximumReceiveSize, 2048U);

    QVERIFY(!vxi11::decodeCreateLink(QByteArray{"short"}).has_value());
}

void Vxi11Test::deviceWriteCarriesTheEndFlag()
{
    const QByteArray request = vxi11::encodeDeviceWrite(11, 5000, 0, vxi11::FlagEnd, "*IDN?\n");
    QCOMPARE(wordAt(request, 0), 11U);
    QCOMPARE(wordAt(request, 1), 5000U);
    QCOMPARE(wordAt(request, 3), vxi11::FlagEnd);
    QCOMPARE(wordAt(request, 4), 6U); // payload length

    vxi11::XdrWriter writer;
    writer.putUnsigned(0);
    writer.putUnsigned(6);
    const auto reply = vxi11::decodeDeviceWrite(writer.take());
    QVERIFY(reply.has_value());
    QCOMPARE(reply->size, 6U);
}

void Vxi11Test::deviceReadRequestsTheTerminator()
{
    const QByteArray request =
        vxi11::encodeDeviceRead(11, 4096, 5000, 0, vxi11::FlagTerminationCharacterSet, '\n');
    QCOMPARE(wordAt(request, 1), 4096U);
    QCOMPARE(wordAt(request, 4), vxi11::FlagTerminationCharacterSet);
    QCOMPARE(wordAt(request, 5), 10U); // '\n'

    vxi11::XdrWriter writer;
    writer.putUnsigned(0);
    writer.putUnsigned(vxi11::ReasonEnd);
    writer.putOpaque("Siglent,SSA3032X,1,1.0");

    const auto reply = vxi11::decodeDeviceRead(writer.take());
    QVERIFY(reply.has_value());
    QCOMPARE(reply->reason, vxi11::ReasonEnd);
    QCOMPARE(reply->data, QByteArray{"Siglent,SSA3032X,1,1.0"});
}

void Vxi11Test::errorCodesMapOntoTheTaxonomy()
{
    QCOMPARE(vxi11::toError(0, "x").code, ErrorCode::None);
    QCOMPARE(vxi11::toError(15, "device_read").code, ErrorCode::Timeout);
    QCOMPARE(vxi11::toError(17, "device_read").code, ErrorCode::TransportFailure);
    QCOMPARE(vxi11::toError(23, "device_read").code, ErrorCode::Cancelled);
    QCOMPARE(vxi11::toError(4, "device_write").code, ErrorCode::InstrumentError);
    QVERIFY(vxi11::toError(4, "device_write").detail.find("invalid link") != std::string::npos);
    QVERIFY(vxi11::toError(999, "x").detail.find("999") != std::string::npos);
}

void Vxi11Test::transportTalksToAFakeInstrument()
{
    InstrumentThread instrument;
    const quint16 port = instrument.start();
    QVERIFY(port != 0);

    // Port set explicitly, so the transport skips the portmapper and connects
    // straight to the core channel.
    TransportDescriptor descriptor{.kind = TransportKind::Vxi11,
                                   .address = "127.0.0.1/inst0",
                                   .port = port,
                                   .baudRate = 0,
                                   .terminator = "\n",
                                   .defaultTimeout = std::chrono::milliseconds{3000}};

    Vxi11Transport transport{descriptor};
    QCOMPARE(transport.deviceName(), std::string{"inst0"});

    const auto opened = transport.open();
    QVERIFY2(opened.has_value(), test::errorText(opened).constData());
    QVERIFY(transport.isOpen());

    QVERIFY(transport.write("*IDN?").has_value());

    // Not QTRY_COMPARE: its expansion converts a chrono duration to int, which
    // GCC rejects under -Wconversion in the Qt version CI builds against.
    constexpr int PollIntervalMs = 10;
    constexpr int MaximumWaitMs = 3000;
    QByteArray received;
    for (int waited = 0; waited < MaximumWaitMs && received.isEmpty(); waited += PollIntervalMs) {
        received = instrument.received();
        if (received.isEmpty()) {
            QTest::qWait(PollIntervalMs);
        }
    }
    QCOMPARE(received, QByteArray{"*IDN?\n"});

    const CancelToken cancel;
    const auto response = transport.read(std::chrono::milliseconds{3000}, cancel);
    QVERIFY2(response.has_value(), test::errorText(response).constData());
    QCOMPARE(*response, std::string{"PeakEmi,Fake VXI-11,SN9,1.0"});

    transport.close();
    QVERIFY(!transport.isOpen());
}

QTEST_MAIN(Vxi11Test)
#include "Vxi11Test.moc"
