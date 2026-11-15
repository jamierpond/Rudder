# A pond app's Permissions… panel: the macOS privacy grants (TCC) it holds,
# their live state, the drag-the-app tile, deep links into System Settings,
# reset and relaunch. CowTerm and ScreenCap show it. Windows and Linux build
# the same panel over Permissions-Default.cpp, which reports every grant in
# force. Needs eacp-ui from the eacp the including project brings; an app
# without it (a client-only pin from a command-line tool) gets no panel.
if (NOT TARGET eacp-ui)
    return()
endif ()

add_library(PermissionsPanel STATIC
        Permissions.cpp
        PermissionsPanel.cpp)

if (APPLE)
    target_sources(PermissionsPanel PRIVATE MacProcesses.mm Permissions-macOS.mm)
    target_link_libraries(PermissionsPanel PUBLIC
            "-framework UserNotifications"
            "-framework Foundation"
            "-framework CoreGraphics"
            "-framework ApplicationServices"
            "-framework AppKit")
else ()
    target_sources(PermissionsPanel PRIVATE Permissions-Default.cpp)
endif ()

target_include_directories(PermissionsPanel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(PermissionsPanel PUBLIC eacp-ui)
target_compile_features(PermissionsPanel PUBLIC cxx_std_20)
