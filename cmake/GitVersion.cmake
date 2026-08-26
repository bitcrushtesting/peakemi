# GitVersion: derives the project version from the git tag.
#
# The tag is the only place the version is written down. Nothing in the tree
# repeats it, so nothing in the tree can disagree with it, and releasing is
# tagging rather than tagging plus remembering to bump a literal.
#
# Include this before project(). After it, the following are set:
#
#   PEAKEMI_VERSION        "0.2.0"                 numeric, for project(VERSION)
#   PEAKEMI_VERSION_MAJOR  0
#   PEAKEMI_VERSION_MINOR  2
#   PEAKEMI_VERSION_PATCH  0
#   PEAKEMI_VERSION_LABEL  "0.2.0" or "0.3.0-rc1"  the tag, without the leading v
#   PEAKEMI_VERSION_COMMIT "abc1234"               short hash, or "unknown"
#   PEAKEMI_VERSION_DIRTY  0 or 1                  uncommitted changes present
#   PEAKEMI_VERSION_FULL   "0.2.0+abc1234.dirty"   what a build reports about itself
#
# Set -DPEAKEMI_EXPECT_VERSION=<label> to make a mismatch a configure error.
# The release workflow passes the tag it is building, so a build that cannot
# see the tag fails there rather than shipping an artifact labelled 0.0.0.

cmake_minimum_required(VERSION 3.24)

find_package(Git QUIET)

# Runs git and returns its trimmed stdout, or "" if git is missing or fails —
# an unpacked tarball has no repository, and that is not an error here.
function(_git_output outvar)
    if(NOT GIT_FOUND)
        set(${outvar} "" PARENT_SCOPE)
        return()
    endif()
    execute_process(
        COMMAND ${GIT_EXECUTABLE} ${ARGN}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _out
        ERROR_QUIET
        RESULT_VARIABLE _result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_result EQUAL 0)
        set(${outvar} "${_out}" PARENT_SCOPE)
    else()
        set(${outvar} "" PARENT_SCOPE)
    endif()
endfunction()

# --match keeps an unrelated tag from becoming the version, and --abbrev=0
# picks the nearest release tag that is an ancestor of HEAD.
_git_output(_tag describe --tags --abbrev=0 --match "v[0-9]*")

if(_tag STREQUAL "")
    set(PEAKEMI_VERSION_LABEL "0.0.0")
    # Loud, because every artifact this build produces carries the wrong
    # version: usually a shallow clone that fetched no tags, occasionally a
    # source tree with no repository at all.
    message(WARNING
        "No v* git tag is visible, so the version falls back to 0.0.0. "
        "Fetch tags (git fetch --tags, or actions/checkout with fetch-depth: 0) "
        "if this build is going anywhere.")
else()
    string(REGEX REPLACE "^v" "" PEAKEMI_VERSION_LABEL "${_tag}")
endif()

# project(VERSION) takes digits only, so a pre-release tag such as v0.3.0-rc1
# contributes 0.3.0 there while the label keeps the suffix.
if(PEAKEMI_VERSION_LABEL MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(PEAKEMI_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(PEAKEMI_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(PEAKEMI_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
    message(FATAL_ERROR
        "Git tag '${_tag}' is not of the form vMAJOR.MINOR.PATCH[-suffix].")
endif()
set(PEAKEMI_VERSION
    "${PEAKEMI_VERSION_MAJOR}.${PEAKEMI_VERSION_MINOR}.${PEAKEMI_VERSION_PATCH}")

_git_output(PEAKEMI_VERSION_COMMIT rev-parse --short HEAD)
if(PEAKEMI_VERSION_COMMIT STREQUAL "")
    set(PEAKEMI_VERSION_COMMIT "unknown")
endif()

# Tracked changes only, as git describe --dirty counts them: an untracked
# scratch file in someone's working copy did not go into the binary.
_git_output(_status status --porcelain --untracked-files=no)
if(_status STREQUAL "")
    set(PEAKEMI_VERSION_DIRTY 0)
else()
    set(PEAKEMI_VERSION_DIRTY 1)
endif()

set(PEAKEMI_VERSION_FULL "${PEAKEMI_VERSION_LABEL}+${PEAKEMI_VERSION_COMMIT}")
if(PEAKEMI_VERSION_DIRTY)
    string(APPEND PEAKEMI_VERSION_FULL ".dirty")
endif()

if(DEFINED PEAKEMI_EXPECT_VERSION
   AND NOT PEAKEMI_EXPECT_VERSION STREQUAL PEAKEMI_VERSION_LABEL)
    message(FATAL_ERROR
        "Version mismatch: expected ${PEAKEMI_EXPECT_VERSION}, but the git tag "
        "says ${PEAKEMI_VERSION_LABEL}.")
endif()

# The version is read at configure time, so a new tag has to re-run configure
# to reach the build. HEAD moves on every commit and checkout; the ref files
# cover a tag created or fetched without HEAD moving.
foreach(_watched HEAD packed-refs)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/${_watched}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/.git/${_watched}")
    endif()
endforeach()
if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.git/refs/tags")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/.git/refs/tags")
endif()
