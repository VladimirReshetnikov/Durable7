cmake_minimum_required(VERSION 3.28)

project(
    ToolsDataStructuresWolframC
    VERSION 0.1.0
    DESCRIPTION "C port of Tools.DataStructures.Wolfram"
    LANGUAGES C
)

option(WOLFRAM_C_BUILD_TESTS "Build the C Wolfram tests." ON)

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

add_library(tools_data_structures_wolfram_c)
add_library(tools::data_structures::wolfram_c ALIAS tools_data_structures_wolfram_c)

target_sources(
    tools_data_structures_wolfram_c
    PRIVATE
        src/wolfram.c
)

target_include_directories(
    tools_data_structures_wolfram_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    tools_data_structures_wolfram_c
    PUBLIC
        tools::data_structures::finger_tree_c
        tools::data_structures::hamt_c
)

set_property(TARGET tools_data_structures_wolfram_c PROPERTY C_STANDARD 17)
set_property(TARGET tools_data_structures_wolfram_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET tools_data_structures_wolfram_c PROPERTY C_EXTENSIONS OFF)

function(wolfram_c_configure_target target)
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

wolfram_c_configure_target(tools_data_structures_hamt_c)
wolfram_c_configure_target(tools_data_structures_wolfram_c)

if(WOLFRAM_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
