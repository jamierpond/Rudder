cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Rudder VERSION 0.1.0 LANGUAGES C CXX)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CPM.cmake)

# Work against a local eacp checkout with:
#   cmake -B build -DCPM_eacp_SOURCE=$HOME/projects/eacp
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY jamierpond/eacp
        GIT_TAG main
)

# Local persistence (Firestore-flavoured JSON documents). Must come after
# eacp -- emberstore fetches its own copy only when no eacp targets exist.
CPMAddPackage(
        NAME emberstore
        GITHUB_REPOSITORY tamber-inc/emberstore
        GIT_TAG main
)

list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# The switcher's pure logic (ranking, MRU, back stack, keybindings, context
# providers) lives in a plain static library so it stays unit-testable without
# the app shell.
add_library(RudderSwitcher STATIC
        Lib/Switcher/BackStack.cpp
        Lib/Switcher/Keybinding.cpp
        Lib/Switcher/LineEdit.cpp
        Lib/Switcher/Mru.cpp
        Lib/Switcher/Providers.cpp
        Lib/Switcher/Ranking.cpp)

target_include_directories(RudderSwitcher PUBLIC Lib/Switcher)
target_link_libraries(RudderSwitcher PUBLIC emberstore)

# The OS seam: running apps, activation, preview and the quick-panel window
# tricks. Only macOS is implemented; a Platform-Windows.cpp slots in here.
if (APPLE)
    add_library(RudderPlatform STATIC
            Platform/Permissions.mm
            Platform/Platform-macOS.mm
            Platform/QuickWindow-macOS.mm)
    target_link_libraries(RudderPlatform PRIVATE
            "-framework AppKit"
            "-framework ApplicationServices")
    # The tamber-web ports (QuickWindow, Permissions) keep their original
    # manual retain/release; Rudder's own platform code is ARC.
    set_source_files_properties(Platform/Platform-macOS.mm
            PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif ()

target_include_directories(RudderPlatform PUBLIC Platform)
target_link_libraries(RudderPlatform PUBLIC eacp-graphics)

# The panel is an eacp UI component tree in one GPUView: no web view, so no
# second process to keep warm.
add_executable(Rudder
        Source/Icon.cpp
        Source/Main.cpp
        Source/PanelView.cpp)

target_link_libraries(Rudder PRIVATE eacp-ui RudderSwitcher RudderPlatform)

set(BUNDLE_ID "com.pond.rudder")

# LSUIElement: an agent app. It launches with no Dock icon and no flash before
# setDockIconVisible(false) confirms it.
set_target_properties(Rudder PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Rudder"
        MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_ID}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${BUNDLE_ID}
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Source/Info.plist.in")

eacp_set_gui_subsystem(Rudder)

if (NOT TARGET NanoTest)
    CPMAddPackage(
            NAME NanoTest
            GITHUB_REPOSITORY eyalamirmusic/NanoTest
            GIT_TAG main)
endif ()

enable_testing()
add_subdirectory(Tests)
