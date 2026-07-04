#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/operation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <vector>

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <crtdbg.h>
#include <windows.h>
#endif

using namespace tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

void add_measure_tests(suite& tests);
void add_interval_tree_tests(suite& tests);
void add_lazy_cell_tests(suite& tests);
void add_atomic_box_tests(suite& tests);
void add_measured_finger_tree_tests(suite& tests);
void add_measured_lazy_cell_tests(suite& tests);
void add_measured_rope_tests(suite& tests);
void add_persistent_deque_tests(suite& tests);
void add_priority_queue_tests(suite& tests);
void add_reversible_deque_tests(suite& tests);
void add_rope_tests(suite& tests);
void add_rope_text_tests(suite& tests);
void add_sorted_collection_tests(suite& tests);
void add_tearable_concurrency_tests(suite& tests);

#ifdef _MSC_VER
void configure_non_interactive_failure_reporting()
{
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_invalid_parameter_handler([](
                                       const wchar_t*,
                                       const wchar_t*,
                                       const wchar_t*,
                                       unsigned int,
                                       uintptr_t) {
        std::abort();
    });

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
}
#endif

int main()
{
#ifdef _MSC_VER
    configure_non_interactive_failure_reporting();
#endif
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    suite tests;

    tests.add("public aggregate header exposes version metadata", [] {
        FT_REQUIRE_EQUAL(library_name, std::string_view{"Tools.DataStructures.FingerTree.Cpp"});
        FT_REQUIRE_EQUAL(version_major, 0U);
        FT_REQUIRE_EQUAL(version_minor, 1U);
        FT_REQUIRE_EQUAL(version_patch, 0U);
    });

    tests.add("index validation follows size_t bounds", [] {
        FT_REQUIRE(is_valid_index(0, 1));
        FT_REQUIRE(!is_valid_index(1, 1));
        throw_if_index_out_of_range(0, 1);
        throw_if_insert_index_out_of_range(1, 1);
    });

    tests.add("deterministic rng replays seeds", [] {
        deterministic_rng first{12345};
        deterministic_rng second{12345};

        for (auto i = 0; i != 16; ++i) {
            FT_REQUIRE_EQUAL(first.next(), second.next());
        }
    });

    tests.add("operation counter observes comparator calls", [] {
        operation_counter counter;
        counting_compare<> compare{counter};
        std::vector values{4, 1, 3, 2};

        std::sort(values.begin(), values.end(), compare);

        FT_REQUIRE_EQUAL(values.front(), 1);
        FT_REQUIRE(counter.comparisons() > 0);
    });

    tests.add("allocation counter can bracket heap work", [] {
        allocation_counting_scope scope;
        auto values = std::vector<int>{};
        values.reserve(32);

        FT_REQUIRE_EQUAL(values.capacity(), static_cast<std::size_t>(32));
        FT_REQUIRE(scope.allocations() > 0);
        FT_REQUIRE(scope.bytes_allocated() >= 32 * sizeof(int));
    });

    tests.add("allocation counter covers nothrow and aligned global allocation paths", [] {
        allocation_counting_scope scope;

        void* const nothrow_memory = ::operator new(64, std::nothrow);
        FT_REQUIRE(nothrow_memory != nullptr);
        ::operator delete(nothrow_memory);

        constexpr auto alignment = std::align_val_t{64};
        void* const aligned_memory = ::operator new(64, alignment, std::nothrow);
        FT_REQUIRE(aligned_memory != nullptr);
        FT_REQUIRE_EQUAL(reinterpret_cast<std::uintptr_t>(aligned_memory) % 64, std::uintptr_t{0});
        ::operator delete(aligned_memory, alignment);

        FT_REQUIRE_EQUAL(scope.allocations(), static_cast<std::size_t>(2));
        FT_REQUIRE_EQUAL(scope.deallocations(), static_cast<std::size_t>(2));
    });

    add_measure_tests(tests);
    add_interval_tree_tests(tests);
    add_lazy_cell_tests(tests);
    add_atomic_box_tests(tests);
    add_measured_finger_tree_tests(tests);
    add_measured_lazy_cell_tests(tests);
    add_measured_rope_tests(tests);
    add_persistent_deque_tests(tests);
    add_priority_queue_tests(tests);
    add_reversible_deque_tests(tests);
    add_rope_tests(tests);
    add_rope_text_tests(tests);
    add_sorted_collection_tests(tests);
    add_tearable_concurrency_tests(tests);

    return tests.run();
}
