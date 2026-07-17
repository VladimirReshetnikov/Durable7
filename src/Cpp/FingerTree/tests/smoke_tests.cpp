#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/operation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <tools/data_structures/test_support/headless_test_process.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
#include <vector>

using namespace tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

void add_measure_tests(suite& tests);
void add_interval_tree_tests(suite& tests);
void add_persistent_interval_map_tests(suite& tests);
void add_lazy_cell_tests(suite& tests);
void add_atomic_box_tests(suite& tests);
void add_brodal_okasaki_heap_tests(suite& tests);
void add_canonical_sorted_set_tests(suite& tests);
void add_command_sequence_tests(suite& tests);
void add_daba_lite_tests(suite& tests);
void add_measured_finger_tree_tests(suite& tests);
void add_measured_lazy_cell_tests(suite& tests);
void add_measured_rope_tests(suite& tests);
void add_persistent_deque_tests(suite& tests);
void add_priority_queue_tests(suite& tests);
void add_priority_search_queue_tests(suite& tests);
void add_range_update_sequence_tests(suite& tests);
void add_reversible_deque_tests(suite& tests);
void add_rope_tests(suite& tests);
void add_rope_text_tests(suite& tests);
void add_rrb_vector_tests(suite& tests);
void add_sorted_collection_tests(suite& tests);
void add_tearable_concurrency_tests(suite& tests);

int main(const int argument_count, const char* const* arguments)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    suite tests;

    tests.set_group("support");
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

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
    tests.add("allocation counter can bracket heap work", [] {
        allocation_counting_scope scope;
        constexpr auto byte_count = std::size_t{32 * sizeof(int)};
        void* const memory = ::operator new(byte_count);

        FT_REQUIRE(memory != nullptr);
        FT_REQUIRE_EQUAL(scope.allocations(), static_cast<std::size_t>(1));
        FT_REQUIRE(scope.bytes_allocated() >= byte_count);
        ::operator delete(memory);
        FT_REQUIRE_EQUAL(scope.deallocations(), static_cast<std::size_t>(1));
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
#endif

    tests.set_group("measure");
    add_measure_tests(tests);

    tests.set_group("interval-tree");
    add_interval_tree_tests(tests);

    tests.set_group("interval-map");
    add_persistent_interval_map_tests(tests);

    tests.set_group("lazy-cell");
    add_lazy_cell_tests(tests);

    tests.set_group("atomic-box");
    add_atomic_box_tests(tests);

    tests.set_group("brodal-okasaki-heap");
    add_brodal_okasaki_heap_tests(tests);

    tests.set_group("canonical-sorted-set");
    add_canonical_sorted_set_tests(tests);

    tests.set_group("command-model");
    add_command_sequence_tests(tests);

    tests.set_group("daba-lite");
    add_daba_lite_tests(tests);

    tests.set_group("measured-tree");
    add_measured_finger_tree_tests(tests);

    tests.set_group("measured-lazy-cell");
    add_measured_lazy_cell_tests(tests);

    tests.set_group("measured-rope");
    add_measured_rope_tests(tests);

    tests.set_group("deque");
    add_persistent_deque_tests(tests);

    tests.set_group("priority-queue");
    add_priority_queue_tests(tests);

    tests.set_group("priority-search-queue");
    add_priority_search_queue_tests(tests);

    tests.set_group("range-update-sequence");
    add_range_update_sequence_tests(tests);

    tests.set_group("reversible-deque");
    add_reversible_deque_tests(tests);

    tests.set_group("rope");
    add_rope_tests(tests);

    tests.set_group("rope-text");
    add_rope_text_tests(tests);

    tests.set_group("rrb-vector");
    add_rrb_vector_tests(tests);

    tests.set_group("sorted-collections");
    add_sorted_collection_tests(tests);

    tests.set_group("concurrency");
    add_tearable_concurrency_tests(tests);

    return tests.run(argument_count, arguments);
}
