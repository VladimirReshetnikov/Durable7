#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

static_assert(std::forward_iterator<ft::rrb_vector<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::rrb_vector<int>>);

struct nonassignable_value final {
    int value = 0;

    nonassignable_value() = default;
    explicit nonassignable_value(const int value_arg) noexcept
        : value(value_arg)
    {
    }
    nonassignable_value(const nonassignable_value&) = default;
    nonassignable_value(nonassignable_value&&) noexcept = default;
    nonassignable_value& operator=(const nonassignable_value&) = delete;
    nonassignable_value& operator=(nonassignable_value&&) = delete;

    friend bool operator==(const nonassignable_value&, const nonassignable_value&) = default;
};

static_assert(std::copy_constructible<nonassignable_value>);

[[nodiscard]] std::vector<int> integer_range(const int first, const std::size_t count)
{
    auto result = std::vector<int>{};
    result.reserve(count);
    for (auto index = std::size_t{0}; index < count; ++index) {
        result.push_back(first + static_cast<int>(index));
    }
    return result;
}

void require_equal(const ft::rrb_vector<int>& actual, const std::vector<int>& expected)
{
    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    FT_REQUIRE(std::ranges::equal(actual, expected));
    actual.validate_invariants();
}

[[nodiscard]] std::size_t minimum_height(const std::size_t count)
{
    auto height = std::size_t{0};
    auto capacity = std::size_t{32};
    while (capacity < count) {
        capacity *= 32;
        ++height;
    }
    return height;
}

struct throwing_value final {
    int value = 0;

    throwing_value() = default;
    explicit throwing_value(const int value_arg) noexcept
        : value(value_arg)
    {
    }

    throwing_value(const throwing_value& other)
        : value(copy(other.value))
    {
    }

    throwing_value(throwing_value&& other) noexcept
        : value(other.value)
    {
    }

    throwing_value& operator=(const throwing_value& other)
    {
        const auto copied = copy(other.value);
        value = copied;
        return *this;
    }

    throwing_value& operator=(throwing_value&& other) noexcept
    {
        value = other.value;
        return *this;
    }

    friend bool operator==(const throwing_value&, const throwing_value&) = default;

    static void allow_copies(const int count) noexcept
    {
        remaining_copies = count;
    }

    static void allow_all_copies() noexcept
    {
        remaining_copies = -1;
    }

private:
    [[nodiscard]] static int copy(const int source)
    {
        if (remaining_copies == 0) {
            throw std::runtime_error("injected RRB value-copy failure");
        }
        if (remaining_copies > 0) {
            --remaining_copies;
        }
        return source;
    }

    inline static int remaining_copies = -1;
};

