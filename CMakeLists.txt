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

target_sources(slotmap
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/key.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/policy.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/bitmap.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/freelist.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/storage.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sparse.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/iterator.cppm
        #        ${CMAKE_CURRENT_SOURCE_DIR}/src/dense.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/slotmap.cppm
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils.cppm
)

target_link_libraries(slotmap PUBLIC result::result)
target_compile_features(slotmap PUBLIC cxx_std_26)

add_subdirectory(demo)