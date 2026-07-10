cmake_minimum_required(VERSION 3.28)

project(
    ToolsDataStructuresFingerTreeCpp
    VERSION 0.1.0
    DESCRIPTION "C++ port of Tools.DataStructures.FingerTree"
    LANGUAGES CXX
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../eng/HeadlessTest.cmake")

option(FINGERTREE_BUILD_TESTS "Build the C++ FingerTree tests." ON)
option(
    FINGERTREE_ENABLE_ALLOCATION_TRACKING
    "Override global new/delete in tests for allocation regression guards. Disable under AddressSanitizer."
    ON
)

add_library(tools_data_structures_finger_tree INTERFACE)
add_library(tools::data_structures::finger_tree ALIAS tools_data_structures_finger_tree)

target_include_directories(
    tools_data_structures_finger_tree
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tools_data_structures_finger_tree INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(
        tools_data_structures_finger_tree
        INTERFACE
            /std:c++latest
            /permissive-
            /Zc:__cplusplus
            /external:anglebrackets
            /external:W0
    )
endif()

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
    add_subdirectory(tests)
endif()
