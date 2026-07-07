add_executable(
    tungsten_smoke_tests
    tungsten_tests.cpp
)

target_link_libraries(
    tungsten_smoke_tests
    PRIVATE
        tools::data_structures::tungsten
)

target_include_directories(
    tungsten_smoke_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

tungsten_configure_target(tungsten_smoke_tests)

add_test(NAME tungsten.smoke COMMAND tungsten_smoke_tests)
