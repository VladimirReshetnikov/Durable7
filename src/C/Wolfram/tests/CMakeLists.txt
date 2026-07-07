add_executable(
    wolfram_c_tests
    wolfram_c_tests.c
)

target_link_libraries(
    wolfram_c_tests
    PRIVATE
        tools::data_structures::wolfram_c
)

wolfram_c_configure_target(wolfram_c_tests)

add_test(NAME wolfram_c.core COMMAND wolfram_c_tests)
