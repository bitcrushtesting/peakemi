# PeakEmi

Cross-platform, open-source **EMI pre-compliance measurement suite**. PeakEmi drives spectrum
analyzers over TCP/VXI-11, USBTMC or serial, automates the two-phase scan → detect → verify loop,
evaluates traces against CISPR/FCC limit lines and produces reproducible reports.

> **Pre-compliance only.** Results are indicative engineering data, not an accredited compliance
> measurement.

| | |
|---|---|
| Status | Early development — build skeleton (milestone M0) |
| Language | C++23, Qt 6.5+ (developed on 6.10) |
| Platforms | Windows 10/11, Linux (Ubuntu 22.04+), macOS 13+ |
| Docs | [Requirements](docs/requirements.md) · [Architecture](docs/architecture.md) · [Tasklist](tasklist.md) |

## Building

Requirements: CMake ≥ 3.24, Ninja, a C++23 compiler (MSVC 2022 / GCC 13+ / Clang 17+) and Qt 6.5+
with the `Widgets`, `Network`, `SerialPort`, `PrintSupport`, `Svg` and `Test` modules.

```bash
# Point CMake at your Qt installation if it is not in the default search path
export CMAKE_PREFIX_PATH="$HOME/Qt/6.10.2/macos"     # or C:/Qt/6.10.2/msvc2022_64, /usr/lib/qt6, ...

cmake --preset debug          # configure
cmake --build --preset debug  # build
ctest --preset debug          # run the tests
```

Available configure presets: `debug`, `release`, `relwithdebinfo`, `dev` (sanitizers + clang-tidy
+ warnings-as-errors) and `ci`. The whole CI flow is `cmake --workflow --preset ci`.

The application binary lands in `build/<preset>/bin/`.

### Build options

| Option | Default | Effect |
|---|---|---|
| `PEAKEMI_BUILD_TESTS` | `ON` | Build the Qt Test suite |
| `PEAKEMI_WITH_PYTHON` | `OFF` | Embed CPython for Python driver plugins (pulls pybind11) |
| `PEAKEMI_WITH_USBTMC` | `OFF` | Build the USBTMC transport (requires libusb-1.0) |
| `PEAKEMI_WITH_VISA` | `OFF` | Enable the optional VISA transport |
| `PEAKEMI_WARNINGS_AS_ERRORS` | `OFF` | Promote compiler warnings to errors (CI turns this on) |
| `PEAKEMI_ENABLE_CLANG_TIDY` | `OFF` | Run clang-tidy during the build |
| `PEAKEMI_ENABLE_SANITIZERS` | `OFF` | ASan + UBSan |
| `PEAKEMI_ENABLE_COVERAGE` | `OFF` | Coverage instrumentation (GCC/Clang) |

## Repository layout

```
cmake/      build modules: options, warnings, dependencies, module helper
docs/       requirements and architecture
src/core/   domain model, measurement engine (no Qt Widgets — headless testable)
src/ui/     Qt Widgets user interface
src/app/    composition root and main()
tests/      unit and integration tests
```

Modules `hal`, `drivers`, `reporting` and `python` are added as the milestones in
[architecture.md §14](docs/architecture.md#14-implementation-roadmap) land.

## Code style

* Formatting: `.clang-format` (clang-format ≥ 16). `clang-format -i $(git ls-files '*.cpp' '*.hpp')`
* Static analysis: `.clang-tidy`, enabled in the build with `-DPEAKEMI_ENABLE_CLANG_TIDY=ON`
* Editor defaults: `.editorconfig`
* Includes are project-rooted: `#include <peakemi/core/Version.hpp>`
* Private members use the `m_` prefix; namespaces are lower case, types `CamelCase`, functions
  `camelBack`

## Licence

GPL-3.0-or-later (plugin API headers dual-licensed — see
[requirements.md §1.4](docs/requirements.md#14-technology-stack-con-3)).
