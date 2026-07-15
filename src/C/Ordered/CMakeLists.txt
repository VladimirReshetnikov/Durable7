cmake_minimum_required(VERSION 3.28)

project(
    ToolsDataStructuresOrderedC
    VERSION 0.1.0
    DESCRIPTION "C port of the neutral persistent ordered set"
    LANGUAGES C
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../eng/HeadlessTest.cmake")

option(ORDERED_C_BUILD_TESTS "Build the C Ordered tests." ON)

add_library(tools_data_structures_hamt_c STATIC ../Hamt/src/hamt.c)
add_library(tools::data_structures::hamt_c ALIAS tools_data_structures_hamt_c)

target_include_directories(
    tools_data_structures_hamt_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Hamt/include>
        $<INSTALL_INTERFACE:include>
)

set_property(TARGET tools_data_structures_hamt_c PROPERTY C_STANDARD 17)
set_property(TARGET tools_data_structures_hamt_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET tools_data_structures_hamt_c PROPERTY C_EXTENSIONS OFF)

add_subdirectory(../FingerTree ${CMAKE_CURRENT_BINARY_DIR}/FingerTree)

add_library(tools_data_structures_ordered_c)
add_library(tools::data_structures::ordered_c ALIAS tools_data_structures_ordered_c)

target_sources(
    tools_data_structures_ordered_c
    PRIVATE
        src/ordered_set.c
)

target_include_directories(
    tools_data_structures_ordered_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    tools_data_structures_ordered_c
    PUBLIC
        tools::data_structures::finger_tree_c
        tools::data_structures::hamt_c
)

set_property(TARGET tools_data_structures_ordered_c PROPERTY C_STANDARD 17)
set_property(TARGET tools_data_structures_ordered_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET tools_data_structures_ordered_c PROPERTY C_EXTENSIONS OFF)

function(ordered_c_configure_target target)
    set_property(TARGET ${target} PROPERTY C_STANDARD 17)
    set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
    set_property(TARGET ${target} PROPERTY C_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(
            ${target}
            PRIVATE
                /permissive-
                /W4
                /WX
                /external:anglebrackets
                /external:W0
        )
        if(${target} STREQUAL "tools_data_structures_hamt_c")
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

ordered_c_configure_target(tools_data_structures_hamt_c)
ordered_c_configure_target(tools_data_structures_ordered_c)

if(ORDERED_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
