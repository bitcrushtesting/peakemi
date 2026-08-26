"""A worked example of a PeakEmi driver plugin.

It drives nothing real: it synthesises a spectrum so the plugin API can be
tried, and read, without hardware. Copy this file, keep the shape, and replace
the bodies with SCPI your instrument understands.

Drop a copy in the user plugin directory, then approve it in
Plugins -> Manage plugins. PeakEmi never imports a plugin you have not
approved, because a plugin runs with your full privileges.

See docs/plugin-api.md for the full specification.
"""

import math
import random

import peakemi_plugin as api

MANIFEST = {
    "name": "Example sweeper",
    "vendor": "PeakEmi",
    "author": "PeakEmi contributors",
    "licence": "GPL-3.0-or-later",
    "description": "Synthetic driver demonstrating the plugin API.",
    "api_version": api.PLUGIN_API_VERSION,
    # Matched against the model field of *IDN?, case-insensitively.
    "models": [r"Example.*", r"Sweeper\d+"],
}


@api.register_driver(MANIFEST)
class ExampleSweeper:
    """The methods PeakEmi calls. All of them are optional except the ones
    marked required; a missing method is reported as an error, never a crash."""

    def __init__(self):
        self._transport = None
        self._params = api.SweepParams()
        self._timeout_ms = 5000

    # --- lifecycle ---------------------------------------------------------

    def open(self, transport):
        """Required. Called once with the transport PeakEmi opened for you.

        Talk to the instrument with transport.query(), .write() and .read();
        each releases the interpreter lock while the I/O is in flight.
        """
        self._transport = transport

    def close(self):
        self._transport = None

    def set_timeout(self, timeout_ms):
        self._timeout_ms = timeout_ms

    # --- identity and capabilities ----------------------------------------

    def identify(self):
        """Required. Return an InstrumentId. A real driver would parse *IDN?:

            raw = self._transport.query("*IDN?", self._timeout_ms)
        """
        identity = api.InstrumentId()
        identity.manufacturer = "PeakEmi"
        identity.model = "Example Sweeper"
        identity.serial = "PLUGIN-0001"
        identity.firmware = "1.0"
        identity.raw = "PeakEmi,Example Sweeper,PLUGIN-0001,1.0"
        return identity

    def capabilities(self):
        """Required. Declare what the instrument can do; PeakEmi validates
        every sweep against this before sending anything (FR-HAL-3)."""
        capabilities = api.Capabilities()
        capabilities.range = api.FrequencyRange(9_000, 3_000_000_000)
        capabilities.minimum_points = 101
        capabilities.maximum_points = 4001
        capabilities.detectors = [
            api.Detector.PEAK,
            api.Detector.QUASI_PEAK,
            api.Detector.AVERAGE,
        ]
        capabilities.resolution_bandwidths_hz = [200, 9_000, 120_000, 1_000_000]
        capabilities.preamp = True
        capabilities.zero_span = True
        capabilities.native_unit = api.AmplitudeUnit.DBUV
        return capabilities

    # --- acquisition -------------------------------------------------------

    def configure_sweep(self, params):
        """Required. Apply the sweep the engine asked for."""
        self._params = params

    def arm_and_trigger(self):
        """Required. Start one sweep and wait for it to finish."""

    def fetch_trace(self):
        """Required. Return the trace the instrument just measured."""
        points = self._params.points
        start = self._params.span.start_hz
        stop = self._params.span.stop_hz

        # Deterministic for a given span, so a repeated run is comparable.
        generator = random.Random((start, stop, points).__hash__())
        amplitudes = []
        for index in range(points):
            frequency = start + (stop - start) * index / max(points - 1, 1)
            noise = 18.0 + generator.gauss(0.0, 1.0)
            emitter = 40.0 * math.exp(-(((frequency - 145_000_000) / 200_000.0) ** 2))
            amplitudes.append(20.0 * math.log10(10 ** (noise / 20) + 10 ** (emitter / 20)))

        trace = api.Trace()
        trace.start_hz = start
        trace.stop_hz = stop
        trace.points = points
        trace.amplitudes = amplitudes
        trace.unit = api.AmplitudeUnit.DBUV
        trace.detector = self._params.detector
        return trace

    def abort(self):
        """Optional. Called from another thread when the user aborts a run."""
