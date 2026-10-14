add_executable(RudderTests
        BackStackTests.cpp
        FuzzyMatchTests.cpp
        MruTests.cpp
        RankingTests.cpp)

target_link_libraries(RudderTests PRIVATE NanoTest NanoTestMain RudderSwitcher)
nano_discover_tests(RudderTests)
