#pragma once

#include <peakemi/hal/Discovery.h>
#include <peakemi/hal/UsbTmcTransport.h>

#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace peakemi::hal {

/// Watches the USB bus for instruments (FR-DIS-2).
///
/// Devices appear and disappear in the UI without a restart. Where libusb
/// offers hotplug notifications the worker uses them and only dispatches
/// events; where it does not — Windows, as of libusb 1.0.27 — it falls back to
/// re-enumerating on a timer, which is cheap because the descriptor scan does
/// not open the devices.
class UsbDiscoveryWorker : public QObject
{
    Q_OBJECT

public:
    explicit UsbDiscoveryWorker(QObject* parent = nullptr);
    ~UsbDiscoveryWorker() override;

    /// True when libusb delivers arrival and removal notifications on this
    /// platform; false when the worker polls instead.
    [[nodiscard]] static bool hasHotplugSupport();

    [[nodiscard]] bool isWatching() const { return m_watching; }

public slots:
    /// Enumerate what is present now, then keep watching.
    void start();
    void stop();
    /// Enumerate once without starting a watch.
    void rescan();

signals:
    void instrumentFound(peakemi::hal::DiscoveredInstrument instrument);
    void instrumentLost(QString address);
    void failed(QString reason);

private:
    void publish(const std::vector<UsbTmcDevice>& devices);

    struct Impl;

    std::unique_ptr<Impl> m_impl;
    QTimer m_timer;
    std::vector<UsbTmcDevice> m_known;
    bool m_watching{false};
};

} // namespace peakemi::hal
