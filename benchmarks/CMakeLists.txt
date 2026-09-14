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

FetchContent_Declare(twiggler_slotmap
        GIT_REPOSITORY https://github.com/twiggler/slotmap
        GIT_TAG 95abc843e45f667a471f0b5bec7638177466bfab
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-configure
        SYSTEM)

# twiggler needs Boost (boost/integer.hpp + Boost.Iterator).
FetchContent_Declare(boost_headers
        URL https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz
        URL_HASH SHA256=f55c340aa49763b1925ccf02b2e83f35fdcf634c9d5164a2acb87540173c741d
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR do-not-configure
        SYSTEM)

FetchContent_MakeAvailable(
        sporacid_slotmap sergeymakeev_slotmap twiggler_slotmap boost_headers)


add_executable(slotmap-bench-internal)

target_sources(slotmap-bench-internal
        PRIVATE FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/internal/bench_internal.cppm)

target_include_directories(slotmap-bench-internal PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})            # bench_common.hpp

target_link_libraries(slotmap-bench-internal PRIVATE ic::slotmap)
target_compile_features(slotmap-bench-internal PRIVATE cxx_std_26)
target_compile_options(slotmap-bench-internal PRIVATE -O3 -march=native -DNDEBUG)


add_executable(slotmap-bench-external)

target_sources(slotmap-bench-external
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external/ext_refs.cpp
        PRIVATE FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/external/bench_external.cppm)

target_include_directories(slotmap-bench-external PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}                    # bench_common.hpp
        ${sporacid_slotmap_SOURCE_DIR}/include         # sporacid  <spore/slot_map.hpp>
        ${sergeymakeev_slotmap_SOURCE_DIR}/slot_map    # SergeyMakeev <slot_map.h>
        ${twiggler_slotmap_SOURCE_DIR}/include         # twiggler  <slotmap/slotmap.hpp>
        ${boost_headers_SOURCE_DIR})                   # <boost/...>

target_link_libraries(slotmap-bench-external PRIVATE ic::slotmap)
target_compile_features(slotmap-bench-external PRIVATE cxx_std_26)
target_compile_options(slotmap-bench-external PRIVATE -O3 -march=native -DNDEBUG)
