add_executable(
    wolfram_smoke_tests
    wolfram_tests.cpp
)

target_link_libraries(
    wolfram_smoke_tests
    PRIVATE
        tools::data_structures::wolfram
)

target_include_directories(
    wolfram_smoke_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

wolfram_configure_target(wolfram_smoke_tests)

add_test(NAME wolfram.smoke COMMAND wolfram_smoke_tests)
