#pragma once

#include <peakemi/core/Error.h>

#include <string>

namespace peakemi::python {

/// Owns the one embedded CPython interpreter (architecture.md 6).
///
/// Initialised lazily on first plugin use so a run that touches no Python pays
/// nothing for it (NFR-PERF-1), and finalised at shutdown. Every entry point
/// into Python acquires the GIL explicitly; the GIL is released around blocking
/// transport I/O, which is C++ code the plugin calls back into (FR-EXT-5).
class PythonInterpreter
{
public:
    [[nodiscard]] static PythonInterpreter& instance();

    /// Start the interpreter if it is not running yet.
    [[nodiscard]] Status ensureStarted();

    [[nodiscard]] bool isRunning() const { return m_running; }

    /// "3.12.4 (main, ...)", or empty when the interpreter is not running.
    [[nodiscard]] std::string version() const;

    /// Stop the interpreter. Nothing may call into Python afterwards.
    void shutdown();

private:
    PythonInterpreter() = default;
    ~PythonInterpreter();

    PythonInterpreter(const PythonInterpreter&) = delete;
    PythonInterpreter& operator=(const PythonInterpreter&) = delete;
    PythonInterpreter(PythonInterpreter&&) = delete;
    PythonInterpreter& operator=(PythonInterpreter&&) = delete;

    bool m_running{false};
};

} // namespace peakemi::python
