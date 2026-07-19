#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
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

/// Value that tallies its own copies and moves through shared state, so a test can assert that
/// an operation forwarded an rvalue rather than silently copying it.
struct copy_probe final {
    std::shared_ptr<std::size_t> copies;
    std::shared_ptr<std::size_t> moves;
    int value = 0;

    copy_probe(
        std::shared_ptr<std::size_t> copy_counter,
        std::shared_ptr<std::size_t> move_counter,
        const int probe_value)
        : copies(std::move(copy_counter))
        , moves(std::move(move_counter))
        , value(probe_value)
    {
    }

    copy_probe(const copy_probe& other)
        : copies(other.copies), moves(other.moves), value(other.value)
    {
        ++*copies;
    }

    copy_probe(copy_probe&& other) noexcept
        : copies(other.copies), moves(other.moves), value(other.value)
    {
        ++*moves;
    }

    copy_probe& operator=(const copy_probe& other)
    {
        if (this != &other) {
            copies = other.copies;
            moves = other.moves;
            value = other.value;
            ++*copies;
        }
        return *this;
    }

    copy_probe& operator=(copy_probe&& other) noexcept
    {
        if (this != &other) {
            copies = other.copies;
            moves = other.moves;
            value = other.value;
            ++*moves;
        }
        return *this;
    }

    ~copy_probe() = default;
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

    tests.add("deque cursor replace_next forwards the caller's rvalue", [] {
        auto copies = std::make_shared<std::size_t>(0);
        auto moves = std::make_shared<std::size_t>(0);
        const auto basis = ft::persistent_deque<copy_probe>{
            copy_probe{copies, moves, 1},
            copy_probe{copies, moves, 2}};
        const auto cursor = basis.get_cursor(0);

        // The two calls differ only in the value category of the argument, so comparing their
        // copy counts isolates whether the move survives the cursor layer. Passing the rvalue
        // through as an lvalue made both paths identical.
        *copies = 0;
        const auto from_lvalue_source = copy_probe{copies, moves, 7};
        *copies = 0;
        const auto from_lvalue = cursor.replace_next(from_lvalue_source);
        const auto lvalue_copies = *copies;

        auto rvalue_source = copy_probe{copies, moves, 7};
        *copies = 0;
        const auto from_rvalue = cursor.replace_next(std::move(rvalue_source));
        const auto rvalue_copies = *copies;

        FT_REQUIRE(rvalue_copies < lvalue_copies);
        FT_REQUIRE_EQUAL(from_lvalue.snapshot().at(0).value, 7);
        FT_REQUIRE_EQUAL(from_rvalue.snapshot().at(0).value, 7);
        FT_REQUIRE_EQUAL(basis.at(0).value, 1);
    });

    tests.add("RRB cursor inserts one element without an intermediate vector", [] {
        auto source_values = std::vector<int>{};
        for (auto value = 0; value != 96; ++value) {
            source_values.push_back(value);
        }
        const auto basis = ft::rrb_vector<int>::from_range(source_values);

        for (const auto position : {std::size_t{0}, std::size_t{47}, std::size_t{96}}) {
            const auto cursor = basis.get_cursor(position).insert(999);
            FT_REQUIRE_EQUAL(cursor.position(), position + 1);
            FT_REQUIRE_EQUAL(cursor.snapshot().size(), source_values.size() + 1);

            auto expected = source_values;
            expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(position), 999);
            FT_REQUIRE((cursor.snapshot().to_vector() == expected));
            FT_REQUIRE(basis.to_vector() == source_values);
        }

        // The single-element path and the range path must agree exactly.
        FT_REQUIRE((basis.get_cursor(10).insert(999).snapshot().to_vector()
            == basis.get_cursor(10).insert_vector(ft::rrb_vector<int>{999}).snapshot().to_vector()));
    });

    tests.add("sequence cursors expose clean-snapshot root identity", [] {
        // Without shares_root_with the clean-snapshot clause could only be checked by comparing
        // values, which cannot tell a retained version from a rebuilt equal one.
        const auto deque = ft::persistent_deque<int>{1, 2, 3};
        FT_REQUIRE(deque.get_cursor(1).snapshot().shares_root_with(deque));
        FT_REQUIRE(!deque.get_cursor(1).insert(9).snapshot().shares_root_with(deque));
        FT_REQUIRE(deque.shares_root_with(deque));

        const auto reversible = ft::reversible_deque<int>{1, 2, 3};
        FT_REQUIRE(reversible.get_cursor(1).snapshot().shares_root_with(reversible));
        FT_REQUIRE(!reversible.get_cursor(1).insert(9).snapshot().shares_root_with(reversible));

        const auto tree = ft::finger_tree<int, ft::sum_measure<int>>{2, 3, 5};
        const auto located = tree.get_cursor_by_measure([](const int total) { return total >= 5; });
        FT_REQUIRE(located.cursor.snapshot().shares_root_with(tree));
        FT_REQUIRE(!located.cursor.insert(9).snapshot().shares_root_with(tree));
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
