add_executable(
    tungsten_c_tests
    tungsten_c_tests.c
)

target_link_libraries(
    tungsten_c_tests
    PRIVATE
        durable7::tungsten_c
)

tungsten_c_configure_target(tungsten_c_tests)

d7_add_headless_test(NAME tungsten_c.core TARGET tungsten_c_tests)
