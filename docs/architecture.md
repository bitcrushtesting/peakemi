# PeakEmi — Software Architecture

| Field | Value |
|---|---|
| Document | Software Architecture Description |
| Version | 0.1 (baseline) |
| Derived from | [requirements.md](requirements.md) |
| Audience | Contributors implementing or reviewing PeakEmi code |

---

## 1. Architectural Drivers

The design is shaped by five requirements that dominate every structural decision:

| Driver | Requirement | Structural consequence |
|---|---|---|
| Instrument diversity | FR-HAL-1…7, FR-EXT-* | A driver interface + registry; nothing above the HAL knows SCPI |
| Responsiveness under slow I/O | FR-THR-1…3, NFR-PERF-2 | Worker-thread acquisition, immutable value objects across threads |
| Testability without hardware | FR-HAL-7, NFR-QUA-2/3 | Simulated driver as a first-class citizen; core logic free of Qt widgets |
| Optional Python | NFR-BLD-5, FR-EXT-5 | Python bridge is a separate, optional library behind the same driver interface |
| Reproducible results | FR-RUN-4/6, FR-DAT-1/2 | An explicit, serialisable session/run model owned by `core` |

**Style:** layered architecture with a hexagonal (ports-and-adapters) core. `core` defines ports
(`ITransport`, `AbstractAnalyzerDriver`, `IReportRenderer`); `hal`, `drivers`, `python` and `ui`
supply adapters. Dependencies point inward only.

---

## 2. System Decomposition

### 2.1 Module map

```text
                         ┌────────────────────────────┐
                         │        app (exe)           │  wiring, CLI, main()
                         └─────────────┬──────────────┘
                                       │
                         ┌─────────────▼──────────────┐
                         │           ui               │  Qt Widgets, plotting,
                         │ (widgets, models, plot)    │  view-models
                         └─────────────┬──────────────┘
                                       │  (signals / view-models)
   ┌───────────────────────────────────▼───────────────────────────────────┐
   │                                 core                                  │
   │  domain model · measurement engine · limits · corrections · session   │
   │  peak detection · export · report composition · settings · logging    │
   └───┬───────────────────────────┬───────────────────────────┬───────────┘
       │ port: AbstractAnalyzerDriver                          │ port: IReportRenderer
   ┌───▼───────────┐        ┌──────▼─────────┐          ┌──────▼─────────┐
   │     hal       │        │  drivers_cpp   │          │   reporting    │
   │ transports,   │◄───────┤ Siglent, Rigol │          │ PDF/CSV/JSON   │
   │ discovery,    │        │ Simulated, …   │          │ writers        │
   │ SCPI helpers  │        └────────────────┘          └────────────────┘
   └───┬───────────┘
       │ port: ITransport                        ┌────────────────────────┐
       │                                         │   python_bridge (opt)  │
       └─────────────────────────────────────────┤ pybind11 embedding,    │
                                                 │ PythonDriverProxy      │
                                                 └────────────────────────┘
```

### 2.2 Libraries and their contracts

| Target | Kind | Depends on | May use Qt | Responsibility |
|---|---|---|---|---|
| `peakemi_core` | static lib | — | Core, Concurrent (no Widgets) | Domain types, measurement engine, limits, corrections, session, peak detection, settings, logging |
| `peakemi_hal` | static lib | core | Core, Network, SerialPort | Transports (TCP, VXI-11, USBTMC, serial), discovery workers, SCPI utilities, driver registry |
| `peakemi_drivers` | static lib | core, hal | Core | Concrete C++ drivers incl. `SimulatedDriver` |
| `peakemi_python` | static lib (optional) | core, hal | Core | CPython embedding, `PythonDriverProxy`, plugin discovery, GIL management |
| `peakemi_reporting` | static lib | core | Core, Gui, PrintSupport, Svg | CSV/JSON writers, PDF report renderer |
| `peakemi_ui` | static lib | core, hal, reporting | Widgets, Graphs/Charts | Widgets, view-models, plot layer |
| `peakemi` | executable | all | all | Composition root, argument parsing, DI wiring |
| `peakemi_tests` | test exes | all | Test | Unit + integration tests |

**Hard rule:** `peakemi_core` must not link Qt Widgets and must not include a single `Q*Widget`
header. This keeps the domain headless-testable (NFR-QUA-3) and keeps the UI replaceable.

### 2.3 Permitted C++23 subset

