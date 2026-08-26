#include <peakemi/core/Logging.h>
#include <peakemi/hal/Scpi.h>
#include <peakemi/hal/UsbTmcProtocol.h>
#include <peakemi/hal/UsbTmcTransport.h>

#include <QByteArray>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <utility>

#ifdef PEAKEMI_HAVE_LIBUSB
#    include <libusb.h>
#endif

namespace peakemi::hal {
namespace {

[[nodiscard]] std::string toHex(std::uint16_t value)
{
    std::array<char, 5> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%04x", value);
    return std::string{buffer.data()};
}

[[nodiscard]] std::optional<std::uint16_t> parseHex(std::string_view text)
{
    std::uint16_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::string UsbTmcDevice::address() const
{
    std::string value = toHex(vendorId) + ':' + toHex(productId);
    if (!serial.empty()) {
        value += ':' + serial;
    }
    return value;
}

Result<UsbTmcDevice> parseUsbAddress(std::string_view address)
{
    const auto firstColon = address.find(':');
    if (firstColon == std::string_view::npos) {
        return fail(ErrorCode::InvalidConfiguration,
                    "USB address '" + std::string{address} + "' is not vendor:product[:serial]");
    }
    const auto secondColon = address.find(':', firstColon + 1);

    const auto vendor = parseHex(address.substr(0, firstColon));
    const auto product =
        parseHex(secondColon == std::string_view::npos
                     ? address.substr(firstColon + 1)
                     : address.substr(firstColon + 1, secondColon - firstColon - 1));
    if (!vendor || !product) {
        return fail(ErrorCode::InvalidConfiguration,
                    "USB address '" + std::string{address} + "' has non-hexadecimal identifiers");
    }

    UsbTmcDevice device;
    device.vendorId = *vendor;
    device.productId = *product;
    if (secondColon != std::string_view::npos) {
        device.serial = std::string{address.substr(secondColon + 1)};
    }
    return device;
}

#ifdef PEAKEMI_HAVE_LIBUSB

namespace {

/// One libusb context for the process: opening several would re-enumerate the
/// bus for every transport and break hotplug callbacks.
[[nodiscard]] libusb_context* sharedContext()
{
    static libusb_context* context = [] {
        libusb_context* created = nullptr;
        if (libusb_init(&created) != LIBUSB_SUCCESS) {
            return static_cast<libusb_context*>(nullptr);
        }
        return created;
    }();
    return context;
}

[[nodiscard]] std::string stringDescriptor(libusb_device_handle* handle, std::uint8_t index)
{
    if (handle == nullptr || index == 0) {
        return {};
    }
    std::array<unsigned char, 256> buffer{};
    const int length = libusb_get_string_descriptor_ascii(
        handle, index, buffer.data(), static_cast<int>(buffer.size()));
    if (length <= 0) {
        return {};
    }
    return std::string{reinterpret_cast<const char*>(buffer.data()),
                       static_cast<std::size_t>(length)};
}

/// The USBTMC interface plus its bulk endpoints, as found in the descriptors.
struct InterfaceLayout
{
    int interfaceNumber{-1};
    std::uint8_t bulkIn{0};
    std::uint8_t bulkOut{0};
    bool found{false};
};

[[nodiscard]] InterfaceLayout findUsbTmcInterface(libusb_device* device)
{
    InterfaceLayout layout;
    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS) {
        return layout;
    }

    for (int i = 0; i < config->bNumInterfaces && !layout.found; ++i) {
        const libusb_interface& interface = config->interface[i];
        for (int alt = 0; alt < interface.num_altsetting; ++alt) {
            const libusb_interface_descriptor& descriptor = interface.altsetting[alt];
            if (descriptor.bInterfaceClass != usbtmc::InterfaceClass ||
                descriptor.bInterfaceSubClass != usbtmc::InterfaceSubClass)
            {
                continue;
            }
            layout.interfaceNumber = descriptor.bInterfaceNumber;
            for (int e = 0; e < descriptor.bNumEndpoints; ++e) {
                const libusb_endpoint_descriptor& endpoint = descriptor.endpoint[e];
                const bool bulk = (endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                                  LIBUSB_TRANSFER_TYPE_BULK;
                if (!bulk) {
                    continue;
                }
                if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
                    layout.bulkIn = endpoint.bEndpointAddress;
                } else {
                    layout.bulkOut = endpoint.bEndpointAddress;
                }
            }
            layout.found = layout.bulkIn != 0 && layout.bulkOut != 0;
            break;
        }
    }
    libusb_free_config_descriptor(config);
    return layout;
}

[[nodiscard]] Error toError(int code, std::string_view context)
{
    const std::string detail = std::string{context} + ": " + libusb_strerror(code);
    switch (code) {
        case LIBUSB_ERROR_TIMEOUT:
            return Error{ErrorCode::Timeout, detail};
        case LIBUSB_ERROR_NO_DEVICE:
            return Error{ErrorCode::NotConnected, detail};
        case LIBUSB_ERROR_ACCESS:
            return Error{ErrorCode::TransportFailure, detail};
        default:
            break;
    }
    return Error{ErrorCode::TransportFailure, detail};
}

} // namespace

struct UsbTmcTransport::Impl
{
    libusb_device_handle* handle{nullptr};
    InterfaceLayout layout;
    std::uint8_t tag{0};
    bool claimed{false};

