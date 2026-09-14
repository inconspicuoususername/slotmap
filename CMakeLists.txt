cmake_minimum_required(VERSION 4.3)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(slotmap CXX)
    set(CMAKE_CXX_STANDARD 26)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif ()

if (NOT TARGET result::result)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../result ${CMAKE_BINARY_DIR}/result)
endif ()

add_library(slotmap STATIC)
add_library(inco::slotmap ALIAS slotmap)

file(GLOB_RECURSE out_CPP_MODULE_SOURCES CONFIGURE_DEPENDS "src/*.cppm")

target_sources(slotmap
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES
        ${out_CPP_MODULE_SOURCES}
)

target_link_libraries(slotmap PUBLIC result::result)
target_compile_features(slotmap PUBLIC cxx_std_26)

add_subdirectory(demo)

# Off by default: the benchmarks pull the reference slotmaps and Boost via
# FetchContent (a large, one-time download), so opt in explicitly.
option(SLOTMAP_BUILD_BENCHMARKS
        "Build slotmap benchmarks (fetches reference slotmaps + Boost)" OFF)
if (SLOTMAP_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif ()