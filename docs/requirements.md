# PeakEmi — Requirements Specification

| Field | Value |
|---|---|
| Document | Software Requirements Specification (SRS) |
| Project | PeakEmi — EMI pre-compliance measurement suite |
| Version | 0.2 (draft) |
| Status | Baseline for architecture and implementation |
| Related | [architecture.md](architecture.md), [../tasklist.md](../tasklist.md) |

## 0. Conventions

* Requirements are identified as `FR-<area>-<n>` (functional), `NFR-<area>-<n>` (non-functional)
  and `CON-<n>` (constraint). IDs are stable; superseded IDs are retired, never reused.
* **MUST** / **SHOULD** / **MAY** are used per RFC 2119.
* "Trace" = one array of amplitude values with an associated frequency axis.
* "Sweep" = one acquisition of a trace over a configured span.
* "Run" = one complete execution of the automated measurement loop, producing a session file.

---

## 1. Project Overview

**PeakEmi** is a cross-platform, open-source desktop application for **automated EMI pre-compliance
testing**. It drives spectrum analyzers over standard instrument buses, automates the scan →
detect → verify loop, evaluates measured traces against regulatory limit lines (CISPR, FCC,
EN 55032, …) and produces reproducible measurement reports.

### 1.1 Scope

PeakEmi targets the engineering bench: developers who need to know *before* the accredited test
house whether a design will pass radiated or conducted emission limits.

**In scope:** instrument control, automated scanning, limit evaluation, trace visualisation,
session persistence, report generation, driver extensibility.

**Out of scope (v1):** instrument calibration, control of turntables/antenna masts/LISNs,
site attenuation (NSA/CALTS) corrections, immunity (EMS) testing, accredited-lab report formats,
and any claim of formal compliance.

### 1.2 Compliance Disclaimer (CON-1)

PeakEmi produces **indicative pre-compliance data only**. Reports MUST carry a permanent,
non-removable notice stating that the results are not an accredited compliance measurement and
that measurement uncertainty of the setup is not accounted for unless entered manually by the
user. No UI element or export path may suppress this notice.

### 1.3 Target Platforms (CON-2)

| Platform | Baseline | Toolchain |
|---|---|---|
| Windows | 10 22H2 / 11 | MSVC 2022 (primary), MinGW-w64 (best effort) |
| Linux | Ubuntu 22.04 LTS and newer, Debian 12, Fedora 39+ | GCC 13+ / Clang 17+, X11 and Wayland |
| macOS | 13 Ventura and newer, Apple Silicon + Intel | Apple Clang 15+ |

### 1.4 Technology Stack (CON-3)

* **Language:** C++23 (no compiler-specific extensions; features must be available in MSVC 2022,
  GCC 13 and Clang 17 — see [architecture.md](architecture.md) §2.2 for the permitted subset).
* **UI / framework:** Qt 6.5 LTS minimum, developed against Qt 6.10.
* **Charting:** the plotting layer MUST be isolated behind an internal interface so the backend
  (Qt Graphs via `QQuickWidget`, QCustomPlot, Qt Charts) can be substituted without touching
  application logic. Backend selection is an open question — see §6.
* **Scripting:** CPython 3.10+ embedded via `pybind11`.
* **Build:** CMake 3.24+, presets-driven, out-of-source.
* **Licence:** GPL-3.0-or-later for the application; plugin API headers dual-licensed so that
  proprietary in-house drivers remain possible. Any dependency incompatible with this scheme
  MUST be rejected.

---

## 2. Architectural Paradigm & Driver Model

PeakEmi enforces a strictly layered architecture. The core engine knows nothing about specific
instruments; every instrument is reached through the Hardware Abstraction Layer (HAL).

```text
               +--------------------------------------------+
               |            PeakEmi Qt 6 Engine             |
               | (Core logic, UI, charting, session manager)|
               +--------------------------------------------+
                                    |
                   +----------------+----------------+
                   |                                 |
    +------------------------------+   +------------------------------+
    |      C++ Driver Pipeline     |   |    Embedded Python Bridge    |
    |   (High-speed / native libs) |   |    (pybind11 / CPython)      |
    +------------------------------+   +------------------------------+
                   |                                 |
   +---------------+---------------+  +--------------+---------------+
   | Siglent SSA   | Rigol DSA     |  | R&S plugin   | Custom Python |
   | C++ class     | C++ class     |  | script       | script        |
   +---------------+---------------+  +--------------+---------------+
                   |                                 |
                   +----------------+----------------+
                                    |
               +--------------------------------------------+
               |        Transport / Interfacing Layer        |
               |     (Raw TCP, VXI-11, USBTMC, Serial)       |
               +--------------------------------------------+
```

