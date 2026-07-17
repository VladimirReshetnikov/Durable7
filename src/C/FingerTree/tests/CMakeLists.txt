add_executable(
    fingertree_c_tests
    fingertree_c_tests.c
)

target_link_libraries(
    fingertree_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    fingertree_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(fingertree_c_tests)

tds_add_headless_test(NAME fingertree_c.core TARGET fingertree_c_tests)

add_executable(
    rrb_vector_c_tests
    rrb_vector_tests.c
)

target_link_libraries(
    rrb_vector_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    rrb_vector_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(rrb_vector_c_tests)

tds_add_headless_test(NAME fingertree_c.rrb_vector TARGET rrb_vector_c_tests)

add_executable(
    daba_lite_c_tests
    daba_lite_tests.c
)

target_link_libraries(
    daba_lite_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    daba_lite_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(daba_lite_c_tests)

tds_add_headless_test(NAME fingertree_c.daba_lite TARGET daba_lite_c_tests)

add_executable(
    canonical_sorted_set_c_tests
    canonical_sorted_set_tests.c
)

target_link_libraries(
    canonical_sorted_set_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    canonical_sorted_set_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(canonical_sorted_set_c_tests)

tds_add_headless_test(NAME fingertree_c.canonical_sorted_set TARGET canonical_sorted_set_c_tests)

add_executable(
    brodal_okasaki_heap_c_tests
    brodal_okasaki_heap_tests.c
)

target_link_libraries(
    brodal_okasaki_heap_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    brodal_okasaki_heap_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(brodal_okasaki_heap_c_tests)

tds_add_headless_test(NAME fingertree_c.brodal_okasaki_heap TARGET brodal_okasaki_heap_c_tests)

add_executable(
    priority_search_queue_c_tests
    priority_search_queue_tests.c
)

target_link_libraries(
    priority_search_queue_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    priority_search_queue_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(priority_search_queue_c_tests)

tds_add_headless_test(NAME fingertree_c.priority_search_queue TARGET priority_search_queue_c_tests)

add_executable(
    range_update_sequence_c_tests
    range_update_sequence_tests.c
)

target_link_libraries(
    range_update_sequence_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

target_include_directories(
    range_update_sequence_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

fingertree_c_configure_target(range_update_sequence_c_tests)

tds_add_headless_test(NAME fingertree_c.range_update_sequence TARGET range_update_sequence_c_tests)

add_executable(
    persistent_interval_map_c_tests
    persistent_interval_map_tests.c
)

target_link_libraries(
    persistent_interval_map_c_tests
    PRIVATE
        tools::data_structures::finger_tree_c
)

fingertree_c_configure_target(persistent_interval_map_c_tests)

tds_add_headless_test(NAME fingertree_c.persistent_interval_map TARGET persistent_interval_map_c_tests)