Targeting MSVC 2022, GCC 13 and Clang 17 in common:
`std::expected`, `std::span`, `std::string_view`, `<format>` (with fallback to `QString::arg`
where libstdc++ 13 lags), designated initialisers, `constexpr` algorithms, ranges (basic views),
`[[nodiscard]]`, `enum class` everywhere, `std::chrono` for all durations.
Avoid (insufficient/uneven support): modules, `std::flat_map`, `std::generator`, `std::mdspan`,
`std::print`, deducing `this`, coroutines in library code.

---

## 3. Domain Model (`peakemi_core`)

```cpp
namespace peakemi {

enum class Detector { Peak, QuasiPeak, Average, Rms, Sample };
enum class AmplitudeUnit { dBm, dBuV, dBuV_per_m, dBuA };

struct FrequencyRange { Hertz start; Hertz stop; };            // Hertz = std::int64_t strong type

struct SweepParams {
    FrequencyRange span;
    Hertz          rbw{};            // 0 = auto
    Hertz          vbw{};            // 0 = auto
    Detector       detector{Detector::Peak};
    int            points{1001};
    Decibel        refLevel{};
    Decibel        attenuation{};    // NaN = auto
    bool           preamp{false};
    std::chrono::milliseconds sweepTime{0};   // 0 = auto
};

struct Trace {                       // immutable value object, shared_ptr-shared across threads
    FrequencyAxis   axis;            // start/stop/points, or explicit vector for segmented sweeps
    std::vector<double> amplitudes;  // in `unit`
    AmplitudeUnit   unit;
    Detector        detector;
    SweepParams     params;
    InstrumentId    source;
    TimePoint       acquiredAt;
};

struct LimitLine {                   // breakpoints + interpolation rule (FR-LIM-1/2)
    std::string name, standard, note;
    std::vector<LimitPoint> points;  // {frequency, amplitude, interpolation}
    AmplitudeUnit unit;
    double evaluateAt(Hertz f) const;   // returns NaN outside the defined range
};

struct CorrectionTable {             // antenna factor, cable loss, gain … (FR-LIM-6)
    std::string name; CorrectionKind kind;
    std::vector<std::pair<Hertz,double>> points;
    double valueAt(Hertz f) const;   // log-frequency linear interpolation
};

struct MeasurementPoint {            // one Phase-2 verified result (FR-RUN-4)
    Hertz frequency; double rawAmplitude, correctedAmplitude, limitValue, marginDb;
    Detector detector; Hertz rbw, vbw; std::chrono::milliseconds dwell;
    std::vector<AppliedCorrection> corrections;
    Verdict verdict;                 // Pass | Marginal | Fail
    TimePoint measuredAt;
};

struct Session {                     // FR-DAT-1
    SessionMeta meta;                // EUT, operator, notes, app version, run id
    RunConfiguration config;         // sweep plan, thresholds, limits, corrections
    std::vector<Trace> traces;
    std::vector<MeasurementPoint> results;
};

} // namespace peakemi
```

Design notes:

* **Strong types** (`Hertz`, `Decibel`, `Milliseconds`) instead of bare `double`/`int` — unit
  confusion is the classic bug class in this domain.
* **Traces are immutable.** Acquisition produces a `std::shared_ptr<const Trace>` that is passed
  by value across threads (FR-THR-2); no locking is needed on the data path.
* **Corrections are applied in a pure function** `applyCorrections(Trace, span<const
  CorrectionTable>) -> Trace`, so the transformation chain is unit-testable and reportable.
* **Nothing in the domain touches I/O.** Persistence lives in `session/SessionSerializer`,
  which converts `Session` ⇄ JSON (schema-versioned, forward-compatible per FR-DAT-2).

---

## 4. Hardware Abstraction Layer (`peakemi_hal`)

### 4.1 Ports

