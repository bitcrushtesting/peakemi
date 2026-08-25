#include <peakemi/core/Logging.h>
#include <peakemi/hal/Scpi.h>
#include <peakemi/hal/SerialScpiTransport.h>

#include <QElapsedTimer>
#include <QSerialPort>

#include <cstring>
#include <utility>

namespace peakemi::hal {
namespace {

constexpr int WaitSliceMs = 25;

} // namespace

SerialScpiTransport::SerialScpiTransport(TransportDescriptor descriptor)
    : m_descriptor{std::move(descriptor)}
{}

SerialScpiTransport::~SerialScpiTransport()
{
    SerialScpiTransport::close();
}

Status SerialScpiTransport::open()
{
    if (isOpen()) {
        return {};
    }
    m_port = std::make_unique<QSerialPort>(QString::fromStdString(m_descriptor.address));
    m_port->setBaudRate(m_descriptor.baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);
    m_buffer.clear();

    if (!m_port->open(QIODevice::ReadWrite)) {
        const auto reason = m_port->errorString().toStdString();
        m_port.reset();
        return fail(ErrorCode::TransportFailure, m_descriptor.displayName() + ": " + reason);
    }
    qCInfo(lcTransport) << "opened serial port" << QString::fromStdString(m_descriptor.address);
    return {};
}

bool SerialScpiTransport::isOpen() const
{
    return m_port && m_port->isOpen();
}

void SerialScpiTransport::close()
{
    if (m_port) {
        m_port->close();
        m_port.reset();
    }
    m_buffer.clear();
}

Status SerialScpiTransport::write(std::string_view command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    QByteArray payload{command.data(), static_cast<qsizetype>(command.size())};
    const QByteArray terminator = QByteArray::fromStdString(m_descriptor.terminator);
    if (!payload.endsWith(terminator)) {
        payload.append(terminator);
    }
    qCDebug(lcScpi).noquote() << ">" << QString::fromUtf8(payload).trimmed();

    if (m_port->write(payload) != payload.size()) {
        return fail(ErrorCode::TransportFailure, m_port->errorString().toStdString());
    }
    if (!m_port->waitForBytesWritten(static_cast<int>(m_descriptor.defaultTimeout.count()))) {
        return fail(ErrorCode::Timeout, "timed out writing to " + m_descriptor.displayName());
    }
    return {};
}

template<class Predicate>
Status SerialScpiTransport::pump(std::chrono::milliseconds timeout,
                                 const CancelToken& cancel,
                                 Predicate predicate)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate()) {
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "read cancelled");
        }
        if (!isOpen()) {
            return fail(ErrorCode::NotConnected, m_descriptor.displayName());
        }
        if (elapsed.elapsed() >= timeout.count()) {
            return fail(ErrorCode::Timeout,
                        "no response from " + m_descriptor.displayName() + " within " +
                            std::to_string(timeout.count()) + " ms");
        }
        if (m_port->waitForReadyRead(WaitSliceMs)) {
            m_buffer.append(m_port->readAll());
        }
    }
    return {};
}

Result<std::string> SerialScpiTransport::read(std::chrono::milliseconds timeout,
                                              const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    const QByteArray terminator = QByteArray::fromStdString(m_descriptor.terminator);
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.contains(terminator); }); !status)
    {
        return std::unexpected(status.error());
    }
    const auto end = m_buffer.indexOf(terminator);
    const QByteArray line = m_buffer.left(end);
    m_buffer.remove(0, end + terminator.size());
    qCDebug(lcScpi).noquote() << "<" << QString::fromUtf8(line).trimmed();
    return std::string{line.constData(), static_cast<std::size_t>(line.size())};
}

Result<std::vector<std::byte>>
SerialScpiTransport::readBinaryBlock(std::chrono::milliseconds timeout, const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.size() >= 2; }); !status) {
        return std::unexpected(status.error());
    }
    auto header = scpi::parseBlockHeader(
        std::string_view{m_buffer.constData(), static_cast<std::size_t>(m_buffer.size())});
    if (!header) {
        return std::unexpected(header.error());
    }
    const auto total = static_cast<qsizetype>(header->headerSize + header->payloadSize);
    if (auto status = pump(timeout, cancel, [&] { return m_buffer.size() >= total; }); !status) {
        return std::unexpected(status.error());
    }

    std::vector<std::byte> payload(header->payloadSize);
    std::memcpy(payload.data(), m_buffer.constData() + header->headerSize, header->payloadSize);
    m_buffer.remove(0, total);
    return payload;
}

void SerialScpiTransport::clear()
{
    m_buffer.clear();
    if (m_port) {
        m_port->clear();
    }
}

} // namespace peakemi::hal
