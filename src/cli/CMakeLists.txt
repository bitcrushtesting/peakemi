# The headless runner. Deliberately free of Qt6::Widgets: everything here has to
# work on a build machine with no display and no window system, and the one
# thing that does need the toolkit -- the spectrum image in the PDF report -- is
# injected by the executable instead of linked in here.
peakemi_add_module(cli
    QT_AUTOMOC
    SOURCES
        options/CommandLine.cpp
        run/HeadlessRunner.cpp
    HEADERS
        include/peakemi/cli/CommandLine.h
        include/peakemi/cli/HeadlessRunner.h
    PUBLIC
        peakemi::core
        peakemi::hal
        peakemi::drivers
        peakemi::python
        peakemi::reporting
        Qt6::Core
        Qt6::Gui)
