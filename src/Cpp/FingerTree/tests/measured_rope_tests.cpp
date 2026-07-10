#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <ranges>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

using sum_rope = ft::measured_rope<int, ft::sum_measure<int>>;

static_assert(std::forward_iterator<sum_rope::const_iterator>);
static_assert(std::ranges::forward_range<const sum_rope>);

struct non_equality_value final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

using non_equality_rope = ft::measured_rope<non_equality_value, ft::size_measure<non_equality_value>>;
static_assert(std::ranges::forward_range<const non_equality_rope>);
static_assert(!has_equality<
    ft::measured_rope_split<non_equality_value, ft::size_measure<non_equality_value>>>);

struct newline_measure final {
    using element_type = char;
    using measure_type = std::size_t;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    [[nodiscard]] static constexpr measure_type measure(const char value) noexcept
    {
        return value == '\n' ? 1 : 0;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right)
    {
        return ft::checked_add(left, right);
    }
};

template <class T>
[[nodiscard]] std::vector<T> vector_slice(
    const std::vector<T>& values,
    const std::size_t index,
    const std::size_t count)
{
    return std::vector<T>{
        values.begin() + static_cast<std::ptrdiff_t>(index),
        values.begin() + static_cast<std::ptrdiff_t>(index + count)};
}

[[nodiscard]] std::vector<int> iota_vector(const int count, const int first = 1)
{
    auto values = std::vector<int>{};
    values.reserve(static_cast<std::size_t>(count));
    for (auto value = 0; value != count; ++value) {
        values.push_back(first + value);
    }

    return values;
}

[[nodiscard]] int sum_of(const std::vector<int>& values)
{
    return std::accumulate(values.begin(), values.end(), 0);
}

void require_sequence_equal(
    const ft::measured_rope<int, ft::sum_measure<int>>& actual,
    const std::vector<int>& expected,
    const std::source_location location = std::source_location::current())
{
    actual.validate_invariants();
    const auto actual_values = actual.to_vector();
    if (actual_values != expected) {
        std::ostringstream message;
        message << location.file_name() << ':' << location.line() << ": measured rope sequence mismatch";
        throw test_failure(message.str());
    }

    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    FT_REQUIRE_EQUAL(actual.measure(), sum_of(expected));
    for (auto index = std::size_t{0}; index < expected.size(); ++index) {
        FT_REQUIRE_EQUAL(actual[index], expected[index]);
        FT_REQUIRE(actual.try_get(index) != nullptr);
        FT_REQUIRE_EQUAL(*actual.try_get(index), expected[index]);
    }

    if (!expected.empty()) {
        FT_REQUIRE_EQUAL(actual.front(), expected.front());
        FT_REQUIRE_EQUAL(actual.back(), expected.back());
    }
}

