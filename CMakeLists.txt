cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Rudder VERSION 0.1.0 LANGUAGES C CXX)

# An app that publishes contexts (Wim, CowTerm) pulls Rudder in with CPM for
# the client library alone: no app, no tests, no extra dependencies -- it
# brings eacp (and with it Miro) itself.
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

add_subdirectory(Client)

# The switcher's pure logic (ranking, MRU, back stack, keybindings, context
# providers) lives in a plain static library so it stays unit-testable without
# the app shell.
add_library(RudderSwitcher STATIC
        Lib/Switcher/BackStack.cpp
        Lib/Switcher/CaptureGate.cpp
        Lib/Switcher/Favourites.cpp
        Lib/Switcher/Keybinding.cpp
        Lib/Switcher/LineEdit.cpp
        Lib/Switcher/Mru.cpp
        Lib/Switcher/Providers.cpp
        Lib/Switcher/Ranking.cpp)

target_include_directories(RudderSwitcher PUBLIC Lib/Switcher)
target_link_libraries(RudderSwitcher PUBLIC emberstore RudderProtocol)

# The OS seam: running apps, activation, preview and the quick-panel window
# tricks. Only macOS is implemented; a Platform-Windows.cpp slots in here.
if (APPLE)
    add_library(RudderPlatform STATIC
            Platform/ContextServer-macOS.mm
            Platform/Permissions.mm
            Platform/Platform-macOS.mm
            Platform/QuickWindow-macOS.mm)
    target_link_libraries(RudderPlatform PRIVATE
            "-framework AppKit"
            "-framework ApplicationServices"
            "-framework Carbon")
    # The tamber-web ports (QuickWindow, Permissions) keep their original
    # manual retain/release; Rudder's own platform code is ARC.
    set_source_files_properties(Platform/Platform-macOS.mm Platform/ContextServer-macOS.mm
            PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif ()

target_include_directories(RudderPlatform PUBLIC Platform)
target_link_libraries(RudderPlatform PUBLIC eacp-graphics RudderProtocol RudderSwitcher)

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

# Assets/Icon is made by Scripts/make-icons.sh. macOS builds its .icns from
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

if (NOT TARGET NanoTest)
    CPMAddPackage(
            NAME NanoTest
            GITHUB_REPOSITORY eyalamirmusic/NanoTest
            GIT_TAG main)
endif ()

enable_testing()
add_subdirectory(Tests)
