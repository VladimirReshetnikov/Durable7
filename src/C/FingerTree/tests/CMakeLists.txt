add_executable(
    fingertree_c_tests
    fingertree_c_tests.c
)

target_link_libraries(
    fingertree_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    fingertree_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(fingertree_c_tests)

d7_add_headless_test(NAME fingertree_c.core TARGET fingertree_c_tests)

add_executable(
    rrb_vector_c_tests
    rrb_vector_tests.c
)

target_link_libraries(
    rrb_vector_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    rrb_vector_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(rrb_vector_c_tests)

d7_add_headless_test(NAME fingertree_c.rrb_vector TARGET rrb_vector_c_tests)

add_executable(
    daba_lite_c_tests
    daba_lite_tests.c
)

target_link_libraries(
    daba_lite_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    daba_lite_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(daba_lite_c_tests)

d7_add_headless_test(NAME fingertree_c.daba_lite TARGET daba_lite_c_tests)

add_executable(
    canonical_sorted_set_c_tests
    canonical_sorted_set_tests.c
)

target_link_libraries(
    canonical_sorted_set_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    canonical_sorted_set_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(canonical_sorted_set_c_tests)

d7_add_headless_test(NAME fingertree_c.canonical_sorted_set TARGET canonical_sorted_set_c_tests)

add_executable(
    brodal_okasaki_heap_c_tests
    brodal_okasaki_heap_tests.c
)

target_link_libraries(
    brodal_okasaki_heap_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    brodal_okasaki_heap_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(brodal_okasaki_heap_c_tests)

d7_add_headless_test(NAME fingertree_c.brodal_okasaki_heap TARGET brodal_okasaki_heap_c_tests)

add_executable(
    priority_search_queue_c_tests
    priority_search_queue_tests.c
)

target_link_libraries(
    priority_search_queue_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    priority_search_queue_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(priority_search_queue_c_tests)

d7_add_headless_test(NAME fingertree_c.priority_search_queue TARGET priority_search_queue_c_tests)

add_executable(
    range_update_sequence_c_tests
    range_update_sequence_tests.c
)

target_link_libraries(
    range_update_sequence_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    range_update_sequence_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(range_update_sequence_c_tests)

d7_add_headless_test(NAME fingertree_c.range_update_sequence TARGET range_update_sequence_c_tests)

add_executable(
    persistent_interval_map_c_tests
    persistent_interval_map_tests.c
)

target_link_libraries(
    persistent_interval_map_c_tests
    PRIVATE
        durable7::finger_tree_c
)

fingertree_c_configure_target(persistent_interval_map_c_tests)

d7_add_headless_test(NAME fingertree_c.persistent_interval_map TARGET persistent_interval_map_c_tests)

add_executable(
    persistent_chunked_bit_set_c_tests
    persistent_chunked_bit_set_tests.c
)
target_link_libraries(
    persistent_chunked_bit_set_c_tests
    PRIVATE
        durable7::finger_tree_c
)
target_include_directories(
    persistent_chunked_bit_set_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
fingertree_c_configure_target(persistent_chunked_bit_set_c_tests)
d7_add_headless_test(
    NAME fingertree_c.persistent_chunked_bit_set
    TARGET persistent_chunked_bit_set_c_tests
)

add_executable(
    incremental_ancestor_c_tests
    incremental_ancestor_tests.c
)

target_link_libraries(
    incremental_ancestor_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    incremental_ancestor_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(incremental_ancestor_c_tests)

d7_add_headless_test(NAME fingertree_c.incremental_ancestor TARGET incremental_ancestor_c_tests)

add_executable(
    ancestral_slice_queue_c_tests
    ancestral_slice_queue_tests.c
)

target_link_libraries(
    ancestral_slice_queue_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    ancestral_slice_queue_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(ancestral_slice_queue_c_tests)

d7_add_headless_test(NAME fingertree_c.ancestral_slice_queue TARGET ancestral_slice_queue_c_tests)

add_executable(
    bilateral_ancestral_deque_c_tests
    bilateral_ancestral_deque_tests.c
)

target_link_libraries(
    bilateral_ancestral_deque_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    bilateral_ancestral_deque_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(bilateral_ancestral_deque_c_tests)

d7_add_headless_test(NAME fingertree_c.bilateral_ancestral_deque TARGET bilateral_ancestral_deque_c_tests)

add_executable(
    contextual_rank_sequence_c_tests
    contextual_rank_sequence_tests.c
)

target_link_libraries(
    contextual_rank_sequence_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    contextual_rank_sequence_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(contextual_rank_sequence_c_tests)

d7_add_headless_test(NAME fingertree_c.contextual_rank_sequence TARGET contextual_rank_sequence_c_tests)

add_executable(
    persistent_delta_map_c_tests
    persistent_delta_map_tests.c
)

target_link_libraries(
    persistent_delta_map_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    persistent_delta_map_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(persistent_delta_map_c_tests)

d7_add_headless_test(NAME fingertree_c.persistent_delta_map TARGET persistent_delta_map_c_tests)

add_executable(
    persistent_monotone_action_heap_c_tests
    persistent_monotone_action_heap_tests.c
)

target_link_libraries(
    persistent_monotone_action_heap_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    persistent_monotone_action_heap_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(persistent_monotone_action_heap_c_tests)

d7_add_headless_test(NAME fingertree_c.persistent_monotone_action_heap TARGET persistent_monotone_action_heap_c_tests)

add_executable(
    persistent_run_delta_vector_c_tests
    persistent_run_delta_vector_tests.c
)

target_link_libraries(
    persistent_run_delta_vector_c_tests
    PRIVATE
        durable7::finger_tree_c
)

target_include_directories(
    persistent_run_delta_vector_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(persistent_run_delta_vector_c_tests)

d7_add_headless_test(NAME fingertree_c.persistent_run_delta_vector TARGET persistent_run_delta_vector_c_tests)
