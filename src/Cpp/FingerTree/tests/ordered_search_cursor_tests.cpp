#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

static_assert(ft::interval<int>{1, 2} == ft::interval<int>{1, 2});

struct tagged_value final {
    int key;
    char representative;

    friend bool operator==(const tagged_value&, const tagged_value&) = default;
};

struct tagged_value_less final {
    [[nodiscard]] bool operator()(const tagged_value& left, const tagged_value& right) const noexcept
    {
        return left.key < right.key;
    }
};

/// Copyable ordering that tallies its invocations through shared state, so a test can assert
/// how many comparisons an operation actually performs rather than only what it returns.
struct counting_less final {
    std::shared_ptr<std::size_t> calls = std::make_shared<std::size_t>(0);

    [[nodiscard]] bool operator()(const int left, const int right) const
    {
        ++*calls;
        return left < right;
    }
};

/// Ordering by key only, tallying invocations, for the sorted-map bound factories.
struct counting_pair_less final {
    std::shared_ptr<std::size_t> calls = std::make_shared<std::size_t>(0);

    [[nodiscard]] bool operator()(const int left, const int right) const
    {
        ++*calls;
        return left < right;
    }
};

constexpr auto probe_size = std::size_t{4096};

// A finger-tree bound descent touches O(log n) spine nodes and compares a bounded number of
// times per level. This ceiling is far above that and far below probe_size, so it separates a
// logarithmic descent from the linear scan the bound factories used to perform without being
// sensitive to the exact constant.
constexpr auto logarithmic_comparison_ceiling = std::size_t{256};

[[nodiscard]] std::vector<int> ascending_probe_values()
{
    auto values = std::vector<int>{};
    values.reserve(probe_size);
    for (auto index = std::size_t{0}; index != probe_size; ++index) {
        values.push_back(static_cast<int>(index));
    }
    return values;
}

