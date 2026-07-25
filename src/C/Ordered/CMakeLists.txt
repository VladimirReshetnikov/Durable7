cmake_minimum_required(VERSION 3.28)

project(
    Durable7OrderedC
    VERSION 0.1.0
    DESCRIPTION "C port of the neutral persistent ordered set"
    LANGUAGES C
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../eng/HeadlessTest.cmake")

option(ORDERED_C_BUILD_TESTS "Build the C Ordered tests." ON)

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

add_library(durable7_ordered_c)
add_library(durable7::ordered_c ALIAS durable7_ordered_c)

target_sources(
    durable7_ordered_c
    PRIVATE
        src/ordered_map.c
        src/ordered_multimap.c
        src/ordered_cursor.c
        src/ordered_set.c
)

target_include_directories(
    durable7_ordered_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    durable7_ordered_c
    PUBLIC
        durable7::finger_tree_c
        durable7::hamt_c
)

set_property(TARGET durable7_ordered_c PROPERTY C_STANDARD 17)
set_property(TARGET durable7_ordered_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET durable7_ordered_c PROPERTY C_EXTENSIONS OFF)

function(ordered_c_configure_target target)
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

ordered_c_configure_target(durable7_hamt_c)
ordered_c_configure_target(durable7_ordered_c)

if(ORDERED_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
