#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

using size_tree = ft::finger_tree<int, ft::size_measure<int>>;

static_assert(std::forward_iterator<size_tree::const_iterator>);
static_assert(std::ranges::forward_range<const size_tree>);

struct non_equality_value final {
    int value;
};

template <class T>
concept has_equality = requires(const T& left, const T& right) {
    { left == right } -> std::convertible_to<bool>;
};

using non_equality_tree = ft::finger_tree<non_equality_value, ft::size_measure<non_equality_value>>;
static_assert(std::ranges::forward_range<const non_equality_tree>);
static_assert(!has_equality<ft::finger_tree_split<non_equality_value, ft::size_measure<non_equality_value>>>);
static_assert(!has_equality<ft::finger_tree_item_split<non_equality_value, ft::size_measure<non_equality_value>>>);
static_assert(!has_equality<ft::finger_tree_extract_result<non_equality_value, ft::size_measure<non_equality_value>>>);
static_assert(!has_equality<ft::finger_tree_locate_reference_result<int, ft::size_measure<int>>>);

struct int_max_measure {
    using measure_type = int;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return (std::numeric_limits<int>::min)();
    }

    [[nodiscard]] static constexpr measure_type measure(const int element) noexcept
    {
        return element;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return (std::max)(left, right);
    }
};

struct count_and_last_key final {
    std::size_t count = 0;
    int last_key = (std::numeric_limits<int>::min)();

    [[nodiscard]] friend bool operator==(const count_and_last_key&, const count_and_last_key&) = default;
};

struct count_last_key_measure {
    using measure_type = count_and_last_key;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return {};
    }

    [[nodiscard]] static constexpr measure_type measure(const int element) noexcept
    {
        return {1, element};
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return {
            left.count + right.count,
            right.count == 0 ? left.last_key : right.last_key};
    }
};

