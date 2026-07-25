# SPDX-License-Identifier: MIT-0

include_guard(GLOBAL)

function(d7_add_headless_command_test)
    set(one_value_arguments NAME)
    set(multi_value_arguments COMMAND)
    cmake_parse_arguments(
        D7_HEADLESS_COMMAND_TEST
        ""
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN}
    )

    if(NOT D7_HEADLESS_COMMAND_TEST_NAME)
        message(FATAL_ERROR "d7_add_headless_command_test requires NAME.")
    endif()
    if(NOT D7_HEADLESS_COMMAND_TEST_COMMAND)
        message(FATAL_ERROR "d7_add_headless_command_test requires COMMAND.")
    endif()

    if(WIN32)
        find_program(
            D7_POWERSHELL_EXECUTABLE
            NAMES pwsh powershell
            REQUIRED
        )
        string(MAKE_C_IDENTIFIER "${D7_HEADLESS_COMMAND_TEST_NAME}" test_identifier)
        set(
            argument_file
            "${CMAKE_CURRENT_BINARY_DIR}/headless-test-arguments/${test_identifier}-$<CONFIG>.txt"
        )
        set(argument_file_content "")
        foreach(argument IN LISTS D7_HEADLESS_COMMAND_TEST_COMMAND)
            if(argument MATCHES "[\r\n]")
                message(FATAL_ERROR "Headless command-test arguments cannot contain newlines.")
            endif()
            string(APPEND argument_file_content "${argument}\n")
        endforeach()
        file(GENERATE OUTPUT "${argument_file}" CONTENT "${argument_file_content}")
        add_test(
            NAME ${D7_HEADLESS_COMMAND_TEST_NAME}
            COMMAND
                "${D7_POWERSHELL_EXECUTABLE}"
                -NoLogo
                -NoProfile
                -NonInteractive
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Invoke-HeadlessCommandTest.ps1"
                -ArgumentFile "${argument_file}"
        )
    else()
        add_test(
            NAME ${D7_HEADLESS_COMMAND_TEST_NAME}
            COMMAND ${D7_HEADLESS_COMMAND_TEST_COMMAND}
        )
    endif()
endfunction()

function(d7_add_headless_test)
    set(one_value_arguments NAME TARGET)
    set(multi_value_arguments ARGUMENTS)
    cmake_parse_arguments(
        D7_HEADLESS_TEST
        ""
        "${one_value_arguments}"
        "${multi_value_arguments}"
        ${ARGN}
    )

    if(NOT D7_HEADLESS_TEST_NAME)
        message(FATAL_ERROR "d7_add_headless_test requires NAME.")
    endif()
    if(NOT D7_HEADLESS_TEST_TARGET)
        message(FATAL_ERROR "d7_add_headless_test requires TARGET.")
    endif()
    if(NOT TARGET ${D7_HEADLESS_TEST_TARGET})
        message(FATAL_ERROR "d7_add_headless_test target '${D7_HEADLESS_TEST_TARGET}' does not exist.")
    endif()

    target_include_directories(
        ${D7_HEADLESS_TEST_TARGET}
        PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/test_support/include"
    )

    if(WIN32)
        find_program(
            D7_POWERSHELL_EXECUTABLE
            NAMES pwsh powershell
            REQUIRED
        )
        add_test(
            NAME ${D7_HEADLESS_TEST_NAME}
            COMMAND
                "${D7_POWERSHELL_EXECUTABLE}"
                -NoLogo
                -NoProfile
                -NonInteractive
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Invoke-HeadlessTest.ps1"
                "$<TARGET_FILE:${D7_HEADLESS_TEST_TARGET}>"
                ${D7_HEADLESS_TEST_ARGUMENTS}
        )
    else()
        add_test(
            NAME ${D7_HEADLESS_TEST_NAME}
            COMMAND
                ${D7_HEADLESS_TEST_TARGET}
                ${D7_HEADLESS_TEST_ARGUMENTS}
        )
    endif()

    # On Windows, Clang's ASan executable imports interception entry points from
    # the exact runtime shipped with that compiler. Visual Studio also puts an
    # independently versioned DLL with the same basename on PATH. Put the
    # compiler-matched runtime beside the executable so the loader cannot choose
    # an incompatible PATH entry before the test reaches main().
    set(_d7_compiler "")
    set(_d7_compiler_id "")
    set(_d7_sanitizer_flags "")
    if(CMAKE_CXX_COMPILER)
        set(_d7_compiler "${CMAKE_CXX_COMPILER}")
        set(_d7_compiler_id "${CMAKE_CXX_COMPILER_ID}")
        string(APPEND _d7_sanitizer_flags " ${CMAKE_CXX_FLAGS}")
    elseif(CMAKE_C_COMPILER)
        set(_d7_compiler "${CMAKE_C_COMPILER}")
        set(_d7_compiler_id "${CMAKE_C_COMPILER_ID}")
        string(APPEND _d7_sanitizer_flags " ${CMAKE_C_FLAGS}")
    endif()
    string(APPEND _d7_sanitizer_flags " ${CMAKE_EXE_LINKER_FLAGS}")

    if(
        WIN32
        AND _d7_compiler_id MATCHES "Clang"
        AND _d7_sanitizer_flags MATCHES "sanitize=[^ ]*address"
    )
        execute_process(
            COMMAND "${_d7_compiler}" --print-resource-dir
            RESULT_VARIABLE _d7_resource_dir_result
            OUTPUT_VARIABLE _d7_resource_dir
            ERROR_VARIABLE _d7_resource_dir_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _d7_resource_dir_result EQUAL 0)
            message(
                FATAL_ERROR
                "Could not locate the Clang runtime directory: ${_d7_resource_dir_error}"
            )
        endif()

        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
            set(_d7_asan_arch aarch64)
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_d7_asan_arch x86_64)
        else()
            set(_d7_asan_arch i386)
        endif()
        set(
            _d7_asan_runtime
            "${_d7_resource_dir}/lib/windows/clang_rt.asan_dynamic-${_d7_asan_arch}.dll"
        )
        if(NOT EXISTS "${_d7_asan_runtime}")
            message(
                FATAL_ERROR
                "Clang AddressSanitizer runtime not found at '${_d7_asan_runtime}'."
            )
        endif()

        add_custom_command(
            TARGET ${D7_HEADLESS_TEST_TARGET}
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${_d7_asan_runtime}"
                "$<TARGET_FILE_DIR:${D7_HEADLESS_TEST_TARGET}>"
            COMMENT
                "Copying compiler-matched AddressSanitizer runtime for ${D7_HEADLESS_TEST_TARGET}"
            VERBATIM
        )
    endif()
endfunction()
