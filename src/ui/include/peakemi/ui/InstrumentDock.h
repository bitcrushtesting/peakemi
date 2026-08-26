#pragma once

#include <peakemi/hal/Discovery.h>
#include <peakemi/hal/UsbDiscovery.h>

#include <QDockWidget>
#include <QList>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;

namespace peakemi::ui {

/// Instrument inventory: the simulated instrument, discovered LAN endpoints,
/// serial ports and manually added addresses, with connect/disconnect and a
/// manual driver override (FR-DIS-4/5).
class InstrumentDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit InstrumentDock(QWidget* parent = nullptr);
    ~InstrumentDock() override;

    void setConnected(bool connected);

signals:
    /// An empty driver id means "let the registry decide from *IDN?".
    void connectRequested(peakemi::TransportDescriptor descriptor, QString driverId);
    void disconnectRequested();
    void statusMessage(QString message);

private slots:
    void refreshSerialPorts();
    void startLanScan();
    void addManualAddress();
    void onInstrumentFound(const peakemi::hal::DiscoveredInstrument& instrument);
    void onScanFinished(int found, bool aborted);
    void onUsbInstrumentFound(const peakemi::hal::DiscoveredInstrument& instrument);
    void onUsbInstrumentLost(const QString& address);
    void refreshVisaResources();
    void connectSelected();

private:
    [[nodiscard]] QTreeWidgetItem* categoryItem(const QString& name);
    void addInstrument(const hal::DiscoveredInstrument& instrument, const QString& category);
    void removeInstrument(const QString& address);
    void populateDrivers();

    QTreeWidget* m_tree{nullptr};
    QComboBox* m_driverOverride{nullptr};
    QPushButton* m_connectButton{nullptr};
    QPushButton* m_disconnectButton{nullptr};
    QPushButton* m_scanButton{nullptr};
    QProgressBar* m_scanProgress{nullptr};
    QLabel* m_scanHint{nullptr};

    QThread* m_discoveryThread{nullptr};
    hal::LanDiscoveryWorker* m_discovery{nullptr};
    hal::UsbDiscoveryWorker* m_usb{nullptr};
    QList<hal::DiscoveredInstrument> m_instruments;
};

} // namespace peakemi::ui
