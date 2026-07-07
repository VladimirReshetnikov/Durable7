add_executable(
    tungsten_c_tests
    tungsten_c_tests.c
)

target_link_libraries(
    tungsten_c_tests
    PRIVATE
        tools::data_structures::tungsten_c
)

tungsten_c_configure_target(tungsten_c_tests)

add_test(NAME tungsten_c.core COMMAND tungsten_c_tests)
