#pragma once

#include <peakemi/core/ITransport.h>

#include <QByteArray>
#include <QString>

#include <memory>

class QTcpSocket;

namespace peakemi::hal {

/// Raw SCPI over TCP (FR-COM-1), the default bus for LAN instruments.
///
/// Blocking by design: it is used from an acquisition worker thread, and the
/// waits are chopped into short slices so a cancellation token takes effect
/// within milliseconds instead of at the end of the timeout (FR-HAL-5).
class TcpScpiTransport final : public ITransport
{
public:
    explicit TcpScpiTransport(TransportDescriptor descriptor);
    ~TcpScpiTransport() override;

    [[nodiscard]] Status open() override;
    [[nodiscard]] bool isOpen() const override;
    void close() override;

    [[nodiscard]] Status write(std::string_view command) override;
    [[nodiscard]] Result<std::string> read(std::chrono::milliseconds timeout,
                                           const CancelToken& cancel) override;
    [[nodiscard]] Result<std::vector<std::byte>>
    readBinaryBlock(std::chrono::milliseconds timeout, const CancelToken& cancel) override;
    void clear() override;

    [[nodiscard]] TransportDescriptor descriptor() const override { return m_descriptor; }

private:
    /// Pump the socket until @p predicate is satisfied, the timeout expires or
    /// the token is cancelled.
    template<class Predicate>
    [[nodiscard]] Status
    pump(std::chrono::milliseconds timeout, const CancelToken& cancel, Predicate predicate);

    TransportDescriptor m_descriptor;
    std::unique_ptr<QTcpSocket> m_socket;
    QByteArray m_buffer;
};

} // namespace peakemi::hal
