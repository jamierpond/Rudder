add_executable(RudderTests
        BackStackTests.cpp
        CaptureGateTests.cpp
        FavouritesTests.cpp
        FocusTests.cpp
        FrecencyTests.cpp
        FuzzyMatchTests.cpp
        KeybindingTests.cpp
        LineEditTests.cpp
        MruTests.cpp
        ProtocolTests.cpp
        ProvidersTests.cpp
        RankingTests.cpp
        RenamesTests.cpp
        UrlTests.cpp
        WebTests.cpp)

target_link_libraries(RudderTests PRIVATE NanoTest NanoTestMain RudderSwitcher)

# The contexts transport end to end, client and server in one process. Windows
# only: the macOS server would take over a running Rudder's socket file, where
# a named pipe a live Rudder holds just refuses (and the test steps aside).
if (WIN32)
    target_sources(RudderTests PRIVATE TransportTests-Windows.cpp)
    target_link_libraries(RudderTests PRIVATE RudderClient RudderPlatform)
endif ()

nano_discover_tests(RudderTests)
