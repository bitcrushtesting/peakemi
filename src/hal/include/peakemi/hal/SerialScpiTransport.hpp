#pragma once

#include <peakemi/core/ITransport.hpp>

#include <QByteArray>

#include <memory>

class QSerialPort;

namespace peakemi::hal {

/// SCPI over RS-232/UART (FR-COM-3). Baud rate, framing and terminator come
/// from the descriptor because serial instruments agree on nothing.
class SerialScpiTransport final : public ITransport
{
public:
    explicit SerialScpiTransport(TransportDescriptor descriptor);
    ~SerialScpiTransport() override;

    [[nodiscard]] Status open() override;
    [[nodiscard]] bool isOpen() const override;
    void close() override;

    [[nodiscard]] Status write(std::string_view command) override;
    [[nodiscard]] Result<std::string> read(std::chrono::milliseconds timeout,
                                           const CancelToken& cancel) override;
    [[nodiscard]] Result<std::vector<std::byte>> readBinaryBlock(
        std::chrono::milliseconds timeout,
        const CancelToken& cancel) override;
    void clear() override;

    [[nodiscard]] TransportDescriptor descriptor() const override { return m_descriptor; }

private:
    template<class Predicate>
    [[nodiscard]] Status pump(std::chrono::milliseconds timeout,
                              const CancelToken& cancel,
                              Predicate predicate);

    TransportDescriptor m_descriptor;
    std::unique_ptr<QSerialPort> m_port;
    QByteArray m_buffer;
};

} // namespace peakemi::hal
