# What every pond app (Rudder, Grazer, CowTerm, DiffView) shares at launch:
# its data folder under pond/<App>, and the one-running-copy claim. Needs the
# eacp the including project brings, so it is added after eacp.
add_library(PondApp STATIC
        DataDirectory.cpp
        SingleInstance.cpp)
target_include_directories(PondApp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(PondApp PUBLIC eacp-network-rpc)
target_compile_features(PondApp PUBLIC cxx_std_20)
