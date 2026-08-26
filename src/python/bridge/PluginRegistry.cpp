#include "bridge/PythonIncludes.h"

#include <peakemi/core/Logging.h>
#include <peakemi/hal/DriverRegistry.h>
#include <peakemi/python/PluginRegistry.h>
#include <peakemi/python/PythonDriverProxy.h>
#include <peakemi/python/PythonInterpreter.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <type_traits>
#include <utility>

#ifdef PEAKEMI_HAVE_PYTHON
#    include "bindings/PluginModule.h"

namespace py = pybind11;
#endif

namespace peakemi::python {
namespace {

#ifdef PEAKEMI_HAVE_PYTHON

/// Owns a Python object on behalf of a C++ callable.
///
/// Releasing a py::object runs interpreter code and needs the GIL, and either
/// can throw. A driver factory lives in the registry as a std::function whose
/// destruction happens at an arbitrary later moment -- so capturing a
/// py::object directly would put a throwing destructor there. The holder does
/// the release safely, and callables capture a shared_ptr to it.
class PythonObjectHolder
{
public:
    explicit PythonObjectHolder(py::object object) : m_object{std::move(object)} {}

    ~PythonObjectHolder()
    {
        try {
            if (PythonInterpreter::instance().isRunning()) {
                const py::gil_scoped_acquire gil;
                m_object = py::object{};
            } else {
                // The interpreter is gone; the object died with it.
                m_object.release();
            }
        } catch (const std::exception& error) {
            qCWarning(lcDriver) << "releasing a plugin object failed:" << error.what();
        } catch (...) {
            qCWarning(lcDriver) << "releasing a plugin object failed";
        }
    }

    PythonObjectHolder(const PythonObjectHolder&) = delete;
    PythonObjectHolder& operator=(const PythonObjectHolder&) = delete;
    PythonObjectHolder(PythonObjectHolder&&) = delete;
    PythonObjectHolder& operator=(PythonObjectHolder&&) = delete;

    [[nodiscard]] const py::object& get() const { return m_object; }

private:
    py::object m_object;
};

/// Import one file as a module of its own, without putting its directory on
/// sys.path: two plugins may legitimately share a helper file name.
[[nodiscard]] py::object importFile(const QString& path)
{
    const auto util = py::module_::import("importlib.util");
    const QString name = QStringLiteral("peakemi_plugin_") + QFileInfo{path}.completeBaseName();
    auto spec = util.attr("spec_from_file_location")(name.toStdString(), path.toStdString());
    if (spec.is_none()) {
        throw std::runtime_error("no import machinery accepts " + path.toStdString());
    }
    auto module = util.attr("module_from_spec")(spec);
    spec.attr("loader").attr("exec_module")(module);
    return module;
}
#endif

} // namespace

QString DiscoveredPlugin::displayName() const
{
    return manifest.name.empty() ? QFileInfo{path}.fileName()
                                 : QString::fromStdString(manifest.name);
}

struct PluginRegistry::Impl
{
#ifdef PEAKEMI_HAVE_PYTHON
    /// Keeps imported modules alive for as long as their drivers may be used.
    std::vector<py::object> modules;
    std::vector<Registration> registrations;
#endif
};

PluginRegistry::PluginRegistry() : m_impl{std::make_unique<Impl>()}, m_searchPaths{searchPaths()} {}

PluginRegistry::~PluginRegistry()
{
#ifdef PEAKEMI_HAVE_PYTHON
    // Dropping module and class references runs Python destructors, which can
    // raise; a destructor must not let that escape.
    try {
        if (PythonInterpreter::instance().isRunning()) {
            const py::gil_scoped_acquire gil;
            m_impl->registrations.clear();
            m_impl->modules.clear();
        }
    } catch (const std::exception& error) {
        qCWarning(lcDriver) << "releasing the loaded plugins failed:" << error.what();
    } catch (...) {
        qCWarning(lcDriver) << "releasing the loaded plugins failed";
    }
#endif
}

bool PluginRegistry::isSupported()
{
#ifdef PEAKEMI_HAVE_PYTHON
    return true;
#else
    return false;
#endif
}

QString PluginRegistry::userPluginDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/plugins/drivers");
}

QStringList PluginRegistry::searchPaths()
{
    // The system directory ships with the application and is read-only; the
    // user directory is where people drop their own drivers (FR-EXT-2).
    QStringList paths;
    paths << QCoreApplication::applicationDirPath() +
                 QStringLiteral("/../share/peakemi/plugins/drivers");
    paths << userPluginDirectory();
    return paths;
}

void PluginRegistry::setSearchPaths(QStringList paths)
{
    m_searchPaths = std::move(paths);
}

void PluginRegistry::rescan()
{
    m_plugins.clear();
    if (auto status = m_trust.load(); !status && status.error().code != ErrorCode::IoFailure) {
        qCWarning(lcDriver).noquote() << QString::fromStdString(status.error().message());
    }

    for (const auto& directory : m_searchPaths) {
        QDir folder{directory};
        if (!folder.exists()) {
            continue;
        }
        for (const auto& entry :
             folder.entryInfoList({QStringLiteral("*.py")}, QDir::Files, QDir::Name))
        {
            DiscoveredPlugin plugin;
            plugin.path = entry.absoluteFilePath();

            switch (m_trust.state(plugin.path)) {
                case TrustState::Trusted:
                    loadPlugin(plugin);
                    break;
                case TrustState::Changed:
                    plugin.state = PluginState::AwaitingApproval;
                    plugin.lastError =
                        "the file has changed since it was approved; approve it again to load it";
                    break;
                case TrustState::Unknown:
                    plugin.state = PluginState::AwaitingApproval;
                    plugin.lastError = "not approved yet; PeakEmi never imports a plugin the user "
                                       "has not confirmed";
                    break;
            }
            m_plugins.push_back(std::move(plugin));
        }
    }
    qCInfo(lcDriver) << "plugin scan found" << m_plugins.size() << "file(s)";
}