```cpp
class ITransport {                                   // FR-COM-5
public:
    virtual ~ITransport() = default;
    virtual std::expected<void, TransportError>       open()  = 0;
    virtual std::expected<void, TransportError>       write(std::string_view) = 0;
    virtual std::expected<std::string, TransportError> read(std::chrono::milliseconds) = 0;
    virtual std::expected<std::vector<std::byte>, TransportError>
                                                      readBinaryBlock(std::chrono::milliseconds) = 0;
    virtual void clear() = 0;
    virtual void close() = 0;
    virtual TransportDescriptor descriptor() const = 0;
};

class AbstractAnalyzerDriver {                        // FR-HAL-1/2
public:
    virtual ~AbstractAnalyzerDriver() = default;
    virtual DriverInfo    info()          const = 0;
    virtual Capabilities  capabilities()  const = 0;     // FR-HAL-3
    virtual std::expected<void, DriverError> open(std::shared_ptr<ITransport>) = 0;
    virtual std::expected<InstrumentId, DriverError> identify() = 0;
    virtual std::expected<void, DriverError> configureSweep(const SweepParams&) = 0;
    virtual std::expected<void, DriverError> armAndTrigger(CancelToken) = 0;
    virtual std::expected<Trace, DriverError> fetchTrace(CancelToken) = 0;
    virtual void abort() = 0;                            // callable from another thread
    virtual std::vector<InstrumentError> lastErrors() = 0;
    virtual void close() = 0;
};
```

* Errors travel as `std::expected` values, never exceptions across the port boundary — Python
  drivers convert their exceptions into the same error type.
* `CancelToken` is a cheap atomic-flag handle; every long operation polls it (FR-HAL-5).
* `Capabilities` is queried once per connection and cached; the engine validates all
  `SweepParams` against it before any command is sent (FR-HAL-3).

### 4.2 Transports

| Adapter | Implementation |
|---|---|
| `TcpScpiTransport` | `QTcpSocket`, newline-terminated SCPI, IEEE-488.2 definite-length block parsing |
| `Vxi11Transport` | ONC-RPC core/abort channels; used for discovery and for instruments without a raw socket |
| `UsbTmcTransport` | libusb bulk in/out with USBTMC bulk headers; hotplug via libusb callbacks |
| `SerialTransport` | `QSerialPort`, configurable framing/terminator |
| `VisaTransport` (optional) | dlopen'd VISA runtime; absent runtime simply removes the option (FR-COM-4) |

### 4.3 Driver registry and matching

`DriverRegistry` holds `DriverFactory` entries contributed by (a) statically registered C++
drivers and (b) the Python bridge. Each factory publishes a matcher over the parsed `*IDN?`
fields; matching is scored (exact model > model family regex > vendor-only), and the best score
wins. Ties or zero matches raise a manual-selection request to the UI (FR-DIS-4).

### 4.4 Discovery

Three independent `QObject` workers on their own threads, each emitting `instrumentFound`:

* `LanDiscoveryWorker` — VXI-11 broadcast + optional bounded TCP sweep (semaphore-limited
  concurrency, per-host timeout, abortable; opt-in per FR-DIS-1).
* `UsbDiscoveryWorker` — libusb enumeration + hotplug (FR-DIS-2).
* `SerialEnumerator` — port listing only; probing is user-triggered (FR-DIS-3).

Results funnel into `InstrumentInventory` (a `QAbstractItemModel` in the UI layer wrapping a
plain core container), which merges live discoveries with persisted "known instruments".

---

## 5. Measurement Engine (`peakemi_core`)

### 5.1 State machine

```text
        ┌──────┐  configure  ┌────────────┐  start   ┌──────────────┐
        │ Idle ├────────────►│ Configured ├─────────►│ Phase1Sweep  │
        └──────┘             └────────────┘          └──────┬───────┘
             ▲                      ▲                       │ sweep complete
             │                      │                       ▼
             │                      │                ┌──────────────┐
             │                      │                │ PeakAnalysis │
             │                      │                └──────┬───────┘
             │                      │            peaks found│ none → Finished
             │                      │                       ▼
             │        abort         │                ┌──────────────┐ next peak
             ├──────────────────────┼────────────────┤ Phase2Dwell  │◄────────┐
             │                      │                └──────┬───────┘         │
             │                      │                       │ point stored ───┘
             │                      │                       ▼
             │                 ┌────┴─────┐           ┌──────────┐
             └─────────────────┤  Paused  │◄──────────┤ Finished │
                               └──────────┘           └──────────┘
```

`MeasurementEngine` owns this machine, runs on a dedicated worker thread, and exposes only
signals (`phaseChanged`, `traceAcquired`, `peaksFlagged`, `pointMeasured`, `progress`,
`runFailed`, `runFinished`) plus thread-safe commands (`start`, `pause`, `resume`, `abort`).
It never touches the UI and never blocks it (FR-THR-1/3, FR-RUN-5).

### 5.2 Phase 1 — peak profiling

1. **Segment planning.** If `span / points` exceeds the usable bin width for the required RBW,
   the span is split into segments sized to the instrument's point budget, swept sequentially and
   stitched into one logical trace.
