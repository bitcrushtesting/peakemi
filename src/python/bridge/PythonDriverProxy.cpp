#include "bridge/PythonIncludes.h"

#include <peakemi/core/Logging.h>
#include <peakemi/python/PythonDriverProxy.h>

#include <utility>

#ifdef PEAKEMI_HAVE_PYTHON
namespace py = pybind11;
#endif

namespace peakemi::python {

#ifdef PEAKEMI_HAVE_PYTHON

namespace {

/// Format a Python exception the way a traceback would appear, so a plugin
/// author sees the same text they would in an interpreter (FR-EXT-4).
[[nodiscard]] std::string formatException(const py::error_already_set& error)
{
    try {
        const py::gil_scoped_acquire gil;
        const auto traceback = py::module_::import("traceback");
        const auto lines =
            traceback.attr("format_exception")(error.type(), error.value(), error.trace());
        std::string text;
        for (const auto& line : lines) {
            text += line.cast<std::string>();
        }
        return text.empty() ? std::string{error.what()} : text;
    } catch (const std::exception&) {
        return std::string{error.what()};
    }
}

} // namespace

struct PythonDriverProxy::Impl
{
    PluginManifest manifest;
    py::object driver;
    std::string origin;
    TransportPtr transport;
    Capabilities capabilities;
    std::chrono::milliseconds timeout{5000};
    bool open{false};
    std::vector<InstrumentError> errors;

