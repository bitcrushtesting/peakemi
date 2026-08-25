#include <peakemi/drivers/SimulatedDriver.h>
#include <peakemi/hal/DriverRegistry.h>
#include <peakemi/ui/InstrumentDock.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace peakemi::ui {
namespace {

constexpr int DescriptorRole = Qt::UserRole + 1;

[[nodiscard]] QString categoryFor(TransportKind kind)
{
    switch (kind) {
        case TransportKind::Simulated:
            return QObject::tr("Simulated");
        case TransportKind::Serial:
            return QObject::tr("Serial ports");
        case TransportKind::UsbTmc:
            return QObject::tr("USB");
        case TransportKind::Tcp:
        case TransportKind::Vxi11:
        case TransportKind::Visa:
            break;
    }
    return QObject::tr("Network");
}

} // namespace

InstrumentDock::InstrumentDock(QWidget* parent) : QDockWidget{tr("Instruments"), parent}
{
    setObjectName(QStringLiteral("instrumentDock"));

    auto* content = new QWidget{this};
    auto* layout = new QVBoxLayout{content};

    m_tree = new QTreeWidget{content};
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Instrument"), tr("Address")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    layout->addWidget(m_tree, 1);

    auto* driverRow = new QHBoxLayout;
    driverRow->addWidget(new QLabel{tr("Driver"), content});
    m_driverOverride = new QComboBox{content};
    m_driverOverride->setToolTip(
        tr("Automatic selection uses the *IDN? response; override it when the match is wrong"));
    driverRow->addWidget(m_driverOverride, 1);
    layout->addLayout(driverRow);

    auto* buttons = new QHBoxLayout;
    m_connectButton = new QPushButton{tr("Connect"), content};
    m_disconnectButton = new QPushButton{tr("Disconnect"), content};
    m_disconnectButton->setEnabled(false);
    buttons->addWidget(m_connectButton);
    buttons->addWidget(m_disconnectButton);
    layout->addLayout(buttons);

    auto* discovery = new QHBoxLayout;
    m_scanButton = new QPushButton{tr("Scan LAN…"), content};
    auto* refreshSerial = new QPushButton{tr("Refresh ports"), content};
    auto* manual = new QPushButton{tr("Add address…"), content};
    discovery->addWidget(m_scanButton);
    discovery->addWidget(refreshSerial);
    discovery->addWidget(manual);
    layout->addLayout(discovery);

    m_scanProgress = new QProgressBar{content};
    m_scanProgress->setVisible(false);
    layout->addWidget(m_scanProgress);

    m_scanHint = new QLabel{tr("A LAN scan sends unsolicited traffic to every host on the "
                               "subnet, so it only runs when you ask for it."),
                            content};
    m_scanHint->setWordWrap(true);
    m_scanHint->setEnabled(false);
    layout->addWidget(m_scanHint);

    setWidget(content);

    connect(m_connectButton, &QPushButton::clicked, this, &InstrumentDock::connectSelected);
    connect(m_disconnectButton, &QPushButton::clicked, this, &InstrumentDock::disconnectRequested);
    connect(m_scanButton, &QPushButton::clicked, this, &InstrumentDock::startLanScan);
    connect(refreshSerial, &QPushButton::clicked, this, &InstrumentDock::refreshSerialPorts);
    connect(manual, &QPushButton::clicked, this, &InstrumentDock::addManualAddress);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &InstrumentDock::connectSelected);

    // The LAN sweep blocks on sockets, so it gets its own thread (FR-THR-1).
    m_discoveryThread = new QThread{this};
    m_discoveryThread->setObjectName(QStringLiteral("peakemi.discovery"));
    m_discovery = new hal::LanDiscoveryWorker;
    m_discovery->moveToThread(m_discoveryThread);
    connect(m_discoveryThread, &QThread::finished, m_discovery, &QObject::deleteLater);
    connect(m_discovery,
            &hal::LanDiscoveryWorker::instrumentFound,
            this,
            &InstrumentDock::onInstrumentFound);
    connect(
        m_discovery, &hal::LanDiscoveryWorker::scanFinished, this, &InstrumentDock::onScanFinished);
    connect(m_discovery,
            &hal::LanDiscoveryWorker::scanProgress,
            this,
            [this](int completed, int total) {
                m_scanProgress->setRange(0, total);
                m_scanProgress->setValue(completed);
            });
    m_discoveryThread->start();

    populateDrivers();

    // The simulated instrument is always available: a new user can complete a
    // run without owning any hardware (FR-HAL-7, NFR-UX-1).
    addInstrument(
        hal::DiscoveredInstrument{
            .descriptor = TransportDescriptor{.kind = TransportKind::Simulated,
                                              .address = "simulated",
                                              .port = 0,
                                              .baudRate = 0,
                                              .terminator = "\n",
                                              .defaultTimeout = std::chrono::milliseconds{1000}},
            .identity = InstrumentId{.manufacturer = "PeakEmi",
                                     .model = "Simulated Analyzer",
                                     .serial = "SIM-0001",
                                     .firmware = "1.0",
                                     .raw = "PeakEmi,Simulated Analyzer,SIM-0001,1.0"},
            .description = tr("Deterministic synthetic spectrum, no hardware needed")},
        categoryFor(TransportKind::Simulated));
    refreshSerialPorts();
    m_tree->expandAll();
}

InstrumentDock::~InstrumentDock()
{
    if (m_discovery != nullptr) {
        m_discovery->requestAbort();
    }
    m_discoveryThread->quit();
    m_discoveryThread->wait();
}

