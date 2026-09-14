include(FetchContent)

FetchContent_Declare(sporacid_slotmap
        GIT_REPOSITORY https://github.com/sporacid/slot-map
        GIT_TAG 17fb64f60a606df726c72ece20933cac5cd3ead3
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-configure
        SYSTEM)

FetchContent_Declare(sergeymakeev_slotmap
        GIT_REPOSITORY https://github.com/SergeyMakeev/SlotMap
        GIT_TAG 122fb6960bc417f82f65a0469d38bfa05b506a25
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-configure
        SYSTEM)

FetchContent_MakeAvailable(sporacid_slotmap sergeymakeev_slotmap)

add_executable(slotmap-bench)

target_sources(slotmap-bench
        PRIVATE
        refs.cpp
        PRIVATE FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/bench_main.cppm
)

target_include_directories(slotmap-bench PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${sporacid_slotmap_SOURCE_DIR}/include        # sporacid  <spore/slot_map.hpp>
        ${sergeymakeev_slotmap_SOURCE_DIR}/slot_map   # SergeyMakeev <slot_map.h>
)

target_link_libraries(slotmap-bench PRIVATE ic::slotmap)
target_compile_features(slotmap-bench PRIVATE cxx_std_26)

# Benchmarks are only meaningful optimised; force it regardless of the outer build type.
target_compile_options(slotmap-bench PRIVATE -O3 -march=native -DNDEBUG)
