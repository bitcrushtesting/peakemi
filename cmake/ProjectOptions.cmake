# Project-wide build options and the interface targets that carry them.
include_guard(GLOBAL)

include(CMakeDependentOption)

option(PEAKEMI_BUILD_TESTS        "Build the PeakEmi test suite"                 ON)
option(PEAKEMI_WITH_PYTHON        "Embed CPython for Python driver plugins"      OFF)
option(PEAKEMI_WITH_USBTMC        "Build the USBTMC transport (requires libusb)" OFF)
option(PEAKEMI_WITH_VISA          "Enable the optional VISA transport"           OFF)
option(PEAKEMI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"            OFF)
option(PEAKEMI_ENABLE_CLANG_TIDY  "Run clang-tidy as part of the build"          OFF)
option(PEAKEMI_ENABLE_SANITIZERS  "Enable address+undefined sanitizers"          OFF)
option(PEAKEMI_ENABLE_COVERAGE    "Instrument for code coverage (GCC/Clang)"     OFF)

# peakemi_options: compile options every PeakEmi target inherits.
add_library(peakemi_options INTERFACE)
add_library(peakemi::options ALIAS peakemi_options)

target_compile_features(peakemi_options INTERFACE cxx_std_23)

target_compile_definitions(peakemi_options INTERFACE
    $<$<CONFIG:Debug>:PEAKEMI_DEBUG>
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_USE_QSTRINGBUILDER
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

if(PEAKEMI_ENABLE_SANITIZERS)
    if(MSVC)
        target_compile_options(peakemi_options INTERFACE /fsanitize=address)
    else()
        target_compile_options(peakemi_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(peakemi_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

if(PEAKEMI_ENABLE_COVERAGE)
    if(MSVC)
        message(WARNING "PEAKEMI_ENABLE_COVERAGE is not supported with MSVC; ignoring.")
    else()
        target_compile_options(peakemi_options INTERFACE --coverage -O0 -g)
        target_link_options(peakemi_options INTERFACE --coverage)
    endif()
endif()

if(PEAKEMI_ENABLE_CLANG_TIDY)
    find_program(PEAKEMI_CLANG_TIDY_EXE NAMES clang-tidy)
    if(PEAKEMI_CLANG_TIDY_EXE)
        # include() does not open a new scope: this lands in the including directory
        # scope (the top level) and is therefore inherited by every subdirectory.
        set(CMAKE_CXX_CLANG_TIDY "${PEAKEMI_CLANG_TIDY_EXE};--quiet")
        message(STATUS "clang-tidy: ${PEAKEMI_CLANG_TIDY_EXE}")
    else()
        message(WARNING "PEAKEMI_ENABLE_CLANG_TIDY is ON but clang-tidy was not found.")
    endif()
endif()
