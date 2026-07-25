#include <durable7/ordered/ordered.hpp>

#include "../../FingerTree/tests/test_support/test_runner.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ordered = durable7::ordered;
using namespace durable7::finger_tree::tests;

namespace {

struct ci_hash final {
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept
    {
        auto result = std::size_t{0};
        for (const auto character : value) {
            result = result * 131u + static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        return result;
    }
};

struct ci_equal final {
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const noexcept
    {
        return left.size() == right.size()
            && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a))
                    == std::tolower(static_cast<unsigned char>(b));
            });
    }
};

} // namespace

void add_persistent_ordered_multimap_tests(suite& tests)
{
    tests.add("ordered multimap preserves grouped insertion order", [] {
        const auto map = ordered::persistent_ordered_multimap<std::string, int>{}
            .add("b", 2).add("a", 9).add("b", 1).add("c", 7).add("a", 8);
        FT_REQUIRE((map.keys_to_vector() == std::vector<std::string>{"b", "a", "c"}));
        FT_REQUIRE((map.to_vector() == std::vector<std::pair<std::string, int>>{
            {"b", 2}, {"b", 1}, {"a", 9}, {"a", 8}, {"c", 7}}));
        FT_REQUIRE_EQUAL(map.key_count(), std::size_t{3});
        FT_REQUIRE_EQUAL(map.pair_count(), std::int64_t{5});
        FT_REQUIRE(map.debug_validate());
    });

    tests.add("ordered multimap retains representatives and duplicate identity", [] {
        using map_type = ordered::persistent_ordered_multimap<
            std::string, std::string, ci_hash, ci_equal, ci_hash, ci_equal>;
        const auto map = map_type::create(ci_hash{}, ci_equal{}, ci_hash{}, ci_equal{})
            .add("Key", "Value");
        const auto duplicate = map.add("KEY", "VALUE");
        FT_REQUIRE(duplicate.shares_index_with(map));
        FT_REQUIRE_EQUAL(*map.try_get_key("key"), std::string{"Key"});
        FT_REQUIRE_EQUAL(*map.try_get_value("KEY", "value"), std::string{"Value"});
    });

    tests.add("ordered multimap contracts and reappends groups", [] {
        const auto source = ordered::persistent_ordered_multimap<std::string, int>{}
            .add("a", 1).add("b", 2).add("a", 3);
        const auto without_a = source.remove("a", 1).remove("a", 3);
        FT_REQUIRE((without_a.keys_to_vector() == std::vector<std::string>{"b"}));
        const auto readded = without_a.add("a", 4);
        FT_REQUIRE((readded.keys_to_vector() == std::vector<std::string>{"b", "a"}));
        FT_REQUIRE(source.contains("a", 1));
        FT_REQUIRE(readded.debug_validate());
    });

    tests.add("ordered multimap whole-group removals and branches are persistent", [] {
        const auto root = ordered::persistent_ordered_multimap<int, std::string>{}
            .add(1, "a").add(2, "b");
        const auto left = root.add(1, "c");
        const auto right = root.remove_key(1).add(3, "d");
        FT_REQUIRE_EQUAL(root.pair_count(), std::int64_t{2});
        FT_REQUIRE_EQUAL(left.pair_count(), std::int64_t{3});
        FT_REQUIRE(!root.contains(1, "c"));
        FT_REQUIRE(right.contains(3, "d"));
        FT_REQUIRE(root.remove(9, "x").shares_index_with(root));
        FT_REQUIRE(root.debug_validate() && left.debug_validate() && right.debug_validate());
    });
}