void PluginRegistry::loadPlugin(DiscoveredPlugin& plugin)
{
#ifdef PEAKEMI_HAVE_PYTHON
    if (auto started = PythonInterpreter::instance().ensureStarted(); !started) {
        plugin.state = PluginState::Failed;
        plugin.lastError = started.error().message();
        return;
    }

    const py::gil_scoped_acquire gil;
    pendingRegistrations().clear();
    try {
        auto module = importFile(plugin.path);

        auto& registrations = pendingRegistrations();
        if (registrations.empty()) {
            plugin.state = PluginState::Rejected;
            plugin.lastError = "the file registered no driver; decorate a class with "
                               "peakemi_plugin.register_driver(manifest)";
            return;
        }

        // A file may register several drivers; the first one names the plugin.
        for (auto& registration : registrations) {
            registration.origin = plugin.path.toStdString();
            if (auto valid = validateManifest(registration.manifest); !valid) {
                plugin.state = PluginState::Rejected;
                plugin.lastError = valid.error().message();
                return;
            }
        }

        plugin.manifest = registrations.front().manifest;
        m_impl->modules.push_back(std::move(module));
        m_impl->registrations.insert(
            m_impl->registrations.end(), registrations.begin(), registrations.end());
        registrations.clear();
        plugin.state = PluginState::Loaded;
        plugin.lastError.clear();
        qCInfo(lcDriver) << "plugin loaded:" << plugin.displayName();
    } catch (const py::error_already_set& error) {
        plugin.state = PluginState::Failed;
        plugin.lastError = error.what();
    } catch (const std::exception& error) {
        plugin.state = PluginState::Failed;
        plugin.lastError = error.what();
    }
#else
    plugin.state = PluginState::Failed;
    plugin.lastError = "this build has no embedded Python; configure with "
                       "-DPEAKEMI_WITH_PYTHON=ON";
#endif
}

Status PluginRegistry::approveAndLoad(const QString& path)
{
    if (auto approved = m_trust.approve(path); !approved) {
        return approved;
    }
    rescan();
    publishToDriverRegistry();
    return {};
}

void PluginRegistry::publishToDriverRegistry()
{
#ifdef PEAKEMI_HAVE_PYTHON
    auto& registry = hal::DriverRegistry::instance();
    for (const auto& registration : m_impl->registrations) {
        // Not const: a const captured entity becomes a const member of the
        // closure, and "moving" a const member copies it. That copy allocates,
        // so the closure's move constructor could throw -- inside a
        // std::function the registry moves around. The assertions below hold
        // only because these are non-const.
        auto manifest = registration.manifest;
        auto origin = registration.origin;
        auto driverClass = std::make_shared<PythonObjectHolder>(registration.driverClass);

        // Named rather than written inline: a driver entry is three separate
        // decisions -- what the driver is, when it claims an instrument, and
        // how it is built -- and each reads better with a name on it.
        const DriverInfo info{.id = "python." + manifest.name,
                              .name = manifest.name,
                              .vendor = manifest.vendor,
                              .version = manifest.apiVersion,
                              .origin = origin,
                              .supportedTransports = {TransportKind::Tcp,
                                                      TransportKind::Vxi11,
                                                      TransportKind::UsbTmc,
                                                      TransportKind::Serial}};

        auto matcher = [manifest](const InstrumentId& identity) {
            return scoreManifest(manifest, identity);
        };

        auto factory = [manifest, origin, driverClass]() -> DriverPtr {
            try {
                const py::gil_scoped_acquire gil;
                auto instance = driverClass->get()();
                // The proxy takes a borrowed reference and keeps the object
                // alive itself, so the handle is released here.
                auto proxy = std::make_shared<PythonDriverProxy>(manifest, instance.ptr(), origin);
                instance.release();
                return proxy;
            } catch (const std::exception& error) {
                qCWarning(lcDriver) << "cannot instantiate" << QString::fromStdString(manifest.name)
                                    << ":" << error.what();
                return nullptr;
            } catch (...) {
                qCWarning(lcDriver)
                    << "cannot instantiate" << QString::fromStdString(manifest.name);
                return nullptr;
            }
        };

        // Both closures end up inside std::function, which moves them. These
        // assertions are what keeps that move from silently becoming a
        // throwing copy again if someone adds a const capture later.
        static_assert(std::is_nothrow_destructible_v<decltype(matcher)>);
        static_assert(std::is_nothrow_move_constructible_v<decltype(matcher)>);
        static_assert(std::is_nothrow_destructible_v<decltype(factory)>);
        static_assert(std::is_nothrow_move_constructible_v<decltype(factory)>);

        registry.registerDriver(hal::DriverRegistry::Entry{
            .info = info, .matcher = std::move(matcher), .factory = std::move(factory)});
    }
#endif
}

} // namespace peakemi::python
