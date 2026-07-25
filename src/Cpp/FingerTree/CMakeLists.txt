cmake_minimum_required(VERSION 3.28)

project(
    Durable7FingerTreeCpp
    VERSION 0.1.0
    DESCRIPTION "C++ port of Durable7.FingerTree"
    LANGUAGES CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../eng/HeadlessTest.cmake")

option(FINGERTREE_BUILD_TESTS "Build the C++ FingerTree tests." ON)
option(FINGERTREE_BUILD_SAMPLES "Build the C++ FingerTree samples." ON)
option(FINGERTREE_BUILD_BENCHMARKS "Build the dependency-free C++ FingerTree benchmark harness." ON)
option(
    FINGERTREE_ENABLE_ALLOCATION_TRACKING
    "Override global new/delete in tests/benchmarks for allocation guards. Disable under sanitizers."
    ON
)

add_library(durable7_finger_tree INTERFACE)
add_library(durable7::finger_tree ALIAS durable7_finger_tree)

if(WIN32)
    target_link_libraries(durable7_finger_tree INTERFACE bcrypt)
else()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(durable7_finger_tree INTERFACE OpenSSL::Crypto)
endif()

target_include_directories(
    durable7_finger_tree
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(durable7_finger_tree INTERFACE cxx_std_23)
set_target_properties(
    durable7_finger_tree
    PROPERTIES
        EXPORT_NAME finger_tree
)

# Keep an export created by MSVC consumable by non-MSVC compilers. Generator
# expressions defer the compiler-specific flags until the consuming project.
target_compile_options(
    durable7_finger_tree
    INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/std:c++latest>
        $<$<CXX_COMPILER_ID:MSVC>:/permissive->
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
        $<$<CXX_COMPILER_ID:MSVC>:/external:anglebrackets>
        $<$<CXX_COMPILER_ID:MSVC>:/external:W0>
)

function(fingertree_configure_target target)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)
    set_property(TARGET ${target} PROPERTY CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /std:c++latest
                /permissive-
                /Zc:__cplusplus
                /W4
                /WX
                /external:anglebrackets
                /external:W0
        )
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # The smoke tests observe replacement new/delete; GCC allocation-DCE can erase those side effects.
            target_compile_options(
                ${target}
                PRIVATE
                    -fno-allocation-dce
            )
        endif()
    endif()
endfunction()

if(FINGERTREE_BUILD_TESTS)
    enable_testing()
endif()

if(FINGERTREE_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(FINGERTREE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(FINGERTREE_BUILD_TESTS)
    add_subdirectory(tests)

    d7_add_headless_command_test(
        NAME fingertree.installed-consumer
        COMMAND
            "${CMAKE_COMMAND}"
            "-DFINGERTREE_SOURCE_BINARY_DIR:PATH=${CMAKE_BINARY_DIR}"
            "-DFINGERTREE_CONSUMER_SOURCE_DIR:PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer"
            "-DFINGERTREE_CONSUMER_WORK_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/installed-consumer/$<CONFIG>"
            "-DFINGERTREE_GENERATOR:STRING=${CMAKE_GENERATOR}"
            "-DFINGERTREE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
            "-DFINGERTREE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
            "-DFINGERTREE_CONFIGURATION:STRING=$<CONFIG>"
            "-DFINGERTREE_CTEST_COMMAND:FILEPATH=${CMAKE_CTEST_COMMAND}"
            "-DFINGERTREE_INSTALL_DATAROOTDIR:STRING=${CMAKE_INSTALL_DATAROOTDIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInstalledConsumer.cmake"
    )
    set_tests_properties(
        fingertree.installed-consumer
        PROPERTIES
            LABELS "fingertree;integration;packaging"
    )
endif()

install(
    TARGETS durable7_finger_tree
    EXPORT Durable7FingerTreeTargets
)
install(
    DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)
install(
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../../LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/Durable7FingerTree"
)

set(
    FINGERTREE_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/Durable7FingerTree"
    CACHE STRING
    "Install location for the Durable7FingerTree CMake package."
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Durable7FingerTreeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Durable7FingerTreeConfig.cmake"
    INSTALL_DESTINATION "${FINGERTREE_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/Durable7FingerTreeConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

install(
    EXPORT Durable7FingerTreeTargets
    FILE Durable7FingerTreeTargets.cmake
    NAMESPACE durable7::
    DESTINATION "${FINGERTREE_INSTALL_CMAKEDIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/Durable7FingerTreeConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/Durable7FingerTreeConfigVersion.cmake"
    DESTINATION "${FINGERTREE_INSTALL_CMAKEDIR}"
)