void InstrumentDock::populateDrivers()
{
    m_driverOverride->clear();
    m_driverOverride->addItem(tr("Automatic (match on *IDN?)"), QString{});
    for (const auto& driver : hal::DriverRegistry::instance().drivers()) {
        m_driverOverride->addItem(QString::fromStdString(driver.name),
                                  QString::fromStdString(driver.id));
    }
}

QTreeWidgetItem* InstrumentDock::categoryItem(const QString& name)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (m_tree->topLevelItem(i)->text(0) == name) {
            return m_tree->topLevelItem(i);
        }
    }
    auto* item = new QTreeWidgetItem{m_tree, {name}};
    item->setFlags(Qt::ItemIsEnabled);
    item->setExpanded(true);
    return item;
}

void InstrumentDock::addInstrument(const hal::DiscoveredInstrument& instrument,
                                   const QString& category)
{
    auto* parent = categoryItem(category);
    const auto address = QString::fromStdString(instrument.descriptor.displayName());
    for (int i = 0; i < parent->childCount(); ++i) {
        if (parent->child(i)->text(1) == address) {
            return; // already listed
        }
    }

    const QString name = instrument.identity.isEmpty()
                             ? instrument.description
                             : QString::fromStdString(instrument.identity.displayName());
    auto* item = new QTreeWidgetItem{parent, {name, address}};
    item->setData(0, DescriptorRole, static_cast<int>(m_instruments.size()));
    item->setToolTip(0, instrument.description);
    m_instruments.append(instrument);
}

void InstrumentDock::refreshSerialPorts()
{
    for (const auto& port : hal::enumerateSerialPorts()) {
        addInstrument(port, categoryFor(TransportKind::Serial));
    }
    m_tree->expandAll();
}

void InstrumentDock::startLanScan()
{
    if (m_discovery->isScanning()) {
        m_discovery->requestAbort();
        emit statusMessage(tr("Aborting LAN scan…"));
        return;
    }

    const auto prefixes = hal::LanDiscoveryWorker::localSubnetPrefixes();
    if (prefixes.isEmpty()) {
        QMessageBox::information(
            this, tr("No network"), tr("No IPv4 network interface was found to scan."));
        return;
    }

    bool accepted = false;
    const QString prefix = QInputDialog::getItem(
        this,
        tr("Scan subnet"),
        tr("PeakEmi will probe every host on the selected /24 subnet on ports 5025 and 5555.\n"
           "This generates unsolicited network traffic."),
        prefixes,
        0,
        true,
        &accepted);
    if (!accepted || prefix.isEmpty()) {
        return;
    }

    hal::LanDiscoveryWorker::Settings settings;
    settings.subnetPrefix = prefix;
    m_scanProgress->setVisible(true);
    m_scanProgress->setRange(0, 0);
    m_scanButton->setText(tr("Abort scan"));
    emit statusMessage(tr("Scanning %1.0/24…").arg(prefix));
    QMetaObject::invokeMethod(m_discovery,
                              "scan",
                              Qt::QueuedConnection,
                              Q_ARG(peakemi::hal::LanDiscoveryWorker::Settings, settings));
}

void InstrumentDock::onInstrumentFound(const hal::DiscoveredInstrument& instrument)
{
    addInstrument(instrument, categoryFor(instrument.descriptor.kind));
    emit statusMessage(
        tr("Found %1").arg(QString::fromStdString(instrument.identity.displayName())));
}

void InstrumentDock::onScanFinished(int found, bool aborted)
{
    m_scanProgress->setVisible(false);
    m_scanButton->setText(tr("Scan LAN…"));
    emit statusMessage(aborted ? tr("LAN scan aborted after %n instrument(s).", nullptr, found)
                               : tr("LAN scan finished: %n instrument(s).", nullptr, found));
}

void InstrumentDock::addManualAddress()
{
    QDialog dialog{this};
    dialog.setWindowTitle(tr("Add instrument address"));
    auto* form = new QFormLayout{&dialog};

    auto* host = new QLineEdit{QStringLiteral("192.168.1.10"), &dialog};
    auto* port = new QSpinBox{&dialog};
    port->setRange(1, 65535);
    port->setValue(5025);
    form->addRow(tr("Host"), host);
    form->addRow(tr("Port"), port);

    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog};
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted || host->text().trimmed().isEmpty()) {
        return;
    }

    hal::DiscoveredInstrument instrument{
        .descriptor = TransportDescriptor{.kind = TransportKind::Tcp,
                                          .address = host->text().trimmed().toStdString(),
                                          .port = port->value(),
                                          .baudRate = 0,
                                          .terminator = "\n",
                                          .defaultTimeout = std::chrono::milliseconds{5000}},
        .identity = {},
        .description = tr("Added manually")};
    addInstrument(instrument, categoryFor(TransportKind::Tcp));
    m_tree->expandAll();
}

void InstrumentDock::connectSelected()
{
    auto* item = m_tree->currentItem();
    if (item == nullptr || item->parent() == nullptr) {
        emit statusMessage(tr("Select an instrument first."));
        return;
    }
    const int index = item->data(0, DescriptorRole).toInt();
    if (index < 0 || index >= m_instruments.size()) {
        return;
    }
    emit connectRequested(m_instruments[index].descriptor,
                          m_driverOverride->currentData().toString());
}

void InstrumentDock::setConnected(bool connected)
{
    m_connectButton->setEnabled(!connected);
    m_disconnectButton->setEnabled(connected);
}

} // namespace peakemi::ui
