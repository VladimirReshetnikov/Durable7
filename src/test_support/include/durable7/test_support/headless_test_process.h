// SPDX-License-Identifier: MIT-0
#ifndef DURABLE7_TEST_SUPPORT_HEADLESS_TEST_PROCESS_H
#define DURABLE7_TEST_SUPPORT_HEADLESS_TEST_PROCESS_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if defined(_MSC_VER)
#include <crtdbg.h>

static void __cdecl d7_test_invalid_parameter_handler(
    const wchar_t* expression,
    const wchar_t* function_name,
    const wchar_t* file_name,
    unsigned int line_number,
    uintptr_t reserved)
{
    (void)expression;
    (void)function_name;
    (void)file_name;
    (void)line_number;
    (void)reserved;
    fputs("fatal: invalid parameter passed to the Microsoft C runtime\n", stderr);
    fflush(stderr);
    abort();
}
#endif
#endif

static void d7_configure_headless_test_process(void)
{
#if defined(_WIN32)
    const UINT suppressed_error_modes =
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    (void)SetErrorMode(GetErrorMode() | suppressed_error_modes);

#if defined(_MSC_VER)
    (void)_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    (void)_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    (void)_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    (void)_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    (void)_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    (void)_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    (void)_set_error_mode(_OUT_TO_STDERR);
    (void)_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    (void)_set_invalid_parameter_handler(d7_test_invalid_parameter_handler);
#endif
#endif
}

static int d7_is_headless_test_process_configured(void)
{
#if !defined(_WIN32)
    return 1;
#else
    const UINT suppressed_error_modes =
        SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    if ((GetErrorMode() & suppressed_error_modes) != suppressed_error_modes) {
        return 0;
    }

#if defined(_MSC_VER)
    if (_set_error_mode(_REPORT_ERRMODE) != _OUT_TO_STDERR) {
        return 0;
    }
    if ((_set_abort_behavior(0, 0) & (_WRITE_ABORT_MSG | _CALL_REPORTFAULT)) != 0) {
        return 0;
    }
    if (_get_invalid_parameter_handler() != d7_test_invalid_parameter_handler) {
        return 0;
    }
    if ((_CrtSetReportMode(_CRT_WARN, _CRTDBG_REPORT_MODE) & _CRTDBG_MODE_WNDW) != 0 ||
        (_CrtSetReportMode(_CRT_ERROR, _CRTDBG_REPORT_MODE) & _CRTDBG_MODE_WNDW) != 0 ||
        (_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE) & _CRTDBG_MODE_WNDW) != 0) {
        return 0;
    }
#endif

    return 1;
#endif
}

static int d7_enter_headless_test_process(void)
{
    d7_configure_headless_test_process();
    if (!d7_is_headless_test_process_configured()) {
        fputs("fatal: failed to configure non-interactive test error reporting\n", stderr);
        fflush(stderr);
        return 0;
    }

    return 1;
}

#endif
