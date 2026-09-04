# peakemi::drivers and peakemi::reporting already export peakemi::core.
peakemi_add_test(engine_run
    SOURCES EngineRunTest.cpp
    LIBS    peakemi::drivers peakemi::reporting)

# peakemi::ui is linked for the spectrum image the PDF report embeds, which is
# also what the executable injects; the test therefore covers the same path.
peakemi_add_test(headless_run
    SOURCES HeadlessRunTest.cpp
    LIBS    peakemi::cli peakemi::ui)
