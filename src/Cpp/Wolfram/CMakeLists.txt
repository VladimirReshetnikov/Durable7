cmake_minimum_required(VERSION 3.28)

project(
    ToolsDataStructuresWolframCpp
    VERSION 0.1.0
    DESCRIPTION "C++ port of Tools.DataStructures.Wolfram"
    LANGUAGES CXX
)

option(WOLFRAM_BUILD_TESTS "Build the C++ Wolfram collection tests." ON)

add_library(tools_data_structures_wolfram INTERFACE)
add_library(tools::data_structures::wolfram ALIAS tools_data_structures_wolfram)

target_include_directories(
    tools_data_structures_wolfram
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../FingerTree/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Hamt/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tools_data_structures_wolfram INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(
        tools_data_structures_wolfram
        INTERFACE
            /std:c++latest
            /permissive-
            /Zc:__cplusplus
            /external:anglebrackets
            /external:W0
    )
endif()

function(wolfram_configure_target target)
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
    endif()
endfunction()

if(WOLFRAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
