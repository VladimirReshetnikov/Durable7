#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

struct additive_range_algebra final {
    using measure_type = std::int64_t;
    using tag_type = std::int64_t;

    [[nodiscard]] static constexpr measure_type empty() noexcept { return 0; }
    [[nodiscard]] static constexpr measure_type measure(const std::int64_t value) noexcept { return value; }
    [[nodiscard]] static constexpr measure_type combine(
        const measure_type left,
        const measure_type right) noexcept
    {
        return left + right;
    }
    [[nodiscard]] static constexpr tag_type identity_tag() noexcept { return 0; }
    [[nodiscard]] static constexpr bool is_identity(const tag_type& tag) noexcept { return tag == 0; }
    [[nodiscard]] static constexpr tag_type compose(
        const tag_type& newer,
        const tag_type& older) noexcept
    {
        return newer + older;
    }
    [[nodiscard]] static constexpr std::int64_t apply_element(
        const tag_type& tag,
        const std::int64_t value) noexcept
    {
        return value + tag;
    }
    [[nodiscard]] static constexpr measure_type apply_measure(
        const tag_type& tag,
        const measure_type& measure_value,
        const std::size_t count) noexcept
    {
        return measure_value + tag * static_cast<std::int64_t>(count);
    }
};

void add_sequence_cursor_tests_impl(suite& tests)
{
    tests.add("deque cursor retains versions and distinguishes a stored empty optional", [] {
        using value_type = std::optional<int>;
        const auto basis = ft::persistent_deque<value_type>{1, std::nullopt, 3};
        const auto cursor = basis.get_cursor(2);

        FT_REQUIRE(cursor.try_peek_previous() != nullptr);
        FT_REQUIRE(!cursor.try_peek_previous()->has_value());
        FT_REQUIRE_EQUAL(cursor.try_peek_next()->value(), 3);

        const auto edited = cursor
            .insert_range(std::vector<value_type>{7, 8})
            .delete_previous()
            .replace_next(9);
        FT_REQUIRE_EQUAL(edited.position(), std::size_t{3});
        FT_REQUIRE((edited.snapshot().to_vector()
            == std::vector<value_type>{1, std::nullopt, 7, 9}));
        FT_REQUIRE((basis.to_vector() == std::vector<value_type>{1, std::nullopt, 3}));
        FT_REQUIRE_THROWS(std::logic_error, basis.get_cursor().delete_previous());
        FT_REQUIRE_THROWS(std::out_of_range, basis.get_cursor(4));
    });

    tests.add("reversible cursor follows logical order and maps its reversed gap", [] {
        const auto basis = ft::reversible_deque<int>{1, 2, 3, 4}.reverse();
        const auto cursor = basis.get_cursor(1);
        FT_REQUIRE_EQUAL(cursor.try_peek_previous().value(), 4);
        FT_REQUIRE_EQUAL(cursor.try_peek_next().value(), 3);

        const auto edited = cursor.insert(9).delete_next();
        FT_REQUIRE((edited.snapshot().to_vector() == std::vector<int>{4, 9, 2, 1}));
        const auto reversed = edited.reverse();
        FT_REQUIRE_EQUAL(reversed.position(), std::size_t{2});
        FT_REQUIRE((reversed.snapshot().to_vector() == std::vector<int>{1, 2, 9, 4}));
    });

    tests.add("general cursor exposes ordered measures without a public position", [] {
        const auto tree = ft::finger_tree<int, ft::sum_measure<int>>{2, 3, 5, 7};
        const auto located = tree.get_cursor_by_measure([](const int total) { return total >= 6; });
        FT_REQUIRE(located.found);
        FT_REQUIRE_EQUAL(located.cursor.measure_before(), 5);
        FT_REQUIRE_EQUAL(located.cursor.measure_after(), 12);
        FT_REQUIRE_EQUAL(*located.cursor.try_peek_next(), 5);

        const auto edited = located.cursor.insert(11).delete_next().replace_next(13);
        FT_REQUIRE((edited.snapshot().to_vector() == std::vector<int>{2, 3, 11, 13}));
        FT_REQUIRE((tree.to_vector() == std::vector<int>{2, 3, 5, 7}));
        const auto moved = edited.move_previous().move_next();
        FT_REQUIRE_EQUAL(moved.measure_before(), edited.measure_before());
        const auto miss = tree.get_cursor_by_measure([](const int total) { return total > 100; });
        FT_REQUIRE(!miss.found);
        FT_REQUIRE(miss.cursor.is_at_end());
    });

    tests.add("RRB cursor splices an existing persistent vector", [] {
        auto source_values = std::vector<int>{};
        for (auto value = 0; value != 96; ++value) {
            source_values.push_back(value);
        }
        const auto basis = ft::rrb_vector<int>::from_range(source_values);
        const auto inserted = ft::rrb_vector<int>{500, 501, 502};
        const auto cursor = basis.get_cursor(32).insert_vector(inserted);

        FT_REQUIRE_EQUAL(cursor.position(), std::size_t{35});
        const auto values = cursor.snapshot().to_vector();
        FT_REQUIRE((std::vector<int>(values.begin() + 30, values.begin() + 37)
            == std::vector<int>{30, 31, 500, 501, 502, 32, 33}));
        FT_REQUIRE(basis.to_vector() == source_values);
    });

    tests.add("Range cursor preserves measures and applies directional tags", [] {
        using sequence_type = ft::range_update_sequence<std::int64_t, additive_range_algebra>;
        const auto basis = sequence_type{1, 2, 3, 4};
        const auto cursor = basis.get_cursor(2);
        FT_REQUIRE_EQUAL(cursor.measure_before(), std::int64_t{3});
        FT_REQUIRE_EQUAL(cursor.measure_after(), std::int64_t{7});
        FT_REQUIRE_EQUAL(cursor.measure_previous(2), std::int64_t{3});
        FT_REQUIRE_EQUAL(cursor.measure_next(2), std::int64_t{7});

        const auto edited = cursor.apply_previous(1, 10).apply_next(2, 20).replace_next(99);
        FT_REQUIRE_EQUAL(edited.position(), std::size_t{2});
        FT_REQUIRE((edited.snapshot().to_vector() == std::vector<std::int64_t>{1, 12, 99, 24}));
        FT_REQUIRE((basis.to_vector() == std::vector<std::int64_t>{1, 2, 3, 4}));
        FT_REQUIRE_THROWS(std::out_of_range, cursor.apply_previous(3, 1));
    });
}

} // namespace

void add_sequence_cursor_tests(suite& tests)
{
    add_sequence_cursor_tests_impl(tests);
}