    ~Impl() { release(); }

    void release()
    {
        if (handle != nullptr) {
            if (claimed) {
                libusb_release_interface(handle, layout.interfaceNumber);
                claimed = false;
            }
            libusb_close(handle);
            handle = nullptr;
        }
    }
};

bool UsbTmcTransport::isSupported()
{
    return true;
}

Result<std::vector<UsbTmcDevice>> UsbTmcTransport::enumerate()
{
    auto* context = sharedContext();
    if (context == nullptr) {
        return fail(ErrorCode::TransportFailure, "libusb could not be initialised");
    }

    libusb_device** list = nullptr;
    const auto count = libusb_get_device_list(context, &list);
    if (count < 0) {
        return std::unexpected(toError(static_cast<int>(count), "libusb_get_device_list"));
    }

    std::vector<UsbTmcDevice> devices;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(list[i], &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (!findUsbTmcInterface(list[i]).found) {
            continue;
        }

        UsbTmcDevice device;
        device.vendorId = descriptor.idVendor;
        device.productId = descriptor.idProduct;

        // Opening the device is the only way to read its string descriptors.
        // A device we cannot open is still worth listing, without its strings.
        libusb_device_handle* handle = nullptr;
        if (libusb_open(list[i], &handle) == LIBUSB_SUCCESS) {
            device.manufacturer = stringDescriptor(handle, descriptor.iManufacturer);
            device.product = stringDescriptor(handle, descriptor.iProduct);
            device.serial = stringDescriptor(handle, descriptor.iSerialNumber);
            libusb_close(handle);
        }
        devices.push_back(std::move(device));
    }
    libusb_free_device_list(list, 1);
    return devices;
}

UsbTmcTransport::UsbTmcTransport(TransportDescriptor descriptor)
    : m_descriptor{std::move(descriptor)}
    , m_impl{std::make_unique<Impl>()}
{}

UsbTmcTransport::~UsbTmcTransport()
{
    UsbTmcTransport::close();
}

Status UsbTmcTransport::open()
{
    if (isOpen()) {
        return {};
    }
    auto wanted = parseUsbAddress(m_descriptor.address);
    if (!wanted) {
        return std::unexpected(wanted.error());
    }

    auto* context = sharedContext();
    if (context == nullptr) {
        return fail(ErrorCode::TransportFailure, "libusb could not be initialised");
    }

    libusb_device** list = nullptr;
    const auto count = libusb_get_device_list(context, &list);
    if (count < 0) {
        return std::unexpected(toError(static_cast<int>(count), "libusb_get_device_list"));
    }

    Status result = fail(ErrorCode::NotConnected, "no USBTMC device at " + m_descriptor.address);
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(list[i], &descriptor) != LIBUSB_SUCCESS) {
            continue;
        }
        if (descriptor.idVendor != wanted->vendorId || descriptor.idProduct != wanted->productId) {
            continue;
        }

