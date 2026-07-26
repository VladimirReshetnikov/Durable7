/// Tests for the reversible deque, including that reversal shares rather than rebuilds.

#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <source_location>
#include <sstream>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

static_assert(std::input_iterator<ft::reversible_deque<int>::const_iterator>);
static_assert(std::forward_iterator<ft::reversible_deque<int>::const_iterator>);
static_assert(std::ranges::input_range<const ft::reversible_deque<int>>);
static_assert(std::ranges::forward_range<const ft::reversible_deque<int>>);

struct non_equality_value final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

static_assert(std::ranges::input_range<const ft::reversible_deque<non_equality_value>>);
static_assert(!has_equality<ft::reversible_deque_split<non_equality_value>>);
static_assert(!has_equality<ft::reversible_deque_pop<non_equality_value>>);

template <class T>
void require_sequence_equal(
    const ft::reversible_deque<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    const auto actual_values = actual.to_vector();
    if (actual_values == expected) {
        actual.validate_invariants();
        for (std::size_t index = 0; index < expected.size(); ++index) {
            FT_REQUIRE_EQUAL(actual[index], expected[index]);
        }

        if (!expected.empty()) {
            FT_REQUIRE_EQUAL(actual.front(), expected.front());
            FT_REQUIRE_EQUAL(actual.back(), expected.back());
        }

        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": reversible deque sequence mismatch";
    throw test_failure(message.str());
}

[[nodiscard]] std::vector<int> iota_vector(const int count, const int first = 0)
{
    auto values = std::vector<int>{};
    values.reserve(static_cast<std::size_t>(count));
    for (auto value = 0; value != count; ++value) {
        values.push_back(first + value);
    }

    return values;
}

[[nodiscard]] std::vector<int> reversed(std::vector<int> values)
{
    std::ranges::reverse(values);
    return values;
}

void add_reversible_deque_tests_impl(suite& tests)
{
    tests.add("reversible deque endpoint operations preserve original versions", [] {
        const auto original = ft::reversible_deque<int>{2, 3};
        const auto updated = original.push_front(1).push_back(4);

        require_sequence_equal(original, {2, 3});
        require_sequence_equal(updated, {1, 2, 3, 4});
        require_sequence_equal(updated.remove_first(), {2, 3, 4});
        require_sequence_equal(updated.remove_last(), {1, 2, 3});

        const auto popped_front = updated.try_pop_front();
        FT_REQUIRE(popped_front.has_value());
        FT_REQUIRE_EQUAL(popped_front->value, 1);
        require_sequence_equal(popped_front->rest, {2, 3, 4});

        const auto popped_back = updated.try_pop_back();
        FT_REQUIRE(popped_back.has_value());
        FT_REQUIRE_EQUAL(popped_back->value, 4);
        require_sequence_equal(popped_back->rest, {1, 2, 3});
    });

    tests.add("reversible deque reverse is correct and involutive", [] {
        for (auto size = 0; size <= 50; ++size) {
            const auto values = iota_vector(size);
            const auto deque = ft::reversible_deque<int>::from_range(values);
            const auto flipped = deque.reverse();

            require_sequence_equal(flipped, reversed(values));
            require_sequence_equal(deque, values);
            require_sequence_equal(flipped.reverse(), values);
        }
    });

    tests.add("reversible deque operations after reverse match reversed model", [] {
        const auto deque = ft::reversible_deque<int>{1, 2, 3, 4, 5}.reverse();

        FT_REQUIRE_EQUAL(deque.front(), 5);
        FT_REQUIRE_EQUAL(deque.back(), 1);
        FT_REQUIRE_EQUAL(deque[2], 3);
        require_sequence_equal(deque.push_front(9), {9, 5, 4, 3, 2, 1});
        require_sequence_equal(deque.push_back(9), {5, 4, 3, 2, 1, 9});
        require_sequence_equal(deque.remove_first(), {4, 3, 2, 1});
        require_sequence_equal(deque.remove_last(), {5, 4, 3, 2});
        require_sequence_equal(deque.set_item(2, 9), {5, 4, 9, 2, 1});
        require_sequence_equal(deque.insert_at(2, 9), {5, 4, 9, 3, 2, 1});
        require_sequence_equal(deque.remove_at(2), {5, 4, 2, 1});

        const auto split = deque.split_at(2);
        require_sequence_equal(split.left, {5, 4});
        require_sequence_equal(split.right, {3, 2, 1});
    });

    tests.add("reversible deque concat handles all orientation combinations", [] {
        const auto sizes = std::vector{0, 1, 2, 3, 8, 21, 50};
        for (const auto left_size : sizes) {
            for (const auto right_size : sizes) {
                const auto left_values = iota_vector(left_size);
                const auto right_values = iota_vector(right_size, 100);
                const auto left = ft::reversible_deque<int>::from_range(left_values);
                const auto right = ft::reversible_deque<int>::from_range(right_values);

                auto expected = left_values;
                expected.insert(expected.end(), right_values.begin(), right_values.end());
                require_sequence_equal(left.concat(right), expected);

                expected = reversed(left_values);
                expected.insert(expected.end(), right_values.begin(), right_values.end());
                require_sequence_equal(left.reverse().concat(right), expected);

                expected = left_values;
                auto reversed_right = reversed(right_values);
                expected.insert(expected.end(), reversed_right.begin(), reversed_right.end());
                require_sequence_equal(left.concat(right.reverse()), expected);

                expected = reversed(left_values);
                expected.insert(expected.end(), reversed_right.begin(), reversed_right.end());
                require_sequence_equal(left.reverse().concat(right.reverse()), expected);
            }
        }
    });

    tests.add("reversible deque split reconstructs at every index", [] {
        for (auto size = 0; size <= 40; ++size) {
            const auto values = iota_vector(size);
            for (const auto& deque : {ft::reversible_deque<int>::from_range(values),
                     ft::reversible_deque<int>::from_range(values).reverse()}) {
                const auto sequence = deque.to_vector();
                for (auto index = std::size_t{0}; index <= sequence.size(); ++index) {
                    const auto split = deque.split_at(index);
                    FT_REQUIRE_EQUAL(split.left.size(), index);
                    FT_REQUIRE_EQUAL(split.right.size(), sequence.size() - index);
                    require_sequence_equal(split.left.concat(split.right), sequence);
                }
            }
        }
    });

    tests.add("reversible deque large reverse stays valid and indexable", [] {
        constexpr auto size = 1500;
        const auto expected = reversed(iota_vector(size));
        const auto deque = ft::reversible_deque<int>::from_range(iota_vector(size)).reverse();

        deque.validate_invariants();
        FT_REQUIRE_EQUAL(deque.size(), static_cast<std::size_t>(size));
        for (auto index = std::size_t{0}; index < expected.size(); index += 137) {
            FT_REQUIRE_EQUAL(deque[index], expected[index]);
        }

        require_sequence_equal(deque, expected);
    });

    tests.add("reversible deque forward iterator streams logical orientation and retains source lifetime", [] {
        const auto values = iota_vector(1'500);
        const auto deque = ft::reversible_deque<int>::from_range(values).reverse();
        const auto expected = reversed(values);

        auto iterated = std::vector<int>{};
        iterated.reserve(expected.size());
        for (const auto& value : deque) {
            iterated.push_back(value);
        }
        FT_REQUIRE(iterated == expected);

        auto first = deque.begin();
        auto second = first;
        ++first;
        FT_REQUIRE_EQUAL(*second, expected[0]);
        FT_REQUIRE_EQUAL(*first, expected[1]);

        auto surviving = [] {
            return ft::reversible_deque<int>::from_range(iota_vector(1'500)).reverse().begin();
        }();
        iterated.clear();
        while (surviving != ft::reversible_deque<int>::const_iterator{}) {
            iterated.push_back(*surviving);
            ++surviving;
        }
        FT_REQUIRE(iterated == expected);

        auto copied = std::vector<int>{};
        copied.reserve(expected.size());
        deque.copy_to(std::back_inserter(copied));
        FT_REQUIRE(copied == expected);

        const auto independent = ft::reversible_deque<int>::from_range(values).reverse();
        FT_REQUIRE(deque.begin() != independent.begin());
        FT_REQUIRE(deque.split_at(733) == independent.split_at(733));
        FT_REQUIRE(deque.split_at(733) != independent.split_at(734));
        FT_REQUIRE(deque.try_pop_front() == independent.try_pop_front());
        FT_REQUIRE(deque.try_pop_back() == independent.try_pop_back());
    });

    tests.add("reversible deque traversal allocates no per-element mirrored state", [] {
        const auto deque = ft::reversible_deque<int>::from_range(iota_vector(16'384)).reverse();
        auto iterator = deque.begin();
        const auto end = deque.end();
        long long sum = 0;
        allocation_counting_scope allocations;
        while (iterator != end) {
            sum += *iterator;
            ++iterator;
        }

        FT_REQUIRE_EQUAL(sum, 134'209'536LL);
        FT_REQUIRE_EQUAL(allocations.allocations(), std::size_t{0});
    });

    tests.add("reversible deque reverse allocation count is independent of size", [] {
        const auto small = ft::reversible_deque<int>::from_range(iota_vector(128));
        const auto large = ft::reversible_deque<int>::from_range(iota_vector(4096));

        const auto reverse_allocations = [](ft::reversible_deque<int> deque) {
            const auto expected_size = deque.size();
            allocation_counting_scope allocations;
            for (auto iteration = 0; iteration != 128; ++iteration) {
                deque = deque.reverse();
            }

            FT_REQUIRE_EQUAL(deque.size(), expected_size);
            return allocations.allocations();
        };

        FT_REQUIRE_EQUAL(reverse_allocations(small), reverse_allocations(large));
    });

    tests.add("reversible deque deep reversed middle set remains consistent", [] {
        constexpr auto size = 300;
        auto model = reversed(iota_vector(size));
        auto deque = ft::reversible_deque<int>::from_range(iota_vector(size)).reverse();
        require_sequence_equal(deque, model);

        for (const auto index : std::vector<std::size_t>{0, 1, 2, 7, size / 3, size / 2, 2 * size / 3, size - 3, size - 2, size - 1}) {
            const auto value = -static_cast<int>(index) - 1;
            model[index] = value;
            deque = deque.set_item(index, value);
            require_sequence_equal(deque, model);
        }

        require_sequence_equal(deque.reverse(), reversed(model));
        const auto split = deque.split_at(size / 2);
        require_sequence_equal(split.left.concat(split.right), model);
        auto with_back = model;
        with_back.push_back(777);
        require_sequence_equal(deque.push_back(777), with_back);
        auto with_front = model;
        with_front.insert(with_front.begin(), 999);
        require_sequence_equal(deque.push_front(999), with_front);
    });

    tests.add("reversible deque randomized history with reverse matches vector model", [] {
        auto rng = deterministic_rng{0x0f1a7e};
        auto model = std::vector<int>{};
        auto deque = ft::reversible_deque<int>{};

        for (auto step = 0; step != 220; ++step) {
            switch (rng.next_index(10)) {
            case 0:
                model.insert(model.begin(), step);
                deque = deque.push_front(step);
                break;
            case 1:
                model.push_back(step);
                deque = deque.push_back(step);
                break;
            case 2:
                if (!model.empty()) {
                    model.erase(model.begin());
                    deque = deque.remove_first();
                }
                break;
            case 3:
                if (!model.empty()) {
                    model.pop_back();
                    deque = deque.remove_last();
                }
                break;
            case 4: {
                const auto index = rng.next_index(model.size() + 1);
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), step);
                deque = deque.insert_at(index, step);
                break;
            }
            case 5:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
                    deque = deque.remove_at(index);
                }
                break;
            case 6:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    model[index] = step;
                    deque = deque.set_item(index, step);
                }
                break;
            case 7:
                std::ranges::reverse(model);
                deque = deque.reverse();
                break;
            case 8: {
                const auto index = rng.next_index(model.size() + 1);
                const auto split = deque.split_at(index);
                deque = split.left.concat(split.right);
                break;
            }
            default: {
                const auto index = rng.next_index(model.size() + 1);
                const auto split = deque.split_at(index);
                deque = split.right.concat(split.left);
                auto rotated = std::vector<int>{model.begin() + static_cast<std::ptrdiff_t>(index), model.end()};
                rotated.insert(rotated.end(), model.begin(), model.begin() + static_cast<std::ptrdiff_t>(index));
                model = std::move(rotated);
                break;
            }
            }

            require_sequence_equal(deque, model);
        }
    });

    tests.add("reversible deque empty behavior is degenerate", [] {
        const auto empty = ft::reversible_deque<int>{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE(empty.to_vector().empty());
        FT_REQUIRE(empty.reverse().empty());
        FT_REQUIRE_THROWS(std::logic_error, empty.front());
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_first());
        FT_REQUIRE_THROWS(std::out_of_range, empty.at(0));
        FT_REQUIRE(!empty.try_pop_front().has_value());
    });
}

} // namespace

void add_reversible_deque_tests(suite& tests)
{
    add_reversible_deque_tests_impl(tests);
}