void add_measured_rope_tests_impl(suite& tests)
{
    tests.add("measured rope positional operations match vector model and measure", [] {
        const auto model = iota_vector(6000);
        const auto rope = ft::measured_rope<int, ft::sum_measure<int>>::from_range(model);
        require_sequence_equal(rope, model);

        auto inserted = std::vector<int>{};
        inserted.reserve(model.size() + 1);
        inserted.insert(inserted.end(), model.begin(), model.begin() + 3000);
        inserted.push_back(99999);
        inserted.insert(inserted.end(), model.begin() + 3000, model.end());
        require_sequence_equal(rope.insert_at(3000, 99999), inserted);

        auto removed = model;
        removed.erase(removed.begin() + 1000, removed.begin() + 3000);
        require_sequence_equal(rope.remove_range(1000, 2000), removed);

        auto replaced = model;
        replaced[1234] = 77;
        require_sequence_equal(rope.set_item(1234, 77), replaced);

        const auto split = rope.split_at(2500);
        require_sequence_equal(split.left, vector_slice(model, 0, 2500));
        require_sequence_equal(split.right, vector_slice(model, 2500, model.size() - 2500));
        require_sequence_equal(split.left.concat(split.right), model);
    });

    tests.add("measured rope prefix measure matches running sum", [] {
        const auto values = iota_vector(500);
        const auto rope = ft::measured_rope<int, ft::sum_measure<int>>::from_range(values);

        auto running = 0;
        for (auto count = std::size_t{0}; count <= values.size(); ++count) {
            FT_REQUIRE_EQUAL(rope.prefix_measure(count), running);
            if (count < values.size()) {
                running += values[count];
            }
        }
    });

    tests.add("measured rope forward iterator streams chunks with multipass and retained lifetime semantics", [] {
        const auto values = iota_vector(6'000);
        const auto rope = sum_rope::from_range(values);

        auto first = rope.begin();
        auto same = first;
        FT_REQUIRE(first == same);
        FT_REQUIRE_EQUAL(*first, 1);
        ++first;
        FT_REQUIRE(first != same);
        ++same;
        FT_REQUIRE(first == same);

        const auto independent = sum_rope::from_range(values);
        FT_REQUIRE(rope.begin() != independent.begin());

        auto surviving = [] {
            return sum_rope::from_range(iota_vector(6'000)).begin();
        }();
        auto iterated = std::vector<int>{};
        iterated.reserve(values.size());
        while (surviving != sum_rope::const_iterator{}) {
            iterated.push_back(*surviving);
            ++surviving;
        }
        FT_REQUIRE(iterated == values);

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        rope.for_each([](const int&) {});
        auto iterator = rope.begin();
        const auto end = rope.end();
        auto checksum = std::uint64_t{0};
        auto iteration_allocations = std::size_t{0};
        {
            allocation_counting_scope allocations;
            while (iterator != end) {
                checksum += static_cast<std::uint64_t>(*iterator);
                ++iterator;
            }
            iteration_allocations = allocations.allocations();
        }
        FT_REQUIRE(checksum > 0);
        FT_REQUIRE_EQUAL(iteration_allocations, static_cast<std::size_t>(0));

        auto copied = std::vector<int>(4'000);
        auto copy_allocations = std::size_t{0};
        {
            allocation_counting_scope allocations;
            rope.copy_to(997, std::span<int>{copied.data(), copied.size()});
            copy_allocations = allocations.allocations();
        }
        FT_REQUIRE(copied == vector_slice(values, 997, copied.size()));
        FT_REQUIRE(copy_allocations < copied.size() / 16);
#endif
    });

    tests.add("measured rope same-type insertion avoids forwarding recursion and split results have value equality", [] {
        const auto source = sum_rope{1, 2, 5, 6};
        const auto middle = sum_rope{3, 4};
        require_sequence_equal(source.insert_range(2, middle), {1, 2, 3, 4, 5, 6});
        require_sequence_equal(source.insert_range(2, sum_rope{3, 4}), {1, 2, 3, 4, 5, 6});

        const auto first = sum_rope::from_range(iota_vector(4'096));
        const auto second = sum_rope::from_range(iota_vector(4'096));
        FT_REQUIRE(first.split_at(1'337) == second.split_at(1'337));
        FT_REQUIRE(first.split_at(1'337) != second.split_at(1'338));
    });

    tests.add("measured rope measure split and locate scan inside boundary chunk", [] {
        const auto values = iota_vector(400);
        const auto rope = ft::measured_rope<int, ft::sum_measure<int>>::from_range(values);
        const auto total = sum_of(values);

        for (auto threshold = -1; threshold <= total + 1; threshold += 37) {
            auto running = 0;
            auto boundary = std::size_t{0};
            while (boundary < values.size() && running + values[boundary] <= threshold) {
                running += values[boundary];
                ++boundary;
            }

            const auto split = rope.split_by_measure(ft::sum_above_predicate<int>{threshold});
            require_sequence_equal(split.left, vector_slice(values, 0, boundary));
            require_sequence_equal(split.right, vector_slice(values, boundary, values.size() - boundary));
            FT_REQUIRE_EQUAL(split.left.measure(), running);

            const auto located = rope.try_locate_by_measure(ft::sum_above_predicate<int>{threshold});
            if (boundary < values.size()) {
                FT_REQUIRE(located.has_value());
                FT_REQUIRE_EQUAL(located.index, boundary);
                FT_REQUIRE_EQUAL(located.measure_before, running);
                FT_REQUIRE_EQUAL(*located.value, values[boundary]);
            } else {
                FT_REQUIRE(!located.has_value());
                FT_REQUIRE_EQUAL(located.index, values.size());
                FT_REQUIRE_EQUAL(located.measure_before, total);
            }
        }
    });

    tests.add("measured rope newline measure supports offset to line primitives", [] {
        const auto text = std::string{"line zero\nline one\nline two\n\nline four"};
        const auto rope = ft::measured_rope<char, newline_measure>::from_range(text);

        FT_REQUIRE_EQUAL(rope.size(), text.size());
        FT_REQUIRE_EQUAL(rope.measure(), static_cast<std::size_t>(std::ranges::count(text, '\n')));

        for (auto offset = std::size_t{0}; offset <= text.size(); ++offset) {
            const auto expected = static_cast<std::size_t>(
                std::ranges::count(std::string_view{text.data(), offset}, '\n'));
            FT_REQUIRE_EQUAL(rope.prefix_measure(offset), expected);
        }

        for (auto line = std::size_t{1}; line <= rope.measure(); ++line) {
            const auto located = rope.try_locate_by_measure([line](const std::size_t count) {
                return count >= line;
            });
            FT_REQUIRE(located.has_value());
            FT_REQUIRE_EQUAL(*located.value, '\n');
            FT_REQUIRE_EQUAL(located.measure_before, line - 1);
            FT_REQUIRE_EQUAL(
                static_cast<std::size_t>(std::ranges::count(
                    std::string_view{text.data(), located.index + 1},
                    '\n')),
                line);
        }
    });

    tests.add("measured rope randomized history matches vector and sum model", [] {
        auto rng = deterministic_rng{0x6d50a5};
        auto model = std::vector<int>{};
        auto rope = ft::measured_rope<int, ft::sum_measure<int>>{};
        auto next = 1;

        for (auto step = 0; step != 240; ++step) {
            switch (rng.next_index(9)) {
            case 0:
                model.insert(model.begin(), next);
                rope = rope.push_front(next++);
                break;
            case 1:
                model.push_back(next);
                rope = rope.push_back(next++);
                break;
            case 2:
                if (!model.empty()) {
                    model.erase(model.begin());
                    rope = rope.remove_first();
                }
                break;
            case 3:
                if (!model.empty()) {
                    model.pop_back();
                    rope = rope.remove_last();
                }
                break;
            case 4: {
                const auto index = rng.next_index(model.size() + 1);
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), next);
                rope = rope.insert_at(index, next++);
                break;
            }
            case 5:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
                    rope = rope.remove_at(index);
                }
                break;
            case 6:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    model[index] = next;
                    rope = rope.set_item(index, next++);
                }
                break;
            case 7: {
                const auto index = rng.next_index(model.size() + 1);
                auto run = iota_vector(static_cast<int>(rng.next_index(400)), next);
                next += static_cast<int>(run.size());
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), run.begin(), run.end());
                rope = rope.insert_range(index, run);
                break;
            }
            default: {
                const auto index = rng.next_index(model.size() + 1);
                const auto split = rope.split_at(index);
                rope = split.left.concat(split.right);
                break;
            }
            }

            require_sequence_equal(rope, model);
            if (!model.empty()) {
                const auto threshold = rng.next_int(0, sum_of(model));
                auto running = 0;
                auto boundary = std::size_t{0};
                while (boundary < model.size() && running + model[boundary] <= threshold) {
                    running += model[boundary];
                    ++boundary;
                }

                const auto located = rope.try_locate_by_measure(ft::sum_above_predicate<int>{threshold});
                FT_REQUIRE_EQUAL(located.has_value(), boundary < model.size());
                if (located.has_value()) {
                    FT_REQUIRE_EQUAL(located.index, boundary);
                    FT_REQUIRE_EQUAL(located.measure_before, running);
                } else {
                    FT_REQUIRE_EQUAL(located.index, model.size());
                    FT_REQUIRE_EQUAL(located.measure_before, sum_of(model));
                }
            }
        }
    });

    tests.add("measured rope empty and argument validation behavior", [] {
        const auto empty = ft::measured_rope<int, ft::sum_measure<int>>{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE_EQUAL(empty.size(), static_cast<std::size_t>(0));
        FT_REQUIRE_EQUAL(empty.measure(), 0);
        const auto empty_located = empty.try_locate_by_measure(ft::sum_above_predicate<int>{0});
        FT_REQUIRE(!empty_located.has_value());
        FT_REQUIRE_EQUAL(empty_located.index, static_cast<std::size_t>(0));
        FT_REQUIRE_EQUAL(empty_located.measure_before, 0);
        FT_REQUIRE_THROWS(std::logic_error, empty.front());
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_first());
        FT_REQUIRE_THROWS(std::out_of_range, empty.at(0));

        const auto rope = ft::measured_rope<int, ft::sum_measure<int>>{1, 2, 3};
        FT_REQUIRE_THROWS(std::out_of_range, rope.at(3));
        FT_REQUIRE_THROWS(std::out_of_range, rope.insert_at(4, 0));
        FT_REQUIRE_THROWS(std::out_of_range, rope.remove_range(2, 5));
        FT_REQUIRE_THROWS(std::out_of_range, rope.prefix_measure(4));
    });
}

} // namespace

void add_measured_rope_tests(suite& tests)
{
    add_measured_rope_tests_impl(tests);
}
