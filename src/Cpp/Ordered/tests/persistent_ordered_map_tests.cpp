/// Tests for the insertion-ordered persistent map, including that a value replacement keeps the
/// entry's position.

#include <durable7/ordered/ordered.hpp>

#include "../../FingerTree/tests/test_support/test_runner.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ordered = durable7::ordered;
using namespace durable7::finger_tree::tests;

namespace {

struct case_insensitive_hash final {
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept
    {
        auto hash = std::size_t{1469598103934665603ULL};
        for (const auto character : value) {
            hash ^= static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(character)));
            hash *= std::size_t{1099511628211ULL};
        }
        return hash;
    }
};

struct case_insensitive_equal final {
    [[nodiscard]] bool operator()(
        const std::string& left,
        const std::string& right) const noexcept
    {
        return left.size() == right.size()
            && std::equal(left.begin(), left.end(), right.begin(), [](char left, char right) {
                return std::tolower(static_cast<unsigned char>(left))
                    == std::tolower(static_cast<unsigned char>(right));
            });
    }
};

template <class Map>
void require_valid(const Map& map)
{
    FT_REQUIRE(map.debug_validate());
    FT_REQUIRE_EQUAL(map.size(), map.to_vector().size());
}

void add_tests(suite& tests)
{
    tests.add("ordered map construction retains first keys positions and last values", [] {
        using map_type = ordered::persistent_ordered_map<
            std::string,
            int,
            case_insensitive_hash,
            case_insensitive_equal>;
        const auto source = std::vector<std::pair<std::string, int>>{
            {"Alpha", 1}, {"beta", 2}, {"ALPHA", 3}, {"Gamma", 4}};
        const auto map = map_type::create_range(
            source, case_insensitive_hash{}, case_insensitive_equal{});

        FT_REQUIRE_EQUAL(map.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(map.entry_at(0).key, std::string{"Alpha"});
        FT_REQUIRE_EQUAL(map.at("alpha"), 3);
        FT_REQUIRE_EQUAL(map.index_of_key("BETA"), std::ptrdiff_t{1});
        FT_REQUIRE_EQUAL(*map.try_get_key("ALPHA"), std::string{"Alpha"});
        require_valid(map);
    });

    tests.add("ordered map strict insertion and replacement preserve policy", [] {
        const auto map = ordered::persistent_ordered_map<int, std::string>{}
            .add(1, "one")
            .add(2, "two");
        FT_REQUIRE_THROWS(std::invalid_argument, map.add(1, "other"));
        const auto [same_add, added] = map.try_add(2, "other");
        FT_REQUIRE(!added);
        FT_REQUIRE(same_add.shares_index_with(map));

        const auto same_value = map.set_item(1, "one");
        FT_REQUIRE(same_value.shares_index_with(map));
        const auto replaced = map.set_item(1, "ONE");
        FT_REQUIRE_EQUAL(replaced.entry_at(0).key, 1);
        FT_REQUIRE_EQUAL(replaced.entry_at(0).value, std::string{"ONE"});
        FT_REQUIRE(replaced.shares_index_with(map));
        require_valid(replaced);
    });

    tests.add("ordered map explicit movements retain immutable branches", [] {
        const auto source = ordered::persistent_ordered_map<int, char>{}
            .add(1, 'a').add(2, 'b').add(3, 'c');
        const auto moved = source.move_to_first(3).move_to(1, 2).move_to_last(3);
        FT_REQUIRE((moved.keys_to_vector() == std::vector<int>{2, 1, 3}));
        FT_REQUIRE((source.keys_to_vector() == std::vector<int>{1, 2, 3}));
        FT_REQUIRE_THROWS(std::out_of_range, source.move_to_first(9));
        require_valid(moved);
    });

    tests.add("ordered map positional removal range reverse and sort agree", [] {
        const auto source = ordered::persistent_ordered_map<int, int>{}
            .add(1, 40).add(2, 10).add(3, 30).add(4, 20);
        const auto range = source.get_range(1, 2);
        FT_REQUIRE((range.keys_to_vector() == std::vector<int>{2, 3}));
        const auto reversed = source.reverse();
        FT_REQUIRE((reversed.keys_to_vector() == std::vector<int>{4, 3, 2, 1}));
        const auto sorted = source.sort([](const auto& left, const auto& right) {
            return left.value < right.value;
        });
        FT_REQUIRE((sorted.keys_to_vector() == std::vector<int>{2, 4, 3, 1}));
        const auto removed = source.remove_at(1).remove_first().remove_last();
        FT_REQUIRE((removed.keys_to_vector() == std::vector<int>{3}));
        require_valid(range);
        require_valid(reversed);
        require_valid(sorted);
        require_valid(removed);
    });

    tests.add("ordered map custom value equality suppresses replacement", [] {
        using map_type = ordered::persistent_ordered_map<
            int,
            std::string,
            std::hash<int>,
            std::equal_to<int>,
            case_insensitive_equal>;
        const auto map = map_type::create({}, {}, case_insensitive_equal{}).add(1, "Value");
        const auto same = map.set_item(1, "value");
        FT_REQUIRE(same.shares_index_with(map));
        FT_REQUIRE_EQUAL(same.at(1), std::string{"Value"});
        require_valid(same.clear());
    });

    tests.add("ordered map repeated gap insertions relabel without losing entries", [] {
        auto map = ordered::persistent_ordered_map<int, int>{}.add(0, 0).add(1, 10);
        for (auto value = 2; value != 96; ++value) {
            map = map.insert(1, value, value * 10);
        }
        FT_REQUIRE_EQUAL(map.size(), std::size_t{96});
        FT_REQUIRE_EQUAL(map.entry_at(0).key, 0);
        FT_REQUIRE_EQUAL(map.entry_at(1).key, 95);
        FT_REQUIRE_EQUAL(map.back().key, 1);
        for (auto value = 0; value != 96; ++value) {
            FT_REQUIRE_EQUAL(map.at(value), value * 10);
        }
        require_valid(map);
    });
}

} // namespace

void add_persistent_ordered_map_tests(suite& tests)
{
    add_tests(tests);
}