2. **Correction.** `applyCorrections()` folds antenna factor, cable loss, amplifier gain and
   attenuator values into the trace (FR-LIM-6).
3. **Limit evaluation.** `LimitEvaluator` computes per-point margins against all active limit
   lines and returns the per-point worst case.
4. **Peak detection.** `PeakDetector` finds local maxima with a prominence threshold, keeps those
   within *X* dB of the limit, enforces minimum frequency spacing, ranks by smallest margin and
   truncates to the configured maximum (FR-RUN-1/2). Pure function, fully unit-tested.

### 5.3 Phase 2 — dwell verification

For each candidate: pick the CISPR band for its frequency → derive mandated RBW (200 Hz below
150 kHz, 9 kHz to 30 MHz, 120 kHz to 1 GHz, 1 MHz above) → configure narrow/zero span with the
required detector → arm, dwell, fetch → take the maximum → build a `MeasurementPoint` with full
provenance → emit and autosave (FR-RUN-3/4, FR-APP-4). Instrument errors abort the run after a
bounded retry count (FR-RUN-7). Multi-pass max-hold (FR-RUN-8) wraps the whole loop.

---

## 6. Python Bridge (`peakemi_python`, optional)

```text
DriverRegistry ──registers──► PythonDriverFactory ──creates──► PythonDriverProxy (C++)
                                                                     │  pybind11 call,
                                                                     │  GIL acquired here only
                                                                     ▼
                                                       plugin module (peakemi_plugin API)
```

* **Interpreter lifetime.** One interpreter, initialised lazily on first plugin use
  (NFR-PERF-1), finalised at shutdown. Plugin calls happen on the acquisition worker thread with
  an explicit `py::gil_scoped_acquire`; the GIL is released around blocking transport I/O
  (FR-EXT-5) because transports are C++ objects exposed to Python.
* **`PythonDriverProxy`** implements `AbstractAnalyzerDriver` and translates every Python
  exception into a `DriverError` carrying the formatted traceback — a plugin can never propagate
  an exception into C++ (FR-EXT-4). Every call is time-boxed.
* **Plugin API surface** (module `peakemi_plugin`): `AnalyzerDriver` base class, `Transport`
  wrapper, `SweepParams`/`Trace`/`Capabilities` bindings, `@register_driver(manifest)` decorator.
  Versioned as `PLUGIN_API_VERSION = "1.0"`; incompatible plugins are rejected (FR-EXT-3).
* **Discovery & trust.** Scan system plugin dir + `<user-data>/plugins/drivers`. Each file's
  SHA-256 is checked against a trust store; unknown files require explicit user approval before
  import (NFR-EXT-1).
* **Build flag** `PEAKEMI_WITH_PYTHON` (default ON, must build cleanly OFF — NFR-BLD-5).

---

## 7. UI Layer (`peakemi_ui`)

Pattern: **MVVM-ish** — widgets are dumb; a view-model per screen exposes Qt properties/signals
and forwards commands to `core`. No business logic in widgets; no `core` type is constructed
inside a `paintEvent`.

```text
MainWindow
├── InstrumentDock        instrument tree (discovered + known), connect/disconnect, driver override
├── RunConfigDock         span, RBW/VBW, detector, thresholds, limit set, corrections, dwell
├── SpectrumPlotView      live + max-hold + reference traces, limit overlays, markers, peak flags
├── ResultsDock           Phase-2 table (sortable, verdict-coloured), synced with plot selection
├── LogDock               application log + raw SCPI console (FR-COM-6)
└── Dialogs               plugin manager, limit editor, correction editor, report/export, settings
```

### 7.1 Widgets vs. QML — decision and evidence (ADR-1)

The shell is **Qt Widgets**; QML is used only if the chosen plot backend requires it.

What argues for Widgets here:

* `QDockWidget` has no QML equivalent — the instrument/config/results/log layout is dockable,
  floatable and savable via `saveState()` out of the box.
* `QTableView` + `QSortFilterProxyModel` for the peak/results table, and `QPrinter`/`QPdfWriter`
  with `QPainter` for the PDF report (FR-DAT-4), are C++/Widgets APIs. A QML UI would still need
  a C++ rendering path for reports.
* Native menu bar, standard shortcuts and platform dialogs come for free on all three targets.
* Lower contributor barrier for a C++ instrumentation project, and no QML runtime to ship or
  debug at deployment time.

What argues for QML:

