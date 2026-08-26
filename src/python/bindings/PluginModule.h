#pragma once

#include "bridge/PythonIncludes.h"

#include <peakemi/python/PluginManifest.h>

#ifdef PEAKEMI_HAVE_PYTHON

#    include <string>
#    include <vector>

namespace peakemi::python {

/// One driver a plugin registered through the decorator.
struct Registration
{
    PluginManifest manifest;
    pybind11::object driverClass;
    /// File the registration came from, filled in by the registry.
    std::string origin;
};

/// Registrations made since the last call, in order. The registry drains this
/// after importing each plugin file, so a file that registers nothing is
/// reported as such instead of silently doing nothing.
[[nodiscard]] std::vector<Registration>& pendingRegistrations();

} // namespace peakemi::python
#endif