void add_rrb_vector_tests_impl(suite& tests)
{
    tests.add("RRB packed construction covers radix boundaries", [] {
        constexpr auto counts = std::array<std::size_t, 9>{0, 1, 31, 32, 33, 1'023, 1'024, 1'025, 100'000};
        for (const auto count : counts) {
            const auto values = integer_range(0, count);
            const auto vector = ft::rrb_vector<int>::from_range(values);
            require_equal(vector, values);
            const auto statistics = vector.structure_statistics();
            FT_REQUIRE_EQUAL(statistics.count, count);
            FT_REQUIRE_EQUAL(statistics.relaxed_branch_count, std::size_t{0});
            for (auto index = std::size_t{0}; index < count; index += (std::max)(std::size_t{1}, count / 101)) {
                FT_REQUIRE_EQUAL(vector[index], static_cast<int>(index));
            }
        }
    });

    tests.add("RRB endpoints indexed update and pop preserve old versions", [] {
        const auto empty = ft::rrb_vector<std::string>{};
        const auto one = empty.add_last("middle");
        const auto two = one.add_first("left");
        const auto three = two.add_last("right");
        FT_REQUIRE_EQUAL(three.front(), std::string{"left"});
        FT_REQUIRE_EQUAL(three.back(), std::string{"right"});
        FT_REQUIRE(three.to_vector() == std::vector<std::string>({"left", "middle", "right"}));

        const auto changed = three.set_item(1, "center");
        FT_REQUIRE(changed.to_vector() == std::vector<std::string>({"left", "center", "right"}));
        FT_REQUIRE(three.to_vector() == std::vector<std::string>({"left", "middle", "right"}));
        FT_REQUIRE(three.set_item(1, "middle").shares_root_with(three));
        FT_REQUIRE((three.split_at(1) == ft::rrb_vector_split<std::string>{
            ft::rrb_vector<std::string>{"left"}, ft::rrb_vector<std::string>{"middle", "right"}}));

        const auto popped = three.pop_last();
        FT_REQUIRE_EQUAL(popped.value, std::string{"right"});
        FT_REQUIRE(popped.rest.to_vector() == std::vector<std::string>({"left", "middle"}));
        FT_REQUIRE((popped == ft::rrb_vector_pop<std::string>{
            "right", ft::rrb_vector<std::string>{"left", "middle"}}));
        FT_REQUIRE(!empty.try_pop_last().has_value());
        FT_REQUIRE_THROWS(std::logic_error, empty.pop_last());

        const auto nonassignable = ft::rrb_vector<nonassignable_value>{
            nonassignable_value{1}, nonassignable_value{2}, nonassignable_value{3}};
        const auto nonassignable_changed = nonassignable
            .set_item(1, nonassignable_value{20})
            .concat(ft::rrb_vector<nonassignable_value>{nonassignable_value{4}});
        FT_REQUIRE_EQUAL(nonassignable_changed[1].value, 20);
        FT_REQUIRE_EQUAL(nonassignable_changed.back().value, 4);
        FT_REQUIRE_EQUAL(nonassignable[1].value, 2);
    });

    tests.add("RRB unequal-height concat rebalances boundary spines", [] {
        constexpr auto cases = std::array<std::pair<std::size_t, std::size_t>, 5>{
            std::pair{std::size_t{1}, std::size_t{100'000}},
            std::pair{std::size_t{100'000}, std::size_t{1}},
            std::pair{std::size_t{31}, std::size_t{33}},
            std::pair{std::size_t{1'023}, std::size_t{1'025}},
            std::pair{std::size_t{50'000}, std::size_t{50'000}}};
        for (const auto& [left_count, right_count] : cases) {
            const auto left_values = integer_range(0, left_count);
            const auto right_values = integer_range(static_cast<int>(left_count), right_count);
            const auto left = ft::rrb_vector<int>::from_range(left_values);
            const auto right = ft::rrb_vector<int>::from_range(right_values);
            auto expected = left_values;
            expected.insert(expected.end(), right_values.begin(), right_values.end());
            require_equal(left.concat(right), expected);
            FT_REQUIRE(left.concat({}).shares_root_with(left));
            FT_REQUIRE(ft::rrb_vector<int>{}.concat(right).shares_root_with(right));
        }
    });

    tests.add("RRB exact leaf-boundary splits retain leaf identities", [] {
        const auto vector = ft::rrb_vector<int>::from_range(integer_range(0, 32 * 128));
        const auto original = vector.leaf_identities();
        constexpr auto boundaries = std::array<std::size_t, 7>{32, 64, 32 * 31, 32 * 32, 32 * 33, 32 * 96, 32 * 127};
        for (const auto boundary : boundaries) {
            const auto split = vector.split_at(boundary);
            auto retained = split.left.leaf_identities();
            const auto right = split.right.leaf_identities();
            retained.insert(retained.end(), right.begin(), right.end());
            FT_REQUIRE(retained == original);
            split.left.validate_invariants();
            split.right.validate_invariants();
        }
        FT_REQUIRE(vector.split_at(0).right.shares_root_with(vector));
        FT_REQUIRE(vector.split_at(vector.size()).left.shares_root_with(vector));
    });

    tests.add("RRB relaxed suffix uses size tables while packed input does not", [] {
        const auto packed = ft::rrb_vector<int>::from_range(integer_range(0, 32 * 1'024));
        const auto packed_statistics = packed.structure_statistics();
        FT_REQUIRE(packed_statistics.regular_branch_count > 0);
        FT_REQUIRE_EQUAL(packed_statistics.relaxed_branch_count, std::size_t{0});

        const auto relaxed = packed.split_at(1).right;
        const auto relaxed_statistics = relaxed.structure_statistics();
        FT_REQUIRE(relaxed_statistics.relaxed_branch_count > 0);
        for (auto index = std::size_t{0}; index < relaxed.size(); index += 997) {
            FT_REQUIRE_EQUAL(relaxed[index], static_cast<int>(index + 1));
        }
    });

    tests.add("RRB insert and remove ranges preserve root on semantic no-ops", [] {
        const auto base = ft::rrb_vector<int>::from_range(integer_range(0, 100));
        const auto inserted = base.insert_range(25, std::vector<int>{-3, -2, -1});
        auto expected = integer_range(0, 100);
        expected.insert(expected.begin() + 25, {-3, -2, -1});
        require_equal(inserted, expected);
        require_equal(inserted.remove_range(25, 3), integer_range(0, 100));
        FT_REQUIRE(base.insert_range(50, std::vector<int>{}).shares_root_with(base));
        FT_REQUIRE(base.remove_range(50, 0).shares_root_with(base));
        FT_REQUIRE_THROWS(std::out_of_range, base.insert_range(101, std::vector<int>{1}));
        FT_REQUIRE_THROWS(std::out_of_range, base.remove_range(99, 2));
    });

    tests.add("RRB randomized persistent edits match a vector model", [] {
        deterministic_rng random{0x7272625f6d6f6465ULL};
        auto actual = ft::rrb_vector<int>{};
        auto expected = std::vector<int>{};
        auto snapshots = std::vector<std::pair<ft::rrb_vector<int>, std::vector<int>>>{};

        for (auto step = 0; step != 10'000; ++step) {
            switch (random.next_index(5)) {
            case 0:
                actual = actual.add_last(step);
                expected.push_back(step);
                break;
            case 1:
                actual = actual.add_first(step);
                expected.insert(expected.begin(), step);
                break;
            case 2:
                if (!expected.empty()) {
                    const auto index = random.next_index(expected.size());
                    actual = actual.set_item(index, -step);
                    expected[index] = -step;
                }
                break;
            case 3: {
                const auto index = random.next_index(expected.size() + 1);
                const auto values = std::vector<int>{step, step + 1, step + 2};
                actual = actual.insert_range(index, values);
                expected.insert(expected.begin() + static_cast<std::ptrdiff_t>(index), values.begin(), values.end());
                break;
            }
            case 4:
                if (!expected.empty()) {
                    const auto index = random.next_index(expected.size());
                    const auto count = random.next_index(expected.size() - index + 1);
                    actual = actual.remove_range(index, count);
                    expected.erase(
                        expected.begin() + static_cast<std::ptrdiff_t>(index),
                        expected.begin() + static_cast<std::ptrdiff_t>(index + count));
                }
                break;
            }

            FT_REQUIRE_EQUAL(actual.size(), expected.size());
            if (step % 127 == 0) {
                require_equal(actual, expected);
            }
            if (step % 701 == 0) {
                snapshots.emplace_back(actual, expected);
            }
        }

        require_equal(actual, expected);
        for (const auto& [snapshot, model] : snapshots) {
            require_equal(snapshot, model);
        }
    });

    tests.add("RRB adversarial split-concat history bounds density and height", [] {
        constexpr auto count = std::size_t{32 * 1'024};
        const auto expected = integer_range(0, count);
        auto actual = ft::rrb_vector<int>::from_range(expected);
        deterministic_rng random{0x7272625f73706c74ULL};

        for (auto operation = 0; operation != 2'000; ++operation) {
            const auto boundary = [&] {
                switch (operation % 7) {
                case 0: return std::size_t{1};
                case 1: return std::size_t{31};
                case 2: return std::size_t{32};
                case 3: return std::size_t{1'023};
                case 4: return std::size_t{1'024};
                case 5: return count - 1;
                default: return random.next_index(count - 1) + 1;
                }
            }();
            const auto split = actual.split_at(boundary);
            actual = split.left.concat(split.right);
            if (operation % 127 == 0) {
                actual.validate_invariants();
            }
        }

        require_equal(actual, expected);
        const auto statistics = actual.structure_statistics();
        FT_REQUIRE(statistics.height <= minimum_height(count) + 1);
        FT_REQUIRE(statistics.leaf_count <= (count + 15) / 16);
        FT_REQUIRE(statistics.branch_count < statistics.leaf_count);
    });

    tests.add("RRB uneven fragment concatenation remains dense", [] {
        auto actual = ft::rrb_vector<int>{};
        auto expected = std::vector<int>{};
        for (auto fragment = 0; fragment != 2'048; ++fragment) {
            const auto length = static_cast<std::size_t>(1 + fragment * 17 % 63);
            auto values = integer_range(static_cast<int>(expected.size()), length);
            actual = actual.concat(ft::rrb_vector<int>::from_range(values));
            expected.insert(expected.end(), values.begin(), values.end());
        }

        require_equal(actual, expected);
        const auto statistics = actual.structure_statistics();
        FT_REQUIRE(statistics.height <= minimum_height(actual.size()) + 1);
        FT_REQUIRE(statistics.leaf_count <= (actual.size() + 15) / 16);
    });

    tests.add("RRB append builder caches immutable isolated snapshots", [] {
        auto source = integer_range(0, 1'000);
        auto builder = ft::rrb_vector<int>::create_builder();
        builder.add_range(source);
        source[0] = -1;
        const auto first = builder.to_immutable();
        const auto first_again = builder.to_immutable();
        FT_REQUIRE(first_again.shares_root_with(first));
        require_equal(first, integer_range(0, 1'000));

        builder.add(1'000);
        builder.add_range(integer_range(1'001, 999));
        const auto second = builder.to_immutable();
        require_equal(first, integer_range(0, 1'000));
        require_equal(second, integer_range(0, 2'000));

        auto adopted = second.to_builder();
        FT_REQUIRE(adopted.to_immutable().shares_root_with(second));
        adopted.add(2'000);
        const auto third = adopted.to_immutable();
        require_equal(second, integer_range(0, 2'000));
        require_equal(third, integer_range(0, 2'001));
        adopted.clear();
        FT_REQUIRE(adopted.empty());
        FT_REQUIRE(adopted.to_immutable().empty());
    });

    tests.add("RRB immutable and builder operations provide strong exception safety", [] {
        throwing_value::allow_all_copies();
        const auto values = std::vector<throwing_value>{
            throwing_value{1}, throwing_value{2}, throwing_value{3}, throwing_value{4}};
        const auto vector = ft::rrb_vector<throwing_value>::from_range(values);
        const auto identity = vector.root_identity();

        throwing_value::allow_copies(0);
        FT_REQUIRE_THROWS(std::runtime_error, vector.set_item(1, throwing_value{9}));
        FT_REQUIRE_EQUAL(vector.root_identity(), identity);
        throwing_value::allow_all_copies();
        FT_REQUIRE_EQUAL(vector[1].value, 2);

        auto builder = vector.to_builder();
        builder.add(throwing_value{5});
        const auto count_before = builder.size();
        const auto extra = std::vector<throwing_value>{throwing_value{6}, throwing_value{7}};
        throwing_value::allow_copies(0);
        FT_REQUIRE_THROWS(std::runtime_error, builder.add_range(extra));
        FT_REQUIRE_EQUAL(builder.size(), count_before);
        FT_REQUIRE_THROWS(std::runtime_error, builder.to_immutable());
        FT_REQUIRE_EQUAL(builder.size(), count_before);

        throwing_value::allow_all_copies();
        const auto snapshot = builder.to_immutable();
        FT_REQUIRE_EQUAL(snapshot.size(), std::size_t{5});
        FT_REQUIRE_EQUAL(snapshot.back().value, 5);
    });
}

} // namespace

void add_rrb_vector_tests(suite& tests)
{
    add_rrb_vector_tests_impl(tests);
}
