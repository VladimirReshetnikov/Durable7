#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

template <class T>
concept has_rvalue_peek_previous = requires(const T&& cursor) {
    std::move(cursor).try_peek_previous();
};

template <class T>
concept has_rvalue_peek_next = requires(const T&& cursor) {
    std::move(cursor).try_peek_next();
};

static_assert(std::forward_iterator<ft::rope<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::rope<int>>);
static_assert(!std::default_initializable<ft::rope_cursor<int>>);
static_assert(std::copy_constructible<ft::rope_cursor<int>>);
static_assert(std::same_as<decltype(std::declval<const ft::rope_cursor<int>&>().try_peek_next()), const int*>);
static_assert(requires(const ft::rope_cursor<int>& cursor) { cursor.try_peek_previous(); });
static_assert(!has_rvalue_peek_previous<ft::rope_cursor<int>>);
static_assert(requires(const ft::rope_cursor<int>& cursor) { cursor.try_peek_next(); });
static_assert(!has_rvalue_peek_next<ft::rope_cursor<int>>);

struct non_equality_value final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

static_assert(std::ranges::forward_range<const ft::rope<non_equality_value>>);
static_assert(!has_equality<ft::rope_split<non_equality_value>>);
static_assert(!has_equality<ft::rope_cursor<non_equality_value>>);

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

void require_cursor_equal(
    const ft::rope_cursor<int>& actual,
    const std::vector<int>& expected,
    const std::size_t expected_position,
    const std::source_location location = std::source_location::current())
{
    if (actual.size() != expected.size()
        || actual.position() != expected_position
        || actual.is_at_start() != (expected_position == 0)
        || actual.is_at_end() != (expected_position == expected.size())) {
        std::ostringstream message;
        message << location.file_name() << ':' << location.line() << ": rope cursor state mismatch";
        throw test_failure(message.str());
    }

    const auto* previous = actual.try_peek_previous();
    if (expected_position == 0) {
        FT_REQUIRE(previous == nullptr);
    } else {
        FT_REQUIRE(previous != nullptr);
        FT_REQUIRE_EQUAL(*previous, expected[expected_position - 1]);
    }

    const auto* next = actual.try_peek_next();
    if (expected_position == expected.size()) {
        FT_REQUIRE(next == nullptr);
    } else {
        FT_REQUIRE(next != nullptr);
        FT_REQUIRE_EQUAL(*next, expected[expected_position]);
    }

    require_sequence_equal(actual.snapshot(), expected, location);
}

void move_assign_cursor(
    ft::rope_cursor<int>& destination,
    ft::rope_cursor<int>& source)
{
    destination = std::move(source);
}

