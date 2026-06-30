add_executable(
    fingertree_smoke_tests
    atomic_box_tests.cpp
    interval_tree_tests.cpp
    lazy_cell_tests.cpp
    measured_finger_tree_tests.cpp
    measured_lazy_cell_tests.cpp
    measure_tests.cpp
    persistent_deque_tests.cpp
    priority_queue_tests.cpp
    reversible_deque_tests.cpp
    sorted_collection_tests.cpp
    smoke_tests.cpp
    test_support/allocation_counter.cpp
)

target_link_libraries(
    fingertree_smoke_tests
    PRIVATE
        tools::data_structures::finger_tree
)

target_include_directories(
    fingertree_smoke_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_configure_target(fingertree_smoke_tests)

add_test(NAME fingertree.smoke COMMAND fingertree_smoke_tests)
