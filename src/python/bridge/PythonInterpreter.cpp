#include "bridge/PythonIncludes.h"

#include <peakemi/core/Logging.h>
#include <peakemi/python/PythonInterpreter.h>

#ifdef PEAKEMI_HAVE_PYTHON
namespace py = pybind11;
#endif

namespace peakemi::python {

PythonInterpreter& PythonInterpreter::instance()
{
    static PythonInterpreter interpreter;
    return interpreter;
}

PythonInterpreter::~PythonInterpreter()
{
    // Finalising the interpreter runs Python code, which can raise. A
    // destructor must not let that escape.
    try {
        shutdown();
    } catch (const std::exception& error) {
        qCWarning(lcDriver) << "stopping the embedded interpreter failed:" << error.what();
    } catch (...) {
        qCWarning(lcDriver) << "stopping the embedded interpreter failed";
    }
}

#ifdef PEAKEMI_HAVE_PYTHON

Status PythonInterpreter::ensureStarted()
{
    if (m_running) {
        return {};
    }
    try {
        py::initialize_interpreter();
        m_running = true;
        // Release the GIL held by initialisation: acquisition threads take it
        // when they call in, and holding it here would block them all.
        PyEval_SaveThread();
        qCInfo(lcDriver).noquote()
            << "embedded Python started:" << QString::fromStdString(version());
        return {};
    } catch (const std::exception& error) {
        return fail(ErrorCode::NotImplemented,
                    std::string{"the embedded Python interpreter failed to start: "} +
                        error.what());
    }
}

std::string PythonInterpreter::version() const
{
    if (!m_running) {
        return {};
    }
    const py::gil_scoped_acquire gil;
    try {
        return py::module_::import("sys").attr("version").cast<std::string>();
    } catch (const std::exception&) {
        return {};
    }
}

void PythonInterpreter::shutdown()
{
    if (!m_running) {
        return;
    }
    // Take the GIL back before finalising; it was released after start-up.
    PyGILState_Ensure();
    py::finalize_interpreter();
    m_running = false;
}

#else // PEAKEMI_HAVE_PYTHON

Status PythonInterpreter::ensureStarted()
{
    return fail(ErrorCode::NotImplemented,
                "this build has no embedded Python; configure with -DPEAKEMI_WITH_PYTHON=ON");
}

std::string PythonInterpreter::version() const
{
    return {};
}

void PythonInterpreter::shutdown() {}

#endif // PEAKEMI_HAVE_PYTHON

} // namespace peakemi::python