std::vector<int> insert_model_values(
    const std::vector<int>& source,
    const std::size_t position,
    const std::initializer_list<int> inserted)
{
    if (position > source.size()) {
        throw test_failure("model insertion position is out of range");
    }

    auto result = std::vector<int>{};
    result.reserve(source.size() + inserted.size());
    for (auto index = std::size_t{0}; index != position; ++index) {
        result.push_back(source[index]);
    }
    for (const auto value : inserted) {
        result.push_back(value);
    }
    for (auto index = position; index != source.size(); ++index) {
        result.push_back(source[index]);
    }
    return result;
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

    tests.add("rope cursor gap navigation and endpoint failures are exact", [] {
        const auto values = std::vector<int>{10, 20, 30, 40, 50};
        const auto source = ft::rope<int>::from_range(values);
        const auto cursor = source.get_cursor(2);

        require_cursor_equal(cursor, values, 2);
        require_cursor_equal(cursor.move_previous(), values, 1);
        require_cursor_equal(cursor.move_next(), values, 3);

        const auto start = cursor.seek(0);
        require_cursor_equal(start, values, 0);
        FT_REQUIRE_THROWS(std::logic_error, start.move_previous());
        FT_REQUIRE_THROWS(std::logic_error, start.delete_previous());

        const auto end = cursor.seek(values.size());
        require_cursor_equal(end, values, values.size());
        FT_REQUIRE_THROWS(std::logic_error, end.move_next());
        FT_REQUIRE_THROWS(std::logic_error, end.delete_next());
        FT_REQUIRE_THROWS(std::logic_error, end.replace_next(0));

        FT_REQUIRE_THROWS(std::out_of_range, source.get_cursor(values.size() + 1));
        FT_REQUIRE_THROWS(std::out_of_range, cursor.seek(values.size() + 1));

        const auto empty = ft::rope<int>{}.get_cursor();
        require_cursor_equal(empty, {}, 0);
        FT_REQUIRE_THROWS(std::logic_error, empty.move_previous());
        FT_REQUIRE_THROWS(std::logic_error, empty.move_next());
        FT_REQUIRE_THROWS(std::logic_error, empty.delete_previous());
        FT_REQUIRE_THROWS(std::logic_error, empty.delete_next());
        FT_REQUIRE_THROWS(std::logic_error, empty.replace_next(0));
    });

    tests.add("rope cursor moves preserve both immutable values", [] {
        const auto values = iota_vector(32);
        const auto source = ft::rope<int>::from_range(values);

        auto move_source = source.get_cursor(11);
        const auto move_destination = std::move(move_source);
        require_cursor_equal(move_source, values, 11);
        require_cursor_equal(move_destination, values, 11);

        auto assignment_source = source.get_cursor(23);
        auto assignment_destination = source.get_cursor(3);
        assignment_destination = std::move(assignment_source);
        require_cursor_equal(assignment_source, values, 23);
        require_cursor_equal(assignment_destination, values, 23);

        move_assign_cursor(assignment_destination, assignment_destination);
        require_cursor_equal(assignment_destination, values, 23);
    });

    tests.add("rope cursor edits branch retained snapshots and preserve no-ops", [] {
        const auto original_values = iota_vector(20);
        const auto source = ft::rope<int>::from_range(original_values);
        const auto ancestor = source.get_cursor(10);

        auto inserted_values = original_values;
        inserted_values.insert(inserted_values.begin() + 10, -1);
        require_cursor_equal(ancestor.insert(-1), inserted_values, 11);

        auto range_values = original_values;
        range_values.insert(range_values.begin() + 10, {-3, -2, -1});
        const auto caller_owned = std::vector<int>{-3, -2, -1};
        require_cursor_equal(ancestor.insert_range(caller_owned), range_values, 13);
        require_cursor_equal(
            ancestor.insert_range(ft::rope<int>::from_range(caller_owned)),
            range_values,
            13);

        auto without_previous = original_values;
        without_previous.erase(without_previous.begin() + 9);
        require_cursor_equal(ancestor.delete_previous(), without_previous, 9);

        auto without_next = original_values;
        without_next.erase(without_next.begin() + 10);
        require_cursor_equal(ancestor.delete_next(), without_next, 10);

        auto replaced_values = original_values;
        replaced_values[10] = -2;
        require_cursor_equal(ancestor.replace_next(-2), replaced_values, 10);
        require_cursor_equal(ancestor, original_values, 10);

        const auto equal_replacement = ancestor.replace_next(original_values[10]);
        require_cursor_equal(equal_replacement, original_values, 10);
        FT_REQUIRE(ancestor.snapshot().begin() != equal_replacement.snapshot().begin());

        const auto same_position = ancestor.seek(ancestor.position());
        const auto empty_insert = ancestor.insert_range(std::vector<int>{});
        require_cursor_equal(same_position, original_values, 10);
        require_cursor_equal(empty_insert, original_values, 10);
        FT_REQUIRE(ancestor.snapshot().begin() == same_position.snapshot().begin());
        FT_REQUIRE(ancestor.snapshot().begin() == empty_insert.snapshot().begin());

        const auto no_equality_source = ft::rope<non_equality_value>::from_range(
            std::vector<non_equality_value>{{1}});
        const auto no_equality_replaced = no_equality_source.get_cursor().replace_next(non_equality_value{2});
        FT_REQUIRE_EQUAL(no_equality_replaced.snapshot()[0].value, 2);
        FT_REQUIRE_EQUAL(no_equality_source[0].value, 1);
    });

    tests.add("rope cursor operations are exact across chunk boundaries", [] {
        const auto values = iota_vector(4'098);
        const auto source = ft::rope<int>::from_range(values);
        const auto boundaries = std::vector<std::size_t>{
            0, 1, 15, 16, 255, 256, 257, 2'047, 2'048, 2'049, values.size()};

        for (const auto position : boundaries) {
            const auto cursor = source.get_cursor(position);
            require_cursor_equal(cursor, values, position);

            const auto with_insert = insert_model_values(values, position, {-1});
            const auto inserted = cursor.insert(-1);
            require_cursor_equal(inserted, with_insert, position + 1);
            require_cursor_equal(inserted.delete_previous(), values, position);

            const auto with_range = insert_model_values(values, position, {-3, -2, -1});
            const auto ranged = cursor.insert_range(std::vector<int>{-3, -2, -1});
            require_cursor_equal(ranged, with_range, position + 3);
            require_cursor_equal(
                ranged.delete_previous().delete_previous().delete_previous(),
                values,
                position);

            if (position != 0) {
                auto removed = values;
                removed.erase(removed.begin() + static_cast<std::ptrdiff_t>(position - 1));
                require_cursor_equal(cursor.delete_previous(), removed, position - 1);
            }

            if (position != values.size()) {
                auto removed = values;
                removed.erase(removed.begin() + static_cast<std::ptrdiff_t>(position));
                require_cursor_equal(cursor.delete_next(), removed, position);

                auto replaced = values;
                replaced[position] = -4;
                require_cursor_equal(cursor.replace_next(-4), replaced, position);
            }
        }
    });

    tests.add("rope cursor deterministic command history matches vector gap model", [] {
        auto rng = deterministic_rng{0x51c0};
        auto model = iota_vector(513);
        auto position = std::size_t{256};
        auto cursor = ft::rope<int>::from_range(model).get_cursor(position);
        auto retained = std::vector<ft::rope_cursor<int>>{};
        auto retained_models = std::vector<std::vector<int>>{};
        auto retained_positions = std::vector<std::size_t>{};
        auto next_value = -1;

        for (auto step = 0; step != 500; ++step) {
            switch (rng.next_index(12)) {
            case 0:
                if (position == 0) {
                    FT_REQUIRE_THROWS(std::logic_error, cursor.move_previous());
                } else {
                    cursor = cursor.move_previous();
                    --position;
                }
                break;
            case 1:
                if (position == model.size()) {
                    FT_REQUIRE_THROWS(std::logic_error, cursor.move_next());
                } else {
                    cursor = cursor.move_next();
                    ++position;
                }
                break;
            case 2:
                position = rng.next_index(model.size() + 1);
                cursor = cursor.seek(position);
                break;
            case 3:
                cursor = cursor.insert(next_value);
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(position), next_value--);
                ++position;
                break;
            case 4: {
                const auto length = rng.next_index(9);
                auto values = std::vector<int>{};
                values.reserve(length);
                for (auto index = std::size_t{0}; index != length; ++index) {
                    values.push_back(next_value--);
                }

                cursor = cursor.insert_range(values);
                model.insert(
                    model.begin() + static_cast<std::ptrdiff_t>(position),
                    values.begin(),
                    values.end());
                position += length;
                break;
            }
            case 5:
                if (position == 0) {
                    FT_REQUIRE_THROWS(std::logic_error, cursor.delete_previous());
                } else {
                    cursor = cursor.delete_previous();
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(position - 1));
                    --position;
                }
                break;
            case 6:
                if (position == model.size()) {
                    FT_REQUIRE_THROWS(std::logic_error, cursor.delete_next());
                } else {
                    cursor = cursor.delete_next();
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(position));
                }
                break;
            case 7:
                if (position == model.size()) {
                    FT_REQUIRE_THROWS(std::logic_error, cursor.replace_next(next_value));
                } else {
                    cursor = cursor.replace_next(next_value);
                    model[position] = next_value--;
                }
                break;
            case 8:
                retained.push_back(cursor);
                retained_models.push_back(model);
                retained_positions.push_back(position);
                break;
            case 9: {
                const auto boundaries = std::vector<std::size_t>{0, 1, 15, 16, 255, 256, 257, 2'047, 2'048, 2'049};
                position = (std::min)(boundaries[rng.next_index(boundaries.size())], model.size());
                cursor = cursor.seek(position);
                break;
            }
            case 10:
                cursor = cursor.seek(position);
                break;
            default:
                if (!retained.empty()) {
                    const auto retained_index = rng.next_index(retained.size());
                    auto branch_model = retained_models[retained_index];
                    const auto branch_position = retained_positions[retained_index];
                    const auto branch_value = next_value--;
                    branch_model.insert(
                        branch_model.begin() + static_cast<std::ptrdiff_t>(branch_position),
                        branch_value);
                    require_cursor_equal(
                        retained[retained_index].insert(branch_value),
                        branch_model,
                        branch_position + 1);
                    require_cursor_equal(
                        retained[retained_index],
                        retained_models[retained_index],
                        branch_position);
                }
                break;
            }

            require_cursor_equal(cursor, model, position);
        }

        for (auto index = std::size_t{0}; index != retained.size(); ++index) {
            require_cursor_equal(retained[index], retained_models[index], retained_positions[index]);
        }
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

    tests.add("rope growth is preflighted against size overflow", [] {
        // Doubling by concatenation shares structure, so a rope of the maximum representable
        // size costs a bounded number of operations to build and no proportional storage.
        const auto seed = ft::rope<int>{0};
        auto power = seed;
        auto expanded = ft::rope<int>{};
        for (auto bit = 0; bit != std::numeric_limits<std::size_t>::digits; ++bit) {
            expanded = expanded.concat(power);
            if (bit + 1 != std::numeric_limits<std::size_t>::digits) {
                power = power.concat(power);
            }
        }

        FT_REQUIRE_EQUAL(expanded.size(), (std::numeric_limits<std::size_t>::max)());

        // Each growth path must reject the overflow up front. insert_at is checked away from
        // the end as well, because the cursor's own position-based check cannot see an overflow
        // when the gap sits before the final element.
        FT_REQUIRE_THROWS(std::overflow_error, expanded.insert_at(expanded.size(), 1));
        FT_REQUIRE_THROWS(std::overflow_error, expanded.insert_at(0, 1));
        FT_REQUIRE_THROWS(std::overflow_error, expanded.insert_range(0, seed));
        FT_REQUIRE_THROWS(std::overflow_error, expanded.concat(seed));
        FT_REQUIRE_THROWS(std::overflow_error, expanded.get_cursor(0).insert(1));

        // The rejected operations leave both operands untouched.
        FT_REQUIRE_EQUAL(expanded.size(), (std::numeric_limits<std::size_t>::max)());
        FT_REQUIRE_EQUAL(seed.size(), std::size_t{1});
        FT_REQUIRE_EQUAL(*expanded.try_get(0), 0);

        // An empty addition is not growth and must still be accepted.
        FT_REQUIRE_EQUAL(expanded.concat(ft::rope<int>{}).size(), expanded.size());
    });
}

} // namespace

void add_rope_tests(suite& tests)
{
    add_rope_tests_impl(tests);
}
