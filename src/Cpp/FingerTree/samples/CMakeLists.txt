find_package(Threads REQUIRED)

add_library(
    fingertree_sample_runs
    STATIC
        showcase.cpp
        persistent_snapshots.cpp
)
target_link_libraries(
    fingertree_sample_runs
    PUBLIC
        durable7::finger_tree
    PRIVATE
        Threads::Threads
)
target_include_directories(
    fingertree_sample_runs
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
fingertree_configure_target(fingertree_sample_runs)

add_executable(fingertree_showcase showcase_main.cpp)
target_link_libraries(fingertree_showcase PRIVATE fingertree_sample_runs)
fingertree_configure_target(fingertree_showcase)

add_executable(fingertree_persistent_snapshots persistent_snapshots_main.cpp)
target_link_libraries(fingertree_persistent_snapshots PRIVATE fingertree_sample_runs)
fingertree_configure_target(fingertree_persistent_snapshots)

if(FINGERTREE_BUILD_TESTS)
    add_executable(fingertree_sample_smoke_tests sample_smoke_tests.cpp)
    target_link_libraries(fingertree_sample_smoke_tests PRIVATE fingertree_sample_runs)
    fingertree_configure_target(fingertree_sample_smoke_tests)
    d7_add_headless_test(NAME fingertree.samples TARGET fingertree_sample_smoke_tests)
    set_tests_properties(fingertree.samples PROPERTIES LABELS "fingertree;samples")
endif()
