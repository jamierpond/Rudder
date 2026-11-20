cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)

project(Rudder VERSION 0.1.0 LANGUAGES C CXX)

# An app that publishes contexts (Grazer, CowTerm) pulls Rudder in with lockstep_add_package for
# the client library alone: no app, no tests, no extra dependencies -- it
# brings eacp (and with it Miro) itself. The libraries the pond apps share
# (PondApp, SessionLog, SearchKeys, ...) come from PondDev, not from here.
option(RUDDER_CLIENT_ONLY "Build only the RudderClient library" OFF)

if (RUDDER_CLIENT_ONLY)
    if (APPLE)
        enable_language(OBJCXX)
    endif ()

    add_subdirectory(Client)
    return()
endif ()

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No source this project compiles uses C++20 modules, so scanning each one for them is wasted work.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Work against a local eacp checkout with:
#   cmake -B build -DLOCKSTEP_eacp_SOURCE=$HOME/projects/eacp
#
# eacp jp/develop: develop plus Jamie's unmerged fixes, one commit each.
# Rudder needs #143 (GlobalHotKey::isAvailable). Move back to develop once it merges.
lockstep_add_package(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG jp/develop
        SYSTEM YES)

# The libraries every pond app shares: PondApp, SessionLog, FrecencySearch,
# SearchKeys, PermissionsPanel and pond_add_version_library. After eacp, so
# PondDev uses this eacp instead of fetching its own. Work against a local
# checkout with:
#   cmake -B build -DLOCKSTEP_PondDev_SOURCE=$HOME/projects/PondDev
lockstep_add_package(
        NAME PondDev
        GITHUB_REPOSITORY jamierpond/PondDev
        GIT_TAG main
        SYSTEM YES)

# Crash reports (Crashout): Rudder writes one to
# <app support>/Rudder/Crashes/pending/<id>/ when it crashes, which
# scripts/crash-reports.py lists. Added here, past the client-only return, so
# Grazer and CowTerm never get a second copy through RudderClient.
lockstep_add_package(
        NAME Crashout
        GITHUB_REPOSITORY jamierpond/Crashout
        GIT_TAG main
        SYSTEM YES)

# Local persistence (Firestore-flavoured JSON documents). Must come after
# eacp -- emberstore fetches its own copy only when no eacp targets exist.
lockstep_add_package(
        NAME emberstore
        GITHUB_REPOSITORY tamber-inc/emberstore
        GIT_TAG main
        SYSTEM YES)

if (NOT TARGET NanoTest)
    lockstep_add_package(
            NAME NanoTest
            GITHUB_REPOSITORY eyalamirmusic/NanoTest
            GIT_TAG main
            SYSTEM YES)
endif ()

# Set after the packages, so only this project's own targets build with -Werror.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

add_subdirectory(Client)

# The switcher's pure logic (ranking, MRU, back stack, keybindings, context
# providers) lives in a plain static library so it stays unit-testable without
# the app shell.
add_library(RudderSwitcher STATIC
        Lib/Switcher/BackStack.cpp
        Lib/Switcher/CaptureGate.cpp
        Lib/Switcher/ClosingContexts.cpp
        Lib/Switcher/CycleGesture.cpp
        Lib/Switcher/DragHandle.cpp
        Lib/Switcher/Favourites.cpp
        Lib/Switcher/Focus.cpp
        Lib/Switcher/Frecency.cpp
        Lib/Switcher/Keybinding.cpp
        Lib/Switcher/LearnedPickStore.cpp
        Lib/Switcher/LineEdit.cpp
        Lib/Switcher/ListedRows.cpp
        Lib/Switcher/Mru.cpp
        Lib/Switcher/PanelFrame.cpp
        Lib/Switcher/Providers.cpp
        Lib/Switcher/Ranking.cpp
        Lib/Switcher/RenamedAppIds.cpp
        Lib/Switcher/Renames.cpp
        Lib/Switcher/Slots.cpp
        Lib/Switcher/SnapshotFrame.cpp
        Lib/Switcher/TextFit.cpp
        Lib/Switcher/Url.cpp
        Lib/Switcher/Web.cpp)

target_include_directories(RudderSwitcher PUBLIC Lib/Switcher)
target_link_libraries(RudderSwitcher PUBLIC emberstore FrecencySearch RudderProtocol)

# The OS seam: running apps, activation, preview and the quick-panel window
# tricks, one file per feature and platform behind the headers in Platform/.
if (APPLE)
    add_library(RudderPlatform STATIC
            Platform/ContextServer-macOS.mm
            Platform/Permissions-macOS.mm
            Platform/Platform-macOS.mm
            Platform/QuickWindow-macOS.mm)
    target_link_libraries(RudderPlatform PRIVATE
            "-framework AppKit"
            "-framework ApplicationServices"
            "-framework Carbon"
            "-weak_framework ScreenCaptureKit")
    # The tamber-web ports (QuickWindow, Permissions) keep their original
    # manual retain/release; Rudder's own platform code is ARC.
    set_source_files_properties(Platform/Platform-macOS.mm Platform/ContextServer-macOS.mm
            PROPERTIES COMPILE_OPTIONS -fobjc-arc)
elseif (WIN32)
    add_library(RudderPlatform STATIC
            Platform/ContextServer-Windows.cpp
            Platform/Permissions-Windows.cpp
            Platform/Platform-Windows.cpp
            Platform/QuickWindow-Windows.cpp)
    target_link_libraries(RudderPlatform PRIVATE
            eacp-network
            dwmapi
            shcore
            shlwapi
            version)
endif ()

target_include_directories(RudderPlatform PUBLIC Platform)
target_link_libraries(RudderPlatform PUBLIC eacp-graphics RudderProtocol RudderSwitcher)

# The panel is an eacp UI component tree in one GPUView: no web view, so no
# second process to keep warm.
add_executable(Rudder
        Source/HotKeys.cpp
        Source/Icon.cpp
        Source/Main.cpp
        Source/PanelDump.cpp
        Source/PanelView.cpp)

target_link_libraries(Rudder PRIVATE eacp-ui PondApp RudderSwitcher RudderPlatform SearchKeys SessionLog)
pond_add_version_library(RudderVersion)
target_link_libraries(Rudder PRIVATE RudderVersion)

eacp_enable_crash_reporting(Rudder SYMBOLS_DIR ${PROJECT_BINARY_DIR}/symbols)
target_link_libraries(Rudder PRIVATE eacp-crash-faults)

set(BUNDLE_ID "audio.pond.rudder")

# LSUIElement: an agent app. It launches with no Dock icon and no flash before
# setDockIconVisible(false) confirms it.
set_target_properties(Rudder PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Rudder"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID}
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Source/Info.plist.in")

eacp_set_gui_subsystem(Rudder)

# Assets/Icon is made by scripts/make-icons.sh. macOS builds its .icns from
# Icon.png; Windows takes a prebuilt .ico whose small frames are a plain
# silhouette, which IMAGE (one picture downscaled to every size) can't express.
if (WIN32)
    eacp_set_app_icon(Rudder ICO "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/Icon.ico")
else ()
    eacp_set_app_icon(Rudder IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Icon/Icon.png")
endif ()

find_package(ResEmbed REQUIRED)
res_embed_add(Rudder FILES
        Assets/Icon/Icon.png
        Assets/Icon/Tray.png
        Assets/Icon/TrayWindows.png)

if (PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(Tests)
endif ()
