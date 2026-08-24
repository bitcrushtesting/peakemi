# Warning configuration. Warnings are on everywhere; promoting them to errors is
# opt-in via PEAKEMI_WARNINGS_AS_ERRORS (CI turns it on, local builds usually not).
include_guard(GLOBAL)

add_library(peakemi_warnings INTERFACE)
add_library(peakemi::warnings ALIAS peakemi_warnings)

set(PEAKEMI_MSVC_WARNINGS
    /W4
    /permissive-        # standards conformance
    /w14242 /w14254 /w14263 /w14265 /w14287 /we4289
    /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555
    /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
    /wd4127             # conditional expression is constant (noisy with Qt macros)
)

set(PEAKEMI_GCC_CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
)

set(PEAKEMI_GCC_EXTRA_WARNINGS
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wuseless-cast
)

if(MSVC)
    set(PEAKEMI_WARNINGS ${PEAKEMI_MSVC_WARNINGS})
    if(PEAKEMI_WARNINGS_AS_ERRORS)
        list(APPEND PEAKEMI_WARNINGS /WX)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(PEAKEMI_WARNINGS ${PEAKEMI_GCC_CLANG_WARNINGS} ${PEAKEMI_GCC_EXTRA_WARNINGS})
    if(PEAKEMI_WARNINGS_AS_ERRORS)
        list(APPEND PEAKEMI_WARNINGS -Werror)
    endif()
else() # Clang / AppleClang
    set(PEAKEMI_WARNINGS ${PEAKEMI_GCC_CLANG_WARNINGS})
    if(PEAKEMI_WARNINGS_AS_ERRORS)
        list(APPEND PEAKEMI_WARNINGS -Werror)
    endif()
endif()

target_compile_options(peakemi_warnings INTERFACE ${PEAKEMI_WARNINGS})