        const auto layout = findUsbTmcInterface(list[i]);
        if (!layout.found) {
            continue;
        }

        libusb_device_handle* handle = nullptr;
        const int opened = libusb_open(list[i], &handle);
        if (opened != LIBUSB_SUCCESS) {
            result = std::unexpected(toError(opened, "libusb_open"));
            continue;
        }
        if (!wanted->serial.empty() &&
            stringDescriptor(handle, descriptor.iSerialNumber) != wanted->serial)
        {
            libusb_close(handle);
            continue;
        }

        // On Linux a kernel driver may hold the interface; take it over and
        // give it back automatically when the handle closes.
        libusb_set_auto_detach_kernel_driver(handle, 1);
        const int claimed = libusb_claim_interface(handle, layout.interfaceNumber);
        if (claimed != LIBUSB_SUCCESS) {
            libusb_close(handle);
            result = std::unexpected(toError(claimed, "libusb_claim_interface"));
            continue;
        }

        m_impl->handle = handle;
        m_impl->layout = layout;
        m_impl->claimed = true;
        m_impl->tag = 0;
        result = Status{};
        qCInfo(lcTransport) << "USBTMC device" << QString::fromStdString(m_descriptor.address)
                            << "claimed on interface" << layout.interfaceNumber;
        break;
    }

    libusb_free_device_list(list, 1);
    return result;
}

bool UsbTmcTransport::isOpen() const
{
    return m_impl && m_impl->handle != nullptr;
}

void UsbTmcTransport::close()
{
    if (m_impl) {
        m_impl->release();
    }
}

Status UsbTmcTransport::write(std::string_view command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }

    std::string payload{command};
    if (!payload.ends_with(m_descriptor.terminator)) {
        payload += m_descriptor.terminator;
    }
    qCDebug(lcScpi).noquote() << ">" << QString::fromStdString(payload).trimmed();

    m_impl->tag = usbtmc::nextTag(m_impl->tag);
    const QByteArray transfer = usbtmc::encodeMessageOut(m_impl->tag, payload);

    int transferred = 0;
    const int code = libusb_bulk_transfer(
        m_impl->handle,
        m_impl->layout.bulkOut,
        reinterpret_cast<unsigned char*>(const_cast<char*>(transfer.constData())),
        static_cast<int>(transfer.size()),
        &transferred,
        static_cast<unsigned int>(m_descriptor.defaultTimeout.count()));
    if (code != LIBUSB_SUCCESS) {
        return std::unexpected(toError(code, "bulk write"));
    }
    if (transferred != transfer.size()) {
        return fail(ErrorCode::TransportFailure, "short USBTMC write");
    }
    return {};
}

Result<std::vector<std::byte>> UsbTmcTransport::readBinaryBlock(std::chrono::milliseconds timeout,
                                                                const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }

    constexpr std::uint32_t ChunkSize = 16384;
    std::vector<std::byte> payload;
    for (;;) {
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "read cancelled");
        }

        m_impl->tag = usbtmc::nextTag(m_impl->tag);
        const QByteArray request = usbtmc::encodeRequestIn(m_impl->tag, ChunkSize, '\n', false);
        int transferred = 0;
        int code = libusb_bulk_transfer(
            m_impl->handle,
            m_impl->layout.bulkOut,
            reinterpret_cast<unsigned char*>(const_cast<char*>(request.constData())),
            static_cast<int>(request.size()),
            &transferred,
            static_cast<unsigned int>(timeout.count()));
        if (code != LIBUSB_SUCCESS) {
            return std::unexpected(toError(code, "bulk request"));
        }

        std::vector<unsigned char> buffer(usbtmc::HeaderSize + ChunkSize + 3);
        code = libusb_bulk_transfer(m_impl->handle,
                                    m_impl->layout.bulkIn,
                                    buffer.data(),
                                    static_cast<int>(buffer.size()),
                                    &transferred,
                                    static_cast<unsigned int>(timeout.count()));
        if (code != LIBUSB_SUCCESS) {
            return std::unexpected(toError(code, "bulk read"));
        }

        const auto received =
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(buffer.data()),
                                       static_cast<std::size_t>(transferred)};
        auto header = usbtmc::parseMessageIn(received, m_impl->tag);
        if (!header) {
            return std::unexpected(header.error());
        }

        const auto available = std::min<std::size_t>(
            header->transferSize, static_cast<std::size_t>(transferred) - usbtmc::HeaderSize);
        payload.insert(payload.end(),
                       received.begin() + usbtmc::HeaderSize,
                       received.begin() + usbtmc::HeaderSize +
                           static_cast<std::ptrdiff_t>(available));

        if (header->endOfMessage) {
            break;
        }
    }
    return payload;
}

