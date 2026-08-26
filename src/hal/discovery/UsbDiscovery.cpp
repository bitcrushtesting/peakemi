#include <peakemi/core/Logging.h>
#include <peakemi/hal/UsbDiscovery.h>

#include <algorithm>
#include <utility>

#ifdef PEAKEMI_HAVE_LIBUSB
#    include <libusb.h>
#endif

namespace peakemi::hal {
namespace {

/// How often to re-enumerate where hotplug notifications are unavailable.
constexpr int PollIntervalMs = 2000;
/// How often to let libusb dispatch pending hotplug callbacks.
constexpr int DispatchIntervalMs = 250;

[[nodiscard]] DiscoveredInstrument toInstrument(const UsbTmcDevice& device)
{
    TransportDescriptor descriptor{.kind = TransportKind::UsbTmc,
                                   .address = device.address(),
                                   .port = 0,
                                   .baudRate = 0,
                                   .terminator = "\n",
                                   .defaultTimeout = std::chrono::milliseconds{5000}};

    // The USB string descriptors are not an *IDN? response, but they are what
    // the bus knows; the identity is filled in properly once a driver connects.
    InstrumentId identity;
    identity.manufacturer = device.manufacturer;
    identity.model = device.product;
    identity.serial = device.serial;

    QString description = QObject::tr("USBTMC device");
    if (!device.product.empty()) {
        description = QString::fromStdString(device.manufacturer + ' ' + device.product);
    }
    return DiscoveredInstrument{
        .descriptor = descriptor, .identity = identity, .description = description};
}

} // namespace

struct UsbDiscoveryWorker::Impl
{
#ifdef PEAKEMI_HAVE_LIBUSB
    libusb_hotplug_callback_handle hotplug{0};
    bool registered{false};
#endif
};

UsbDiscoveryWorker::UsbDiscoveryWorker(QObject* parent)
    : QObject{parent}
    , m_impl{std::make_unique<Impl>()}
{
    connect(&m_timer, &QTimer::timeout, this, [this] {
#ifdef PEAKEMI_HAVE_LIBUSB
        if (m_impl->registered) {
            // Dispatch pending callbacks without blocking; the callback runs on
            // this thread, so emitting from it is safe.
            timeval immediately{0, 0};
            libusb_handle_events_timeout_completed(nullptr, &immediately, nullptr);
            return;
        }
#endif
        rescan();
    });
}

UsbDiscoveryWorker::~UsbDiscoveryWorker()
{
    UsbDiscoveryWorker::stop();
}

bool UsbDiscoveryWorker::hasHotplugSupport()
{
#ifdef PEAKEMI_HAVE_LIBUSB
    return libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) != 0;
#else
    return false;
#endif
}

void UsbDiscoveryWorker::publish(const std::vector<UsbTmcDevice>& devices)
{
    for (const auto& device : devices) {
        const bool known = std::find(m_known.begin(), m_known.end(), device) != m_known.end();
        if (!known) {
            qCInfo(lcDiscovery) << "USB instrument arrived:"
                                << QString::fromStdString(device.address());
            emit instrumentFound(toInstrument(device));
        }
    }
    for (const auto& previous : m_known) {
        const bool present = std::find(devices.begin(), devices.end(), previous) != devices.end();
        if (!present) {
            qCInfo(lcDiscovery) << "USB instrument left:"
                                << QString::fromStdString(previous.address());
            emit instrumentLost(QString::fromStdString(previous.address()));
        }
    }
    m_known = devices;
}

void UsbDiscoveryWorker::rescan()
{
    auto devices = UsbTmcTransport::enumerate();
    if (!devices) {
        // A build without USB support reports this once, not on every tick.
        if (m_watching) {
            m_timer.stop();
            m_watching = false;
        }
        emit failed(QString::fromStdString(devices.error().message()));
        return;
    }
    publish(*devices);
}

void UsbDiscoveryWorker::start()
{
    if (m_watching) {
        return;
    }
    rescan();

#ifdef PEAKEMI_HAVE_LIBUSB
    if (hasHotplugSupport() && !m_impl->registered) {
        const int code = libusb_hotplug_register_callback(
            nullptr,
            static_cast<libusb_hotplug_event>(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                                              LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT),
            LIBUSB_HOTPLUG_NO_FLAGS,
            LIBUSB_HOTPLUG_MATCH_ANY,
            LIBUSB_HOTPLUG_MATCH_ANY,
            LIBUSB_HOTPLUG_MATCH_ANY,
            [](libusb_context*, libusb_device*, libusb_hotplug_event, void* user) {
                // Re-enumerate rather than inspect the single device: the
                // arriving device may not have its interfaces ready yet, and a
                // full scan is what decides whether it is a USBTMC instrument.
                auto* worker = static_cast<UsbDiscoveryWorker*>(user);
                QMetaObject::invokeMethod(worker, "rescan", Qt::QueuedConnection);
                return 0;
            },
            this,
            &m_impl->hotplug);
        m_impl->registered = code == LIBUSB_SUCCESS;
        if (!m_impl->registered) {
            qCWarning(lcDiscovery) << "hotplug registration failed:" << libusb_strerror(code);
        }
    }
#endif

    m_timer.setInterval(hasHotplugSupport() ? DispatchIntervalMs : PollIntervalMs);
    m_timer.start();
    m_watching = true;
    qCInfo(lcDiscovery) << "watching the USB bus"
                        << (hasHotplugSupport() ? "for hotplug events" : "by polling");
}

void UsbDiscoveryWorker::stop()
{
    m_timer.stop();
    m_watching = false;
#ifdef PEAKEMI_HAVE_LIBUSB
    if (m_impl->registered) {
        libusb_hotplug_deregister_callback(nullptr, m_impl->hotplug);
        m_impl->registered = false;
    }
#endif
}

} // namespace peakemi::hal
