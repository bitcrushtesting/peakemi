# PeakEmi — Driver Plugin API

| Field | Value |
|---|---|
| Document | Plugin API specification |
| API version | 1.0 |
| Status | Stable within major version 1 |
| Related | [architecture.md §6](architecture.md#6-python-bridge-peakemi_python-optional) · [requirements.md §2.2](requirements.md#22-dual-language-extensibility) |

A PeakEmi driver plugin is a single Python file that registers a class. PeakEmi
imports it into its own embedded interpreter, wraps the class in a
`PythonDriverProxy`, and from then on the measurement engine cannot tell it from
a driver written in C++.

A complete, working example ships in
[`plugins/drivers/example_sweeper.py`](../plugins/drivers/example_sweeper.py).

## 1. Trust

**Plugins run with your full user privileges.** PeakEmi therefore never imports
a plugin you have not explicitly approved (NFR-EXT-1):

* approve a file in **Run → Manage driver plugins…**;
* approval covers *that file's contents*, recorded as a SHA-256 hash. Editing an
  approved plugin withdraws the approval until you approve it again;
* PeakEmi never downloads or installs plugins by itself.

Approvals are stored in `plugin-trust.json` in the application's configuration
directory.

## 2. Where plugins live

| Directory | Purpose |
|---|---|
| `<install>/share/peakemi/plugins/drivers` | Ships with the application, read-only |
| `<user-data>/plugins/drivers` | Your own drivers — the **Open plugin folder** button opens it |

Every `*.py` file directly inside those directories is a candidate. Each file is
imported under its own module name, so two plugins may use the same helper file
names without colliding.

## 3. Registering a driver

```python
import peakemi_plugin as api

MANIFEST = {
    "name": "Acme SA1000",            # required, shown in the UI
    "vendor": "Acme",                 # matched against the *IDN? manufacturer
    "models": [r"SA10\d\d"],          # required, regular expressions
    "api_version": api.PLUGIN_API_VERSION,   # required
    "author": "You",
    "licence": "GPL-3.0-or-later",
    "description": "One line about the driver.",
}

@api.register_driver(MANIFEST)
class AcmeSA1000:
    ...
```

A file may register more than one class. A file that registers none is reported
as rejected rather than silently ignored.

### Version compatibility

`api_version` is `"major.minor"`. The **major** version must equal the one the
running PeakEmi speaks, or the plugin is rejected and the reason is shown in the
plugin manager (FR-EXT-3). Minor versions only ever add, so a plugin written for
`1.0` keeps working against `1.7`.

### How a driver is chosen

When an instrument answers `*IDN?`, every registered driver — C++ and Python —
scores itself against the reply, and the highest score wins (FR-DIS-4):

| Score | Meaning |
|---|---|
| 100 | A literal pattern naming exactly this model |
| 70 | A regular expression matching this model |
| 30 | The vendor matches, the model does not |
| 0 | Not mine |

A tie surfaces a manual driver-selection dialog; it never picks silently.

## 4. The driver class

PeakEmi calls these methods. The five marked **required** must exist; a missing
method is reported as an error against that plugin, never as a crash.

| Method | Required | Purpose |
|---|---|---|
| `open(transport)` | yes | Called once with an open `Transport`. |
| `close()` | no | Release whatever `open` acquired. |
| `identify()` | yes | Return an `InstrumentId`. |
| `capabilities()` | yes | Return a `Capabilities`; every sweep is validated against it. |
| `configure_sweep(params)` | yes | Apply a `SweepParams`. |
| `arm_and_trigger()` | yes | Start one sweep and wait for it. |
| `fetch_trace()` | yes | Return a `Trace`. |
| `abort()` | no | Called from another thread when the user aborts. |
| `set_timeout(timeout_ms)` | no | Timeout for subsequent operations. |

Raising is a normal way to report failure: the exception, with its traceback,
becomes the error PeakEmi shows and logs (FR-EXT-4).

## 5. The `peakemi_plugin` module

### `Transport`

```python
transport.write("*RST")                      # send a command
transport.read(timeout_ms=5000)              # read one response
transport.query("*IDN?", timeout_ms=5000)    # write then read
```

Each call releases the interpreter lock while the I/O is in flight, so a plugin
waiting on an instrument does not block the rest of the interpreter (FR-EXT-5).
A failure raises `RuntimeError` carrying the transport's own message.

### Value types

| Type | Notable members |
|---|---|
| `FrequencyRange(start_hz, stop_hz)` | `start_hz`, `stop_hz` |
| `SweepParams` | `span`, `rbw_hz`, `vbw_hz`, `detector`, `points`, `ref_level_db`, `preamp`, `sweep_time` |
| `Capabilities` | `range`, `minimum_points`, `maximum_points`, `detectors`, `resolution_bandwidths_hz`, `preamp`, `zero_span`, `native_unit` |
| `Trace` | `start_hz`, `stop_hz`, `points`, `amplitudes`, `unit`, `detector` |
| `InstrumentId` | `manufacturer`, `model`, `serial`, `firmware`, `raw` |
| `Detector` | `PEAK`, `QUASI_PEAK`, `AVERAGE`, `RMS`, `SAMPLE` |
| `AmplitudeUnit` | `DBM`, `DBUV`, `DBUV_PER_M`, `DBUA` |

Frequencies are whole hertz, levels are decibels, and durations are Python
`timedelta` objects.

## 6. Threading

Plugin methods are called from the acquisition thread, never from the GUI
thread. PeakEmi holds the interpreter lock while it calls in and releases it
around transport I/O. A plugin does not need to think about the GIL unless it
starts threads of its own, which it should not.

## 7. Checklist before shipping a plugin

* `capabilities()` reports what the instrument can really do — PeakEmi rejects
  configurations outside it rather than sending commands the instrument refuses.
* `fetch_trace()` returns as many amplitudes as `params.points` requested.
* `abort()` returns promptly; it is called while a sweep is running.
* The manifest names the models the driver actually supports.
