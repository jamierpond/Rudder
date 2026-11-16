# One version string per app: <semver>+<short commit>, "+<commit>.dirty" when
# tracked files have uncommitted changes, the semver alone outside a git checkout.
#
#   pond_add_version_library(CowTermVersion)
#   target_link_libraries(ct PRIVATE CowTermVersion)
#
#   #include <PondVersion.h>
#   pond::getVersionString(); // "0.1.0+f30059f"
#
#   pond_add_version_test(ct ct) # ctest: `ct --version` prints "ct <semver>..." and exits 0
#
# Rudder includes this file, so every app that adds Rudder with lockstep gets
# the function. This file is identical in every pond app that keeps its own
# copy; change Rudder's, then copy it to them.

if (CMAKE_SCRIPT_MODE_FILE AND DEFINED TESTED_EXECUTABLE)
    execute_process(
            COMMAND "${TESTED_EXECUTABLE}" --version
            OUTPUT_VARIABLE printedVersion
            RESULT_VARIABLE exitCode)

    string(REPLACE "." "\\." semanticVersionPattern "${SEMANTIC_VERSION}")
    set(expectedPattern "^${TOOL_NAME} ${semanticVersionPattern}(\\+[0-9a-f]+(\\.dirty)?)?\r?\n$")

    if (NOT exitCode EQUAL 0 OR NOT printedVersion MATCHES "${expectedPattern}")
        message(FATAL_ERROR "`${TOOL_NAME} --version` exited ${exitCode} and printed "
                "'${printedVersion}', not '${TOOL_NAME} ${SEMANTIC_VERSION}[+<commit>[.dirty]]'")
    endif ()

    return()
endif ()

if (CMAKE_SCRIPT_MODE_FILE)
    set(versionString "${SEMANTIC_VERSION}")

    # A source tarball has no .git, and must not pick up an enclosing checkout's commit.
    if (EXISTS "${SOURCE_DIRECTORY}/.git")
        execute_process(
                COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY "${SOURCE_DIRECTORY}"
                OUTPUT_VARIABLE commit
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE commitResult
                ERROR_QUIET)

        # --no-optional-locks: never take the index lock another git command may hold.
        execute_process(
                COMMAND git --no-optional-locks status --porcelain --untracked-files=no
                WORKING_DIRECTORY "${SOURCE_DIRECTORY}"
                OUTPUT_VARIABLE changedFiles
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)

        if (commitResult EQUAL 0 AND commit)
            string(APPEND versionString "+${commit}")

            if (changedFiles)
                string(APPEND versionString ".dirty")
            endif ()
        endif ()
    endif ()

    # file(CONFIGURE) leaves an unchanged file untouched, so nothing recompiles or relinks.
    file(CONFIGURE OUTPUT "${OUTPUT}" CONTENT [[
#include "PondVersion.h"

std::string pond::getVersionString()
{
    return "@versionString@";
}
]] @ONLY)
    return()
endif ()

function(pond_add_version_library libraryName)
    set(generatedDirectory "${CMAKE_CURRENT_BINARY_DIR}/${libraryName}")
    set(generatedSource "${generatedDirectory}/PondVersion.cpp")

    file(CONFIGURE OUTPUT "${generatedDirectory}/PondVersion.h" CONTENT [[
#pragma once

#include <string>

namespace pond
{
std::string getVersionString();
}
]])

    # Runs on every build, so a new commit shows without reconfiguring.
    add_custom_target(${libraryName}Stamp
            BYPRODUCTS "${generatedSource}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DSOURCE_DIRECTORY=${PROJECT_SOURCE_DIR}"
                    "-DSEMANTIC_VERSION=${PROJECT_VERSION}"
                    "-DOUTPUT=${generatedSource}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            COMMENT "Stamping ${PROJECT_NAME}'s version"
            VERBATIM)

    add_library(${libraryName} STATIC "${generatedSource}")
    add_dependencies(${libraryName} ${libraryName}Stamp)
    target_include_directories(${libraryName} PUBLIC "${generatedDirectory}")
    target_compile_features(${libraryName} PUBLIC cxx_std_17)
endfunction()

function(pond_add_version_test executableTarget toolName)
    add_test(NAME ${executableTarget}PrintsItsVersion
            COMMAND "${CMAKE_COMMAND}"
                    "-DTESTED_EXECUTABLE=$<TARGET_FILE:${executableTarget}>"
                    "-DTOOL_NAME=${toolName}"
                    "-DSEMANTIC_VERSION=${PROJECT_VERSION}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
endfunction()
