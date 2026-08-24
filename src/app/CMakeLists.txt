qt_add_executable(peakemi
    main.cpp)

target_link_libraries(peakemi
    PRIVATE
        peakemi::ui
        peakemi::core
        peakemi::options
        peakemi::warnings)

set_target_properties(peakemi PROPERTIES
    FOLDER "app"
    WIN32_EXECUTABLE      TRUE
    MACOSX_BUNDLE         TRUE
    MACOSX_BUNDLE_BUNDLE_NAME       "PeakEmi"
    MACOSX_BUNDLE_GUI_IDENTIFIER    "org.peakemi.PeakEmi"
    MACOSX_BUNDLE_BUNDLE_VERSION    "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

install(TARGETS peakemi
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)
