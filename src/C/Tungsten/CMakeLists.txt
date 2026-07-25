cmake_minimum_required(VERSION 3.28)

project(
    Durable7TungstenC
    VERSION 0.1.0
    DESCRIPTION "C port of Durable7.Tungsten"
    LANGUAGES C
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../eng/HeadlessTest.cmake")

option(TUNGSTEN_C_BUILD_TESTS "Build the C Tungsten tests." ON)

add_library(durable7_hamt_c STATIC ../Hamt/src/hamt.c)
add_library(durable7::hamt_c ALIAS durable7_hamt_c)

target_include_directories(
    durable7_hamt_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Hamt/include>
        $<INSTALL_INTERFACE:include>
)

set_property(TARGET durable7_hamt_c PROPERTY C_STANDARD 17)
set_property(TARGET durable7_hamt_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET durable7_hamt_c PROPERTY C_EXTENSIONS OFF)

add_subdirectory(../FingerTree ${CMAKE_CURRENT_BINARY_DIR}/FingerTree)

add_library(durable7_tungsten_c)
add_library(durable7::tungsten_c ALIAS durable7_tungsten_c)

target_sources(
    durable7_tungsten_c
    PRIVATE
        src/tungsten.c
)

target_include_directories(
    durable7_tungsten_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    durable7_tungsten_c
    PUBLIC
        durable7::finger_tree_c
        durable7::hamt_c
)

set_property(TARGET durable7_tungsten_c PROPERTY C_STANDARD 17)
set_property(TARGET durable7_tungsten_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET durable7_tungsten_c PROPERTY C_EXTENSIONS OFF)

function(tungsten_c_configure_target target)
    set_property(TARGET ${target} PROPERTY C_STANDARD 17)
    set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
    set_property(TARGET ${target} PROPERTY C_EXTENSIONS OFF)

    if(MSVC)
        # <stdatomic.h> is gated behind /experimental:c11atomics in MSVC's C11/C17 modes.
        target_compile_options(
            ${target}
            PRIVATE
                /permissive-
                /W4
                /WX
                /experimental:c11atomics
                /external:anglebrackets
                /external:W0
        )
        if(${target} STREQUAL "durable7_hamt_c")
            target_compile_options(${target} PRIVATE /wd4200)
        endif()
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
        )
    endif()
endfunction()

tungsten_c_configure_target(durable7_hamt_c)
tungsten_c_configure_target(durable7_tungsten_c)

if(TUNGSTEN_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
