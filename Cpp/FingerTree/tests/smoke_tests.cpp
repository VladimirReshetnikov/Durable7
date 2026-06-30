#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/operation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <vector>

using namespace tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

int main()
{
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

    return tests.run();
}
