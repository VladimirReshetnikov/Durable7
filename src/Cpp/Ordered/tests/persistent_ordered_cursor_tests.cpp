#include <tools/data_structures/ordered/ordered.hpp>

#include "../../FingerTree/tests/test_support/test_runner.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ordered = tools::data_structures::ordered;
using namespace tools::data_structures::finger_tree::tests;

namespace {

struct cursor_ci_hash final {
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

struct cursor_ci_equal final {
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

void add_persistent_ordered_cursor_tests(suite& tests)
{
    tests.add("ordered set cursors preserve explicit gaps policies and snapshots", [] {
        using set_type = ordered::persistent_ordered_set<
            std::string, cursor_ci_hash, cursor_ci_equal>;
        const auto source = set_type::create_range(
            std::vector<std::string>{"Alpha", "beta", "gamma"},
            cursor_ci_hash{},
            cursor_ci_equal{});
        const auto cursor = ordered::get_cursor(source, 1);
        FT_REQUIRE_EQUAL(*cursor.try_peek_previous(), std::string{"Alpha"});
        FT_REQUIRE_EQUAL(*cursor.try_peek_next(), std::string{"beta"});

        const auto inserted = cursor.insert("delta");
        FT_REQUIRE_EQUAL(inserted.position(), std::size_t{2});
        FT_REQUIRE((inserted.snapshot().to_vector()
            == std::vector<std::string>{"Alpha", "delta", "beta", "gamma"}));
        const auto duplicate = inserted.try_insert("BETA");
        FT_REQUIRE(!duplicate.inserted);
        FT_REQUIRE_EQUAL(duplicate.cursor.position(), std::size_t{2});
        FT_REQUIRE(duplicate.cursor.snapshot().shares_index_with(inserted.snapshot()));

        const auto deleted = inserted.delete_previous();
        FT_REQUIRE((deleted.snapshot().to_vector()
            == std::vector<std::string>{"Alpha", "beta", "gamma"}));
        FT_REQUIRE(deleted.snapshot().contains("ALPHA"));
        const auto found = ordered::get_cursor_at_item(source, std::string{"BETA"});
        FT_REQUIRE(found.found);
        FT_REQUIRE_EQUAL(found.cursor.position(), std::size_t{1});
        const auto missing = ordered::get_cursor_at_item(source, std::string{"missing"});
        FT_REQUIRE(!missing.found);
        FT_REQUIRE_EQUAL(missing.cursor.position(), source.size());
        FT_REQUIRE((cursor.snapshot().to_vector()
            == std::vector<std::string>{"Alpha", "beta", "gamma"}));
        FT_REQUIRE_THROWS(std::logic_error, ordered::get_cursor(source).move_previous());
    });

    tests.add("ordered map cursors focus duplicates and edit adjacent entries", [] {
        const auto source = ordered::persistent_ordered_map<std::string, int>{}
            .add("a", 1).add("b", 2).add("c", 3);
        const auto cursor = ordered::get_cursor(source, 1);
        const auto inserted = cursor.insert("x", 9);
        const auto updated = inserted.set_next_value(20);
        FT_REQUIRE_EQUAL(updated.position(), std::size_t{2});
        FT_REQUIRE((updated.snapshot().to_vector()
            == std::vector<ordered::ordered_map_entry<std::string, int>>{
                {"a", 1}, {"x", 9}, {"b", 20}, {"c", 3}}));

        const auto duplicate = updated.try_insert("b", 200);
        FT_REQUIRE(!duplicate.inserted);
        FT_REQUIRE_EQUAL(duplicate.cursor.position(), std::size_t{2});
        FT_REQUIRE(duplicate.cursor.snapshot().shares_index_with(updated.snapshot()));
        const auto deleted = updated.delete_previous().delete_next();
        FT_REQUIRE_EQUAL(deleted.position(), std::size_t{1});
        FT_REQUIRE((deleted.snapshot().keys_to_vector() == std::vector<std::string>{"a", "c"}));
        FT_REQUIRE((cursor.snapshot().keys_to_vector()
            == std::vector<std::string>{"a", "b", "c"}));
    });

    tests.add("ordered multimap cursors use flattened grouped pair ranks", [] {
        const auto source = ordered::persistent_ordered_multimap<std::string, int>{}
            .add("b", 2).add("a", 9).add("b", 1).add("c", 7);
        FT_REQUIRE((source.to_vector() == std::vector<std::pair<std::string, int>>{
            {"b", 2}, {"b", 1}, {"a", 9}, {"c", 7}}));

        const auto found = ordered::get_cursor_at_pair(source, std::string{"b"}, 1);
        FT_REQUIRE(found.found);
        FT_REQUIRE_EQUAL(found.cursor.position(), std::int64_t{1});
        const auto added = found.cursor.add("b", 3);
        FT_REQUIRE_EQUAL(added.position(), std::int64_t{3});
        const auto duplicate = added.try_add("b", 3);
        FT_REQUIRE(!duplicate.inserted);
        FT_REQUIRE_EQUAL(duplicate.cursor.position(), std::int64_t{3});
        FT_REQUIRE(duplicate.cursor.snapshot().shares_index_with(added.snapshot()));

        const auto deleted = added.delete_previous().delete_next();
        FT_REQUIRE_EQUAL(deleted.position(), std::int64_t{2});
        FT_REQUIRE((deleted.snapshot().to_vector()
            == std::vector<std::pair<std::string, int>>{{"b", 2}, {"b", 1}, {"c", 7}}));
        FT_REQUIRE_EQUAL(deleted.try_peek_next()->first, std::string{"c"});
        const auto group = ordered::get_cursor_at_group(source, std::string{"a"});
        FT_REQUIRE(group.found);
        FT_REQUIRE_EQUAL(group.cursor.position(), std::int64_t{2});
        const auto missing = ordered::get_cursor_at_group(source, std::string{"z"});
        FT_REQUIRE(!missing.found);
        FT_REQUIRE_EQUAL(missing.cursor.position(), source.pair_count());
        FT_REQUIRE(!ordered::get_cursor(source).try_peek_previous());
        FT_REQUIRE((found.cursor.snapshot().to_vector() == source.to_vector()));
    });
}