### 2.1 Hardware Abstraction Layer

* **FR-HAL-1** All instrument drivers MUST implement the abstract interface
  `AbstractAnalyzerDriver`.
* **FR-HAL-2** The interface MUST expose at minimum:
  `capabilities()`, `open(transport)`, `identify()`, `configureSweep(SweepParams)`,
  `armAndTrigger()`, `fetchTrace()`, `abort()`, `lastErrors()`, `close()`.
* **FR-HAL-3** `capabilities()` MUST report the instrument's declared limits and features
  (frequency range, trace point counts, supported detectors — peak / quasi-peak / average / RMS,
  supported RBW/VBW values, preamp, attenuation range, tracking generator presence). The engine
  MUST validate every requested configuration against these capabilities and reject it with an
  actionable message rather than sending an unsupported command.
* **FR-HAL-4** Drivers MUST encapsulate all vendor SCPI dialect differences. No SCPI string may
  appear outside a driver.
* **FR-HAL-5** Every driver operation MUST be cancellable and MUST honour a caller-supplied
  timeout; a driver MUST NOT block the calling thread indefinitely.
* **FR-HAL-6** Drivers MUST be stateless with respect to the UI, and MUST tolerate being
  destroyed at any time (including mid-sweep).
* **FR-HAL-7** A **simulated driver** MUST ship with the application, generating deterministic
  synthetic spectra (configurable noise floor, injectable narrowband emitters). It is the
  reference target for UI development and automated tests, and requires no hardware.

### 2.2 Dual-Language Extensibility

* **FR-EXT-1 Native C++ drivers** are used for baseline instruments where performance or
  dependency-free distribution matters. They are compiled into the application binary or into
  optional shared modules.
* **FR-EXT-2 Python plugin drivers** are discovered at start-up from a designated plugin
  directory (`<user-data>/plugins/drivers/*.py` plus a read-only system directory). Discovery is
  by import and registration — Python sources are *interpreted*, not compiled — and each plugin
  registers itself through a documented decorator/registry call.
* **FR-EXT-3** A Python plugin MUST declare a manifest (name, vendor, models matched by `*IDN?`
  regex, plugin API version, author, licence). Plugins whose declared API version is incompatible
  with the running application MUST be rejected and reported, not loaded.
* **FR-EXT-4** A failing plugin (import error, exception, timeout, protocol violation) MUST NOT
  crash or hang the application. Failures are caught, logged with a Python traceback, and the
  plugin is marked unavailable in the UI.
* **FR-EXT-5** Python drivers execute off the GUI thread. The bridge MUST manage the GIL
  correctly and MUST NOT hold it while performing blocking I/O on behalf of the interpreter.
* **FR-EXT-6** A plugin manager UI MUST list discovered plugins with status (loaded / rejected /
  error), origin path and last error, and MUST allow rescanning without restarting.
* **NFR-EXT-1 (security)** Python plugins execute with full user privileges. The application MUST
  state this clearly, MUST NOT auto-install plugins from the network, and MUST require an
  explicit user confirmation the first time a given plugin file (identified by path + content
  hash) is loaded.

### 2.3 Threading Model

* **FR-THR-1** The GUI thread MUST never perform blocking I/O. All instrument communication runs
  in worker threads.
* **FR-THR-2** Acquisition results MUST cross thread boundaries as immutable, copy-on-write value
  objects; no shared mutable trace buffers.
* **FR-THR-3** The UI MUST remain responsive (menu, abort button, zoom/pan) during a run,
  including while an instrument is unresponsive up to its timeout.

---

## 3. Communication & Auto-Discovery

### 3.1 Supported Transports

* **FR-COM-1 Ethernet (TCP/IP):** raw SCPI sockets (default ports 5025/5555) and VXI-11.
  HiSLIP MAY be added later.
