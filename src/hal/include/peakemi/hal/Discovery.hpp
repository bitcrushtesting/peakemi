#pragma once

#include <peakemi/core/ITransport.hpp>
#include <peakemi/core/InstrumentId.hpp>

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>

namespace peakemi::hal {

/// An endpoint discovery produced, ready to be connected or persisted as a
/// "known instrument" (FR-DIS-5).
struct DiscoveredInstrument
{
    TransportDescriptor descriptor;
    InstrumentId identity;
    QString description; ///< port description, vendor string, discovery source
};

/// Bounded TCP sweep of the local subnet (FR-DIS-1).
///
/// Opt-in by construction: nothing scans until `scan()` is invoked, because the
/// sweep generates unsolicited traffic on the bench network. Concurrency is
/// capped, every host has its own short timeout and the whole pass is abortable.
class LanDiscoveryWorker : public QObject
{
    Q_OBJECT

public:
    struct Settings
    {
        QString subnetPrefix;                          ///< e.g. "192.168.1"
        QList<quint16> ports{5025, 5555};
        int concurrency{32};
        std::chrono::milliseconds hostTimeout{300};
        std::chrono::milliseconds identifyTimeout{800};
    };

    explicit LanDiscoveryWorker(QObject* parent = nullptr);
    ~LanDiscoveryWorker() override;

    /// IPv4 /24 prefixes of the machine's own interfaces, as scan candidates.
    [[nodiscard]] static QStringList localSubnetPrefixes();

    [[nodiscard]] bool isScanning() const { return m_scanning.load(); }

    /// Thread-safe: may be called from the GUI thread while a scan runs.
    void requestAbort();

public slots:
    /// Sweeps `<prefix>.1` to `<prefix>.254` on the configured ports.
    void scan(peakemi::hal::LanDiscoveryWorker::Settings settings);

signals:
    void instrumentFound(peakemi::hal::DiscoveredInstrument instrument);
    void scanProgress(int completed, int total);
    void scanFinished(int found, bool aborted);

private:
    std::atomic_bool m_scanning{false};
    std::atomic_bool m_abortRequested{false};
};

/// Enumerates serial ports (FR-DIS-3). Listing only: probing an unknown serial
/// device by writing to it is unsafe, so it stays a user-triggered action.
[[nodiscard]] QList<DiscoveredInstrument> enumerateSerialPorts();

/// Ask one endpoint for its identity. Used by the sweep and by the manual
/// "probe this port" action.
[[nodiscard]] Result<InstrumentId> identifyEndpoint(const TransportDescriptor& descriptor,
                                                    std::chrono::milliseconds timeout);

} // namespace peakemi::hal

Q_DECLARE_METATYPE(peakemi::hal::DiscoveredInstrument)
Q_DECLARE_METATYPE(peakemi::hal::LanDiscoveryWorker::Settings)
