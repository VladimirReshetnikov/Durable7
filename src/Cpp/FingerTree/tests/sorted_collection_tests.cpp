#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <source_location>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

template <class T>
void require_vector_equal(
    const std::vector<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    if (actual == expected) {
        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": vector mismatch";
    throw test_failure(message.str());
}

[[nodiscard]] std::vector<int> sorted_vector(std::vector<int> values)
{
    std::ranges::sort(values);
    return values;
}

[[nodiscard]] std::vector<int> set_vector(const std::set<int>& values)
{
    return std::vector<int>{values.begin(), values.end()};
}

template <class T>
void require_optional_equal(const std::optional<T>& actual, const std::optional<T>& expected)
{
    FT_REQUIRE_EQUAL(actual.has_value(), expected.has_value());
    if (expected.has_value()) {
        FT_REQUIRE_EQUAL(*actual, *expected);
    }
}

struct tagged_value final {
    int key;
    char tag;

    [[nodiscard]] bool operator==(const tagged_value&) const = default;
};

struct tagged_key_less final {
    [[nodiscard]] bool operator()(const tagged_value& left, const tagged_value& right) const
    {
        return left.key < right.key;
    }
};

struct directional_less final {
    bool descending = false;

    [[nodiscard]] bool operator()(const int left, const int right) const noexcept
    {
        return descending ? left > right : left < right;
    }

    [[nodiscard]] bool operator==(const directional_less&) const = default;
};

template <class Map>
[[nodiscard]] std::vector<typename Map::key_type> map_keys(const Map& map)
{
    auto keys = std::vector<typename Map::key_type>{};
    for (const auto& [key, value] : map) {
        (void)value;
        keys.push_back(key);
    }

    return keys;
}

void add_sorted_collection_tests_impl(suite& tests)
{
    static_assert(std::is_same_v<
        decltype(std::declval<const ft::sorted_bag<int>&>().at(0)),
        const int&>);
    static_assert(std::is_same_v<
        decltype(std::declval<const ft::sorted_set<int>&>()[0]),
        const int&>);
    static_assert(std::is_same_v<
        decltype(std::declval<const ft::sorted_map<int, int>&>().entry_at(0)),
        const std::pair<int, int>&>);
    static_assert(std::forward_iterator<ft::sorted_bag<int>::const_iterator>);
    static_assert(std::forward_iterator<ft::sorted_set<int>::const_iterator>);
    static_assert(std::forward_iterator<ft::sorted_map<int, int>::const_iterator>);
    static_assert(std::ranges::forward_range<const ft::sorted_bag<int>>);
    static_assert(std::ranges::forward_range<const ft::sorted_set<int>>);
    static_assert(std::ranges::forward_range<const ft::sorted_map<int, int>>);

    tests.add("sorted bag construction preserves duplicates and sorted order", [] {
        const auto bag = ft::sorted_bag<int>::from_range(std::vector{5, 1, 3, 3, 9, 1});

        FT_REQUIRE_EQUAL(bag.size(), static_cast<std::size_t>(6));
        require_vector_equal(bag.to_vector(), std::vector{1, 1, 3, 3, 5, 9});
        FT_REQUIRE_EQUAL(bag.min(), 1);
        FT_REQUIRE_EQUAL(bag.max(), 9);
    });

    tests.add("sorted collection facades stream ordered values through forward ranges", [] {
        const auto bag = ft::sorted_bag<int>::from_range(std::vector{5, 1, 3, 3, 9, 1});
        const auto set = ft::sorted_set<int>::from_range(std::vector{5, 1, 3, 3, 9, 1});
        const auto map = ft::sorted_map<int, int>::from_range(
            std::vector<std::pair<int, int>>{{5, 50}, {1, 10}, {3, 30}});

        auto bag_values = std::vector<int>{};
        bag.copy_to(std::back_inserter(bag_values));
        FT_REQUIRE(bag_values == bag.to_vector());
        FT_REQUIRE((std::vector<int>{bag.begin(), bag.end()} == bag_values));

        auto set_values = std::vector<int>{};
        set.copy_to(std::back_inserter(set_values));
        FT_REQUIRE(set_values == set.to_vector());
        FT_REQUIRE((std::vector<int>{set.begin(), set.end()} == set_values));

        auto map_values = std::vector<std::pair<int, int>>{};
        map.copy_to(std::back_inserter(map_values));
        FT_REQUIRE(map_values == map.to_vector());
        FT_REQUIRE((std::vector<std::pair<int, int>>{map.begin(), map.end()} == map_values));
    });

    tests.add("sorted bag ranks counts indexing and removals match sorted vector model", [] {
        const auto source = std::vector{4, 1, 4, 7, 1, 9, 4, 2};
        auto model = sorted_vector(source);
        auto bag = ft::sorted_bag<int>::from_range(source);

        for (auto index = std::size_t{0}; index < model.size(); ++index) {
            FT_REQUIRE_EQUAL(bag[index], model[index]);
        }

        for (const auto key : {0, 1, 2, 4, 5, 9, 10}) {
            const auto less = static_cast<std::size_t>(std::ranges::count_if(model, [key](const int value) {
                return value < key;
            }));
            const auto at_most = static_cast<std::size_t>(std::ranges::count_if(model, [key](const int value) {
                return value <= key;
            }));
            const auto equal = static_cast<std::size_t>(std::ranges::count(model, key));

            FT_REQUIRE_EQUAL(bag.count_less_than(key), less);
            FT_REQUIRE_EQUAL(bag.count_at_most(key), at_most);
            FT_REQUIRE_EQUAL(bag.count_of(key), equal);
            FT_REQUIRE_EQUAL(bag.contains(key), equal != 0);
        }

        FT_REQUIRE_THROWS(std::out_of_range, bag.at(model.size()));

        const auto one_removed = bag.try_remove(4);
        FT_REQUIRE(one_removed.has_value());
        require_vector_equal(one_removed->to_vector(), std::vector{1, 1, 2, 4, 4, 7, 9});
        require_vector_equal(bag.remove_all(4).to_vector(), std::vector{1, 1, 2, 7, 9});
        require_vector_equal(bag.get_range(2, 7).to_vector(), std::vector{2, 4, 4, 4, 7});
        FT_REQUIRE(!bag.try_remove(99).has_value());
    });

    tests.add("sorted bag supports descending order and stable equal-key construction", [] {
        const auto descending = ft::sorted_bag<int, std::greater<>>::from_range(
            std::vector{5, 1, 9, 3},
            std::greater<>{});
        require_vector_equal(descending.to_vector(), std::vector{9, 5, 3, 1});
        FT_REQUIRE_EQUAL(descending.min(), 9);
        FT_REQUIRE_EQUAL(descending.max(), 1);
        FT_REQUIRE_EQUAL(descending.add(4).count_less_than(4), static_cast<std::size_t>(2));

        const auto tagged = std::vector<tagged_value>{
            {7, 'a'},
            {7, 'b'},
            {7, 'c'},
            {7, 'd'}};
        const auto bag = ft::sorted_bag<tagged_value, tagged_key_less>::from_range(tagged, tagged_key_less{});
        require_vector_equal(bag.to_vector(), tagged);
        FT_REQUIRE(&bag.at(0) == &bag.min());
        FT_REQUIRE(&bag[0] == &bag.min());
    });

    tests.add("sorted bag randomized history matches multiset vector model", [] {
        auto rng = deterministic_rng{0x51b001};
        auto model = std::vector<int>{};
        auto bag = ft::sorted_bag<int>{};

        for (auto step = 0; step != 320; ++step) {
            switch (rng.next_index(4)) {
            case 0:
            case 1: {
                const auto value = rng.next_int(0, 49);
                model.push_back(value);
                bag = bag.add(value);
                break;
            }
            case 2:
                if (!model.empty()) {
                    const auto selected = rng.next_index(model.size());
                    const auto value = model[selected];
                    model.erase(std::ranges::find(model, value));
                    auto removed = bag.try_remove(value);
                    FT_REQUIRE(removed.has_value());
                    bag = *removed;
                }
                break;
            default: {
                const auto value = rng.next_int(0, 49);
                std::erase(model, value);
                bag = bag.remove_all(value);
                break;
            }
            }

            FT_REQUIRE_EQUAL(bag.size(), model.size());
            require_vector_equal(bag.to_vector(), sorted_vector(model));
        }
    });

    tests.add("sorted set uniqueness rank navigation and range match model", [] {
        const auto set = ft::sorted_set<int>::from_range(std::vector{5, 1, 3, 3, 9, 1, 5});
        require_vector_equal(set.to_vector(), std::vector{1, 3, 5, 9});
        FT_REQUIRE_EQUAL(set.size(), static_cast<std::size_t>(4));
        require_vector_equal(set.add(3).to_vector(), set.to_vector());
        require_vector_equal(set.add(4).to_vector(), std::vector{1, 3, 4, 5, 9});

        const auto values = std::vector{2, 4, 6, 8, 10};
        const auto navigable = ft::sorted_set<int>::from_range(values);
        for (auto index = std::size_t{0}; index < values.size(); ++index) {
            FT_REQUIRE_EQUAL(navigable[index], values[index]);
            require_optional_equal(navigable.index_of(values[index]), std::optional{index});
        }
        FT_REQUIRE(&navigable[0] == &navigable.min());
        FT_REQUIRE(&navigable.at(values.size() - 1) == &navigable.max());
        require_optional_equal(navigable.index_of(5), std::optional<std::size_t>{});
        FT_REQUIRE_THROWS(std::out_of_range, navigable.at(values.size()));

        for (const auto key : {1, 2, 5, 6, 11}) {
            auto floor = std::optional<int>{};
            auto ceiling = std::optional<int>{};
            auto lower = std::optional<int>{};
            auto higher = std::optional<int>{};
            for (const auto value : values) {
                if (value <= key) {
                    floor = value;
                }
                if (!ceiling.has_value() && value >= key) {
                    ceiling = value;
                }
                if (value < key) {
                    lower = value;
                }
                if (!higher.has_value() && value > key) {
                    higher = value;
                }
            }

            require_optional_equal(navigable.try_floor(key), floor);
            require_optional_equal(navigable.try_ceiling(key), ceiling);
            require_optional_equal(navigable.try_lower(key), lower);
            require_optional_equal(navigable.try_higher(key), higher);
        }

        require_vector_equal(navigable.get_range(5, 9).to_vector(), std::vector{6, 8});
        FT_REQUIRE(navigable.get_range(11, 5).empty());
    });

    tests.add("sorted set algebra and relations match std set", [] {
        auto rng = deterministic_rng{0x5e7a19};
        auto left_values = std::vector<int>{};
        auto right_values = std::vector<int>{};
        for (auto index = 0; index != 80; ++index) {
            left_values.push_back(rng.next_int(0, 99));
            right_values.push_back(rng.next_int(0, 99));
        }

        const auto left = ft::sorted_set<int>::from_range(left_values);
        const auto right = ft::sorted_set<int>::from_range(right_values);
        const auto left_model = std::set<int>{left_values.begin(), left_values.end()};
        const auto right_model = std::set<int>{right_values.begin(), right_values.end()};

        auto expected = std::vector<int>{};
        std::ranges::set_union(left_model, right_model, std::back_inserter(expected));
        require_vector_equal(left.union_with(right).to_vector(), expected);

        expected.clear();
        std::ranges::set_intersection(left_model, right_model, std::back_inserter(expected));
        require_vector_equal(left.intersect(right).to_vector(), expected);

        expected.clear();
        std::ranges::set_difference(left_model, right_model, std::back_inserter(expected));
        require_vector_equal(left.except(right).to_vector(), expected);

        expected.clear();
        std::ranges::set_symmetric_difference(left_model, right_model, std::back_inserter(expected));
        require_vector_equal(left.symmetric_except(right).to_vector(), expected);

        const auto intersection = left.intersect(right);
        FT_REQUIRE(intersection.is_subset_of(left));
        FT_REQUIRE(intersection.is_subset_of(right));
        FT_REQUIRE(left.is_superset_of(intersection));
        FT_REQUIRE_EQUAL(left.overlaps(right), !intersection.empty());
        FT_REQUIRE(left.set_equals(ft::sorted_set<int>::from_range(left_values)));
    });

    tests.add("sorted set algebra preserves fast persistent paths", [] {
        auto values = std::vector<int>{};
        for (auto value = 0; value != 2'000; ++value) {
            values.push_back(value);
        }
        const auto left = ft::sorted_set<int>::from_range(values);

        auto right_values = std::vector<int>{};
        for (auto value = 2'000; value != 4'000; ++value) {
            right_values.push_back(value);
        }
        const auto right = ft::sorted_set<int>::from_range(right_values);
        const auto empty = ft::sorted_set<int>{};
        (void)empty;
        const auto left_size = left.size();
        (void)left.min();
        (void)left.max();

        {
            allocation_counting_scope allocations;
            const auto same_union = left.union_with(left);
            const auto same_intersection = left.intersect(left);
            const auto same_difference = left.except(left);
            FT_REQUIRE_EQUAL(same_union.size(), left_size);
            FT_REQUIRE_EQUAL(same_intersection.size(), left_size);
            FT_REQUIRE(same_difference.empty());
            FT_REQUIRE_EQUAL(allocations.allocations(), static_cast<std::size_t>(0));
        }

        {
            auto combined = ft::sorted_set<int>{};
            auto construction_allocations = std::size_t{0};
            {
                allocation_counting_scope allocations;
                combined = left.union_with(right);
                construction_allocations = allocations.allocations();
            }

            FT_REQUIRE_EQUAL(combined.size(), static_cast<std::size_t>(4'000));
            FT_REQUIRE_EQUAL(combined.min(), 0);
            FT_REQUIRE_EQUAL(combined.max(), 3'999);
            FT_REQUIRE(construction_allocations < static_cast<std::size_t>(512));
        }
    });

    tests.add("sorted set algebra normalizes incompatible runtime comparator state", [] {
        using set_type = ft::sorted_set<int, directional_less>;
        const auto ascending = set_type::from_range(std::vector{1, 3, 5}, directional_less{false});
        const auto descending = set_type::from_range(std::vector{6, 5, 4, 2}, directional_less{true});

        require_vector_equal(ascending.union_with(descending).to_vector(), std::vector{1, 2, 3, 4, 5, 6});
        require_vector_equal(ascending.intersect(descending).to_vector(), std::vector{5});
        require_vector_equal(ascending.except(descending).to_vector(), std::vector{1, 3});
        require_vector_equal(ascending.symmetric_except(descending).to_vector(), std::vector{1, 2, 3, 4, 6});
        FT_REQUIRE(ascending.overlaps(descending));
        FT_REQUIRE(!ascending.set_equals(descending));

        const auto same_members_descending =
            set_type::from_range(std::vector{5, 3, 1}, directional_less{true});
        FT_REQUIRE(ascending.set_equals(same_members_descending));
        FT_REQUIRE(ascending.is_subset_of(same_members_descending));
        FT_REQUIRE(ascending.is_superset_of(same_members_descending));

        const auto empty_ascending = set_type{directional_less{false}};
        require_vector_equal(empty_ascending.union_with(descending).to_vector(), std::vector{2, 4, 5, 6});
    });

    tests.add("sorted set supports descending order and randomized updates", [] {
        const auto descending = ft::sorted_set<int, std::greater<>>::from_range(
            std::vector{5, 1, 9, 3, 5},
            std::greater<>{});
        require_vector_equal(descending.to_vector(), std::vector{9, 5, 3, 1});
        FT_REQUIRE_EQUAL(descending.min(), 9);
        require_optional_equal(descending.try_ceiling(6), std::optional{5});

        auto rng = deterministic_rng{0x5e7};
        auto model = std::set<int>{};
        auto set = ft::sorted_set<int>{};
        for (auto step = 0; step != 360; ++step) {
            const auto value = rng.next_int(0, 49);
            if (rng.next_index(2) == 0) {
                model.insert(value);
                set = set.add(value);
            } else {
                model.erase(value);
                set = set.remove(value);
            }

            require_vector_equal(set.to_vector(), set_vector(model));
            FT_REQUIRE_EQUAL(set.contains(value), model.contains(value));
        }
    });

    tests.add("sorted map construction set insert remove and lookup match std map", [] {
        using map_type = ft::sorted_map<int, std::string>;
        const auto map = map_type::from_range(std::vector<std::pair<int, std::string>>{
            {3, "c"},
            {1, "a"},
            {2, "b"},
            {1, "A"}});

        FT_REQUIRE_EQUAL(map.size(), static_cast<std::size_t>(3));
        require_vector_equal(map.keys_to_vector(), std::vector{1, 2, 3});
        FT_REQUIRE_EQUAL(map.at(1), std::string{"A"});
        require_vector_equal(map.values_to_vector(), std::vector<std::string>{"A", "b", "c"});

        auto model = std::map<int, std::string>{};
        auto current = map_type{};
        for (const auto key : {5, 1, 3, 5, 2}) {
            current = current.set_item(key, "v" + std::to_string(key));
            model[key] = "v" + std::to_string(key);
        }

        require_vector_equal(current.to_vector(), std::vector<std::pair<int, std::string>>{model.begin(), model.end()});
        FT_REQUIRE(current.contains_key(3));
        require_optional_equal(current.try_get(3), std::optional<std::string>{"v3"});
        FT_REQUIRE_THROWS(std::invalid_argument, current.insert(3, "dup"));
        FT_REQUIRE(!current.try_insert(3, "dup").has_value());

        const auto inserted = current.try_insert(9, "v9");
        FT_REQUIRE(inserted.has_value());
        FT_REQUIRE_EQUAL(inserted->at(9), std::string{"v9"});

        const auto removed = current.try_remove(3);
        FT_REQUIRE(removed.has_value());
        FT_REQUIRE(!removed->contains_key(3));
        require_vector_equal(current.remove(99).to_vector(), current.to_vector());
        FT_REQUIRE_THROWS(std::out_of_range, current.at(42));
    });

    tests.add("sorted map replacement retains the new comparator-equivalent key", [] {
        using map_type = ft::sorted_map<tagged_value, std::string, tagged_key_less>;
        const auto original = map_type{tagged_key_less{}}.set_item(tagged_value{7, 'a'}, "old");
        const auto replaced = original.set_item(tagged_value{7, 'z'}, "new");

        FT_REQUIRE_EQUAL(replaced.size(), static_cast<std::size_t>(1));
        FT_REQUIRE_EQUAL(replaced.entry_at(0).first.tag, 'z');
        FT_REQUIRE_EQUAL(replaced.entry_at(0).second, std::string{"new"});
        FT_REQUIRE(&replaced.entry_at(0) == &replaced.min_entry());
        FT_REQUIRE_EQUAL(original.entry_at(0).first.tag, 'a');
    });

    tests.add("sorted map order statistics navigation range and custom order work", [] {
        using map_type = ft::sorted_map<int, std::string>;
        const auto keys = std::vector{10, 20, 30, 40, 50};
        const auto entries = [&] {
            auto result = std::vector<std::pair<int, std::string>>{};
            for (const auto key : keys) {
                result.emplace_back(key, "v" + std::to_string(key));
            }
            return result;
        }();
        const auto map = map_type::from_range(entries);

        for (auto index = std::size_t{0}; index < keys.size(); ++index) {
            FT_REQUIRE_EQUAL(map.entry_at(index).first, keys[index]);
            require_optional_equal(map.index_of_key(keys[index]), std::optional{index});
        }
        require_optional_equal(map.index_of_key(25), std::optional<std::size_t>{});
        FT_REQUIRE_THROWS(std::out_of_range, map.entry_at(keys.size()));
        FT_REQUIRE_EQUAL(map.min_entry().first, 10);
        FT_REQUIRE_EQUAL(map.max_entry().first, 50);

        for (const auto key : {5, 10, 25, 30, 55}) {
            auto floor = std::optional<int>{};
            auto ceiling = std::optional<int>{};
            auto lower = std::optional<int>{};
            auto higher = std::optional<int>{};
            for (const auto value : keys) {
                if (value <= key) {
                    floor = value;
                }
                if (!ceiling.has_value() && value >= key) {
                    ceiling = value;
                }
                if (value < key) {
                    lower = value;
                }
                if (!higher.has_value() && value > key) {
                    higher = value;
                }
            }

            FT_REQUIRE_EQUAL(map.try_floor_entry(key).has_value(), floor.has_value());
            if (floor.has_value()) {
                FT_REQUIRE_EQUAL(map.try_floor_entry(key)->first, *floor);
            }
            FT_REQUIRE_EQUAL(map.try_ceiling_entry(key).has_value(), ceiling.has_value());
            if (ceiling.has_value()) {
                FT_REQUIRE_EQUAL(map.try_ceiling_entry(key)->first, *ceiling);
            }
            FT_REQUIRE_EQUAL(map.try_lower_entry(key).has_value(), lower.has_value());
            if (lower.has_value()) {
                FT_REQUIRE_EQUAL(map.try_lower_entry(key)->first, *lower);
            }
            FT_REQUIRE_EQUAL(map.try_higher_entry(key).has_value(), higher.has_value());
            if (higher.has_value()) {
                FT_REQUIRE_EQUAL(map.try_higher_entry(key)->first, *higher);
            }
        }

        require_vector_equal(map.get_range(15, 45).keys_to_vector(), std::vector{20, 30, 40});
        FT_REQUIRE(map.get_range(45, 15).empty());

        const auto descending = ft::sorted_map<int, std::string, std::greater<>>::from_range(
            std::vector<std::pair<int, std::string>>{{1, "v1"}, {5, "v5"}, {3, "v3"}},
            std::greater<>{});
        require_vector_equal(descending.keys_to_vector(), std::vector{5, 3, 1});
        FT_REQUIRE_EQUAL(descending.min_entry().first, 5);
        FT_REQUIRE_EQUAL(descending.try_ceiling_entry(4)->first, 3);
    });

    tests.add("sorted map randomized history matches std map", [] {
        using map_type = ft::sorted_map<int, std::string>;
        auto rng = deterministic_rng{0x0badcafe};
        auto model = std::map<int, std::string>{};
        auto map = map_type{};

        for (auto step = 0; step != 360; ++step) {
            const auto key = rng.next_int(0, 39);
            if (rng.next_index(3) != 0) {
                auto value = "s" + std::to_string(step);
                model[key] = value;
                map = map.set_item(key, std::move(value));
            } else {
                model.erase(key);
                map = map.remove(key);
            }

            require_vector_equal(map.to_vector(), std::vector<std::pair<int, std::string>>{model.begin(), model.end()});
        }
    });
}

} // namespace

void add_sorted_collection_tests(suite& tests)
{
    add_sorted_collection_tests_impl(tests);
}
