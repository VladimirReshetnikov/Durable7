# SPDX-License-Identifier: MIT-0

include_guard(GLOBAL)

function(tds_add_headless_command_test)
    set(one_value_arguments NAME)
    set(multi_value_arguments COMMAND)
    cmake_parse_arguments(
        TDS_HEADLESS_COMMAND_TEST
        ""
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN}
    )

    if(NOT TDS_HEADLESS_COMMAND_TEST_NAME)
        message(FATAL_ERROR "tds_add_headless_command_test requires NAME.")
    endif()
    if(NOT TDS_HEADLESS_COMMAND_TEST_COMMAND)
        message(FATAL_ERROR "tds_add_headless_command_test requires COMMAND.")
    endif()

    if(WIN32)
        find_program(
            TDS_POWERSHELL_EXECUTABLE
            NAMES pwsh powershell
            REQUIRED
        )
        string(MAKE_C_IDENTIFIER "${TDS_HEADLESS_COMMAND_TEST_NAME}" test_identifier)
        set(
            argument_file
            "${CMAKE_CURRENT_BINARY_DIR}/headless-test-arguments/${test_identifier}-$<CONFIG>.txt"
        )
        set(argument_file_content "")
        foreach(argument IN LISTS TDS_HEADLESS_COMMAND_TEST_COMMAND)
            if(argument MATCHES "[\r\n]")
                message(FATAL_ERROR "Headless command-test arguments cannot contain newlines.")
            endif()
            string(APPEND argument_file_content "${argument}\n")
        endforeach()
        file(GENERATE OUTPUT "${argument_file}" CONTENT "${argument_file_content}")
        add_test(
            NAME ${TDS_HEADLESS_COMMAND_TEST_NAME}
            COMMAND
                "${TDS_POWERSHELL_EXECUTABLE}"
                -NoLogo
                -NoProfile
                -NonInteractive
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Invoke-HeadlessCommandTest.ps1"
                -ArgumentFile "${argument_file}"
        )
    else()
        add_test(
            NAME ${TDS_HEADLESS_COMMAND_TEST_NAME}
            COMMAND ${TDS_HEADLESS_COMMAND_TEST_COMMAND}
        )
    endif()
endfunction()

function(tds_add_headless_test)
    set(one_value_arguments NAME TARGET)
    set(multi_value_arguments ARGUMENTS)
    cmake_parse_arguments(
        TDS_HEADLESS_TEST
        ""
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN}
    )

    if(NOT TDS_HEADLESS_TEST_NAME)
        message(FATAL_ERROR "tds_add_headless_test requires NAME.")
    endif()
    if(NOT TDS_HEADLESS_TEST_TARGET)
        message(FATAL_ERROR "tds_add_headless_test requires TARGET.")
    endif()
    if(NOT TARGET ${TDS_HEADLESS_TEST_TARGET})
        message(FATAL_ERROR "tds_add_headless_test target '${TDS_HEADLESS_TEST_TARGET}' does not exist.")
    endif()

    target_include_directories(
        ${TDS_HEADLESS_TEST_TARGET}
        PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/test_support/include"
    )

    if(WIN32)
        find_program(
            TDS_POWERSHELL_EXECUTABLE
            NAMES pwsh powershell
            REQUIRED
        )
        add_test(
            NAME ${TDS_HEADLESS_TEST_NAME}
            COMMAND
                "${TDS_POWERSHELL_EXECUTABLE}"
                -NoLogo
                -NoProfile
                -NonInteractive
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Invoke-HeadlessTest.ps1"
                "$<TARGET_FILE:${TDS_HEADLESS_TEST_TARGET}>"
                ${TDS_HEADLESS_TEST_ARGUMENTS}
        )
    else()
        add_test(
            NAME ${TDS_HEADLESS_TEST_NAME}
            COMMAND
                ${TDS_HEADLESS_TEST_TARGET}
                ${TDS_HEADLESS_TEST_ARGUMENTS}
        )
    endif()

    # On Windows, Clang's ASan executable imports interception entry points from
    # the exact runtime shipped with that compiler. Visual Studio also puts an
    # independently versioned DLL with the same basename on PATH. Put the
    # compiler-matched runtime beside the executable so the loader cannot choose
    # an incompatible PATH entry before the test reaches main().
    set(_tds_compiler "")
    set(_tds_compiler_id "")
    set(_tds_sanitizer_flags "")
    if(CMAKE_CXX_COMPILER)
        set(_tds_compiler "${CMAKE_CXX_COMPILER}")
        set(_tds_compiler_id "${CMAKE_CXX_COMPILER_ID}")
        string(APPEND _tds_sanitizer_flags " ${CMAKE_CXX_FLAGS}")
    elseif(CMAKE_C_COMPILER)
        set(_tds_compiler "${CMAKE_C_COMPILER}")
        set(_tds_compiler_id "${CMAKE_C_COMPILER_ID}")
        string(APPEND _tds_sanitizer_flags " ${CMAKE_C_FLAGS}")
    endif()
    string(APPEND _tds_sanitizer_flags " ${CMAKE_EXE_LINKER_FLAGS}")

    if(
        WIN32
        AND _tds_compiler_id MATCHES "Clang"
        AND _tds_sanitizer_flags MATCHES "sanitize=[^ ]*address"
    )
        execute_process(
            COMMAND "${_tds_compiler}" --print-resource-dir
            RESULT_VARIABLE _tds_resource_dir_result
            OUTPUT_VARIABLE _tds_resource_dir
            ERROR_VARIABLE _tds_resource_dir_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _tds_resource_dir_result EQUAL 0)
            message(
                FATAL_ERROR
                "Could not locate the Clang runtime directory: ${_tds_resource_dir_error}"
            )
        endif()

        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
            set(_tds_asan_arch aarch64)
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_tds_asan_arch x86_64)
        else()
            set(_tds_asan_arch i386)
        endif()
        set(
            _tds_asan_runtime
            "${_tds_resource_dir}/lib/windows/clang_rt.asan_dynamic-${_tds_asan_arch}.dll"
        )
        if(NOT EXISTS "${_tds_asan_runtime}")
            message(
                FATAL_ERROR
                "Clang AddressSanitizer runtime not found at '${_tds_asan_runtime}'."
            )
        endif()

        add_custom_command(
            TARGET ${TDS_HEADLESS_TEST_TARGET}
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_tds_asan_runtime}"
                "$<TARGET_FILE_DIR:${TDS_HEADLESS_TEST_TARGET}>"
            COMMENT
                "Copying compiler-matched AddressSanitizer runtime for ${TDS_HEADLESS_TEST_TARGET}"
            VERBATIM
        )
    endif()
endfunction()
