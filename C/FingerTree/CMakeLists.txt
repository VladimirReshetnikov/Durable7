cmake_minimum_required(VERSION 3.28)

project(
    ToolsDataStructuresFingerTreeC
    VERSION 0.1.0
    DESCRIPTION "C port of Tools.DataStructures.FingerTree"
    LANGUAGES C
)

option(FINGERTREE_C_BUILD_TESTS "Build the C FingerTree tests." ON)

add_library(tools_data_structures_finger_tree_c)
add_library(tools::data_structures::finger_tree_c ALIAS tools_data_structures_finger_tree_c)

target_sources(
    tools_data_structures_finger_tree_c
    PRIVATE
        src/fingertree.c
)

target_include_directories(
    tools_data_structures_finger_tree_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

set_property(TARGET tools_data_structures_finger_tree_c PROPERTY C_STANDARD 11)
set_property(TARGET tools_data_structures_finger_tree_c PROPERTY C_STANDARD_REQUIRED ON)
set_property(TARGET tools_data_structures_finger_tree_c PROPERTY C_EXTENSIONS OFF)

function(fingertree_c_configure_target target)
    set_property(TARGET ${target} PROPERTY C_STANDARD 11)
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

fingertree_c_configure_target(tools_data_structures_finger_tree_c)

if(FINGERTREE_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
