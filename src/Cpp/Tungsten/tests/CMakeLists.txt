add_executable(
    tungsten_smoke_tests
    tungsten_tests.cpp
)

target_link_libraries(
    tungsten_smoke_tests
    PRIVATE
        durable7::tungsten
)

target_include_directories(
    tungsten_smoke_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

tungsten_configure_target(tungsten_smoke_tests)

d7_add_headless_test(NAME tungsten.smoke TARGET tungsten_smoke_tests)