* **FR-COM-2 USB (USBTMC):** via libusb on all platforms, with native driver fallback on Windows.
* **FR-COM-3 Serial (RS-232/UART):** via `QSerialPort`, with user-configurable baud rate, framing
  and line terminator.
* **FR-COM-4 VISA (optional):** if an NI-VISA / Keysight IO / `pyvisa` installation is present it
  MAY be offered as an additional transport, but PeakEmi MUST be fully functional without any
  VISA runtime installed.
* **FR-COM-5** All transports implement one internal `ITransport` interface (`write`, `read`,
  `query`, `setTimeout`, `clear`, `close`) so drivers are transport-agnostic.
* **FR-COM-6** A raw SCPI console MUST be available for manual command entry and for capturing
  a session log of all traffic (used for bug reports and new-driver development).

### 3.2 Auto-Discovery

* **FR-DIS-1 LAN discovery** runs on a background worker: VXI-11/mDNS-style broadcast probing
  plus an optional bounded TCP sweep of the local subnet on the instrument ports. The sweep MUST
  be rate-limited and concurrency-capped, MUST be opt-in (it generates unsolicited network
  traffic), and MUST be abortable.
* **FR-DIS-2 USB discovery** enumerates USBTMC-class devices at start-up and reacts to hotplug
  events; devices appear and disappear in the UI without a restart.
* **FR-DIS-3 Serial discovery** enumerates available ports but MUST NOT auto-probe them by
  default (writing to an unknown serial device is unsafe); probing is user-triggered per port.
* **FR-DIS-4 Identification handshake:** on a candidate endpoint, PeakEmi sends `*IDN?`, parses
  the response into manufacturer / model / serial / firmware, and selects the driver whose
  matcher scores highest. Ambiguity or no match MUST surface a manual driver-selection dialog,
  never a silent failure.
* **FR-DIS-5** Discovered instruments are persisted as user-nameable "known instruments" so a
  subsequent session connects directly without re-scanning.
* **NFR-DIS-1** A full discovery pass on a /24 subnet MUST complete within 15 s and MUST NOT
  saturate the link or the UI thread.

---

## 4. Functional Capabilities

### 4.1 Limit Lines & Standards

* **FR-LIM-1** Limit lines are imported from flat CSV and JSON files with a documented, stable
  schema: ordered `(frequency_hz, amplitude, unit)` breakpoints plus metadata (standard name,
  class A/B, detector, distance, conducted vs radiated).
* **FR-LIM-2** Interpolation between breakpoints MUST be selectable per segment: linear in
  amplitude over log frequency (the CISPR convention) or linear/linear, with discontinuous steps
  supported at band edges.
* **FR-LIM-3** A built-in, read-only catalogue of common limit sets ships with the application
  (e.g. CISPR 32 / EN 55032 class A and B radiated and conducted, FCC Part 15 subpart B class A
  and B). Each entry cites its source standard and edition. User-defined limits are stored
  separately and are always editable.
* **FR-LIM-4** Multiple limit lines MAY be active simultaneously and are rendered as overlays on
  the live trace.
* **FR-LIM-5 Margin analysis:** for every trace point the engine computes `margin = limit −
  measurement` in dB, tracks the worst-case margin per band, and classifies points as pass,
  marginal (within a configurable margin threshold, default 6 dB) or fail.
* **FR-LIM-6 Correction factors:** the measurement chain MUST support user-supplied correction
  tables (antenna factor, cable loss, LISN transducer factor, pre-amplifier gain, attenuator),
  interpolated over frequency and applied to the trace before limit evaluation. Applied
  corrections MUST be listed in the report.

### 4.2 Automated Pre-Compliance Measurement Loop

The core feature is a two-phase scan:

