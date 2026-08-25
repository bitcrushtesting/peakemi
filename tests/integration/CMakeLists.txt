# peakemi::drivers and peakemi::reporting already export peakemi::core.
peakemi_add_test(engine_run
    SOURCES EngineRunTest.cpp
    LIBS    peakemi::drivers peakemi::reporting)
