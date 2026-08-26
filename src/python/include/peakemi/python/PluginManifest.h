#pragma once

#include <peakemi/core/Error.h>
#include <peakemi/core/InstrumentId.h>

#include <string>
#include <string_view>
#include <vector>

namespace peakemi::python {

/// Version of the plugin API this build speaks (FR-EXT-3).
///
/// Bumped when the surface a plugin sees changes incompatibly. A plugin
/// declaring a different major version is rejected and reported, never loaded.
inline constexpr std::string_view PluginApiVersion = "1.0";

/// What a plugin declares about itself.
struct PluginManifest
{
    std::string name;
    std::string vendor;
    std::string author;
    std::string licence;
    std::string description;
    std::string apiVersion;
    /// Regular expressions matched against the model field of *IDN?.
    std::vector<std::string> models;

    [[nodiscard]] bool isEmpty() const { return name.empty(); }

    friend bool operator==(const PluginManifest&, const PluginManifest&) = default;
};

/// Whether a plugin's declared API version can run against this build.
///
/// The major version must match exactly; a plugin written for a later minor
/// version of the same major is accepted, because minor versions only add.
[[nodiscard]] bool isApiVersionCompatible(std::string_view declared);

/// Reject a manifest that would produce a driver nobody can identify or trust.
[[nodiscard]] Status validateManifest(const PluginManifest& manifest);

/// Score this manifest against an identified instrument, using the same scale
/// as the C++ driver registry so both compete fairly (FR-DIS-4).
[[nodiscard]] int scoreManifest(const PluginManifest& manifest, const InstrumentId& identity);

} // namespace peakemi::python
