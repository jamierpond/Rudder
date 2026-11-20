add_executable(RudderTests
        BackStackTests.cpp
        CaptureGateTests.cpp
        ClosingContextsTests.cpp
        CycleGestureTests.cpp
        DragHandleTests.cpp
        FavouritesTests.cpp
        FocusTests.cpp
        ListedRowsTests.cpp
        FrecencySimulationTests.cpp
        FrecencyTests.cpp
        KeybindingTests.cpp
        LineEditTests.cpp
        MruTests.cpp
        PanelFrameTests.cpp
        ProtocolTests.cpp
        ProvidersTests.cpp
        RankingTests.cpp
        RenamedAppIdsTests.cpp
        RenamesTests.cpp
        SearchOrderTests.cpp
        SlotsTests.cpp
        SnapshotFrameTests.cpp
        TextFitTests.cpp
        UrlTests.cpp
        WebTests.cpp)

target_link_libraries(RudderTests PRIVATE NanoTest NanoTestMain RudderSwitcher)

# The contexts transport end to end, client and server in one process. On
# macOS over a socket in a temporary folder, never a running Rudder's.
if (APPLE)
    target_sources(RudderTests PRIVATE TransportTests-macOS.cpp)
    target_link_libraries(RudderTests PRIVATE RudderClient RudderPlatform)

    # The switcher's real key path: PanelView::keyDown into the query field.
    target_sources(RudderTests PRIVATE
            PanelKeysTests-macOS.cpp
            ../Source/Icon.cpp
            ../Source/PanelView.cpp)
    target_include_directories(RudderTests PRIVATE ../Source)
    target_link_libraries(RudderTests PRIVATE eacp-ui SearchKeys)
endif ()

if (WIN32)
    target_sources(RudderTests PRIVATE TransportTests-Windows.cpp)
    target_link_libraries(RudderTests PRIVATE RudderClient RudderPlatform)
endif ()

nano_discover_tests(RudderTests)
