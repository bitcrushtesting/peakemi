cmake_minimum_required(VERSION 3.24)

project(PeakEmi
    VERSION      0.1.0
    DESCRIPTION  "Cross-platform EMI pre-compliance measurement suite"
    HOMEPAGE_URL "https://github.com/bitcrushtesting/peakemi"
    LANGUAGES    CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD          23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

# ---------------------------------------------------------------------------
# Options, warnings, dependencies
# ---------------------------------------------------------------------------
include(ProjectOptions)
include(CompilerWarnings)
include(Dependencies)
include(PeakEmiModule)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(PEAKEMI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(PEAKEMI_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "PeakEmi ${PROJECT_VERSION}")
message(STATUS "  build type ......... ${CMAKE_BUILD_TYPE}")
message(STATUS "  compiler ........... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  tests .............. ${PEAKEMI_BUILD_TESTS}")
message(STATUS "  tools .............. ${PEAKEMI_BUILD_TOOLS}")
message(STATUS "  python plugins ..... ${PEAKEMI_WITH_PYTHON}")
message(STATUS "  usbtmc ............. ${PEAKEMI_WITH_USBTMC}")
message(STATUS "  visa ............... ${PEAKEMI_WITH_VISA}")
message(STATUS "  warnings as errors . ${PEAKEMI_WARNINGS_AS_ERRORS}")
message(STATUS "  sanitizers ......... ${PEAKEMI_ENABLE_SANITIZERS}")
message(STATUS "")
