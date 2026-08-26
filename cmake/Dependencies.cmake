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
# pkg-config finds it on Linux; Windows and a stock macOS have no pkg-config at
# all, so fall back to searching for the header and the library directly. Either
# way the rest of the build sees one target: peakemi::libusb.
if(PEAKEMI_WITH_USBTMC)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB QUIET IMPORTED_TARGET libusb-1.0)
    endif()

    if(TARGET PkgConfig::LIBUSB)
        add_library(peakemi_libusb INTERFACE)
        target_link_libraries(peakemi_libusb INTERFACE PkgConfig::LIBUSB)
        message(STATUS "libusb: ${LIBUSB_VERSION} (pkg-config)")
    else()
        find_path(PEAKEMI_LIBUSB_INCLUDE_DIR
            NAMES libusb.h
            PATH_SUFFIXES libusb-1.0)
        find_library(PEAKEMI_LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)

        if(NOT PEAKEMI_LIBUSB_INCLUDE_DIR OR NOT PEAKEMI_LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "PEAKEMI_WITH_USBTMC=ON but libusb-1.0 was not found.\n"
                "  Linux:   apt install libusb-1.0-0-dev\n"
                "  macOS:   brew install libusb\n"
                "  Windows: vcpkg install libusb, then pass CMAKE_TOOLCHAIN_FILE\n"
                "Or set -DPEAKEMI_WITH_USBTMC=OFF.")
        endif()

        add_library(peakemi_libusb INTERFACE)
        target_include_directories(peakemi_libusb SYSTEM INTERFACE "${PEAKEMI_LIBUSB_INCLUDE_DIR}")
        target_link_libraries(peakemi_libusb INTERFACE "${PEAKEMI_LIBUSB_LIBRARY}")
        message(STATUS "libusb: ${PEAKEMI_LIBUSB_LIBRARY}")
    endif()

    add_library(peakemi::libusb ALIAS peakemi_libusb)
endif()
