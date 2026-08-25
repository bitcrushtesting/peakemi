# PeakEmi tasklist

## Done

* **Build system** — CMakeLists (modern C++23, Qt 6), `CMakePresets.json`,
  `.clang-format`, `.clang-tidy`, `.editorconfig`.
* **Core domain (`peakemi_core`)** — strong units, traces, capabilities, limit lines and
  the built-in CISPR/FCC catalogue, correction chain, CISPR band table, peak detection,
  run configuration with segment planning, session model and versioned JSON container,
  atomic file writes, categorised + rotating logging.
* **Ports** — `ITransport` and `AbstractAnalyzerDriver`, error values via `std::expected`,
  `CancelToken`.
* **HAL (`peakemi_hal`)** — SCPI parsing helpers, TCP and serial transports, driver
  registry with scored `*IDN?` matching, LAN sweep and serial port enumeration.
* **Drivers (`peakemi_drivers`)** — deterministic `SimulatedDriver`, generic SCPI driver
  with Siglent SSA/SVA and Rigol DSA profiles.
* **Engine** — two-phase scan → detect → verify state machine on a worker thread, with
  pause/resume/abort, bounded retries, multi-pass max-hold and autosave.
* **Reporting (`peakemi_reporting`)** — CSV export of traces and results, PDF report.
* **User interface (`peakemi_ui`)** — main window with instrument, configuration, results
  and log/SCPI-console docks, decimating spectrum plot behind `IPlotBackend`, run
  controller owning the acquisition thread, session and export actions.
* **Tests** — 11 Qt Test executables: unit tests for every non-UI algorithm, component
  tests against a scripted transport, UI smoke tests and a headless integration test of
  the full two-phase run.
* **CI** — GitHub Actions matrix (Windows/MSVC, Ubuntu/GCC, macOS/Clang) building with
  warnings as errors and running the suite headless, plus a format/clang-tidy job.
* **Release** — tag-triggered workflow that verifies the tag sits on `main` and matches
  `project(VERSION ...)`, then builds and publishes an AppImage, a macOS `.dmg` and a
  Windows zip.
* **Documentation** — README screenshots generated headlessly from the real main window
  by `tools/screenshots` (`PEAKEMI_BUILD_TOOLS=ON`).

## Next

* **M5** — USBTMC and VXI-11 transports, USB hotplug discovery, more instrument drivers.
* **M6** — report templates (logo, address, free text) with a template editor,
  JSON export of the result table.
* **M7** — embedded Python bridge: bindings, plugin discovery and trust store, plugin
  manager UI, example plugin.
* **M8** — code signing and notarisation for the macOS and Windows artifacts, and a Windows
  installer.
* **Open questions** — plot backend benchmark (Qt Graphs via `QQuickWidget` vs the
  current QPainter backend), LISN control, minimum Qt version.
  See [requirements.md §6](docs/requirements.md#6-assumptions-risks--open-questions).
