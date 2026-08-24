# Central, pinned dependency resolution. Nothing outside this file may call
# find_package() or FetchContent for a third-party library.
include_guard(GLOBAL)

include(FetchContent)

# --- Qt ---------------------------------------------------------------------
set(PEAKEMI_QT_MIN_VERSION 6.5)
set(PEAKEMI_QT_COMPONENTS Core Gui Widgets Network SerialPort Concurrent PrintSupport Svg)
if(PEAKEMI_BUILD_TESTS)
    list(APPEND PEAKEMI_QT_COMPONENTS Test)
endif()

find_package(Qt6 ${PEAKEMI_QT_MIN_VERSION} REQUIRED COMPONENTS ${PEAKEMI_QT_COMPONENTS})
message(STATUS "Qt: ${Qt6_VERSION} (${Qt6_DIR})")

# Charting backend: Qt Graphs preferred, Qt Charts as fallback (architecture.md §7).
find_package(Qt6 ${PEAKEMI_QT_MIN_VERSION} QUIET COMPONENTS Graphs)
if(Qt6Graphs_FOUND)
    set(PEAKEMI_PLOT_BACKEND "Graphs" CACHE INTERNAL "")
else()
    find_package(Qt6 ${PEAKEMI_QT_MIN_VERSION} QUIET COMPONENTS Charts)
    if(Qt6Charts_FOUND)
        set(PEAKEMI_PLOT_BACKEND "Charts" CACHE INTERNAL "")
    else()
        set(PEAKEMI_PLOT_BACKEND "None" CACHE INTERNAL "")
        message(WARNING "Neither Qt Graphs nor Qt Charts found; the plot view will be a stub.")
    endif()
endif()
message(STATUS "Plot backend: ${PEAKEMI_PLOT_BACKEND}")

qt_standard_project_setup(REQUIRES ${PEAKEMI_QT_MIN_VERSION})

# --- nlohmann/json (session, limit and correction file I/O) -----------------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# --- Optional: embedded CPython + pybind11 ----------------------------------
if(PEAKEMI_WITH_PYTHON)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.13.6
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    FetchContent_MakeAvailable(pybind11)
    message(STATUS "Python: ${Python3_VERSION} (${Python3_EXECUTABLE})")
endif()

# --- Optional: libusb for the USBTMC transport ------------------------------
if(PEAKEMI_WITH_USBTMC)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
    endif()
    if(NOT TARGET PkgConfig::LIBUSB)
        message(FATAL_ERROR
            "PEAKEMI_WITH_USBTMC=ON but libusb-1.0 was not found. "
            "Install libusb (brew install libusb / apt install libusb-1.0-0-dev) or set the option OFF.")
    endif()
endif()