* **FR-RUN-1 Phase 1 — rapid peak profile scan.** A fast peak-detector sweep over the configured
  span (subdivided into segments if the span exceeds the instrument's usable resolution) captures
  the overall spectrum. The engine identifies local maxima and flags every peak whose corrected
  amplitude is within *X* dB of the active limit line (default X = 6 dB, user-configurable).
* **FR-RUN-2** Peak selection MUST be de-duplicated by a configurable minimum frequency spacing
  and capped by a configurable maximum count, ranked by smallest margin.
* **FR-RUN-3 Phase 2 — targeted dwell verification.** For each flagged peak the engine
  reconfigures the instrument to a narrow span (or zero span) centred on the peak, switches the
  detector to the CISPR-required quasi-peak or average mode, applies the standard-mandated RBW
  for the band (200 Hz / 9 kHz / 120 kHz / 1 MHz), dwells for a configurable time (default
  1000 ms), and records the final maximum value.
* **FR-RUN-4** Each verified point is stored with full provenance: frequency, raw amplitude,
  corrections applied, detector, RBW/VBW, dwell time, limit value, margin, timestamp,
  instrument identity and firmware.
* **FR-RUN-5** A run MUST be pausable, resumable and abortable at any point; aborting leaves the
  instrument in a defined, safe state and preserves partial results.
* **FR-RUN-6** Runs MUST be reproducible: a run stores its complete configuration, and re-running
  a stored configuration against the same instrument MUST require no manual reconfiguration.
* **FR-RUN-7** If the instrument reports an error or the transport drops, the run MUST stop with
  a clear diagnosis; automatic retry is limited to a configurable, bounded number of attempts.
* **FR-RUN-8** Optional maximum-hold / multiple-pass modes (N passes, keep worst case per point)
  for capturing intermittent emissions.

### 4.3 Interactive Data Presentation

* **FR-VIS-1** The plot MUST render traces of at least 40,001 points with smooth interactive
  zoom, pan and cursor tracking on all three platforms.
* **FR-VIS-2** Simultaneous display of: live trace, max-hold trace, reference/saved traces,
  active limit lines, flagged peaks and verified Phase 2 measurement points.
* **FR-VIS-3** Logarithmic and linear frequency axes; amplitude in dBµV, dBm and dBµV/m with
  correct conversion.
* **FR-VIS-4** Markers: manual markers, peak marker, delta markers, and a peak table
  synchronised with the plot selection.
* **FR-VIS-5** The plot MUST be exportable as PNG and SVG at a user-selectable resolution.

### 4.4 Sessions & Data Export

* **FR-DAT-1 Session model:** a session bundles instrument configuration, correction tables,
  limit sets, all captured traces, Phase 2 results, operator notes and EUT metadata
  (device name, serial, operating mode, test setup, photos optional).
* **FR-DAT-2** Sessions are saved to and loaded from a single, documented, versioned JSON
  container (large trace arrays MAY be stored in a companion binary/compressed payload).
  Backward compatibility: a newer application MUST read older session files.
* **FR-DAT-3 CSV export** of traces and of the Phase 2 result table, with a header block
  documenting units, corrections and instrument identity.
* **FR-DAT-4 PDF report** generation via Qt print support: title page with EUT and operator data,
  test configuration, plot with limit overlay, Phase 2 result table with pass/fail colouring,
  applied correction tables, the §1.2 disclaimer, and appendices for notes.
* **FR-DAT-5** Report templates MUST be customisable at least in company name, logo, address and
  free-text sections.
* **FR-DAT-6** All exports MUST record the application version and a run identifier.

### 4.5 Application Services

* **FR-APP-1 Logging:** rotating structured application log (levels, categories), including a
  full instrument I/O transcript when enabled. A "collect diagnostics" action bundles logs and
  configuration for bug reports (with instrument serials optionally redacted).
* **FR-APP-2 Settings:** persisted via `QSettings`/JSON in the platform's standard config
  location; no writes into the installation directory.
* **FR-APP-3 Localisation-ready:** all user-visible strings go through Qt translation; English is
  the source language. Additional translations are optional.
* **FR-APP-4 Crash safety:** an interrupted run leaves recoverable data on disk (autosave of the
  session at configurable intervals and after every Phase 2 point).
* **FR-APP-5** No telemetry, no network access other than instrument communication and an
  explicitly user-triggered update check.

---

## 5. Non-Functional Requirements

### 5.1 Performance
* **NFR-PERF-1** Application cold start to interactive main window ≤ 2 s on a 2020-class laptop
  (excluding first-run Python interpreter warm-up, which MUST be lazy/background).
* **NFR-PERF-2** Trace ingest, correction, limit evaluation and re-render for 40,001 points
  ≤ 50 ms, sustaining a 10 Hz update rate without dropping user input.
* **NFR-PERF-3** Steady-state memory for a session with 100 stored traces of 40,001 points
  ≤ 1 GB.

### 5.2 Reliability
* **NFR-REL-1** No unhandled exception may terminate the application; failures degrade to an
  error dialog plus log entry.
* **NFR-REL-2** Loss of the instrument connection MUST be detected within one timeout period and
  surfaced, never silently retried forever.
* **NFR-REL-3** Session files MUST be written atomically (write-temp-then-rename).

### 5.3 Portability & Build
* **NFR-BLD-1** One CMake tree builds all three platforms; no platform-specific project files.
* **NFR-BLD-2** The build MUST succeed with warnings-as-errors on the primary compilers.
* **NFR-BLD-3** Third-party dependencies are acquired reproducibly (pinned versions via CMake
  `FetchContent`/`find_package`), and every dependency's licence MUST be recorded.
* **NFR-BLD-4** Distributables: Windows installer/portable zip, Linux AppImage (and distro
  packaging welcome), macOS signed/notarised `.app` in a `.dmg`.
* **NFR-BLD-5** Embedding Python MUST be optional at configure time (`PEAKEMI_WITH_PYTHON=ON/OFF`);
  with it off, the application builds and runs with C++ drivers only.

### 5.4 Code Quality & Testing
* **NFR-QUA-1** Uniform formatting enforced by `.clang-format`; static analysis by `.clang-tidy`;
  both run in CI and MUST be clean.
* **NFR-QUA-2** Unit tests (Qt Test) for all non-UI logic: limit interpolation, margin
  computation, peak detection, corrections, SCPI parsing, session serialisation. Target ≥ 80 %
  line coverage of the `core` and `hal` libraries.
* **NFR-QUA-3** Integration tests run the full two-phase loop against the simulated driver in CI,
  headless (`offscreen` platform plugin).
* **NFR-QUA-4** CI matrix: Windows/MSVC, Ubuntu/GCC, macOS/Clang — build, test, format and tidy
  checks on every pull request.
* **NFR-QUA-5** Public headers of `core`, `hal` and the plugin API MUST be documented
  (Doxygen-style) and the plugin API MUST have a written, versioned specification with a worked
  example driver.

### 5.5 Usability & Accessibility
* **NFR-UX-1** A new user MUST be able to go from launch to a completed simulated run without
  reading documentation.
* **NFR-UX-2** Destructive or instrument-affecting actions are confirmable and reversible where
  physically possible.
* **NFR-UX-3** Keyboard navigation for all primary workflows; respects the system light/dark
  theme and a high-contrast option; UI scales to 125 %/150 %/200 % display scaling.

---

## 6. Assumptions, Risks & Open Questions

**Assumptions**
1. The user owns a spectrum analyzer with SCPI remote control and the necessary transducers.
2. Bench instruments are on a trusted local network; no authentication is available or expected.

**Risks**
| Risk | Impact | Mitigation |
|---|---|---|
| Vendor SCPI dialects deviate from documentation | Driver breakage | Capability probing, SCPI transcript logging, simulated driver for regression |
| Embedded CPython complicates distribution (ABI, signing, notarisation) | Build/release pain | Python support is an optional build flag (NFR-BLD-5); ship a known-good runtime per platform |
| Quasi-peak dwell makes runs long | Poor UX | Bounded peak counts, pause/resume, progress with time estimate |
| GPL-3.0 vs proprietary in-house drivers | Adoption friction | Plugin API headers dual-licensed (CON-3) |

**Open questions**
1. Which instruments constitute the v1 supported set (proposal: Siglent SSA3000X/SVA1000X, Rigol
   DSA800/DSA700, plus the simulated driver)?
2. Plot backend: Qt Graphs 2D is QML-only (no widget wrapper as of Qt 6.10), so it requires a
   `QQuickWidget` host. Is that acceptable, or is the QPainter-based QCustomPlot the safer v1
   choice? — to be settled by a benchmark spike before the UI milestone
   (see [architecture.md](architecture.md) §7.1).
3. Should conducted-emission LISN control (relay switching of line/neutral) be in v1?
4. Minimum Qt version: hold at 6.5 LTS or require 6.8+ for newer Graphs features?
