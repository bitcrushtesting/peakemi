qt_add_executable(peakemi_screenshots
    Screenshots.cpp)

target_link_libraries(peakemi_screenshots
    PRIVATE
        peakemi::ui
        peakemi::options
        peakemi::warnings)

set_target_properties(peakemi_screenshots PROPERTIES
    FOLDER "tools"
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE FALSE)
