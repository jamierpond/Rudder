add_executable(RudderTests
        BackStackTests.cpp
        CaptureGateTests.cpp
        DragHandleTests.cpp
        FavouritesTests.cpp
        FocusTests.cpp
        FrecencySimulationTests.cpp
        FrecencyTests.cpp
        FuzzyMatchTests.cpp
        KeybindingTests.cpp
        LineEditTests.cpp
        MruTests.cpp
        PanelFrameTests.cpp
        ProtocolTests.cpp
        ProvidersTests.cpp
        RankingTests.cpp
        RenamesTests.cpp
        TextFitTests.cpp
        UrlTests.cpp
        WebTests.cpp)

target_link_libraries(RudderTests PRIVATE NanoTest NanoTestMain RudderSwitcher)

# The contexts transport end to end, client and server in one process. On
# macOS over a socket in a temporary folder, never a running Rudder's.
if (APPLE)
    target_sources(RudderTests PRIVATE TransportTests-macOS.cpp)
    target_link_libraries(RudderTests PRIVATE RudderClient RudderPlatform)
endif ()

if (WIN32)
    target_sources(RudderTests PRIVATE TransportTests-Windows.cpp)
    target_link_libraries(RudderTests PRIVATE RudderClient RudderPlatform)
endif ()

nano_discover_tests(RudderTests)
