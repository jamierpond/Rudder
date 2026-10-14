# The provider side of Rudder's contexts protocol, for apps that publish
# contexts (Grazer, CowTerm): message encode/decode and a client that connects,
# says hello, pushes contexts and hears activate. App-agnostic, no UI. Pull it
# in with CPM and RUDDER_CLIENT_ONLY=ON to get this target and nothing else.
if (NOT TARGET Miro)
    message(FATAL_ERROR "RudderClient needs the Miro target (it comes with eacp)")
endif ()

add_library(RudderProtocol STATIC RudderProtocol.cpp)

if (APPLE)
    target_sources(RudderProtocol PRIVATE RudderProtocol-macOS.cpp)
elseif (WIN32)
    target_sources(RudderProtocol PRIVATE RudderProtocol-Windows.cpp)
endif ()

target_include_directories(RudderProtocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RudderProtocol PUBLIC Miro)
target_compile_features(RudderProtocol PUBLIC cxx_std_20)

if (APPLE)
    add_library(RudderClient STATIC RudderClient-macOS.mm)
    target_link_libraries(RudderClient PRIVATE "-framework Foundation")
    set_source_files_properties(RudderClient-macOS.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
elseif (WIN32)
    # The same newline-delimited JSON over eacp's IPC channel (a per-user
    # named pipe), read on a thread of its own and delivered on the main thread.
    add_library(RudderClient STATIC RudderClient-Windows.cpp)
    target_link_libraries(RudderClient PRIVATE eacp-network)
else ()
    # There is no Rudder to talk to: the client is silent, as if Rudder weren't
    # running.
    add_library(RudderClient STATIC RudderClient-Linux.cpp)
endif ()

target_include_directories(RudderClient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(RudderClient PUBLIC RudderProtocol)
