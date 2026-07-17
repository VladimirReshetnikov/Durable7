#include <tools/data_structures/finger_tree/persistent_interval_map.hpp>

#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

void add_tests(suite& tests)
{
    tests.add("interval map orders full keys and rejects duplicates", [] {
        const auto map = ft::persistent_interval_map<int, std::string>{}
            .add({1, 5}, "wide")
            .add({1, 3}, "narrow")
            .add({0, 9}, "first");
        const auto entries = map.to_vector();
        FT_REQUIRE((entries[0].key == ft::interval<int>{0, 9}));
        FT_REQUIRE((entries[1].key == ft::interval<int>{1, 3}));
        FT_REQUIRE((entries[2].key == ft::interval<int>{1, 5}));
        FT_REQUIRE_EQUAL(map.at({1, 5}), std::string{"wide"});
        FT_REQUIRE_EQUAL(map.keys_to_vector().size(), std::size_t{3});
        FT_REQUIRE_EQUAL(map.values_to_vector()[1], std::string{"narrow"});
        FT_REQUIRE_THROWS(std::invalid_argument, map.add({1, 3}, "duplicate"));
        FT_REQUIRE_THROWS(std::out_of_range, map.at({8, 9}));
    });

    tests.add("interval map retains key and equal value identity", [] {
        const auto map = ft::persistent_interval_map<int, std::string>{}.add({1, 3}, "value");
        const auto same = map.set_item({1, 3}, "value");
        FT_REQUIRE(same.try_get({1, 3}) == map.try_get({1, 3}));
        const auto changed = map.set_item({1, 3}, "changed");
        FT_REQUIRE_EQUAL(*changed.try_get({1, 3}), std::string{"changed"});
        FT_REQUIRE((changed.try_get_entry({1, 3})->key == ft::interval<int>{1, 3}));
    });

    tests.add("interval map validates every public interval", [] {
        const auto map = ft::persistent_interval_map<int, std::string>{};
        FT_REQUIRE_THROWS(std::invalid_argument, map.add({5, 1}, "invalid"));
        FT_REQUIRE_THROWS(std::invalid_argument, map.try_find_overlap({5, 1}));
    });

    tests.add("interval map prunes overlap and stabbing queries", [] {
        const auto map = ft::persistent_interval_map<int, std::string>{}
            .add({0, 2}, "a")
            .add({4, 9}, "b")
            .add({5, 6}, "c")
            .add({11, 12}, "d");
        const auto overlaps = map.find_overlaps({6, 10});
        FT_REQUIRE_EQUAL(overlaps.size(), std::size_t{2});
        FT_REQUIRE_EQUAL(overlaps[0].value, std::string{"b"});
        FT_REQUIRE_EQUAL(overlaps[1].value, std::string{"c"});
        FT_REQUIRE_EQUAL(map.try_find_containing(5)->value, std::string{"b"});
        FT_REQUIRE_EQUAL(map.count_overlaps({6, 10}), std::size_t{2});
    });

    tests.add("interval map removal preserves retained snapshots", [] {
        const auto source = ft::persistent_interval_map<int, std::string>{}
            .add({1, 2}, "a")
            .add({3, 4}, "b");
        const auto branch = source.remove({1, 2});
        FT_REQUIRE(!branch.contains_key({1, 2}));
        FT_REQUIRE(source.contains_key({1, 2}));
        FT_REQUIRE(branch.remove({1, 2}).to_vector() == branch.to_vector());
    });

    tests.add("interval map value policy and annotations validate", [] {
        struct case_insensitive final {
            bool operator()(const std::string& left, const std::string& right) const
            {
                return left.size() == right.size()
                    && std::equal(left.begin(), left.end(), right.begin(), [](char l, char r) {
                        return static_cast<char>(std::tolower(l))
                            == static_cast<char>(std::tolower(r));
                    });
            }
        };
        const auto map = ft::persistent_interval_map<
            int, std::string, ft::default_comparison<int>, case_insensitive>::create()
            .add({1, 2}, "Value");
        FT_REQUIRE(map.set_item({1, 2}, "value").to_vector() == map.to_vector());
        FT_REQUIRE(map.debug_validate());
        FT_REQUIRE(map.clear().debug_validate());
    });
}

} // namespace

void add_persistent_interval_map_tests(suite& tests)
{
    add_tests(tests);
}