template <class T>
void require_vector_equal(
    const std::vector<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    if (actual == expected) {
        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": vector mismatch. Actual [";
    for (std::size_t index = 0; index != actual.size(); ++index) {
        if (index != 0) {
            message << ", ";
        }

        message << actual[index];
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

[[nodiscard]] std::vector<int> iota_vector(const int count, const int start = 0)
{
    auto result = std::vector<int>{};
    result.reserve(static_cast<std::size_t>(count));
    for (auto value = 0; value != count; ++value) {
        result.push_back(start + value);
    }

    return result;
}

void add_measured_finger_tree_tests_impl(suite& tests)
{
    tests.add("measured finger tree empty reports identity and degenerate operations", [] {
        const auto tree = ft::finger_tree<int, ft::size_measure<int>>{};

        FT_REQUIRE(tree.empty());
        FT_REQUIRE_EQUAL(tree.measure(), static_cast<std::size_t>(0));
        FT_REQUIRE_THROWS(std::logic_error, tree.front());
        FT_REQUIRE_THROWS(std::logic_error, tree.back());
        FT_REQUIRE(!tree.try_view_left().has_value());
        FT_REQUIRE(!tree.try_view_right().has_value());

        const auto split = tree.split([](const std::size_t measure) {
            return measure > 0;
        });
        FT_REQUIRE(split.left.empty());
        FT_REQUIRE(split.right.empty());
        FT_REQUIRE(!tree.try_split_find([](const std::size_t measure) { return measure > 0; }).has_value());
        const auto located = tree.try_locate([](const std::size_t measure) { return measure > 0; });
        FT_REQUIRE(!located.item.has_value());
        FT_REQUIRE_EQUAL(located.measure_before, static_cast<std::size_t>(0));
    });

    tests.add("measured finger tree size measure tracks endpoint construction", [] {
        auto tree = ft::finger_tree<int, ft::size_measure<int>>{};
        auto model = std::vector<int>{};

        for (auto value = 0; value != 120; ++value) {
            if (value % 2 == 0) {
                tree = tree.append(value);
                model.push_back(value);
            } else {
                tree = tree.prepend(value);
                model.insert(model.begin(), value);
            }
        }

        FT_REQUIRE_EQUAL(tree.measure(), model.size());
        FT_REQUIRE_EQUAL(tree.front(), model.front());
        FT_REQUIRE_EQUAL(tree.back(), model.back());
        require_vector_equal(tree.to_vector(), model);
    });

    tests.add("measured finger tree size split matches list slices", [] {
        for (auto size = 0; size <= 96; ++size) {
            const auto values = iota_vector(size);
            const auto tree = ft::finger_tree<int, ft::size_measure<int>>::from_range(values);
            require_vector_equal(tree.to_vector(), values);

            for (auto index = 0; index <= size; ++index) {
                const auto split = tree.split([index](const std::size_t measured) {
                    return measured > static_cast<std::size_t>(index);
                });
                require_vector_equal(split.left.to_vector(), vector_range(values, 0, static_cast<std::size_t>(index)));
                require_vector_equal(
                    split.right.to_vector(),
                    vector_range(values, static_cast<std::size_t>(index), values.size() - static_cast<std::size_t>(index)));
                require_vector_equal(split.left.concat(split.right).to_vector(), values);
            }
        }
    });

    tests.add("measured finger tree concat preserves order measure and associativity", [] {
        constexpr int sizes[] = {0, 1, 2, 5, 13, 40};
        for (const auto a : sizes) {
            for (const auto b : sizes) {
                for (const auto c : sizes) {
                    const auto va = iota_vector(a, 0);
                    const auto vb = iota_vector(b, 100);
                    const auto vc = iota_vector(c, 200);

                    const auto ta = ft::finger_tree<int, ft::size_measure<int>>::from_range(va);
                    const auto tb = ft::finger_tree<int, ft::size_measure<int>>::from_range(vb);
                    const auto tc = ft::finger_tree<int, ft::size_measure<int>>::from_range(vc);

                    auto expected = va;
                    expected.insert(expected.end(), vb.begin(), vb.end());
                    expected.insert(expected.end(), vc.begin(), vc.end());

                    const auto left = ta.concat(tb).concat(tc);
                    const auto right = ta.concat(tb.concat(tc));
                    require_vector_equal(left.to_vector(), expected);
                    require_vector_equal(right.to_vector(), expected);
                    FT_REQUIRE_EQUAL(left.measure(), expected.size());
                }
            }
        }
    });

    tests.add("measured finger tree max measure acts as mergeable priority queue", [] {
        auto values = std::vector<int>{3, 9, 1, 9, 4, 7, 2, 10, 10, 5};
        auto queue = ft::finger_tree<int, int_max_measure>::from_range(values);
        FT_REQUIRE_EQUAL(queue.measure(), 10);

        auto extracted = std::vector<int>{};
        while (!queue.empty()) {
            const auto max = queue.measure();
            auto split = queue.try_split_find([max](const int measure) {
                return measure >= max;
            });
            FT_REQUIRE(split.has_value());
            FT_REQUIRE_EQUAL(split->item, max);
            extracted.push_back(split->item);
            queue = split->left.concat(split->right);
        }

        std::ranges::sort(values, std::greater<>{});
        require_vector_equal(extracted, values);
    });

    tests.add("measured finger tree product-like measure supports indexing and lower-bound search", [] {
        const auto values = [] {
            auto result = std::vector<int>{};
            for (auto value = 0; value != 150; ++value) {
                result.push_back(value * 2);
            }

            return result;
        }();
        const auto tree = ft::finger_tree<int, count_last_key_measure>::from_range(values);

        FT_REQUIRE_EQUAL(tree.measure().count, values.size());
        FT_REQUIRE_EQUAL(tree.measure().last_key, values.back());

        const auto first_ten = tree.split([](const count_and_last_key& measure) {
            return measure.count > 10;
        });
        require_vector_equal(first_ten.left.to_vector(), vector_range(values, 0, 10));
        require_vector_equal(first_ten.right.to_vector(), vector_range(values, 10, values.size() - 10));

        for (const auto target : {-5, 0, 1, 199, 200, 298, 299, 300}) {
            const auto lower = static_cast<std::size_t>(
                std::ranges::lower_bound(values, target) - values.begin());
            const auto split = tree.split([target](const count_and_last_key& measure) {
                return measure.count != 0 && measure.last_key >= target;
            });
            require_vector_equal(split.left.to_vector(), vector_range(values, 0, lower));
            require_vector_equal(split.right.to_vector(), vector_range(values, lower, values.size() - lower));
        }
    });

    tests.add("measured finger tree locate matches split-find at every count threshold", [] {
        for (auto size = 0; size <= 80; ++size) {
            const auto values = iota_vector(size);
            const auto tree = ft::finger_tree<int, ft::size_measure<int>>::from_range(values);

            for (auto threshold = -1; threshold <= size + 1; ++threshold) {
                auto predicate = [threshold](const std::size_t measure) {
                    return measure > static_cast<std::size_t>(threshold);
                };

                const auto located = tree.try_locate(predicate);
                const auto split = tree.try_split_find(predicate);
                FT_REQUIRE_EQUAL(located.item.has_value(), split.has_value());
                if (split.has_value()) {
                    FT_REQUIRE_EQUAL(*located.item, split->item);
                    FT_REQUIRE_EQUAL(located.measure_before, split->left.measure());
                } else {
                    FT_REQUIRE_EQUAL(located.measure_before, tree.measure());
                }
            }
        }
    });

    tests.add("measured finger tree locate reference follows persistent node lifetime", [] {
        using tree_type = ft::finger_tree<int, ft::size_measure<int>>;
        auto snapshot = tree_type{};
        const int* located_item = nullptr;

        {
            const auto source = tree_type::from_range(iota_vector(4'096));
            snapshot = source.append(4'096);

            const auto located = source.try_locate_reference([](const std::size_t count) {
                return count > 1'024;
            });
            FT_REQUIRE(located.has_value());
            FT_REQUIRE_EQUAL(located.measure_before, static_cast<std::size_t>(1'024));
            FT_REQUIRE_EQUAL(*located.item, 1'024);
            located_item = located.item;
        }

        const auto snapshot_located = snapshot.try_locate_reference([](const std::size_t count) {
            return count > 1'024;
        });
        FT_REQUIRE(snapshot_located.has_value());
        FT_REQUIRE(snapshot_located.item == located_item);
        FT_REQUIRE_EQUAL(*located_item, 1'024);

        const auto miss = snapshot.try_locate_reference([](const std::size_t count) {
            return count > 10'000;
        });
        FT_REQUIRE(!miss.has_value());
        FT_REQUIRE_EQUAL(miss.measure_before, snapshot.measure());
    });

    tests.add("measured finger tree locate miss reports whole non-group measure", [] {
        const auto values = iota_vector(64, 10);
        const auto tree = ft::finger_tree<int, count_last_key_measure>::from_range(values);

        const auto below = tree.try_locate([](const count_and_last_key& measure) {
            return measure.last_key >= 1000;
        });
        FT_REQUIRE(!below.item.has_value());
        FT_REQUIRE(below.measure_before == tree.measure());

        const auto empty = ft::finger_tree<int, count_last_key_measure>{};
        const auto empty_located = empty.try_locate([](const count_and_last_key& measure) {
            return measure.last_key >= 0;
        });
        FT_REQUIRE(!empty_located.item.has_value());
        FT_REQUIRE(empty_located.measure_before == count_last_key_measure::empty());
    });

    tests.add("measured finger tree hot reads do not allocate after first force", [] {
        const auto values = iota_vector(4096);
        const auto tree = ft::finger_tree<int, ft::size_measure<int>>::from_range(values);

        FT_REQUIRE_EQUAL(tree.measure(), values.size());
        FT_REQUIRE_EQUAL(tree.front(), values.front());
        FT_REQUIRE_EQUAL(tree.back(), values.back());

        auto sink = std::size_t{0};
        allocation_counting_scope allocations;
        for (auto iteration = 0; iteration != 2000; ++iteration) {
            sink += tree.measure();
            sink += static_cast<std::size_t>(tree.front());
            sink += static_cast<std::size_t>(tree.back());
        }

        FT_REQUIRE(sink > 0);
        FT_REQUIRE_EQUAL(allocations.allocations(), static_cast<std::size_t>(0));
    });

    tests.add("measured finger tree forward iterator is multipass lifetime safe and allocation free after construction", [] {
        const auto values = iota_vector(4'096);
        const auto tree = size_tree::from_range(values);

        auto first = tree.begin();
        auto same = first;
        FT_REQUIRE(first == same);
        FT_REQUIRE_EQUAL(*first, values.front());
        ++first;
        FT_REQUIRE(first != same);
        FT_REQUIRE_EQUAL(*first, values[1]);
        ++same;
        FT_REQUIRE(first == same);

        const auto sharing_snapshot = tree;
        FT_REQUIRE(tree.begin() == sharing_snapshot.begin());
        const auto independently_built = size_tree::from_range(values);
        FT_REQUIRE(tree.begin() != independently_built.begin());

        auto surviving = [] {
            const auto local = size_tree::from_range(iota_vector(4'096));
            return local.begin();
        }();
        auto survived_values = std::vector<int>{};
        survived_values.reserve(values.size());
        while (surviving != size_tree::const_iterator{}) {
            survived_values.push_back(*surviving);
            ++surviving;
        }
        require_vector_equal(survived_values, values);

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        // Force lazy middle publication before measuring iterator bookkeeping.
        tree.for_each([](const int&) {});
        auto iterator = tree.begin();
        const auto end = tree.end();
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

        auto copied = std::vector<int>(values.size());
        auto copy_allocations = std::size_t{0};
        {
            allocation_counting_scope allocations;
            tree.copy_to(copied.begin());
            copy_allocations = allocations.allocations();
        }
        require_vector_equal(copied, values);
        FT_REQUIRE_EQUAL(copy_allocations, static_cast<std::size_t>(0));
#endif
    });

    tests.add("measured finger tree result carriers compare by public value", [] {
        const auto values = iota_vector(128);
        const auto first_tree = size_tree::from_range(values);
        const auto second_tree = size_tree::from_range(values);

        const auto first_split = first_tree.split([](const std::size_t count) { return count > 48; });
        const auto second_split = second_tree.split([](const std::size_t count) { return count > 48; });
        FT_REQUIRE(first_split == second_split);

        const auto different_split = second_tree.split([](const std::size_t count) { return count > 49; });
        FT_REQUIRE(first_split != different_split);

        const auto first_view = first_tree.try_view_left();
        const auto second_view = second_tree.try_view_left();
        FT_REQUIRE(first_view.has_value());
        FT_REQUIRE(second_view.has_value());
        FT_REQUIRE(*first_view == *second_view);

        const auto first_extract = ft::finger_tree_extract_result<int, ft::size_measure<int>>{7, first_tree};
        const auto equal_extract = ft::finger_tree_extract_result<int, ft::size_measure<int>>{7, second_tree};
        const auto different_extract = ft::finger_tree_extract_result<int, ft::size_measure<int>>{8, second_tree};
        FT_REQUIRE(first_extract == equal_extract);
        FT_REQUIRE(first_extract != different_extract);

        const auto generic = non_equality_tree::from_range(
            std::vector<non_equality_value>{{1}, {2}, {3}});
        FT_REQUIRE_EQUAL(generic.measure(), static_cast<std::size_t>(3));
        auto count = std::size_t{0};
        for ([[maybe_unused]] const auto& value : generic) {
            ++count;
        }
        FT_REQUIRE_EQUAL(count, static_cast<std::size_t>(3));
    });

    tests.add("measured finger tree randomized branching history matches model", [] {
        auto rng = deterministic_rng{0x600df00d};
        using tree_type = ft::finger_tree<int, ft::size_measure<int>>;

        auto versions = std::vector<std::pair<tree_type, std::vector<int>>>{{tree_type{}, {}}};

        for (auto step = 0; step != 450; ++step) {
            const auto selected = rng.next_index(versions.size());
            auto tree = versions[selected].first;
            auto model = versions[selected].second;

            switch (rng.next_index(6)) {
            case 0:
                tree = tree.prepend(step);
                model.insert(model.begin(), step);
                break;
            case 1:
                tree = tree.append(step);
                model.push_back(step);
                break;
            case 2:
                if (!model.empty()) {
                    const auto view = tree.try_view_left();
                    FT_REQUIRE(view.has_value());
                    FT_REQUIRE_EQUAL(view->item, model.front());
                    tree = view->right;
                    model.erase(model.begin());
                }
                break;
            case 3:
                if (!model.empty()) {
                    const auto view = tree.try_view_right();
                    FT_REQUIRE(view.has_value());
                    FT_REQUIRE_EQUAL(view->item, model.back());
                    tree = view->left;
                    model.pop_back();
                }
                break;
            case 4: {
                const auto index = rng.next_index(model.size() + 1);
                const auto split = tree.split([index](const std::size_t measure) {
                    return measure > index;
                });
                tree = split.left.concat(split.right);
                break;
            }
            default: {
                const auto other_index = rng.next_index(versions.size());
                if (model.size() + versions[other_index].second.size() <= 600) {
                    tree = tree.concat(versions[other_index].first);
                    model.insert(model.end(), versions[other_index].second.begin(), versions[other_index].second.end());
                }
                break;
            }
            }

            FT_REQUIRE_EQUAL(tree.measure(), model.size());
            require_vector_equal(tree.to_vector(), model);
            versions.push_back({tree, model});
            if (versions.size() > 64) {
                versions.erase(versions.begin());
            }
        }

        for (const auto& [tree, model] : versions) {
            FT_REQUIRE_EQUAL(tree.measure(), model.size());
            require_vector_equal(tree.to_vector(), model);
        }
    });
}

} // namespace

void add_measured_finger_tree_tests(suite& tests)
{
    add_measured_finger_tree_tests_impl(tests);
}
