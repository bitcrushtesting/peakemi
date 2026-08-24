# peakemi_add_module(<name>
#     [SOURCES  <files...>]
#     [HEADERS  <files...>]          # public headers, under include/peakemi/<name>/
#     [PUBLIC   <link libs...>]
#     [PRIVATE  <link libs...>]
#     [QT_AUTOMOC] [QT_AUTOUIC] [QT_AUTORCC])
#
# Creates the static library peakemi_<name> plus the alias peakemi::<name>, wires
# the shared option/warning interface targets and exposes include/ publicly so
# consumers write #include <peakemi/<name>/Foo.hpp>.
include_guard(GLOBAL)

function(peakemi_add_module NAME)
    cmake_parse_arguments(ARG
        "QT_AUTOMOC;QT_AUTOUIC;QT_AUTORCC"
        ""
        "SOURCES;HEADERS;PUBLIC;PRIVATE"
        ${ARGN})

    set(target "peakemi_${NAME}")
    add_library(${target} STATIC ${ARG_SOURCES} ${ARG_HEADERS})
    add_library(peakemi::${NAME} ALIAS ${target})

    target_include_directories(${target}
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    target_link_libraries(${target}
        PUBLIC  ${ARG_PUBLIC}
        PRIVATE ${ARG_PRIVATE} peakemi::options peakemi::warnings)

    # AUTOMOC/AUTOUIC/AUTORCC are evaluated at configure time and do not accept
    # generator expressions, so they are set from plain booleans here.
    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        FOLDER "libs")
    if(ARG_QT_AUTOMOC)
        set_target_properties(${target} PROPERTIES AUTOMOC ON)
    endif()
    if(ARG_QT_AUTOUIC)
        set_target_properties(${target} PROPERTIES AUTOUIC ON)
    endif()
    if(ARG_QT_AUTORCC)
        set_target_properties(${target} PROPERTIES AUTORCC ON)
    endif()
endfunction()

# peakemi_add_test(<name> SOURCES <files...> [LIBS <link libs...>])
# Registers a Qt Test executable with CTest.
function(peakemi_add_test NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    set(target "test_${NAME}")
    add_executable(${target} ${ARG_SOURCES})
    set_target_properties(${target} PROPERTIES AUTOMOC ON FOLDER "tests")
    target_link_libraries(${target}
        PRIVATE ${ARG_LIBS} Qt6::Test peakemi::options peakemi::warnings)

    add_test(NAME ${NAME} COMMAND ${target})
    set_tests_properties(${NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endfunction()
