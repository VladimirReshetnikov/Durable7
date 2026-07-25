add_executable(
    ordered_c_tests
    ordered_set_tests.c
)

target_include_directories(
    ordered_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../test_support/include
)

target_link_libraries(
    ordered_c_tests
    PRIVATE
        durable7::ordered_c
)

ordered_c_configure_target(ordered_c_tests)

d7_add_headless_test(NAME ordered_c.core TARGET ordered_c_tests)

add_executable(
    ordered_map_c_tests
    ordered_map_tests.c
)

target_include_directories(
    ordered_map_c_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../test_support/include
)

target_link_libraries(
    ordered_map_c_tests
    PRIVATE
        durable7::ordered_c
)

ordered_c_configure_target(ordered_map_c_tests)

d7_add_headless_test(NAME ordered_c.map TARGET ordered_map_c_tests)

add_executable(ordered_multimap_c_tests ordered_multimap_tests.c)
target_include_directories(ordered_multimap_c_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../test_support/include)
target_link_libraries(ordered_multimap_c_tests PRIVATE durable7::ordered_c)
ordered_c_configure_target(ordered_multimap_c_tests)
d7_add_headless_test(NAME ordered_c.multimap TARGET ordered_multimap_c_tests)

add_executable(ordered_cursor_c_tests ordered_cursor_tests.c)
target_include_directories(ordered_cursor_c_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../test_support/include)
target_link_libraries(ordered_cursor_c_tests PRIVATE durable7::ordered_c)
ordered_c_configure_target(ordered_cursor_c_tests)
d7_add_headless_test(NAME ordered_c.cursor TARGET ordered_cursor_c_tests)
