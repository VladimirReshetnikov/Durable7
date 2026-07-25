find_package(Threads REQUIRED)

add_executable(
    ordered_smoke_tests
    persistent_ordered_map_tests.cpp
    persistent_ordered_multimap_tests.cpp
    persistent_ordered_cursor_tests.cpp
    persistent_ordered_set_tests.cpp
)

target_link_libraries(
    ordered_smoke_tests
    PRIVATE
        durable7::ordered
        Threads::Threads
)

ordered_configure_target(ordered_smoke_tests)

d7_add_headless_test(NAME ordered.smoke TARGET ordered_smoke_tests)
