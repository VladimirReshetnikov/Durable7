#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <ranges>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

static_assert(std::forward_iterator<ft::rope<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::rope<int>>);

struct non_equality_value final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

static_assert(std::ranges::forward_range<const ft::rope<non_equality_value>>);
static_assert(!has_equality<ft::rope_split<non_equality_value>>);

template <class T>
void require_sequence_equal(
    const ft::rope<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    actual.validate_invariants();
    const auto actual_values = actual.to_vector();
    if (actual_values != expected) {
        std::ostringstream message;
        message << location.file_name() << ':' << location.line() << ": rope sequence mismatch";
        throw test_failure(message.str());
    }

    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    FT_REQUIRE_EQUAL(actual.empty(), expected.empty());
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

[[nodiscard]] std::vector<int> iota_vector(const int count, const int first = 0)
{
    auto values = std::vector<int>{};
    values.reserve(static_cast<std::size_t>(count));
    for (auto value = 0; value != count; ++value) {
        values.push_back(first + value);
    }

    return values;
}

[[nodiscard]] std::vector<int> vector_slice(
    const std::vector<int>& values,
    const std::size_t index,
    const std::size_t count)
{
    return std::vector<int>{
        values.begin() + static_cast<std::ptrdiff_t>(index),
        values.begin() + static_cast<std::ptrdiff_t>(index + count)};
}

[[nodiscard]] std::shared_ptr<const std::vector<int>> shared_block(std::vector<int> values)
{
    return std::make_shared<const std::vector<int>>(std::move(values));
}

void add_rope_tests_impl(suite& tests)
{
    tests.add("rope constructs from ranges initializer lists and owned chunks", [] {
        require_sequence_equal(ft::rope<int>{}, {});
        require_sequence_equal(ft::rope<int>{1, 2, 3}, {1, 2, 3});

        const auto values = iota_vector(5000);
        require_sequence_equal(ft::rope<int>::from_range(values), values);
        require_sequence_equal(ft::rope<int>{values.begin(), values.end()}, values);

        const auto first = shared_block(std::vector<int>{1, 2, 3});
        const auto empty = shared_block({});
        const auto large = shared_block(iota_vector(5000, 4));
        const auto last = shared_block(std::vector<int>{9004});
        auto expected = std::vector<int>{1, 2, 3};
        auto middle = iota_vector(5000, 4);
        expected.insert(expected.end(), middle.begin(), middle.end());
        expected.push_back(9004);

        require_sequence_equal(ft::rope<int>::from_chunks({first, empty, large, last}), expected);
    });

    tests.add("rope endpoint operations preserve earlier versions", [] {
        const auto original = ft::rope<int>{2, 3};
        const auto grown = original.push_front(1).push_back(4);

        require_sequence_equal(original, {2, 3});
        require_sequence_equal(grown, {1, 2, 3, 4});
        require_sequence_equal(grown.remove_first(), {2, 3, 4});
        require_sequence_equal(grown.remove_last(), {1, 2, 3});
    });

    tests.add("rope indexed mutations match vector model across chunks", [] {
        const auto model = iota_vector(6000);
        const auto rope = ft::rope<int>::from_range(model);

        auto inserted = std::vector<int>{};
        inserted.reserve(model.size() + 1);
        inserted.insert(inserted.end(), model.begin(), model.begin() + 3000);
        inserted.push_back(-1);
        inserted.insert(inserted.end(), model.begin() + 3000, model.end());
        require_sequence_equal(rope.insert_at(3000, -1), inserted);

        auto removed = model;
        removed.erase(removed.begin() + 3000);
        require_sequence_equal(rope.remove_at(3000), removed);

        auto replaced = model;
        replaced[3000] = -1;
        require_sequence_equal(rope.set_item(3000, -1), replaced);
        require_sequence_equal(rope, model);
    });

    tests.add("rope range mutations split slice and copy match vector model", [] {
        const auto model = iota_vector(4000);
        const auto rope = ft::rope<int>::from_range(model);
        const auto added = std::vector<int>{-1, -2, -3, -4};

        auto inserted = model;
        inserted.insert(inserted.begin() + 1500, added.begin(), added.end());
        require_sequence_equal(rope.insert_range(1500, added), inserted);
        require_sequence_equal(rope.insert_range(1500, ft::rope<int>::from_range(added)), inserted);

        auto removed = model;
        removed.erase(removed.begin() + 1000, removed.begin() + 3000);
        require_sequence_equal(rope.remove_range(1000, 2000), removed);

        for (auto index = std::size_t{0}; index <= model.size(); index += 137) {
            auto split = rope.split_at(index);
            FT_REQUIRE_EQUAL(split.left.size(), index);
            FT_REQUIRE_EQUAL(split.right.size(), model.size() - index);
            require_sequence_equal(split.left.concat(split.right), model);
            require_sequence_equal(split.left, vector_slice(model, 0, index));
            require_sequence_equal(split.right, vector_slice(model, index, model.size() - index));
        }

        require_sequence_equal(rope.slice(500, 2000), vector_slice(model, 500, 2000));
        FT_REQUIRE(rope.get_range(500, 2000) == vector_slice(model, 500, 2000));

        auto copied = std::vector<int>(2000);
        rope.copy_to(1500, std::span<int>{copied.data(), copied.size()});
        FT_REQUIRE(copied == vector_slice(model, 1500, 2000));
    });

    tests.add("rope forward iterator streams chunks with multipass and retained lifetime semantics", [] {
        const auto values = iota_vector(6'000);
        const auto rope = ft::rope<int>::from_range(values);

        auto first = rope.begin();
        auto same = first;
        FT_REQUIRE(first == same);
        FT_REQUIRE_EQUAL(*first, 0);
        ++first;
        FT_REQUIRE(first != same);
        ++same;
        FT_REQUIRE(first == same);

        const auto independent = ft::rope<int>::from_range(values);
        FT_REQUIRE(rope.begin() != independent.begin());

        auto surviving = [] {
            return ft::rope<int>::from_range(iota_vector(6'000)).begin();
        }();
        auto iterated = std::vector<int>{};
        iterated.reserve(values.size());
        while (surviving != ft::rope<int>::const_iterator{}) {
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

    tests.add("rope same-type insertion avoids forwarding recursion and split results have value equality", [] {
        const auto source = ft::rope<int>{1, 2, 5, 6};
        const auto middle = ft::rope<int>{3, 4};
        require_sequence_equal(source.insert_range(2, middle), {1, 2, 3, 4, 5, 6});
        require_sequence_equal(source.insert_range(2, ft::rope<int>{3, 4}), {1, 2, 3, 4, 5, 6});

        const auto first = ft::rope<int>::from_range(iota_vector(4'096));
        const auto second = ft::rope<int>::from_range(iota_vector(4'096));
        FT_REQUIRE(first.split_at(1'337) == second.split_at(1'337));
        FT_REQUIRE(first.split_at(1'337) != second.split_at(1'338));
    });

    tests.add("rope concat handles empty and boundary coalescing cases", [] {
        const auto sizes = std::vector{0, 1, 200, 3000};
        for (const auto left_size : sizes) {
            for (const auto right_size : sizes) {
                const auto left_values = iota_vector(left_size);
                const auto right_values = iota_vector(right_size, 1000);
                auto expected = left_values;
                expected.insert(expected.end(), right_values.begin(), right_values.end());
                require_sequence_equal(
                    ft::rope<int>::from_range(left_values).concat(ft::rope<int>::from_range(right_values)),
                    expected);
            }
        }

        const auto compact_boundary = ft::rope<int>{1, 2}.concat(ft::rope<int>{3, 4});
        FT_REQUIRE_EQUAL(compact_boundary.chunk_count(), static_cast<std::size_t>(1));
        require_sequence_equal(compact_boundary, {1, 2, 3, 4});
    });

    tests.add("rope compact releases oversized backing storage after slices are dropped", [] {
        auto backing = shared_block(iota_vector(10000));
        auto rope = ft::rope<int>::from_chunks({backing});
        auto slice = rope.slice(2500, 5000);
        rope = ft::rope<int>{};

        FT_REQUIRE(backing.use_count() > 1);
        const auto expected = slice.to_vector();
        auto compacted = slice.compact();
        slice = ft::rope<int>{};

        FT_REQUIRE_EQUAL(backing.use_count(), 1L);
        require_sequence_equal(compacted, expected);
    });

    tests.add("rope randomized history matches vector model", [] {
        auto rng = deterministic_rng{0x705e};
        auto model = std::vector<int>{};
        auto rope = ft::rope<int>{};
        auto next = 0;

        for (auto step = 0; step != 260; ++step) {
            switch (rng.next_index(11)) {
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
                auto run = iota_vector(static_cast<int>(rng.next_index(600)), next);
                next += static_cast<int>(run.size());
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), run.begin(), run.end());
                rope = rope.insert_range(index, run);
                break;
            }
            case 8:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    const auto count = rng.next_index(model.size() - index + 1);
                    model.erase(
                        model.begin() + static_cast<std::ptrdiff_t>(index),
                        model.begin() + static_cast<std::ptrdiff_t>(index + count));
                    rope = rope.remove_range(index, count);
                }
                break;
            case 9: {
                const auto index = rng.next_index(model.size() + 1);
                const auto split = rope.split_at(index);
                rope = split.left.concat(split.right);
                break;
            }
            default: {
                const auto index = rng.next_index(model.size() + 1);
                const auto split = rope.split_at(index);
                rope = split.right.concat(split.left);
                auto rotated = std::vector<int>{model.begin() + static_cast<std::ptrdiff_t>(index), model.end()};
                rotated.insert(rotated.end(), model.begin(), model.begin() + static_cast<std::ptrdiff_t>(index));
                model = std::move(rotated);
                break;
            }
            }

            require_sequence_equal(rope, model);
        }
    });

    tests.add("rope large splice stays valid and indexable", [] {
        constexpr auto size = 100000;
        const auto values = iota_vector(size);
        const auto rope = ft::rope<int>::from_range(values);
        rope.validate_invariants();

        for (auto index = std::size_t{0}; index < values.size(); index += 9973) {
            FT_REQUIRE_EQUAL(rope[index], values[index]);
        }

        auto spliced = rope.remove_range(40000, 20000).insert_range(40000, std::vector<int>{-5, -4, -3, -2, -1});
        spliced.validate_invariants();
        FT_REQUIRE_EQUAL(spliced.size(), static_cast<std::size_t>(size - 20000 + 5));
        FT_REQUIRE_EQUAL(spliced[40000], -5);
        FT_REQUIRE_EQUAL(spliced[40005], 60000);
    });

    tests.add("rope empty and argument validation behavior", [] {
        const auto empty = ft::rope<int>{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE(empty.to_vector().empty());
        FT_REQUIRE(empty.try_get(0) == nullptr);
        FT_REQUIRE_THROWS(std::logic_error, empty.front());
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_first());
        FT_REQUIRE_THROWS(std::out_of_range, empty.at(0));

        const auto rope = ft::rope<int>{1, 2, 3};
        FT_REQUIRE_THROWS(std::out_of_range, rope.at(3));
        FT_REQUIRE_THROWS(std::out_of_range, rope.insert_at(4, 0));
        FT_REQUIRE_THROWS(std::out_of_range, rope.remove_range(2, 5));
        FT_REQUIRE_THROWS(std::invalid_argument, ft::rope<int>::from_chunks({nullptr}));

        const auto split = rope.split_at(0);
        FT_REQUIRE(split.left.empty());
        require_sequence_equal(split.right, {1, 2, 3});
    });
}

} // namespace

void add_rope_tests(suite& tests)
{
    add_rope_tests_impl(tests);
}
