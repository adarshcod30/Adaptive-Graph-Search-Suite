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

# Static-init algorithm registration lives in the library, so consumers must
# link the whole archive or the registry comes up empty.
if(APPLE)
    set(AGSS_WHOLE_ARCHIVE -Wl,-force_load,$<TARGET_FILE:agss_core>)
elseif(MSVC)
    set(AGSS_WHOLE_ARCHIVE agss_core)
else()
    set(AGSS_WHOLE_ARCHIVE -Wl,--whole-archive agss_core -Wl,--no-whole-archive)
endif()

if(AGSS_SANITIZE AND NOT MSVC)
    target_compile_options(agss_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(agss_core PUBLIC -fsanitize=address,undefined)
endif()

if(AGSS_BUILD_CLI)
    add_executable(agss src/cli/main.cpp)
    target_link_libraries(agss PRIVATE ${AGSS_WHOLE_ARCHIVE} agss_core)
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
    )
    target_link_libraries(agss_tests PRIVATE ${AGSS_WHOLE_ARCHIVE} agss_core)
    target_include_directories(agss_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
    add_test(NAME agss_tests COMMAND agss_tests)
endif()

install(TARGETS agss_core EXPORT agssTargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