    /// Call a method on the plugin, turning any failure into an Error.
    template<class Result>
    [[nodiscard]] std::expected<Result, Error> call(const char* method, auto&&... arguments)
    {
        try {
            const py::gil_scoped_acquire gil;
            if (!py::hasattr(driver, method)) {
                return std::unexpected(Error{ErrorCode::NotImplemented,
                                             manifest.name + " has no method '" + method + "'"});
            }
            auto value = driver.attr(method)(std::forward<decltype(arguments)>(arguments)...);
            if constexpr (std::is_void_v<Result>) {
                return {};
            } else {
                return value.template cast<Result>();
            }
        } catch (const py::error_already_set& error) {
            return std::unexpected(
                Error{ErrorCode::InstrumentError, manifest.name + ": " + formatException(error)});
        } catch (const std::exception& error) {
            return std::unexpected(
                Error{ErrorCode::InstrumentError, manifest.name + ": " + error.what()});
        }
    }
};

PythonDriverProxy::PythonDriverProxy(PluginManifest manifest,
                                     void* driverInstance,
                                     std::string origin)
    : m_impl{std::make_unique<Impl>()}
{
    m_impl->manifest = std::move(manifest);
    m_impl->origin = std::move(origin);
    const py::gil_scoped_acquire gil;
    m_impl->driver = py::reinterpret_borrow<py::object>(static_cast<PyObject*>(driverInstance));
}

PythonDriverProxy::~PythonDriverProxy()
{
    // Closing calls into the plugin and releasing the object touches the
    // interpreter; either can raise, and neither may escape a destructor.
    try {
        PythonDriverProxy::close();
        const py::gil_scoped_acquire gil;
        m_impl->driver = py::object{};
    } catch (const std::exception& error) {
        qCWarning(lcDriver) << "releasing the plugin driver failed:" << error.what();
    } catch (...) {
        qCWarning(lcDriver) << "releasing the plugin driver failed";
    }
}

DriverInfo PythonDriverProxy::info() const
{
    return DriverInfo{.id = "python." + m_impl->manifest.name,
                      .name = m_impl->manifest.name,
                      .vendor = m_impl->manifest.vendor,
                      .version = m_impl->manifest.apiVersion,
                      .origin = m_impl->origin,
                      .supportedTransports = {TransportKind::Tcp,
                                              TransportKind::Vxi11,
                                              TransportKind::UsbTmc,
                                              TransportKind::Serial}};
}

Capabilities PythonDriverProxy::capabilities() const
{
    auto reported = m_impl->call<Capabilities>("capabilities");
    if (!reported) {
        qCWarning(lcDriver).noquote() << QString::fromStdString(reported.error().message());
        return m_impl->capabilities;
    }
    m_impl->capabilities = *reported;
    return *reported;
}

Status PythonDriverProxy::open(TransportPtr transport)
{
    m_impl->transport = std::move(transport);
    auto opened = m_impl->call<void>("open", m_impl->transport);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    m_impl->open = true;
    return {};
}

bool PythonDriverProxy::isOpen() const
{
    return m_impl->open;
}

void PythonDriverProxy::close()
{
    if (!m_impl->open) {
        return;
    }
    if (auto closed = m_impl->call<void>("close"); !closed) {
        qCWarning(lcDriver).noquote() << QString::fromStdString(closed.error().message());
    }
    m_impl->open = false;
    m_impl->transport.reset();
}

Result<InstrumentId> PythonDriverProxy::identify()
{
    return m_impl->call<InstrumentId>("identify");
}

Status PythonDriverProxy::configureSweep(const SweepParams& params)
{
    return m_impl->call<void>("configure_sweep", params);
}

Status PythonDriverProxy::armAndTrigger(const CancelToken& cancel)
{
    if (cancel.isCancelled()) {
        return fail(ErrorCode::Cancelled, "sweep aborted");
    }
    return m_impl->call<void>("arm_and_trigger");
}

Result<Trace> PythonDriverProxy::fetchTrace(const CancelToken& cancel)
{
    if (cancel.isCancelled()) {
        return fail(ErrorCode::Cancelled, "trace fetch aborted");
    }
    return m_impl->call<Trace>("fetch_trace");
}

void PythonDriverProxy::abort()
{
    // Best effort: a plugin need not implement abort, and a failure here must
    // not throw across the port boundary.
    if (auto aborted = m_impl->call<void>("abort"); !aborted) {
        qCDebug(lcDriver).noquote() << QString::fromStdString(aborted.error().message());
    }
}

std::vector<InstrumentError> PythonDriverProxy::lastErrors()
{
    return std::exchange(m_impl->errors, {});
}

void PythonDriverProxy::setTimeout(std::chrono::milliseconds timeout)
{
    m_impl->timeout = timeout;
    if (auto applied = m_impl->call<void>("set_timeout", static_cast<int>(timeout.count()));
        !applied)
    {
        qCDebug(lcDriver).noquote() << QString::fromStdString(applied.error().message());
    }
}

#else // PEAKEMI_HAVE_PYTHON

struct PythonDriverProxy::Impl
{
    PluginManifest manifest;
    std::string origin;
};

PythonDriverProxy::PythonDriverProxy(PluginManifest manifest,
                                     void* /*driverInstance*/,
                                     std::string origin)
    : m_impl{std::make_unique<Impl>()}
{
    m_impl->manifest = std::move(manifest);
    m_impl->origin = std::move(origin);
}

PythonDriverProxy::~PythonDriverProxy() = default;

DriverInfo PythonDriverProxy::info() const
{
    return DriverInfo{.id = "python." + m_impl->manifest.name,
                      .name = m_impl->manifest.name,
                      .vendor = m_impl->manifest.vendor,
                      .version = m_impl->manifest.apiVersion,
                      .origin = m_impl->origin,
                      .supportedTransports = {}};
}

Capabilities PythonDriverProxy::capabilities() const
{
    return {};
}

Status PythonDriverProxy::open(TransportPtr /*transport*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no embedded Python");
}

bool PythonDriverProxy::isOpen() const
{
    return false;
}

void PythonDriverProxy::close() {}

Result<InstrumentId> PythonDriverProxy::identify()
{
    return fail(ErrorCode::NotImplemented, "this build has no embedded Python");
}

Status PythonDriverProxy::configureSweep(const SweepParams& /*params*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no embedded Python");
}

Status PythonDriverProxy::armAndTrigger(const CancelToken& /*cancel*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no embedded Python");
}

Result<Trace> PythonDriverProxy::fetchTrace(const CancelToken& /*cancel*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no embedded Python");
}

void PythonDriverProxy::abort() {}

std::vector<InstrumentError> PythonDriverProxy::lastErrors()
{
    return {};
}

void PythonDriverProxy::setTimeout(std::chrono::milliseconds /*timeout*/) {}

#endif // PEAKEMI_HAVE_PYTHON

} // namespace peakemi::python