* The scene graph renders on the GPU, which is the natural fit for 40,001-point traces with live
  zoom/pan (FR-VIS-1, NFR-PERF-2). `QPainter`-based widget plotting is CPU-bound.
* **Qt Graphs 2D (`GraphsView`) is a QML-only module.** Verified against Qt 6.10.2: the
  `Qt6GraphsWidgets` module ships only `Q3DBarsWidgetItem`, `Q3DScatterWidgetItem`,
  `Q3DSurfaceWidgetItem` and `Q3DGraphsWidgetItem` — 3D types. There is no widget wrapper for the
  2D graph view.
* Smoother animation, easier theming, and a path to touch/tablet front-ends later.

Consequence: three viable plot paths, all behind `IPlotBackend` (§7):

| Path | Backend | Cost |
|---|---|---|
| A (default) | `QQuickWidget` hosting a QML `GraphsView` inside the Widgets shell | QML runtime in the process; C++↔QML data marshalling for large arrays |
| B | `QCustomPlot` (QPainter widget, proven for this exact workload) | CPU-bound rendering; GPL-compatible but a vendored third-party file |
| C | Qt Charts `QChartView` | Widget-native, but Charts is the older module and degrades badly above ~10k points |

The benchmark spike (requirements §6, Q2) decides between A and B before milestone M4. A full-QML
UI is deliberately not chosen: it would trade the docking, table and printing infrastructure for
rendering performance that path A already provides where it actually matters.

**Plot backend isolation (CON-3 / FR-VIS-1).** All plotting goes through
`ui/plot/IPlotBackend` with `QtGraphsPlotBackend` as the default implementation. The interface is
deliberately narrow (add/update series, set axes, overlays, markers, export image) so the open
benchmark question (requirements §6, Q2) can be resolved by swapping one class. Large traces are
decimated for display — min/max envelope per pixel column — while the full array stays in the
session (NFR-PERF-2).

---

## 8. Reporting (`peakemi_reporting`)

`IReportRenderer` port in `core`; adapters: `CsvExporter`, `JsonExporter`, `PdfReportRenderer`
(QPainter over `QPdfWriter`, page composition of title block, configuration, plot image,
result table, correction tables, mandatory disclaimer per CON-1). Report branding
(company, logo, address, free text) comes from a `ReportTemplate` value object (FR-DAT-5).

---

## 9. Cross-Cutting Concerns

| Concern | Mechanism |
|---|---|
| Errors | `std::expected<T, Error>` inside libraries; Qt signals to surface to UI; exceptions only at the `main()` boundary (NFR-REL-1) |
| Logging | Qt categorized logging (`peakemi.hal.tcp`, `peakemi.engine`, …) + rotating file sink; SCPI transcript is its own category (FR-APP-1) |
| Cancellation | `CancelToken` passed down through engine → driver → transport |
| Settings | `SettingsStore` in core over `QSettings`/JSON in platform config dir (FR-APP-2) |
| Persistence | Atomic write-temp-then-rename for every session/settings write (NFR-REL-3) |
| Time | `std::chrono` throughout; timestamps stored as ISO-8601 UTC |
| i18n | `tr()` in UI only; core returns error *codes*, UI maps them to translated text (FR-APP-3) |
| Threading | GUI thread + acquisition thread + discovery threads; hand-off exclusively by queued signals with value/`shared_ptr<const T>` payloads |

---

## 10. Source Tree Layout

```text
peakemi/
├── CMakeLists.txt              top-level: options, standards, dependency resolution
├── CMakePresets.json           configure/build/test presets per platform
├── .clang-format .clang-tidy .editorconfig .gitignore
├── cmake/
│   ├── CompilerWarnings.cmake  warning sets per compiler, warnings-as-errors
│   ├── ProjectOptions.cmake    sanitizers, LTO, coverage, PEAKEMI_* options
│   └── Dependencies.cmake      Qt, pybind11, libusb resolution (pinned)
├── docs/                       requirements.md, architecture.md, plugin-api.md, adr/
├── src/
│   ├── core/     domain/ engine/ limits/ corrections/ session/ util/
│   ├── hal/      transport/ discovery/ scpi/ registry/
│   ├── drivers/  simulated/ siglent/ rigol/
│   ├── python/   bridge/ bindings/ plugins/
│   ├── reporting/
│   ├── ui/       windows/ docks/ dialogs/ plot/ models/
│   └── app/      main.cpp, composition root
├── plugins/drivers/            example + user Python drivers
├── resources/                  icons, qrc, limit catalogue, report templates
├── tests/        unit/ integration/ fixtures/
└── .github/workflows/          ci.yml (build+test+format+tidy matrix)
```

