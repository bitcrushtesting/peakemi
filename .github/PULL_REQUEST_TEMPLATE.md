## What this changes

<!-- What the reader will see differently once this is merged. -->

## Why

<!--
Cite the requirement IDs this serves (FR-RUN-3, NFR-PERF-2, CON-1). If it changes
behaviour that requirements.md describes, update that file in the same pull request:
the specification and the code disagreeing is worse than either being wrong alone.
-->

## How it was tested

<!--
Which tests were added or changed, and what you ran against real hardware, if anything.
"A fixed bug ships with the test that would have caught it."
-->

## Checklist

- [ ] Formatted with clang-format 20 (the version CI runs; other releases format differently)
- [ ] `ctest --preset debug` passes
- [ ] Optional features still build: `-DPEAKEMI_WITH_USBTMC=ON -DPEAKEMI_WITH_VISA=ON -DPEAKEMI_WITH_PYTHON=ON`
- [ ] Every string literal handed to a Qt API is wrapped in `QStringLiteral` or `tr()` (the tree builds with `QT_NO_CAST_FROM_ASCII`, and CI builds against the minimum Qt, not yours)
- [ ] Documentation updated if behaviour changed: README, `docs/requirements.md`, `docs/architecture.md`
- [ ] No claim of formal compliance is introduced anywhere (CON-1)

## If this adds or changes an instrument driver

<!-- Delete this section if it does not. -->

- [ ] An `InstrumentProfile` entry in `src/drivers/scpi/InstrumentProfiles.cpp`, with the vendor and model strings matched against `*IDN?`
- [ ] `Capabilities` reflect what the instrument can actually do, not what would be convenient. They are what refuses an impossible sweep with a reason instead of sending it
- [ ] A component test driving the real driver against a recorded transcript in `tests/fixtures/ScriptedTransport.h`, so the driver stays correct with no instrument present
- [ ] The quasi-peak detector and the CISPR bandwidths (200 Hz, 9 kHz, 120 kHz, 1 MHz) are either supported or documented as absent. PeakEmi's second phase depends on them
- [ ] The supported-analyzer list in the README names the new model
- [ ] Say what hardware this was verified against, and what remains untested

<!--
By opening this pull request you agree to license your contribution under
GPL-3.0-or-later, matching the project. Plugin API headers are dual-licensed so that
proprietary in-house drivers remain possible; see requirements.md 1.4.
-->
