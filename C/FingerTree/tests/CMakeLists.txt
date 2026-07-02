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

add_test(NAME fingertree_c.core COMMAND fingertree_c_tests)
