#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.h>
#include <peakemi/python/PluginManifest.h>
#include <peakemi/python/TrustStore.h>

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

namespace peakemi::python {

/// What became of one plugin file (FR-EXT-6).
enum class PluginState : std::uint8_t
{
    /// Found, but the user has not approved this file's contents yet.
    AwaitingApproval,
    /// Imported and registered a driver.
    Loaded,
    /// Refused: incompatible API version or an invalid manifest.
    Rejected,
    /// Import raised, timed out or violated the protocol.
    Failed
};

struct DiscoveredPlugin
{
    QString path;
    PluginState state{PluginState::AwaitingApproval};
    PluginManifest manifest;
    /// Failure text, including the Python traceback where there was one.
    std::string lastError;

    [[nodiscard]] QString displayName() const;
};

/// Finds, vets and loads Python driver plugins (FR-EXT-2/3/4/6).
///
/// Discovery is by import and registration: a plugin registers itself through
/// the decorator in the `peakemi_plugin` module. Nothing is imported until the
/// user has approved that exact file (NFR-EXT-1), and a plugin that raises,
/// hangs or misbehaves is recorded as failed rather than taking the
/// application with it (FR-EXT-4).
class PluginRegistry
{
public:
    PluginRegistry();
    ~PluginRegistry();

    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;
    PluginRegistry(PluginRegistry&&) = delete;
    PluginRegistry& operator=(PluginRegistry&&) = delete;

    /// Whether this build embeds Python at all.
    [[nodiscard]] static bool isSupported();

    /// Directories searched, in order: the read-only system directory first,
    /// then the user's own plugin directory.
    [[nodiscard]] static QStringList searchPaths();
    [[nodiscard]] static QString userPluginDirectory();

    void setSearchPaths(QStringList paths);

    /// Look for plugins and import the approved ones. Safe to call repeatedly:
    /// the plugin manager's rescan button does exactly this (FR-EXT-6).
    void rescan();

    [[nodiscard]] const std::vector<DiscoveredPlugin>& plugins() const { return m_plugins; }

    [[nodiscard]] TrustStore& trustStore() { return m_trust; }

    /// Approve a file and import it, in one step.
    [[nodiscard]] Status approveAndLoad(const QString& path);

    /// Register every loaded plugin with the process-wide driver registry, so
    /// Python drivers compete with the C++ ones on the same terms.
    void publishToDriverRegistry();

private:
    struct Impl;

    void loadPlugin(DiscoveredPlugin& plugin);

    std::unique_ptr<Impl> m_impl;
    TrustStore m_trust;
    QStringList m_searchPaths;
    std::vector<DiscoveredPlugin> m_plugins;
};

} // namespace peakemi::python
