qt_add_executable(peakemi
    main.cpp)

# peakemi::ui pulls in core, hal, drivers and reporting transitively.
target_link_libraries(peakemi
    PRIVATE
        peakemi::ui
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

# Freedesktop integration, also consumed by linuxdeploy when building the
# AppImage in the release workflow.
if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
        DESTINATION share/licenses/peakemi)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/peakemi.desktop"
        DESTINATION share/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/peakemi.png"
        DESTINATION share/icons/hicolor/256x256/apps)
endif()
