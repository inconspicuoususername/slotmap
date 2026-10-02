find_package(GTest QUIET)

if (NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.18.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif ()

add_executable(slotmap-tests slotmap_test.cpp)

target_link_libraries(slotmap-tests
        PRIVATE
        inco::slotmap
        GTest::gtest_main
)

set_target_properties(slotmap-tests PROPERTIES CXX_SCAN_FOR_MODULES ON)

include(GoogleTest)
gtest_discover_tests(slotmap-tests)
