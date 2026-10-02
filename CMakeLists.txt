cmake_minimum_required(VERSION 4.3)

project(slotmap CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SLOTMAP_BUILD_BENCHMARKS
        "Build slotmap benchmarks (fetches reference slotmaps + Boost)" OFF)
option(SLOTMAP_BUILD_DEMO "Build demo" OFF)
option(SLOTMAP_BUILD_TESTS "Build unit tests" OFF)
option(SLOTMAP_ENABLE_FREELIST "Enable freelist alongside bitmap" OFF)

add_library(slotmap STATIC)
add_library(inco::slotmap ALIAS slotmap)

file(GLOB_RECURSE out_CPP_MODULE_SOURCES CONFIGURE_DEPENDS "src/*.cppm")

target_sources(slotmap
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES
        ${out_CPP_MODULE_SOURCES}
)

target_sources(slotmap
        PUBLIC
        FILE_SET header_set
        TYPE HEADERS
        FILES
        "src/macros.h"
)

target_compile_features(slotmap PUBLIC cxx_std_23)

set_target_properties(slotmap PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

if (SLOTMAP_BUILD_BENCHMARKS)
    set(SLOTMAP_ENABLE_FREELIST ON CACHE BOOL "" FORCE)
    target_compile_definitions(slotmap PRIVATE SLOTMAP_EXPERIMENTS)
    add_subdirectory(benchmarks)
endif ()

if (SLOTMAP_BUILD_DEMO)
    add_subdirectory(demo)
endif()

if (SLOTMAP_BUILD_TESTS)
    message(STATUS "Building tests")
    set(SLOTMAP_ENABLE_FREELIST ON CACHE BOOL "" FORCE)
    enable_testing()
    add_subdirectory(tests)
endif ()

if (SLOTMAP_ENABLE_FREELIST)
    # hopefulyl temp off
    # abseil causes segmentation fault when imported in the freelist module so yeah
    # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=127694
#    set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
#    set(ABSL_RUN_TESTS OFF CACHE BOOL "" FORCE)
#
#    FetchContent_Declare(absl
#            GIT_REPOSITORY https://github.com/abseil/abseil-cpp
#            GIT_TAG 20260817.0
#            GIT_SHALLOW TRUE
#            SYSTEM)
#    FetchContent_MakeAvailable(absl)
#
#    target_link_libraries(slotmap PRIVATE absl::flat_hash_set)
#
    target_compile_definitions(slotmap PRIVATE SLOTMAP_ENABLE_FREELIST)
endif()

