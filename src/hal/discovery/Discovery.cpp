#include <peakemi/core/CancelToken.h>
#include <peakemi/core/Logging.h>
#include <peakemi/hal/Discovery.h>
#include <peakemi/hal/Scpi.h>
#include <peakemi/hal/SerialScpiTransport.h>
#include <peakemi/hal/TcpScpiTransport.h>

#include <QHostAddress>
#include <QNetworkInterface>
#include <QSerialPortInfo>
#include <QThreadPool>
#include <QtConcurrent>

#include <algorithm>

namespace peakemi::hal {
namespace {

constexpr int FirstHost = 1;
constexpr int LastHost = 254;

} // namespace

LanDiscoveryWorker::LanDiscoveryWorker(QObject* parent) : QObject{parent} {}

LanDiscoveryWorker::~LanDiscoveryWorker()
{
    requestAbort();
}

QStringList LanDiscoveryWorker::localSubnetPrefixes()
{
    QStringList prefixes;
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            flags.testFlag(QNetworkInterface::IsLoopBack))
        {
            continue;
        }
        for (const auto& entry : interface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            const auto octets = address.toString().split(QLatin1Char('.'));
            if (octets.size() != 4) {
                continue;
            }
            const auto prefix = octets.mid(0, 3).join(QLatin1Char('.'));
            if (!prefixes.contains(prefix)) {
                prefixes.append(prefix);
            }
        }
    }
    return prefixes;
}

void LanDiscoveryWorker::requestAbort()
{
    m_abortRequested.store(true);
}

void LanDiscoveryWorker::scan(LanDiscoveryWorker::Settings settings)
{
    if (m_scanning.exchange(true)) {
        return;
    }
    m_abortRequested.store(false);

    if (settings.subnetPrefix.isEmpty() || settings.ports.isEmpty()) {
        m_scanning.store(false);
        emit scanFinished(0, false);
        return;
    }

    const int hostCount = LastHost - FirstHost + 1;
    const int total = hostCount * static_cast<int>(settings.ports.size());
    std::atomic_int completed{0};
    std::atomic_int found{0};

    // The sweep is CPU-idle but I/O bound, so a dedicated pool with an explicit
    // cap is what keeps it from flooding the link (FR-DIS-1, NFR-DIS-1).
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, settings.concurrency));

    for (int host = FirstHost; host <= LastHost; ++host) {
        for (const quint16 port : settings.ports) {
            const QString address =
                settings.subnetPrefix + QLatin1Char('.') + QString::number(host);
            pool.start([this, address, port, settings, &completed, &found] {
                if (!m_abortRequested.load()) {
                    TransportDescriptor descriptor{.kind = TransportKind::Tcp,
                                                   .address = address.toStdString(),
                                                   .port = port,
                                                   .baudRate = 0,
                                                   .terminator = "\n",
                                                   .defaultTimeout = settings.hostTimeout};
                    auto identity = identifyEndpoint(descriptor, settings.identifyTimeout);
                    if (identity && !identity->isEmpty()) {
                        found.fetch_add(1);
                        emit instrumentFound(
                            DiscoveredInstrument{.descriptor = descriptor,
                                                 .identity = *identity,
                                                 .description = QStringLiteral("LAN sweep")});
                    }
                }
                completed.fetch_add(1);
            });
        }
    }

    int lastReported = 0;
    while (!pool.waitForDone(50)) {
        if (m_abortRequested.load()) {
            pool.clear();
        }
        const int done = completed.load();
        if (done != lastReported) {
            lastReported = done;
            emit scanProgress(done, total);
        }
    }
    emit scanProgress(completed.load(), total);

    const bool aborted = m_abortRequested.load();
    m_scanning.store(false);
    qCInfo(lcDiscovery) << "LAN sweep of" << settings.subnetPrefix << "found" << found.load()
                        << "instrument(s)" << (aborted ? "(aborted)" : "");
    emit scanFinished(found.load(), aborted);
}

QList<DiscoveredInstrument> enumerateSerialPorts()
{
    QList<DiscoveredInstrument> ports;
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        TransportDescriptor descriptor{.kind = TransportKind::Serial,
                                       .address = info.systemLocation().toStdString(),
                                       .port = 0,
                                       .baudRate = 115200,
                                       .terminator = "\n",
                                       .defaultTimeout = std::chrono::milliseconds{2000}};
        QString description = info.description();
        if (description.isEmpty()) {
            description = info.portName();
        }
        if (!info.manufacturer().isEmpty()) {
            description += QStringLiteral(" (") + info.manufacturer() + QLatin1Char(')');
        }
        ports.append(DiscoveredInstrument{
            .descriptor = descriptor, .identity = {}, .description = description});
    }
    return ports;
}

Result<InstrumentId> identifyEndpoint(const TransportDescriptor& descriptor,
                                      std::chrono::milliseconds timeout)
{
    std::unique_ptr<ITransport> transport;
    switch (descriptor.kind) {
        case TransportKind::Tcp:
            transport = std::make_unique<TcpScpiTransport>(descriptor);
            break;
        case TransportKind::Serial:
            transport = std::make_unique<SerialScpiTransport>(descriptor);
            break;
        case TransportKind::Vxi11:
        case TransportKind::UsbTmc:
        case TransportKind::Visa:
        case TransportKind::Simulated:
            return fail(ErrorCode::NotImplemented,
                        std::string{transportKindKey(descriptor.kind)} +
                            " endpoints cannot be probed yet");
    }

    if (auto status = transport->open(); !status) {
        return std::unexpected(status.error());
    }
    const CancelToken cancel;
    auto response = transport->query("*IDN?", timeout, cancel);
    transport->close();
    if (!response) {
        return std::unexpected(response.error());
    }
    return scpi::parseIdn(*response);
}

} // namespace peakemi::hal
