# Contributing to PeakEmi

Everything a contributor needs that a user does not: where the code lives, how it is
written, how it is tested and how a release is cut. For what PeakEmi does, and how to
build and run it, see the [README](README.md).

Before changing behaviour, read the [requirements](docs/requirements.md). Each one has an
identifier, and commits and comments cite them (`FR-RUN-3`, `NFR-PERF-2`) so a reader can
find out why the code is the way it is. The [architecture](docs/architecture.md) explains
the module boundaries and the decisions behind them.

## Repository layout

```
cmake/          build modules: options, warnings, dependencies, module helper
docs/           requirements, architecture and the README screenshots
plugins/        the worked example driver plugin
resources/      example limit lines and correction tables
src/core/       domain model, limits, corrections, engine, session (no Qt Widgets)
src/hal/        transports, SCPI helpers, driver registry, discovery
src/drivers/    simulated driver and the SCPI instrument drivers
src/python/     embedded CPython bridge, plugin discovery and trust store
src/reporting/  CSV/JSON export and PDF report renderer
src/ui/         Qt Widgets user interface, plot layer, view models
src/app/        composition root and main()
tests/          unit, component and integration tests plus fixtures
tools/          developer tools (documentation screenshots, PEAKEMI_BUILD_TOOLS=ON)
```

The `python` module (embedded CPython plugin bridge) lands with milestone M7 —
see [architecture.md §14](docs/architecture.md#14-implementation-roadmap).

## Code style

* Formatting: `.clang-format` (clang-format ≥ 16). `clang-format -i $(git ls-files '*.cpp' '*.h')`
* Static analysis: `.clang-tidy`, enabled in the build with `-DPEAKEMI_ENABLE_CLANG_TIDY=ON`
* Editor defaults: `.editorconfig`
* Includes are project-rooted: `#include <peakemi/core/Version.h>`
* Private members use the `m_` prefix; namespaces are lower case, types `CamelCase`, functions
  `camelBack`

## Tests

```bash
ctest --preset debug                                          # the whole suite
ctest --preset debug -R limits                                # one executable
QT_QPA_PLATFORM=offscreen ./build/debug/bin/test_engine_run   # directly, for the output
```

Everything runs headless, including the UI tests and the integration tests that drive a
complete two-phase run against the simulated analyzer, once through the measurement engine
and once through `peakemi-cli`. A new instrument driver ships with a recorded transcript
fixture; a fixed bug ships with the test that would have caught it.

The optional features are built out by default, so switch them in before touching them:

```bash
cmake --preset debug -DPEAKEMI_WITH_USBTMC=ON -DPEAKEMI_WITH_VISA=ON -DPEAKEMI_WITH_PYTHON=ON
```

## What CI checks, and what it will catch that you did not

Every pull request is built on Windows/MSVC, Ubuntu/GCC and macOS/AppleClang with warnings
as errors, runs the suite headless, and is checked with `clang-format` and `clang-tidy`.
The analysis job builds with every optional feature on, so optional code cannot rot
unnoticed. Each platform then drives the `peakemi-cli` binary it just built through a
simulated run: the exit-code contract is a promise made to build scripts outside this
process, and no in-process test can check it.

Three differences between a local build and CI have each produced a red build that was
green locally. They are worth knowing before you spend a round trip on one:

* **Use the same clang-format and clang-tidy versions CI does** (currently 20). Different
  releases format differently, and a newer analyser reports different findings; both make
  a formatting job fail on code your editor just formatted.
* **CI builds against the minimum supported Qt**, not the newest. Code that compiles
  against a newer Qt locally can still fail there — Qt's own macros and overload sets
  differ between versions. A concrete one: the tree is built with
  `QT_NO_CAST_FROM_ASCII`, and `QString::arg("text")` compiles against Qt 6.10 but not
  against the 6.8 minimum. Wrap every string literal handed to a Qt API in
  `QStringLiteral` or `tr()`.
* **libstdc++ and libc++ disagree about which headers include which.** A translation unit
  that compiles on macOS can fail on Linux for a missing `<cstdint>`. Include what you use.

## Versioning

**The git tag is the version.** `cmake/GitVersion.cmake` reads the nearest `v*` tag at
configure time and hands it to `project(VERSION ...)`; nothing in the tree repeats it, so
nothing in the tree can disagree with it. A build also reports the commit it came from, and
says so when the working tree was dirty:

```
-- PeakEmi 0.2.0+3d09efc.dirty
```

That string is `peakemi::ProjectVersionFull`, and it is the first line of every log — which
build produced a set of measurements is a question that gets asked months later.

Two consequences worth knowing:

* **Clone with tags.** A shallow clone sees no tag, so the version falls back to `0.0.0` with
  a warning. CI checks out with `fetch-depth: 0` for this reason.
* **A new tag re-runs configure by itself.** The module watches `.git/HEAD`, `.git/packed-refs`
  and `.git/refs/tags`, so `cmake --build` after tagging picks the new version up rather than
  building a stale one.

## Releases

Tagging a commit on `main` with `vMAJOR.MINOR.PATCH` builds, tests and publishes an AppImage,
a macOS `.dmg` and a Windows zip as a GitHub release — the tag is the whole procedure, there
is no version to bump first:

```bash
git tag -a v0.2.0 -m "PeakEmi 0.2.0"
git push origin v0.2.0
```

Tags that do not point at a commit on `main` are rejected before anything is built. The
release build is configured with `-DPEAKEMI_EXPECT_VERSION=<version>`, so a checkout that
cannot see the tag fails the build instead of publishing an artifact labelled `0.0.0`. A tag
such as `v0.2.0-rc1` publishes a pre-release, and reports itself as `0.2.0-rc1` while
`project(VERSION)` uses the numeric `0.2.0`.

### Building the macOS disk image by hand

```bash
cmake --build --preset release --target dmg
```

This deploys Qt into `PeakEmi.app`, signs it, and writes
`build/release/PeakEmi-<version>-macos-<arch>.dmg` containing the application, a
symlink to `/Applications` to drag it onto, and the licence. The image is then mounted and
the application inside it is launched, so a bundle that still depends on the Qt of the
machine that built it fails at build time instead of on a user's machine.

The bundle carries its own icon, the example limit lines and correction tables, and the
`offscreen` platform plugin, so the shipped application can also be scripted without a
window server.

Signing uses an ad-hoc signature unless you pass a real identity:

```bash
scripts/macos/make-dmg.sh --app build/release/bin/peakemi.app \
    --output PeakEmi.dmg --version 0.2.0 --qt-bin "$QT_ROOT_DIR/bin" \
    --identity "Developer ID Application: Your Name (TEAMID)"
```

An ad-hoc signature runs on the machine that built it; distributing without a Developer ID
and notarisation still makes Gatekeeper warn on first launch (milestone M8).
