# forge-slicer REST service — embedded HTTP server.
# Built only when ENABLE_FORGE_REST is set from the top-level CMakeLists.

include(FetchContent)

# cpp-httplib (header-only). Pinned to a known-good release.
FetchContent_Declare(
    cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.16.0
)
FetchContent_MakeAvailable(cpp_httplib)

add_library(forge_slicer_rest STATIC
    rest_server.cpp
    rest_server.hpp
)

target_include_directories(forge_slicer_rest
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# nlohmann_json comes from OrcaSlicer's vendored deps_src/nlohmann
# (interface target named nlohmann_json — no ::nlohmann_json suffix).
target_link_libraries(forge_slicer_rest
    PUBLIC httplib::httplib
    PUBLIC nlohmann_json
)

# Reach into libslic3r once Phase 2 wires PresetBundle iteration.
# target_link_libraries(forge_slicer_rest PUBLIC libslic3r)

target_compile_features(forge_slicer_rest PUBLIC cxx_std_17)
