cmake_minimum_required(VERSION 3.28)

project(FingerTreeInstalledConsumer LANGUAGES CXX)

option(FINGERTREE_BUILD_TESTS "Must remain off in the installed-package consumer." OFF)
option(FINGERTREE_BUILD_SAMPLES "Must remain off in the installed-package consumer." OFF)
option(FINGERTREE_BUILD_BENCHMARKS "Must remain off in the installed-package consumer." OFF)
if(FINGERTREE_BUILD_TESTS OR FINGERTREE_BUILD_SAMPLES OR FINGERTREE_BUILD_BENCHMARKS)
    message(FATAL_ERROR "The installed consumer must not build repository tests, samples, or benchmarks.")
endif()
if(NOT DEFINED FINGERTREE_EXPECTED_PREFIX OR FINGERTREE_EXPECTED_PREFIX STREQUAL "")
    message(FATAL_ERROR "The installed consumer requires its expected staging prefix.")
endif()

find_package(Durable7FingerTree 0.1 CONFIG REQUIRED)
file(REAL_PATH "${FINGERTREE_EXPECTED_PREFIX}" expected_prefix)
file(REAL_PATH "${Durable7FingerTree_DIR}" resolved_package_dir)
file(TO_CMAKE_PATH "${expected_prefix}" expected_prefix)
file(TO_CMAKE_PATH "${resolved_package_dir}" resolved_package_dir)
string(FIND "${resolved_package_dir}/" "${expected_prefix}/" prefix_position)
if(NOT prefix_position EQUAL 0)
    message(
        FATAL_ERROR
        "find_package resolved '${resolved_package_dir}' outside the staged prefix '${expected_prefix}'."
    )
endif()

add_executable(fingertree_installed_consumer main.cpp)
target_link_libraries(
    fingertree_installed_consumer
    PRIVATE
        durable7::finger_tree
)

enable_testing()
add_test(NAME fingertree.installed-consumer.run COMMAND fingertree_installed_consumer)