Result<std::string> UsbTmcTransport::read(std::chrono::milliseconds timeout,
                                          const CancelToken& cancel)
{
    auto payload = readBinaryBlock(timeout, cancel);
    if (!payload) {
        return std::unexpected(payload.error());
    }

    std::string text;
    text.reserve(payload->size());
    for (const auto byte : *payload) {
        text.push_back(static_cast<char>(byte));
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    qCDebug(lcScpi).noquote() << "<" << QString::fromStdString(text);
    return text;
}

void UsbTmcTransport::clear()
{
    if (!isOpen()) {
        return;
    }
    // INITIATE_CLEAR on the control endpoint drops whatever the instrument had
    // queued in either direction. The three constants are separate libusb enum
    // types, and combining those directly is deprecated in C++23, so each is
    // widened to the byte the field actually is.
    constexpr std::uint8_t RequestType = static_cast<std::uint8_t>(LIBUSB_ENDPOINT_IN) |
                                         static_cast<std::uint8_t>(LIBUSB_REQUEST_TYPE_CLASS) |
                                         static_cast<std::uint8_t>(LIBUSB_RECIPIENT_INTERFACE);
    const int code =
        libusb_control_transfer(m_impl->handle,
                                RequestType,
                                usbtmc::RequestInitiateClear,
                                0,
                                static_cast<std::uint16_t>(m_impl->layout.interfaceNumber),
                                nullptr,
                                0,
                                static_cast<unsigned int>(m_descriptor.defaultTimeout.count()));
    if (code < 0) {
        qCDebug(lcTransport) << "USBTMC clear failed:" << libusb_strerror(code);
    }
}

#else // PEAKEMI_HAVE_LIBUSB

struct UsbTmcTransport::Impl
{};

bool UsbTmcTransport::isSupported()
{
    return false;
}

Result<std::vector<UsbTmcDevice>> UsbTmcTransport::enumerate()
{
    return fail(ErrorCode::NotImplemented,
                "this build has no USB support; configure with -DPEAKEMI_WITH_USBTMC=ON");
}

UsbTmcTransport::UsbTmcTransport(TransportDescriptor descriptor)
    : m_descriptor{std::move(descriptor)}
{}

UsbTmcTransport::~UsbTmcTransport() = default;

Status UsbTmcTransport::open()
{
    return fail(ErrorCode::NotImplemented, "this build has no USB support");
}

bool UsbTmcTransport::isOpen() const
{
    return false;
}

void UsbTmcTransport::close() {}

Status UsbTmcTransport::write(std::string_view /*command*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no USB support");
}

Result<std::string> UsbTmcTransport::read(std::chrono::milliseconds /*timeout*/,
                                          const CancelToken& /*cancel*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no USB support");
}

Result<std::vector<std::byte>>
UsbTmcTransport::readBinaryBlock(std::chrono::milliseconds /*timeout*/,
                                 const CancelToken& /*cancel*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no USB support");
}

void UsbTmcTransport::clear() {}

#endif // PEAKEMI_HAVE_LIBUSB

} // namespace peakemi::hal
