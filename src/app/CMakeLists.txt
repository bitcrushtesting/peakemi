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

# ---------------------------------------------------------------------------
# macOS bundle: icon, Info.plist and the disk image
# ---------------------------------------------------------------------------
if(APPLE)
    # The icon is rendered from the 1024 px source at build time rather than
    # committed as a binary, so every slot is drawn at its own resolution.
    set(PEAKEMI_ICNS "${CMAKE_CURRENT_BINARY_DIR}/peakemi.icns")
    add_custom_command(
        OUTPUT  "${PEAKEMI_ICNS}"
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/macos/make-icns.sh"
                "${PROJECT_SOURCE_DIR}/resources/peakemi-1024.png"
                "${PEAKEMI_ICNS}"
        DEPENDS "${PROJECT_SOURCE_DIR}/resources/peakemi-1024.png"
                "${PROJECT_SOURCE_DIR}/scripts/macos/make-icns.sh"
        COMMENT "Rendering the application icon"
        VERBATIM)

    target_sources(peakemi PRIVATE "${PEAKEMI_ICNS}")
    set_source_files_properties("${PEAKEMI_ICNS}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)

    set(MACOSX_BUNDLE_EXECUTABLE_NAME peakemi)
    set(MACOSX_BUNDLE_BUNDLE_NAME PeakEmi)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER org.peakemi.PeakEmi)
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING
        "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
    set(MACOSX_BUNDLE_ICON_FILE peakemi.icns)
    set(MACOSX_BUNDLE_COPYRIGHT "GPL-3.0-or-later. Pre-compliance data only.")
    # Named separately: the plist is configured in this scope, and the cache
    # variable does not reach it reliably enough to leave the key empty.
    set(MACOSX_BUNDLE_MINIMUM_SYSTEM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(NOT MACOSX_BUNDLE_MINIMUM_SYSTEM_VERSION)
        set(MACOSX_BUNDLE_MINIMUM_SYSTEM_VERSION "13.0")
    endif()

    set_target_properties(peakemi PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")

    # The example limit lines and correction tables travel inside the bundle,
    # so a user who installs from the disk image has them to hand rather than
    # having to find the repository.
    add_custom_command(TARGET peakemi POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${PROJECT_SOURCE_DIR}/resources/limits"
                "$<TARGET_BUNDLE_CONTENT_DIR:peakemi>/Resources/examples/limits"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${PROJECT_SOURCE_DIR}/resources/corrections"
                "$<TARGET_BUNDLE_CONTENT_DIR:peakemi>/Resources/examples/corrections"
        COMMENT "Adding the example files to the bundle"
        VERBATIM)

    # `cmake --build . --target dmg` produces a disk image from the bundle that
    # was just built: Qt deployed inside it, an Applications symlink to drag
    # onto, the licence, and a mount-and-launch check before it is called done.
    set(PEAKEMI_DMG "${CMAKE_BINARY_DIR}/PeakEmi-${PROJECT_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}.dmg")
    add_custom_target(dmg
        COMMAND "${PROJECT_SOURCE_DIR}/scripts/macos/make-dmg.sh"
                --app "$<TARGET_BUNDLE_DIR:peakemi>"
                --output "${PEAKEMI_DMG}"
                --version "${PROJECT_VERSION}"
                --qt-bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}"
        DEPENDS peakemi
        COMMENT "Bundling ${PEAKEMI_DMG}"
        VERBATIM
        USES_TERMINAL)
    set_target_properties(dmg PROPERTIES FOLDER "app")
endif()

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
