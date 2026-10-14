add_executable(RudderTests
        BackStackTests.cpp
        CaptureGateTests.cpp
        FavouritesTests.cpp
        FuzzyMatchTests.cpp
        KeybindingTests.cpp
        LineEditTests.cpp
        MruTests.cpp
        ProtocolTests.cpp
        ProvidersTests.cpp
        RankingTests.cpp
        UrlTests.cpp)

target_link_libraries(RudderTests PRIVATE NanoTest NanoTestMain RudderSwitcher)
nano_discover_tests(RudderTests)
