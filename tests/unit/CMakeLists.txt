peakemi_add_test(version
    SOURCES VersionTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(units
    SOURCES UnitsTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(limits
    SOURCES LimitsTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(corrections
    SOURCES CorrectionsTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(peakdetector
    SOURCES PeakDetectorTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(session
    SOURCES SessionTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(scpi
    SOURCES ScpiTest.cpp
    LIBS    peakemi::hal)

peakemi_add_test(usbtmc
    SOURCES UsbTmcTest.cpp
    LIBS    peakemi::hal)

peakemi_add_test(vxi11
    SOURCES Vxi11Test.cpp
    LIBS    peakemi::hal)

peakemi_add_test(drivers
    SOURCES DriversTest.cpp
    LIBS    peakemi::drivers)

peakemi_add_test(plugins
    SOURCES PluginTest.cpp
    LIBS    peakemi::python)
# The test drives the shipped example plugin, so it needs to know where it is.
target_compile_definitions(test_plugins PRIVATE
    PEAKEMI_EXAMPLE_PLUGIN="${PROJECT_SOURCE_DIR}/plugins/drivers/example_sweeper.py")

peakemi_add_test(reporting
    SOURCES ReportingTest.cpp
    LIBS    peakemi::reporting nlohmann_json::nlohmann_json)

peakemi_add_test(mainwindow
    SOURCES MainWindowTest.cpp
    LIBS    peakemi::ui)
