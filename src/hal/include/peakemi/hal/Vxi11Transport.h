#pragma once

#include <peakemi/core/ITransport.h>
#include <peakemi/hal/Vxi11Protocol.h>

#include <QByteArray>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QTcpSocket;

namespace peakemi::hal {

/// VXI-11 (TCP/IP Instrument Protocol) transport (FR-COM-1).
///
/// Used for instruments that expose no raw SCPI socket, and by discovery: an
/// endpoint that answers the portmapper on port 111 for program 0x0607AF is an
/// instrument, which is a far cheaper probe than opening a session.
///
/// Only the core channel is implemented. The abort channel exists to interrupt
/// an in-flight RPC from a second connection; cancellation here works by
/// bounding every wait instead, which is enough because the engine's timeouts
/// are what actually limit a stuck sweep (FR-HAL-5).
class Vxi11Transport final : public ITransport
{
public:
    explicit Vxi11Transport(TransportDescriptor descriptor);
    ~Vxi11Transport() override;

    /// Ask the portmapper on @p host which port serves the VXI-11 core channel.
    [[nodiscard]] static Result<std::uint16_t> queryCorePort(const std::string& host,
                                                             std::chrono::milliseconds timeout);

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

    /// Instrument name of the link, e.g. "inst0" (VXI-11 calls this the device).
    [[nodiscard]] std::string deviceName() const { return m_device; }

private:
    /// One complete RPC round trip on an already connected socket.
    [[nodiscard]] static Result<QByteArray> call(QTcpSocket& socket,
                                                 std::uint32_t transactionId,
                                                 std::uint32_t program,
                                                 std::uint32_t version,
                                                 std::uint32_t procedure,
                                                 const QByteArray& parameters,
                                                 std::chrono::milliseconds timeout,
                                                 const CancelToken& cancel);

    [[nodiscard]] Result<QByteArray> coreCall(vxi11::CoreProcedure procedure,
                                              const QByteArray& parameters,
                                              std::chrono::milliseconds timeout,
                                              const CancelToken& cancel);

    /// Read from the link until the instrument reports END or the terminator.
    [[nodiscard]] Result<QByteArray>
    readResponse(std::chrono::milliseconds timeout, const CancelToken& cancel, bool useTerminator);

    TransportDescriptor m_descriptor;
    std::unique_ptr<QTcpSocket> m_socket;
    std::string m_device{"inst0"};
    std::uint32_t m_linkId{0};
    std::uint32_t m_maximumReceiveSize{4096};
    std::uint32_t m_transactionId{1};
    bool m_linked{false};
};

} // namespace peakemi::hal
