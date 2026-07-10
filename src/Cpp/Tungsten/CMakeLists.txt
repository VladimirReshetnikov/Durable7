cmake_minimum_required(VERSION 3.28)

project(
    ToolsDataStructuresTungstenCpp
    VERSION 0.1.0
    DESCRIPTION "C++ port of Tools.DataStructures.Tungsten"
    LANGUAGES CXX
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../eng/HeadlessTest.cmake")

option(TUNGSTEN_BUILD_TESTS "Build the C++ Tungsten collection tests." ON)

add_library(tools_data_structures_tungsten INTERFACE)
add_library(tools::data_structures::tungsten ALIAS tools_data_structures_tungsten)

target_include_directories(
    tools_data_structures_tungsten
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../FingerTree/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../Hamt/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(tools_data_structures_tungsten INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(
        tools_data_structures_tungsten
        INTERFACE
            /std:c++latest
            /permissive-
            /Zc:__cplusplus
            /external:anglebrackets
            /external:W0
    )
endif()

function(tungsten_configure_target target)
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

if(TUNGSTEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
