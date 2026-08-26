#pragma once

/// Includes pybind11 and, through it, the CPython headers.
///
/// Qt defines `slots` as a keyword macro, and CPython's object.h has a struct
/// member of that name, so including the two in the wrong order fails to
/// compile. Every file that touches pybind11 includes this header instead of
/// pybind11 directly, so the workaround lives in exactly one place.
#ifdef PEAKEMI_HAVE_PYTHON

#    pragma push_macro("slots")
#    pragma push_macro("signals")
#    undef slots
#    undef signals

#    include <pybind11/chrono.h>
#    include <pybind11/embed.h>
#    include <pybind11/pybind11.h>
#    include <pybind11/stl.h>

#    pragma pop_macro("signals")
#    pragma pop_macro("slots")

#endif // PEAKEMI_HAVE_PYTHON
