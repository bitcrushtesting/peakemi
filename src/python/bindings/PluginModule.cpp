#include "bindings/PluginModule.h"

#ifdef PEAKEMI_HAVE_PYTHON

#    include <peakemi/core/Capabilities.h>
#    include <peakemi/core/ITransport.h>
#    include <peakemi/core/SweepParams.h>
#    include <peakemi/core/Trace.h>

#    include <string>
#    include <vector>

namespace py = pybind11;

namespace peakemi::python {

std::vector<Registration>& pendingRegistrations()
{
    static std::vector<Registration> registrations;
    return registrations;
}

namespace {

/// Read a manifest out of the dictionary a plugin passes to the decorator.
[[nodiscard]] PluginManifest manifestFromDict(const py::dict& source)
{
    const auto text = [&source](const char* key) -> std::string {
        return source.contains(key) ? py::str(source[key]).cast<std::string>() : std::string{};
    };

    PluginManifest manifest;
    manifest.name = text("name");
    manifest.vendor = text("vendor");
    manifest.author = text("author");
    manifest.licence = source.contains("licence") ? text("licence") : text("license");
    manifest.description = text("description");
    manifest.apiVersion = source.contains("api_version") ? text("api_version") : text("apiVersion");
    if (source.contains("models")) {
        manifest.models = source["models"].cast<std::vector<std::string>>();
    }
    return manifest;
}

} // namespace

/// The module a plugin imports. Embedded rather than installed: plugins run
/// inside PeakEmi and there is no separate Python package to keep in step.
PYBIND11_EMBEDDED_MODULE(peakemi_plugin, module)
{
    module.doc() = "PeakEmi driver plugin API. See docs/plugin-api.md.";
    module.attr("PLUGIN_API_VERSION") = std::string{PluginApiVersion};

    py::enum_<Detector>(module, "Detector")
        .value("PEAK", Detector::Peak)
        .value("QUASI_PEAK", Detector::QuasiPeak)
        .value("AVERAGE", Detector::Average)
        .value("RMS", Detector::Rms)
        .value("SAMPLE", Detector::Sample);

    py::enum_<AmplitudeUnit>(module, "AmplitudeUnit")
        .value("DBM", AmplitudeUnit::dBm)
        .value("DBUV", AmplitudeUnit::dBuV)
        .value("DBUV_PER_M", AmplitudeUnit::dBuV_per_m)
        .value("DBUA", AmplitudeUnit::dBuA);

    py::class_<FrequencyRange>(module, "FrequencyRange")
        .def(py::init([](std::int64_t start, std::int64_t stop) {
                 return FrequencyRange{hertz(start), hertz(stop)};
             }),
             py::arg("start_hz"),
             py::arg("stop_hz"))
        .def_property_readonly("start_hz",
                               [](const FrequencyRange& range) { return range.start.value(); })
        .def_property_readonly("stop_hz",
                               [](const FrequencyRange& range) { return range.stop.value(); })
        .def("__repr__", [](const FrequencyRange& range) {
            return "FrequencyRange(" + std::to_string(range.start.value()) + ", " +
                   std::to_string(range.stop.value()) + ")";
        });

    py::class_<SweepParams>(module, "SweepParams")
        .def(py::init<>())
        .def_readwrite("span", &SweepParams::span)
        .def_property(
            "rbw_hz",
            [](const SweepParams& params) { return params.rbw.value(); },
            [](SweepParams& params, std::int64_t value) { params.rbw = hertz(value); })
        .def_property(
            "vbw_hz",
            [](const SweepParams& params) { return params.vbw.value(); },
            [](SweepParams& params, std::int64_t value) { params.vbw = hertz(value); })
        .def_readwrite("detector", &SweepParams::detector)
        .def_readwrite("points", &SweepParams::points)
        .def_property(
            "ref_level_db",
            [](const SweepParams& params) { return params.refLevel.value(); },
            [](SweepParams& params, double value) { params.refLevel = decibel(value); })
        .def_readwrite("preamp", &SweepParams::preamp)
        .def_readwrite("sweep_time", &SweepParams::sweepTime);

    py::class_<Capabilities>(module, "Capabilities")
        .def(py::init<>())
        .def_readwrite("range", &Capabilities::range)
        .def_readwrite("minimum_points", &Capabilities::minimumPoints)
        .def_readwrite("maximum_points", &Capabilities::maximumPoints)
        .def_readwrite("detectors", &Capabilities::detectors)
        .def_readwrite("preamp", &Capabilities::preamp)
        .def_readwrite("zero_span", &Capabilities::zeroSpan)
        .def_readwrite("native_unit", &Capabilities::nativeUnit)
        .def_property(
            "resolution_bandwidths_hz",
            [](const Capabilities& capabilities) {
                std::vector<std::int64_t> values;
                values.reserve(capabilities.resolutionBandwidths.size());
                for (const auto& bandwidth : capabilities.resolutionBandwidths) {
                    values.push_back(bandwidth.value());
                }
                return values;
            },
            [](Capabilities& capabilities, const std::vector<std::int64_t>& values) {
                capabilities.resolutionBandwidths.clear();
                for (const auto value : values) {
                    capabilities.resolutionBandwidths.push_back(hertz(value));
                }
            });

    py::class_<InstrumentId>(module, "InstrumentId")
        .def(py::init<>())
        .def_readwrite("manufacturer", &InstrumentId::manufacturer)
        .def_readwrite("model", &InstrumentId::model)
        .def_readwrite("serial", &InstrumentId::serial)
        .def_readwrite("firmware", &InstrumentId::firmware)
        .def_readwrite("raw", &InstrumentId::raw);

    py::class_<Trace>(module, "Trace")
        .def(py::init<>())
        .def_readwrite("amplitudes", &Trace::amplitudes)
        .def_readwrite("unit", &Trace::unit)
        .def_readwrite("detector", &Trace::detector)
        .def_property(
            "start_hz",
            [](const Trace& trace) { return trace.axis.start.value(); },
            [](Trace& trace, std::int64_t value) { trace.axis.start = hertz(value); })
        .def_property(
            "stop_hz",
            [](const Trace& trace) { return trace.axis.stop.value(); },
            [](Trace& trace, std::int64_t value) { trace.axis.stop = hertz(value); })
        .def_property(
            "points",
            [](const Trace& trace) { return trace.axis.points; },
            [](Trace& trace, int value) { trace.axis.points = value; });

    // The transport a plugin talks through. Every call releases the GIL for the
    // duration of the I/O, so one plugin blocking on an instrument does not
    // freeze the rest of the interpreter (FR-EXT-5).
    py::class_<ITransport, std::shared_ptr<ITransport>>(module, "Transport")
        .def(
            "write",
            [](ITransport& transport, const std::string& command) {
                py::gil_scoped_release released;
                const auto status = transport.write(command);
                if (!status) {
                    py::gil_scoped_acquire acquired;
                    throw std::runtime_error(status.error().message());
                }
            },
            py::arg("command"))
        .def(
            "read",
            [](ITransport& transport, int timeoutMs) {
                std::string response;
                {
                    py::gil_scoped_release released;
                    const CancelToken cancel;
                    auto result = transport.read(std::chrono::milliseconds{timeoutMs}, cancel);
                    if (!result) {
                        py::gil_scoped_acquire acquired;
                        throw std::runtime_error(result.error().message());
                    }
                    response = *result;
                }
                return response;
            },
            py::arg("timeout_ms") = 5000)
        .def(
            "query",
            [](ITransport& transport, const std::string& command, int timeoutMs) {
                std::string response;
                {
                    py::gil_scoped_release released;
                    const CancelToken cancel;
                    auto result =
                        transport.query(command, std::chrono::milliseconds{timeoutMs}, cancel);
                    if (!result) {
                        py::gil_scoped_acquire acquired;
                        throw std::runtime_error(result.error().message());
                    }
                    response = *result;
                }
                return response;
            },
            py::arg("command"),
            py::arg("timeout_ms") = 5000);

    module.def(
        "register_driver",
        [](py::dict manifest) {
            // Used as a decorator: register_driver({...})(class).
            //
            // The init-capture matters: capturing a const entity would make the
            // closure member const, and "moving" a const member copies it --
            // here that copy touches the interpreter and can throw, inside a
            // callable pybind11 moves around.
            return py::cpp_function(
                [manifest = std::move(manifest)](const py::object& driverClass) {
                    pendingRegistrations().push_back(
                        Registration{.manifest = manifestFromDict(manifest),
                                     .driverClass = driverClass,
                                     .origin = {}});
                    return driverClass;
                });
        },
        py::arg("manifest"),
        "Decorator registering a driver class with its manifest.");
}

} // namespace peakemi::python

#endif // PEAKEMI_HAVE_PYTHON