Each `src/<module>/` owns a `CMakeLists.txt` declaring one target; public headers live under
`src/<module>/include/peakemi/<module>/` and are exposed with `target_include_directories(...
PUBLIC)`. Includes are always angle-bracket, project-rooted: `#include <peakemi/core/Trace.h>`.

---

## 11. Build & Dependency Strategy

| Dependency | Version | Acquisition | Licence | Required |
|---|---|---|---|---|
| Qt | ≥ 6.5, dev on 6.10 | `find_package` (system/official installer) | LGPL-3.0 | yes |
| pybind11 | 2.13.x pinned | `FetchContent` | BSD-3 | only with `PEAKEMI_WITH_PYTHON` |
| CPython | ≥ 3.10 | `find_package(Python3 COMPONENTS Development)` | PSF | only with `PEAKEMI_WITH_PYTHON` |
| libusb | 1.0.27 | system or `FetchContent` | LGPL-2.1 | only with `PEAKEMI_WITH_USBTMC` |
| nlohmann/json | 3.11.x | `FetchContent` | MIT | yes (session/limit I/O) |

Configure-time options: `PEAKEMI_WITH_PYTHON`, `PEAKEMI_WITH_USBTMC`, `PEAKEMI_WITH_VISA`,
`PEAKEMI_BUILD_TESTS`, `PEAKEMI_ENABLE_SANITIZERS`, `PEAKEMI_ENABLE_COVERAGE`,
`PEAKEMI_WARNINGS_AS_ERRORS`, `PEAKEMI_ENABLE_CLANG_TIDY`.

---

## 12. Test Strategy

| Level | Scope | Tooling |
|---|---|---|
| Unit | Limit interpolation, margin maths, peak detection, corrections, SCPI parsing, session round-trip, capability validation | Qt Test, one executable per module |
| Component | Driver against a scripted fake transport replaying recorded SCPI transcripts | Qt Test + fixtures in `tests/fixtures/` |
| Integration | Full two-phase run against `SimulatedDriver`, headless | `QT_QPA_PLATFORM=offscreen` in CI |
| UI smoke | Main window construction, dock wiring, plot with 40k points | Qt Test + `QTest::qWait` |
| Static | clang-format, clang-tidy, compiler warnings-as-errors | CI gates |

Golden rule: any bug fixed gets a regression test; any new driver ships with a recorded
transcript fixture.

---

## 13. Architecture Decision Records (summary)

| # | Decision | Rationale | Consequence |
|---|---|---|---|
| ADR-1 | Qt Widgets shell, QML only inside the plot if needed | See §7.1 | Two UI technologies in one process if the Quick path is taken |
| ADR-2 | `std::expected` over exceptions at port boundaries | Predictable across the Python bridge; forces error handling | More verbose call sites |
| ADR-3 | Python optional at configure time | Distribution/notarisation risk (requirements §6) | Two build configurations must stay green in CI |
| ADR-4 | Immutable traces shared by `shared_ptr<const>` | Lock-free cross-thread data path | Copy cost on modification — acceptable, modifications are rare |
| ADR-5 | Plot backend behind an interface | Backend choice is an open benchmark question | Slight indirection cost in the render path |
| ADR-6 | Simulated driver is a shipped feature, not test-only | Enables demos, UI work and CI without hardware | Must be maintained alongside real drivers |

---

## 14. Implementation Roadmap

1. **M0 — Skeleton.** Build system, tooling configs, CI, empty module targets, `main()` window.
2. **M1 — Core domain.** Types, limits, corrections, peak detection, session serialisation + unit tests.
3. **M2 — HAL + simulated driver.** Transport interface, TCP transport, registry, `SimulatedDriver`, headless integration test.
4. **M3 — Engine.** Two-phase state machine, cancellation, autosave.
5. **M4 — UI.** Main window, plot layer, docks, live acquisition against the simulator.
6. **M5 — Real hardware.** Siglent + Rigol drivers, discovery (LAN → USBTMC → serial).
7. **M6 — Reporting.** CSV/JSON export, PDF report, templates.
8. **M7 — Python bridge.** Bindings, plugin discovery/trust, plugin manager UI, example plugin.
9. **M8 — Packaging.** Installer, AppImage, notarised `.app`, user documentation.