void add_ordered_search_cursor_tests_impl(suite& tests)
{
    tests.add("sorted cursors retain policies and delete exact duplicate occurrences", [] {
        const auto bag = ft::sorted_bag<tagged_value, tagged_value_less>::from_range(
            std::vector<tagged_value>{{1, 'a'}, {1, 'b'}, {1, 'c'}, {2, 'd'}});

        const auto lower = ft::get_cursor_lower_bound(bag, tagged_value{1, 'x'});
        const auto upper = ft::get_cursor_upper_bound(bag, tagged_value{1, 'x'});
        FT_REQUIRE_EQUAL(lower.position(), std::size_t{0});
        FT_REQUIRE_EQUAL(upper.position(), std::size_t{3});

        const auto deleted = ft::get_cursor(bag, 1).delete_next();
        FT_REQUIRE_EQUAL(deleted.position(), std::size_t{1});
        FT_REQUIRE((deleted.snapshot().to_vector()
            == std::vector<tagged_value>{{1, 'a'}, {1, 'c'}, {2, 'd'}}));
        FT_REQUIRE((bag.to_vector()
            == std::vector<tagged_value>{{1, 'a'}, {1, 'b'}, {1, 'c'}, {2, 'd'}}));

        const auto added = lower.add(tagged_value{1, 'e'});
        FT_REQUIRE_EQUAL(added.position(), std::size_t{4});
        FT_REQUIRE((added.snapshot().to_vector()
            == std::vector<tagged_value>{{1, 'a'}, {1, 'b'}, {1, 'c'}, {1, 'e'}, {2, 'd'}}));

        const auto set = ft::sorted_set<int>{1, 3};
        const auto set_added = ft::get_cursor_lower_bound(set, 2).add(2);
        FT_REQUIRE_EQUAL(set_added.position(), std::size_t{2});
        FT_REQUIRE((set_added.snapshot().to_vector() == std::vector{1, 2, 3}));
        FT_REQUIRE((set_added.delete_previous().snapshot().to_vector() == std::vector{1, 3}));

        const auto map = ft::sorted_map<int, int>{{1, 10}, {3, 30}};
        const auto missing = ft::get_cursor_at_key(map, 2);
        FT_REQUIRE(!missing.found);
        FT_REQUIRE_EQUAL(missing.cursor.position(), std::size_t{1});
        const auto inserted = missing.cursor.try_insert(2, 20);
        FT_REQUIRE(inserted.inserted);
        FT_REQUIRE_EQUAL(inserted.cursor.position(), std::size_t{2});
        const auto updated = inserted.cursor.set_next_value(33);
        FT_REQUIRE_EQUAL(updated.snapshot().at(3), 33);
        FT_REQUIRE((updated.delete_previous().snapshot().keys_to_vector() == std::vector{1, 3}));
    });

    tests.add("canonical and priority-search cursors preserve retained policy objects", [] {
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(42);
        const auto canonical = ft::canonical_sorted_set<int>::from_range(
            std::vector{1, 3},
            policy);
        const auto canonical_added = ft::get_cursor_lower_bound(canonical, 2).add(2);
        FT_REQUIRE_EQUAL(canonical_added.position(), std::size_t{2});
        const auto canonical_snapshot = canonical_added.snapshot();
        FT_REQUIRE(canonical_snapshot.policy().is_same_policy(policy));
        FT_REQUIRE((std::vector<int>{canonical_snapshot.begin(), canonical_snapshot.end()}
            == std::vector{1, 2, 3}));
        const auto canonical_deleted = canonical_added.delete_previous().snapshot();
        FT_REQUIRE((std::vector<int>{canonical_deleted.begin(), canonical_deleted.end()}
            == std::vector{1, 3}));

        auto queue = ft::priority_search_queue<int, int, std::string>{};
        queue = queue.set_item(1, 50, "one").set_item(3, 10, "three");
        const auto inserted = ft::get_cursor_lower_bound(queue, 2).try_insert(2, 20, "two");
        FT_REQUIRE(inserted.inserted);
        FT_REQUIRE_EQUAL(inserted.cursor.position(), std::size_t{2});
        FT_REQUIRE_EQUAL(inserted.cursor.snapshot().minimum().key(), 3);
        const auto updated = inserted.cursor.set_next(5, "THREE");
        FT_REQUIRE_EQUAL(updated.snapshot().try_get_entry(3)->value(), std::string{"THREE"});
        FT_REQUIRE_EQUAL(ft::get_cursor_at_minimum_priority(updated.snapshot()).position(), std::size_t{2});
        FT_REQUIRE(!ft::get_cursor_at_key(queue, 2).found);
        FT_REQUIRE(ft::get_cursor_at_key(updated.snapshot(), 2).found);
    });

    tests.add("interval cursors support exact occurrence and overlap navigation", [] {
        using interval = ft::interval<int>;
        const auto tree = ft::interval_tree<int>::from_range(
            std::vector<interval>{{1, 2}, {1, 3}, {4, 7}, {9, 10}});
        const auto exact = ft::get_cursor_at_interval(tree, interval{1, 2});
        FT_REQUIRE(exact.found);
        FT_REQUIRE_EQUAL(exact.cursor.try_peek_next()->high, 2);
        const auto erased = exact.cursor.delete_next();
        FT_REQUIRE(!erased.snapshot().contains(interval{1, 2}));
        FT_REQUIRE(erased.snapshot().contains(interval{1, 3}));
        FT_REQUIRE(tree.contains(interval{1, 2}));

        const auto overlap = ft::find_overlap_cursor(tree, interval{6, 8});
        FT_REQUIRE(overlap.found);
        FT_REQUIRE_EQUAL(overlap.cursor.try_peek_next()->low, 4);
        const auto after = overlap.cursor.seek_next_overlap(interval{9, 9});
        FT_REQUIRE(after.found);
        FT_REQUIRE_EQUAL(after.cursor.try_peek_next()->low, 9);

        using map_type = ft::persistent_interval_map<int, std::string>;
        auto map = map_type::create();
        map = map.add(interval{1, 2}, "a").add(interval{4, 8}, "b");
        const auto map_overlap = ft::find_containing_cursor(map, 6);
        FT_REQUIRE(map_overlap.found);
        FT_REQUIRE_EQUAL(map_overlap.cursor.try_peek_next()->value, std::string{"b"});
        const auto changed = map_overlap.cursor.set_next_value("B");
        FT_REQUIRE_EQUAL(changed.snapshot().at(interval{4, 8}), std::string{"B"});
        FT_REQUIRE((changed.delete_previous().snapshot().keys_to_vector()
            == std::vector<interval>{{4, 8}}));
    });

    tests.add("chunked-bit-set cursors use population-rank gaps", [] {
        const auto set = ft::persistent_chunked_bit_set::create_range(std::vector{1, 64, 130});
        const auto found = ft::get_cursor_at_item(set, 64);
        FT_REQUIRE(found.found);
        FT_REQUIRE_EQUAL(found.cursor.position(), std::uint64_t{1});
        FT_REQUIRE_EQUAL(*found.cursor.try_peek_previous(), 1);
        FT_REQUIRE_EQUAL(*found.cursor.try_peek_next(), 64);

        const auto added = found.cursor.add(65);
        FT_REQUIRE_EQUAL(added.position(), std::uint64_t{3});
        FT_REQUIRE((added.snapshot().to_vector() == std::vector{1, 64, 65, 130}));
        const auto deleted = added.delete_previous();
        FT_REQUIRE((deleted.snapshot().to_vector() == std::vector{1, 64, 130}));
        FT_REQUIRE((set.to_vector() == std::vector{1, 64, 130}));
    });

    tests.add("sorted set and map bound cursors descend instead of scanning", [] {
        const auto values = ascending_probe_values();

        auto set_less = counting_less{};
        const auto set = ft::sorted_set<int, counting_less>::from_range(values, set_less);

        // The bound factories previously walked the collection from the front, so the rank of a
        // late item cost one comparison per preceding item. Each probe below therefore both
        // pins the answer and pins the work done to reach it.
        for (const auto probe : {0, 1, 2047, 4094, 4095}) {
            *set_less.calls = 0;
            const auto lower = ft::get_cursor_lower_bound(set, probe);
            const auto lower_calls = *set_less.calls;
            FT_REQUIRE_EQUAL(lower.position(), static_cast<std::size_t>(probe));
            FT_REQUIRE(lower_calls <= logarithmic_comparison_ceiling);

            *set_less.calls = 0;
            const auto upper = ft::get_cursor_upper_bound(set, probe);
            const auto upper_calls = *set_less.calls;
            FT_REQUIRE_EQUAL(upper.position(), static_cast<std::size_t>(probe) + 1);
            FT_REQUIRE(upper_calls <= logarithmic_comparison_ceiling);
        }

        // A key ordered past every stored item is the worst case for a front-to-back scan.
        *set_less.calls = 0;
        const auto past_end = ft::get_cursor_lower_bound(set, static_cast<int>(probe_size));
        FT_REQUIRE_EQUAL(past_end.position(), probe_size);
        FT_REQUIRE(*set_less.calls <= logarithmic_comparison_ceiling);

        auto entries = std::vector<std::pair<int, int>>{};
        entries.reserve(probe_size);
        for (const auto value : values) {
            entries.emplace_back(value, value * 2);
        }
        auto map_less = counting_pair_less{};
        const auto map = ft::sorted_map<int, int, counting_pair_less>::from_range(entries, map_less);

        for (const auto probe : {0, 2047, 4095}) {
            *map_less.calls = 0;
            const auto lower = ft::get_cursor_lower_bound(map, probe);
            const auto lower_calls = *map_less.calls;
            FT_REQUIRE_EQUAL(lower.position(), static_cast<std::size_t>(probe));
            FT_REQUIRE_EQUAL(lower.try_peek_next()->second, probe * 2);
            FT_REQUIRE(lower_calls <= logarithmic_comparison_ceiling);

            *map_less.calls = 0;
            const auto upper = ft::get_cursor_upper_bound(map, probe);
            const auto upper_calls = *map_less.calls;
            FT_REQUIRE_EQUAL(upper.position(), static_cast<std::size_t>(probe) + 1);
            FT_REQUIRE(upper_calls <= logarithmic_comparison_ceiling);
        }
    });

    tests.add("published bound counts agree with an exhaustive linear reference", [] {
        // The descent replaced a scan, so every published count is checked against the scan it
        // replaced, including the boundaries and the gaps between stored items.
        const auto stored = std::vector{0, 2, 4, 6, 8};
        const auto set = ft::sorted_set<int>::from_range(stored);
        const auto canonical = ft::canonical_sorted_set<int>::from_range(stored);

        auto entries = std::vector<std::pair<int, int>>{};
        for (const auto value : stored) {
            entries.emplace_back(value, value);
        }
        const auto map = ft::sorted_map<int, int>::from_range(entries);

        for (auto probe = -1; probe != 10; ++probe) {
            auto expected_less = std::size_t{0};
            auto expected_at_most = std::size_t{0};
            for (const auto value : stored) {
                expected_less += value < probe ? 1 : 0;
                expected_at_most += value <= probe ? 1 : 0;
            }

            FT_REQUIRE_EQUAL(set.count_less_than(probe), expected_less);
            FT_REQUIRE_EQUAL(set.count_at_most(probe), expected_at_most);
            FT_REQUIRE_EQUAL(map.count_keys_less_than(probe), expected_less);
            FT_REQUIRE_EQUAL(map.count_keys_at_most(probe), expected_at_most);
            FT_REQUIRE_EQUAL(canonical.count_less_than(probe), expected_less);
            FT_REQUIRE_EQUAL(canonical.count_at_most(probe), expected_at_most);

            FT_REQUIRE_EQUAL(ft::get_cursor_lower_bound(set, probe).position(), expected_less);
            FT_REQUIRE_EQUAL(ft::get_cursor_upper_bound(set, probe).position(), expected_at_most);
            FT_REQUIRE_EQUAL(ft::get_cursor_lower_bound(canonical, probe).position(), expected_less);
            FT_REQUIRE_EQUAL(ft::get_cursor_upper_bound(canonical, probe).position(), expected_at_most);
        }
    });

    tests.add("canonical and priority-search peeks select through cached counts", [] {
        const auto values = ascending_probe_values();
        const auto canonical = ft::canonical_sorted_set<int>::from_range(values);

        // Selection is guided by the cached subtree counts alone, so it never consults the
        // ordering policy and a late rank costs no more than an early one.
        for (const auto rank : {std::size_t{0}, std::size_t{1}, probe_size / 2, probe_size - 1}) {
            const auto* selected = canonical.try_select(rank);
            FT_REQUIRE(selected != nullptr);
            FT_REQUIRE_EQUAL(*selected, static_cast<int>(rank));

            const auto cursor = ft::get_cursor(canonical, rank);
            FT_REQUIRE_EQUAL(*cursor.try_peek_next(), static_cast<int>(rank));
        }
        FT_REQUIRE(canonical.try_select(probe_size) == nullptr);

        const auto canonical_end = ft::get_cursor(canonical, probe_size);
        FT_REQUIRE(canonical_end.try_peek_next() == nullptr);
        FT_REQUIRE_EQUAL(*canonical_end.try_peek_previous(), static_cast<int>(probe_size - 1));

        auto queue = ft::priority_search_queue<int, int, int>{};
        for (const auto value : values) {
            queue = queue.set_item(value, static_cast<int>(probe_size) - value, value);
        }

        for (const auto rank : {std::size_t{0}, probe_size / 2, probe_size - 1}) {
            const auto* entry = queue.try_entry_at_rank(rank);
            FT_REQUIRE(entry != nullptr);
            FT_REQUIRE_EQUAL(entry->key(), static_cast<int>(rank));

            const auto cursor = ft::get_cursor(queue, rank);
            FT_REQUIRE_EQUAL(cursor.try_peek_next()->key(), static_cast<int>(rank));
        }
        FT_REQUIRE(queue.try_entry_at_rank(probe_size) == nullptr);

        const auto queue_end = ft::get_cursor(queue, probe_size);
        FT_REQUIRE(queue_end.try_peek_next() == nullptr);
    });
}

} // namespace

void add_ordered_search_cursor_tests(suite& tests)
{
    add_ordered_search_cursor_tests_impl(tests);
}
