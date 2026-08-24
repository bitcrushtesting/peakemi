peakemi_add_test(version
    SOURCES VersionTest.cpp
    LIBS    peakemi::core)

peakemi_add_test(mainwindow
    SOURCES MainWindowTest.cpp
    LIBS    peakemi::ui)
