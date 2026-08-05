add_executable(
    fingertree_smoke_tests
    ancestral_slice_queue_tests.cpp
    atomic_box_tests.cpp
    bilateral_ancestral_deque_tests.cpp
    brodal_okasaki_heap_tests.cpp
    contextual_rank_sequence_tests.cpp
    incremental_ancestor_tests.cpp
    persistent_delta_map_tests.cpp
    persistent_monotone_action_heap_tests.cpp
    persistent_run_delta_vector_tests.cpp
    canonical_sorted_set_tests.cpp
    command_sequence_tests.cpp
    daba_lite_tests.cpp
    interval_tree_tests.cpp
    persistent_interval_map_tests.cpp
    persistent_chunked_bit_set_tests.cpp
    lazy_cell_tests.cpp
    measured_finger_tree_tests.cpp
    measured_lazy_cell_tests.cpp
    measured_rope_tests.cpp
    measure_tests.cpp
    ordered_search_cursor_tests.cpp
    persistent_deque_tests.cpp
    priority_queue_tests.cpp
    priority_search_queue_tests.cpp
    range_update_sequence_tests.cpp
    reversible_deque_tests.cpp
    rope_tests.cpp
    rope_text_tests.cpp
    rrb_vector_tests.cpp
    sequence_cursor_tests.cpp
    sorted_collection_tests.cpp
    smoke_tests.cpp
    tearable_concurrency_tests.cpp
    test_support/allocation_counter.cpp
)

target_link_libraries(
    fingertree_smoke_tests
    PRIVATE
        durable7::finger_tree
)

target_include_directories(
    fingertree_smoke_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(NOT FINGERTREE_ENABLE_ALLOCATION_TRACKING)
    target_compile_definitions(
        fingertree_smoke_tests
        PRIVATE
            FINGERTREE_DISABLE_ALLOCATION_TRACKING=1
    )
endif()

fingertree_configure_target(fingertree_smoke_tests)

set(
    FINGERTREE_TEST_GROUPS
    atomic-box
    brodal-okasaki-heap
    canonical-sorted-set
    command-model
    ancestral-slice-queue
    bilateral-ancestral-deque
    concurrency
    contextual-rank-sequence
    daba-lite
    deque
    incremental-ancestor
    monotone-action-heap
    persistent-delta-map
    run-delta-vector
    interval-tree
    interval-map
    chunked-bit-set
    lazy-cell
    measure
    measured-lazy-cell
    measured-rope
    measured-tree
    ordered-search-cursors
    priority-queue
    priority-search-queue
    range-update-sequence
    reversible-deque
    rope
    rope-text
    rrb-vector
    sequence-cursors
    sorted-collections
)

d7_add_headless_test(
    NAME fingertree.support
    TARGET fingertree_smoke_tests
    ARGUMENTS --group support
)
set_tests_properties(
    fingertree.support
    PROPERTIES
        LABELS "fingertree;support"
)

foreach(group IN LISTS FINGERTREE_TEST_GROUPS)
    d7_add_headless_command_test(
        NAME fingertree.${group}
        COMMAND $<TARGET_FILE:fingertree_smoke_tests> --group ${group}
    )
    set_tests_properties(
        fingertree.${group}
        PROPERTIES
            LABELS "fingertree;${group}"
    )
endforeach()
