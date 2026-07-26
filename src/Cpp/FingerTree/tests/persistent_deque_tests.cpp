/// Tests for the persistent catenable deque.

#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/operation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ft = durable7::finger_tree;
namespace detail = durable7::finger_tree::detail;
using namespace durable7::finger_tree::tests;

namespace {

static_assert(std::forward_iterator<ft::persistent_deque<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::persistent_deque<int>>);

struct non_equality_value final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

static_assert(std::ranges::forward_range<const ft::persistent_deque<non_equality_value>>);
static_assert(!has_equality<ft::deque_split<non_equality_value>>);
static_assert(!has_equality<ft::deque_item_split<non_equality_value>>);
static_assert(!has_equality<ft::deque_range_split<non_equality_value>>);
static_assert(!has_equality<ft::deque_pop<non_equality_value>>);

[[nodiscard]] detail::deque_element<int> deque_leaf(const int value)
{
    return detail::deque_element<int>::leaf(value);
}

[[nodiscard]] detail::deque_tree<int> deque_middle_node_tree(const int first, const int second)
{
    return detail::deque_tree<int>::single(detail::deque_element<int>::node2(deque_leaf(first), deque_leaf(second)));
}

template <class T>
void require_sequence_equal(
    const ft::persistent_deque<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    const auto actual_values = actual.to_vector();
    if (actual_values == expected) {
        actual.validate_invariants();
        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": sequence mismatch. Actual [";
    for (std::size_t index = 0; index != actual_values.size(); ++index) {
        if (index != 0) {
            message << ", ";
        }

        message << actual_values[index];
    }

    message << "], expected [";
    for (std::size_t index = 0; index != expected.size(); ++index) {
        if (index != 0) {
            message << ", ";
        }

        message << expected[index];
    }

    message << ']';
    throw test_failure(message.str());
}

template <class T>
std::vector<T> vector_range(const std::vector<T>& source, const std::size_t index, const std::size_t count)
{
    return std::vector<T>{source.begin() + static_cast<std::ptrdiff_t>(index),
        source.begin() + static_cast<std::ptrdiff_t>(index + count)};
}

[[nodiscard]] ft::persistent_deque<int> make_deque(const int count)
{
    auto result = ft::persistent_deque<int>{};
    for (auto value = 0; value != count; ++value) {
        result = result.push_back(value);
    }

    return result;
}

void add_persistent_deque_tests_impl(suite& tests)
{
    tests.add("persistent deque constructs from initializer list and preserves endpoints", [] {
        const auto deque = ft::persistent_deque<int>{1, 2, 3, 4};

        FT_REQUIRE_EQUAL(deque.size(), static_cast<std::size_t>(4));
        FT_REQUIRE(!deque.empty());
        FT_REQUIRE_EQUAL(deque.front(), 1);
        FT_REQUIRE_EQUAL(deque.back(), 4);
        FT_REQUIRE_EQUAL(*deque.try_front(), 1);
        FT_REQUIRE_EQUAL(*deque.try_back(), 4);
        require_sequence_equal(deque, {1, 2, 3, 4});
    });

    tests.add("persistent deque endpoint operations keep old versions alive", [] {
        auto empty = ft::persistent_deque<std::string>{};
        auto one = empty.push_back("middle");
        auto two = one.push_front("left");
        auto three = two.push_back("right");

        require_sequence_equal(empty, {});
        require_sequence_equal(one, {"middle"});
        require_sequence_equal(two, {"left", "middle"});
        require_sequence_equal(three, {"left", "middle", "right"});

        const auto front = three.pop_first();
        FT_REQUIRE_EQUAL(front.value, std::string{"left"});
        require_sequence_equal(front.rest, {"middle", "right"});

        const auto back = three.pop_last();
        FT_REQUIRE_EQUAL(back.value, std::string{"right"});
        require_sequence_equal(back.rest, {"left", "middle"});
        require_sequence_equal(three, {"left", "middle", "right"});
    });

    tests.add("persistent deque deep rebalance helpers size middle before move", [] {
        const auto prefix = detail::deque_digit<int>{deque_leaf(1)};
        const auto suffix = detail::deque_digit<int>{deque_leaf(4)};
        const auto expected = std::vector<int>{1, 2, 3, 4};

        const auto from_left = detail::deque_deep_left(prefix, deque_middle_node_tree(2, 3), suffix);
        FT_REQUIRE_EQUAL(from_left.size(), expected.size());
        FT_REQUIRE_EQUAL(from_left.validate_and_count(), expected.size());

        auto left_values = std::vector<int>{};
        from_left.copy_leaves_to(left_values);
        FT_REQUIRE(left_values == expected);

        const auto from_right = detail::deque_deep_right(prefix, deque_middle_node_tree(2, 3), suffix);
        FT_REQUIRE_EQUAL(from_right.size(), expected.size());
        FT_REQUIRE_EQUAL(from_right.validate_and_count(), expected.size());

        auto right_values = std::vector<int>{};
        from_right.copy_leaves_to(right_values);
        FT_REQUIRE(right_values == expected);
    });

    tests.add("persistent deque throws and returns null optional values on empty reads", [] {
        const auto deque = ft::persistent_deque<int>{};

        FT_REQUIRE(deque.try_front() == nullptr);
        FT_REQUIRE(deque.try_back() == nullptr);
        FT_REQUIRE(deque.try_get(0) == nullptr);
        FT_REQUIRE(!deque.try_pop_first().has_value());
        FT_REQUIRE(!deque.try_pop_last().has_value());
        FT_REQUIRE_THROWS(std::logic_error, deque.front());
        FT_REQUIRE_THROWS(std::logic_error, deque.back());
        FT_REQUIRE_THROWS(std::logic_error, deque.remove_first());
        FT_REQUIRE_THROWS(std::logic_error, deque.remove_last());
        FT_REQUIRE_THROWS(std::out_of_range, deque.at(0));
    });

    tests.add("persistent deque indexed access set and update match vector model", [] {
        auto deque = make_deque(80);
        auto expected = std::vector<int>{};
        for (auto value = 0; value != 80; ++value) {
            expected.push_back(value);
        }

        for (std::size_t index = 0; index != expected.size(); ++index) {
            FT_REQUIRE_EQUAL(deque[index], expected[index]);
            FT_REQUIRE_EQUAL(*deque.try_get(index), expected[index]);
        }

        auto changed = deque.set_item(0, -10).set_item(79, -20).update_at(40, [](const int value) {
            return value * 10;
        });
        expected[0] = -10;
        expected[79] = -20;
        expected[40] *= 10;

        require_sequence_equal(changed, expected);
        require_sequence_equal(deque, vector_range(std::vector<int>{
                                      0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                      10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
                                      20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                                      30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                      40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
                                      50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
                                      60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
                                      70, 71, 72, 73, 74, 75, 76, 77, 78, 79},
                                      0,
                                      80));
    });

    tests.add("persistent deque insert remove range and split reconstruct", [] {
        auto deque = make_deque(20);
        auto expected = std::vector<int>{};
        for (auto value = 0; value != 20; ++value) {
            expected.push_back(value);
        }

        deque = deque.insert_at(0, -1).insert_at(10, 99);
        deque = deque.insert_at(deque.size(), 100);
        expected.insert(expected.begin(), -1);
        expected.insert(expected.begin() + 10, 99);
        expected.push_back(100);
        require_sequence_equal(deque, expected);

        deque = deque.remove_at(10).remove_range(1, 3);
        expected.erase(expected.begin() + 10);
        expected.erase(expected.begin() + 1, expected.begin() + 4);
        require_sequence_equal(deque, expected);

        const auto middle = deque.get_range(3, 7);
        require_sequence_equal(middle, vector_range(expected, 3, 7));

        const auto split = deque.split_at(5);
        require_sequence_equal(split.left, vector_range(expected, 0, 5));
        require_sequence_equal(split.right, vector_range(expected, 5, expected.size() - 5));
        require_sequence_equal(split.left.concat(split.right), expected);

        const auto item_split = deque.split_item_at(6);
        FT_REQUIRE_EQUAL(item_split.item, expected[6]);
        require_sequence_equal(item_split.left, vector_range(expected, 0, 6));
        require_sequence_equal(item_split.right, vector_range(expected, 7, expected.size() - 7));

        const auto range_split = deque.split_range(2, 4);
        require_sequence_equal(range_split.before, vector_range(expected, 0, 2));
        require_sequence_equal(range_split.range, vector_range(expected, 2, 4));
        require_sequence_equal(range_split.after, vector_range(expected, 6, expected.size() - 6));
    });

    tests.add("persistent deque concat and insert range share values in order", [] {
        const auto left = ft::persistent_deque<int>{1, 2, 3};
        const auto right = ft::persistent_deque<int>{7, 8, 9};
        const auto middle = std::vector<int>{4, 5, 6};

        auto combined = left.concat(right).insert_range(3, middle);
        require_sequence_equal(combined, {1, 2, 3, 4, 5, 6, 7, 8, 9});
        require_sequence_equal(left, {1, 2, 3});
        require_sequence_equal(right, {7, 8, 9});

        const auto appended = left.add_range(middle).add_range(right);
        require_sequence_equal(appended, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    });

    tests.add("persistent deque iterator and copy_to enumerate left to right", [] {
        const auto deque = ft::persistent_deque<int>{3, 1, 4, 1, 5, 9, 2, 6};
        auto iterated = std::vector<int>{};
        for (const auto value : deque) {
            iterated.push_back(value);
        }

        require_sequence_equal(deque, iterated);

        auto copied = std::vector<int>{};
        deque.copy_to(std::back_inserter(copied));
        FT_REQUIRE(iterated == copied);
    });

    tests.add("persistent deque hot endpoint and index reads do not allocate", [] {
        const auto deque = make_deque(4096);
        FT_REQUIRE_EQUAL(deque.front(), 0);
        FT_REQUIRE_EQUAL(deque.back(), 4095);
        FT_REQUIRE_EQUAL(deque[2048], 2048);

        auto sink = std::size_t{0};
        allocation_counting_scope allocations;
        for (auto iteration = 0; iteration != 2000; ++iteration) {
            sink += static_cast<std::size_t>(deque.front());
            sink += static_cast<std::size_t>(deque.back());
            sink += static_cast<std::size_t>(deque[2048]);
        }

        FT_REQUIRE(sink > 0);
        FT_REQUIRE_EQUAL(allocations.allocations(), static_cast<std::size_t>(0));
    });

    tests.add("persistent deque branch push marginal allocation is size independent", [] {
        const auto small = make_deque(1024);
        const auto large = make_deque(32768);

        const auto push_allocations = [](const ft::persistent_deque<int>& deque) {
            allocation_counting_scope allocations;
            const auto branch = deque.push_back(42);
            FT_REQUIRE_EQUAL(branch.size(), deque.size() + 1);
            FT_REQUIRE_EQUAL(branch.back(), 42);
            return allocations.allocations();
        };

        FT_REQUIRE_EQUAL(push_allocations(small), push_allocations(large));
    });

    tests.add("persistent deque iterator equality tracks positions under structural sharing", [] {
        const auto deque = ft::persistent_deque<int>{1, 2, 3};
        const auto doubled = deque.concat(deque);

        // Positions 0 and 3 dereference the same shared leaf object; the
        // iterators must still compare unequal.
        auto first = doubled.begin();
        auto second = doubled.begin();
        std::advance(second, 3);
        FT_REQUIRE_EQUAL(*first, *second);
        FT_REQUIRE(first != second);

        auto same = doubled.begin();
        FT_REQUIRE(first == same);
        ++first;
        ++same;
        FT_REQUIRE(first == same);

        const auto independent = ft::persistent_deque<int>{1, 2, 3, 1, 2, 3};
        FT_REQUIRE(doubled.begin() != independent.begin());
    });

    tests.add("persistent deque forward iterator is multipass lifetime safe and allocation free after construction", [] {
        const auto deque = make_deque(4'096);
        auto first = deque.begin();
        auto same = first;
        FT_REQUIRE(first == same);
        FT_REQUIRE_EQUAL(*first, 0);
        ++first;
        FT_REQUIRE(first != same);
        FT_REQUIRE_EQUAL(*first, 1);
        ++same;
        FT_REQUIRE(first == same);

        auto surviving = [] {
            return make_deque(4'096).begin();
        }();
        auto expected = 0;
        while (surviving != ft::persistent_deque<int>::const_iterator{}) {
            FT_REQUIRE_EQUAL(*surviving, expected);
            ++surviving;
            ++expected;
        }
        FT_REQUIRE_EQUAL(expected, 4'096);

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        // Force delayed middle nodes before isolating iterator bookkeeping.
        const auto warmed = deque.to_vector();
        FT_REQUIRE_EQUAL(warmed.size(), deque.size());
        auto iterator = deque.begin();
        const auto end = deque.end();
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

        auto copied = std::vector<int>(deque.size());
        auto copy_allocations = std::size_t{0};
        {
            allocation_counting_scope allocations;
            deque.copy_to(copied.begin());
            copy_allocations = allocations.allocations();
        }
        FT_REQUIRE(copied == warmed);
        // copy_to owns logarithmic traversal state, never a sequence-sized
        // materialization buffer. MSVC Debug also counts checked-iterator
        // vector proxies for the internally constructed begin/end cursors.
        FT_REQUIRE(copy_allocations <= 2 * deque.tree_depth() + 3);
#endif
    });

    tests.add("persistent deque result carriers compare by public value and generic invariants need no equality", [] {
        const auto first = make_deque(32);
        const auto second = ft::persistent_deque<int>::from_range(first);

        FT_REQUIRE(first.split_at(12) == second.split_at(12));
        FT_REQUIRE(first.split_at(12) != second.split_at(13));
        FT_REQUIRE(first.split_item_at(12) == second.split_item_at(12));
        FT_REQUIRE(first.split_range(7, 11) == second.split_range(7, 11));
        FT_REQUIRE(first.pop_first() == second.pop_first());
        FT_REQUIRE(first.pop_last() == second.pop_last());

        const auto generic = ft::persistent_deque<non_equality_value>::from_range(
            std::vector<non_equality_value>{{1}, {2}, {3}, {4}});
        generic.validate_invariants();
        FT_REQUIRE_EQUAL(generic.size(), static_cast<std::size_t>(4));
        FT_REQUIRE_EQUAL(generic.front().value, 1);
        FT_REQUIRE_EQUAL(generic.back().value, 4);
    });

    tests.add("persistent deque sorted search follows lower and upper bound semantics", [] {
        const auto deque = ft::persistent_deque<int>{1, 3, 3, 3, 7, 9, 9, 12};

        for (const auto key : {0, 1, 2, 3, 4, 9, 12, 13}) {
            const auto values = deque.to_vector();
            const auto lower = static_cast<std::size_t>(std::ranges::lower_bound(values, key) - values.begin());
            const auto upper = static_cast<std::size_t>(std::ranges::upper_bound(values, key) - values.begin());

            FT_REQUIRE_EQUAL(deque.sorted_lower_bound(key), lower);
            FT_REQUIRE_EQUAL(deque.sorted_upper_bound(key), upper);

            const auto search = deque.sorted_binary_search(key);
            if (lower != values.size() && values[lower] == key) {
                FT_REQUIRE_EQUAL(search, static_cast<std::ptrdiff_t>(lower));
                FT_REQUIRE(deque.sorted_contains(key));
            } else {
                FT_REQUIRE_EQUAL(search, ~static_cast<std::ptrdiff_t>(lower));
                FT_REQUIRE(!deque.sorted_contains(key));
            }

            const auto lower_split = deque.split_at_sorted_lower_bound(key);
            require_sequence_equal(lower_split.left, vector_range(values, 0, lower));
            require_sequence_equal(lower_split.right, vector_range(values, lower, values.size() - lower));

            const auto equal_split = deque.split_at_sorted_equal_range(key);
            require_sequence_equal(equal_split.before, vector_range(values, 0, lower));
            require_sequence_equal(equal_split.range, vector_range(values, lower, upper - lower));
            require_sequence_equal(equal_split.after, vector_range(values, upper, values.size() - upper));
        }

        require_sequence_equal(deque.insert_sorted(3), {1, 3, 3, 3, 3, 7, 9, 9, 12});
        require_sequence_equal(deque.remove_all_sorted(3), {1, 7, 9, 9, 12});
    });

    tests.add("persistent deque sorted search uses signposts to bound comparer calls", [] {
        auto deque = ft::persistent_deque<int>{};
        for (auto value = 0; value != 512; ++value) {
            deque = deque.push_back(value * 2);
        }

        operation_counter counter;
        const auto lower = deque.sorted_lower_bound(777, counting_compare<>{counter});
        FT_REQUIRE_EQUAL(lower, static_cast<std::size_t>(389));
        FT_REQUIRE(counter.comparisons() < 80);
    });

    tests.add("persistent deque randomized model replay covers branching versions", [] {
        auto rng = deterministic_rng{0x5eed1234};
        auto deque = ft::persistent_deque<int>{};
        auto model = std::vector<int>{};
        auto retained = std::vector<std::pair<ft::persistent_deque<int>, std::vector<int>>>{};
        retained.push_back({deque, model});

        constexpr auto max_model_size = std::size_t{512};

        for (auto step = 0; step != 700; ++step) {
            const auto operation = rng.next_index(10);
            switch (operation) {
            case 0: {
                if (model.size() >= max_model_size) {
                    break;
                }

                const auto value = rng.next_int(-1000, 1000);
                deque = deque.push_front(value);
                model.insert(model.begin(), value);
                break;
            }
            case 1: {
                if (model.size() >= max_model_size) {
                    break;
                }

                const auto value = rng.next_int(-1000, 1000);
                deque = deque.push_back(value);
                model.push_back(value);
                break;
            }
            case 2:
                if (!model.empty()) {
                    const auto pop = deque.pop_first();
                    FT_REQUIRE_EQUAL(pop.value, model.front());
                    deque = pop.rest;
                    model.erase(model.begin());
                }
                break;
            case 3:
                if (!model.empty()) {
                    const auto pop = deque.pop_last();
                    FT_REQUIRE_EQUAL(pop.value, model.back());
                    deque = pop.rest;
                    model.pop_back();
                }
                break;
            case 4:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    const auto value = rng.next_int(-1000, 1000);
                    deque = deque.set_item(index, value);
                    model[index] = value;
                }
                break;
            case 5: {
                if (model.size() >= max_model_size) {
                    break;
                }

                const auto index = rng.next_index(model.size() + 1);
                const auto value = rng.next_int(-1000, 1000);
                deque = deque.insert_at(index, value);
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), value);
                break;
            }
            case 6:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    deque = deque.remove_at(index);
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
                }
                break;
            case 7:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    const auto split = deque.split_at(index);
                    require_sequence_equal(split.left, vector_range(model, 0, index));
                    require_sequence_equal(split.right, vector_range(model, index, model.size() - index));
                    require_sequence_equal(split.left.concat(split.right), model);
                }
                break;
            case 8:
                if (!retained.empty()) {
                    const auto selected = rng.next_index(retained.size());
                    if (retained[selected].second.size() + model.size() > max_model_size) {
                        break;
                    }

                    const auto suffix = deque;
                    const auto suffix_model = model;
                    deque = retained[selected].first.concat(suffix);
                    model = retained[selected].second;
                    model.insert(model.end(), suffix_model.begin(), suffix_model.end());
                }
                break;
            default:
                retained.push_back({deque, model});
                if (retained.size() > 32) {
                    retained.erase(retained.begin());
                }
                break;
            }

            require_sequence_equal(deque, model);

            if (!model.empty()) {
                const auto index = rng.next_index(model.size());
                FT_REQUIRE_EQUAL(deque[index], model[index]);
                FT_REQUIRE_EQUAL(deque.front(), model.front());
                FT_REQUIRE_EQUAL(deque.back(), model.back());
            }
        }

        for (const auto& [version, expected] : retained) {
            require_sequence_equal(version, expected);
        }
    });
}

} // namespace

void add_persistent_deque_tests(suite& tests)
{
    add_persistent_deque_tests_impl(tests);
}
