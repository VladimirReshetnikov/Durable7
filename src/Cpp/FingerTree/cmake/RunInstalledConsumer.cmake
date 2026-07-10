cmake_minimum_required(VERSION 3.28)

foreach(
    required
    IN ITEMS
        FINGERTREE_SOURCE_BINARY_DIR
        FINGERTREE_CONSUMER_SOURCE_DIR
        FINGERTREE_CONSUMER_WORK_DIR
        FINGERTREE_GENERATOR
        FINGERTREE_CXX_COMPILER
        FINGERTREE_CTEST_COMMAND
        FINGERTREE_INSTALL_DATAROOTDIR
)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "RunInstalledConsumer.cmake requires ${required}.")
    endif()
endforeach()

set(install_prefix "${FINGERTREE_CONSUMER_WORK_DIR}/prefix")
set(consumer_binary_dir "${FINGERTREE_CONSUMER_WORK_DIR}/build")
file(REMOVE_RECURSE "${FINGERTREE_CONSUMER_WORK_DIR}")

set(install_command
    "${CMAKE_COMMAND}"
    --install "${FINGERTREE_SOURCE_BINARY_DIR}"
    --prefix "${install_prefix}"
)
if(DEFINED FINGERTREE_CONFIGURATION AND NOT FINGERTREE_CONFIGURATION STREQUAL "")
    list(APPEND install_command --config "${FINGERTREE_CONFIGURATION}")
endif()

execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_result
    COMMAND_ECHO STDOUT
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "FingerTree package installation failed with exit code ${install_result}.")
endif()
if(
    NOT EXISTS
    "${install_prefix}/${FINGERTREE_INSTALL_DATAROOTDIR}/licenses/ToolsDataStructuresFingerTree/LICENSE"
)
    message(FATAL_ERROR "FingerTree package installation omitted the repository license.")
endif()

set(configure_command
    "${CMAKE_COMMAND}"
    -S "${FINGERTREE_CONSUMER_SOURCE_DIR}"
    -B "${consumer_binary_dir}"
    -G "${FINGERTREE_GENERATOR}"
    "-DCMAKE_PREFIX_PATH:PATH=${install_prefix}"
    "-DCMAKE_CXX_COMPILER:FILEPATH=${FINGERTREE_CXX_COMPILER}"
    "-DFINGERTREE_BUILD_TESTS:BOOL=OFF"
    "-DFINGERTREE_BUILD_SAMPLES:BOOL=OFF"
    "-DFINGERTREE_BUILD_BENCHMARKS:BOOL=OFF"
    "-DFINGERTREE_EXPECTED_PREFIX:PATH=${install_prefix}"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON"
    "-DCMAKE_FIND_PACKAGE_PREFER_CONFIG:BOOL=ON"
    "-DCMAKE_FIND_USE_PACKAGE_REGISTRY:BOOL=OFF"
    "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY:BOOL=OFF"
)
if(DEFINED FINGERTREE_MAKE_PROGRAM AND NOT FINGERTREE_MAKE_PROGRAM STREQUAL "")
    list(APPEND configure_command "-DCMAKE_MAKE_PROGRAM:FILEPATH=${FINGERTREE_MAKE_PROGRAM}")
endif()
if(DEFINED FINGERTREE_CONFIGURATION AND NOT FINGERTREE_CONFIGURATION STREQUAL "")
    list(APPEND configure_command "-DCMAKE_BUILD_TYPE:STRING=${FINGERTREE_CONFIGURATION}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE configure_result
    COMMAND_ECHO STDOUT
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Installed FingerTree consumer configure failed with exit code ${configure_result}.")
endif()

file(READ "${consumer_binary_dir}/compile_commands.json" consumer_compile_commands)
string(REPLACE "\\\\" "/" consumer_compile_commands "${consumer_compile_commands}")
string(REPLACE "\\" "/" consumer_compile_commands "${consumer_compile_commands}")
get_filename_component(source_include_dir "${FINGERTREE_CONSUMER_SOURCE_DIR}/../../include" ABSOLUTE)
file(TO_CMAKE_PATH "${source_include_dir}" source_include_dir)
string(FIND "${consumer_compile_commands}" "${source_include_dir}" source_include_position)
if(NOT source_include_position EQUAL -1)
    message(FATAL_ERROR "Installed consumer leaked the source-tree include directory into its compile command.")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${consumer_binary_dir}")
if(DEFINED FINGERTREE_CONFIGURATION AND NOT FINGERTREE_CONFIGURATION STREQUAL "")
    list(APPEND build_command --config "${FINGERTREE_CONFIGURATION}")
endif()
execute_process(
    COMMAND ${build_command}
    RESULT_VARIABLE build_result
    COMMAND_ECHO STDOUT
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Installed FingerTree consumer build failed with exit code ${build_result}.")
endif()

set(test_command "${FINGERTREE_CTEST_COMMAND}" --test-dir "${consumer_binary_dir}" --output-on-failure)
if(DEFINED FINGERTREE_CONFIGURATION AND NOT FINGERTREE_CONFIGURATION STREQUAL "")
    list(APPEND test_command --build-config "${FINGERTREE_CONFIGURATION}")
endif()
execute_process(
    COMMAND ${test_command}
    RESULT_VARIABLE test_result
    COMMAND_ECHO STDOUT
)
if(NOT test_result EQUAL 0)
    message(FATAL_ERROR "Installed FingerTree consumer test failed with exit code ${test_result}.")
endif()
