cmake_minimum_required(VERSION 3.16)
project(agss VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(AGSS_BUILD_TESTS "Build the test suite" ON)
option(AGSS_BUILD_CLI   "Build the command line tool" ON)
option(AGSS_SANITIZE    "Build with address+UB sanitizers" OFF)
option(AGSS_WERROR      "Treat warnings as errors" OFF)
option(AGSS_BUILD_PYTHON "Build the Python extension module" OFF)

add_library(agss_core
    src/graph_builder.cpp
    src/loader.cpp
    src/algorithm.cpp
    src/json.cpp
    src/kdtree.cpp
    src/directions.cpp
    src/isochrone.cpp
    src/algorithms/uninformed.cpp
    src/algorithms/weighted.cpp
    src/algorithms/global.cpp
    src/algorithms/kshortest.cpp
    src/algorithms/contraction_hierarchy.cpp
    src/algorithms/alt.cpp
    src/algorithms/time_dependent.cpp
    src/algorithms/customizable_ch.cpp
    src/analysis/connectivity.cpp
    src/analysis/centrality.cpp
    src/analysis/spanning.cpp
    src/analysis/flow.cpp
    src/transit/multimodal.cpp
)
target_include_directories(agss_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(agss_core PRIVATE /W4 $<$<BOOL:${AGSS_WERROR}>:/WX>)
else()
    target_compile_options(agss_core PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers
        $<$<BOOL:${AGSS_WERROR}>:-Werror>)
endif()

if(AGSS_SANITIZE AND NOT MSVC)
    target_compile_options(agss_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(agss_core PUBLIC -fsanitize=address,undefined)
endif()

if(AGSS_BUILD_CLI)
    add_executable(agss src/cli/main.cpp)
    target_link_libraries(agss PRIVATE agss_core)
    set_target_properties(agss PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

if(AGSS_BUILD_TESTS)
    enable_testing()
    add_executable(agss_tests
        tests/test_main.cpp
        tests/test_loader.cpp
        tests/test_graph.cpp
        tests/test_differential.cpp
        tests/test_analysis.cpp
        tests/test_features.cpp
        tests/test_ch.cpp
        tests/test_alt.cpp
        tests/test_time_dependent.cpp
        tests/test_cch.cpp
    )
    target_link_libraries(agss_tests PRIVATE agss_core)
    target_include_directories(agss_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    add_test(NAME agss_tests COMMAND agss_tests)
endif()

# ---------------------------------------------------------------- WebAssembly
#
# Configure with `emcmake cmake -S . -B build/wasm`. The browser build drops the
# Python bridge entirely rather than porting it: with the engine running in the
# page there is no subprocess, no shared trace file and no map parameter
# reaching a filesystem, so the traversal and concurrency defects that bridge
# had become unrepresentable rather than merely fixed.
if(EMSCRIPTEN)
    add_executable(agss_wasm src/wasm/bindings.cpp)
    target_link_libraries(agss_wasm PRIVATE agss_core)
    set_target_properties(agss_wasm PROPERTIES
        OUTPUT_NAME agss_engine
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/web/engine
        SUFFIX ".js")
    target_link_options(agss_wasm PRIVATE
        "-sMODULARIZE=1"
        "-sEXPORT_NAME=createAgssEngine"
        "-sENVIRONMENT=web,worker,node"   # node so CI can smoke-test the module headlessly
        "-sALLOW_MEMORY_GROWTH=1"
        "-sMAXIMUM_MEMORY=2GB"            # a national network is tens of MB
        "-sINITIAL_MEMORY=256MB"          # enough to stage a national extract without growing
        "-sSTACK_SIZE=8MB"                 # iterative algorithms still recurse in Dinic
        # HEAP* views are needed to read the packed geometry buffers without a copy
        # through JSON; they are not exported by default.
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToNewUTF8','HEAPF64','HEAP32','HEAPU8']"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_agss_free','_agss_load_graph','_agss_algorithms','_agss_route','_agss_graph_json','_agss_race','_agss_bench','_agss_ch_build','_agss_ch_ready','_agss_ch_query','_agss_cch_customize','_agss_cch_query','_agss_traffic_scan','_agss_node_count','_agss_edge_count','_agss_is_geographic','_agss_coords_buffer','_agss_edges_buffer','_agss_analyze','_agss_isochrone','_agss_kpaths','_agss_directions','_agss_nearest','_agss_load_transit','_agss_build_transit','_agss_use_road_graph']"
        "-sFILESYSTEM=1"                   # in-memory only; nothing touches a host disk
        "--closure=0"
        "-O3")
endif()

# ------------------------------------------------------------------ Python
#
# Researchers work in Python and will not link a C++ library to try an idea,
# so the engine is exposed there too. Built through scikit-build-core; see
# pyproject.toml.
if(AGSS_BUILD_PYTHON)
    find_package(pybind11 CONFIG REQUIRED)
    pybind11_add_module(_agss python/bindings.cpp)
    target_link_libraries(_agss PRIVATE agss_core)
    set_target_properties(agss_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    install(TARGETS _agss DESTINATION agss)
endif()

install(TARGETS agss_core EXPORT agssTargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
