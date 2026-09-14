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
add_library(ic::slotmap ALIAS slotmap)

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
add_subdirectory(benchmarks)