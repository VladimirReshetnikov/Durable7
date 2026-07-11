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
