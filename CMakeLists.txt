# ---------------------------------------------------------------------------
# Test suite build configuration.
#
# Prefers a system-installed GoogleTest; falls back to FetchContent so
# `cmake -B build && cmake --build build` works on a fresh clone with no
# extra setup steps.
# ---------------------------------------------------------------------------

find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "GoogleTest not found via find_package; fetching via FetchContent.")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # Prevent GoogleTest from overriding this project's compiler/linker flags.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(edge_iot_tests
    test_task.cpp
    test_task_profile.cpp
    test_edge_node.cpp
    test_resource_allocator.cpp
    test_task_scheduler.cpp
    test_metrics_collector.cpp
)

target_link_libraries(edge_iot_tests PRIVATE
    edge_iot_core
    GTest::gtest
    GTest::gtest_main
    project_warnings
    project_coverage
)

include(GoogleTest)
gtest_discover_tests(edge_iot_tests)
