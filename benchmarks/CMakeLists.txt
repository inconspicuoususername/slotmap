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


add_library(bench_common OBJECT)

target_sources(bench_common
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/common
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/common/adapters.cppm
)

target_include_directories(bench_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_features(bench_common PUBLIC cxx_std_26)
target_compile_options(bench_common PUBLIC -O3 -march=native -DNDEBUG -fmodules)
target_link_libraries(bench_common PUBLIC inco::slotmap)


add_library(bench_external_deps INTERFACE)
target_include_directories(bench_external_deps INTERFACE
        ${sporacid_slotmap_SOURCE_DIR}/include
        ${sergeymakeev_slotmap_SOURCE_DIR}/slot_map
        ${twiggler_slotmap_SOURCE_DIR}/include
        ${boost_headers_SOURCE_DIR}
)

target_link_libraries(bench_common PRIVATE bench_external_deps)

function(add_slotmap_benchmark TARGET_NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES;MODULES;DEPENDS;OPTIONS" ${ARGN})
    add_executable(${TARGET_NAME})

    if (ARG_SOURCES)
        target_sources(${TARGET_NAME} PRIVATE ${ARG_SOURCES})
    endif ()

    if (ARG_MODULES)
        target_sources(${TARGET_NAME}
                PRIVATE FILE_SET CXX_MODULES
                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
                FILES ${ARG_MODULES}
        )
    endif ()

    target_link_libraries(${TARGET_NAME} PRIVATE
            bench_common
            ${ARG_DEPENDS}
    )

    if (ARG_OPTIONS)
        target_compile_options(${TARGET_NAME} PRIVATE ${ARG_OPTIONS})
    endif ()
endfunction()

add_slotmap_benchmark(slotmap-bench-internal
        MODULES internal/bench_internal.cppm
)

add_slotmap_benchmark(slotmap-bench-external
        MODULES external/bench_external.cppm
        DEPENDS bench_external_deps
)

add_slotmap_benchmark(slotmap-bench-clean
        MODULES external/cleanroom.cppm
        DEPENDS bench_external_deps
        OPTIONS -g -fno-omit-frame-pointer -mavx2
)

add_slotmap_benchmark(slotmap-bench-perf
        MODULES internal/perf_churn.cppm
        OPTIONS -g1
)
