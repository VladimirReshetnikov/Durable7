#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <source_location>
#include <sstream>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
using namespace tools::data_structures::finger_tree::tests;

namespace {

static_assert(std::forward_iterator<ft::interval_tree<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::interval_tree<int>>);

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
    message << location.file_name() << ':' << location.line() << ": vector mismatch";
    throw test_failure(message.str());
}

[[nodiscard]] std::vector<ft::interval<int>> sorted_overlaps(
    const std::vector<ft::interval<int>>& intervals,
    const ft::interval<int>& query)
{
    auto result = std::vector<ft::interval<int>>{};
    for (const auto& interval : intervals) {
        if (interval.overlaps(query)) {
            result.push_back(interval);
        }
    }

    std::ranges::sort(result, {}, [](const auto& interval) {
        return std::pair{interval.low, interval.high};
    });
    return result;
}

struct keyed_endpoint final {
    int order;
    int id;
};

[[nodiscard]] bool operator==(const keyed_endpoint& left, const keyed_endpoint& right)
{
    return left.order == right.order && left.id == right.id;
}

struct keyed_endpoint_comparison final {
    [[nodiscard]] static int compare(const keyed_endpoint& left, const keyed_endpoint& right)
    {
        return ft::compare_with(std::less<>{}, left.order, right.order);
    }
};

void add_interval_tree_tests_impl(suite& tests)
{
    tests.add("interval primitive contains and overlaps closed endpoints", [] {
        const auto interval = ft::interval<int>{3, 8};

        FT_REQUIRE(interval.contains(3));
        FT_REQUIRE(interval.contains(8));
        FT_REQUIRE(interval.contains(5));
        FT_REQUIRE(!interval.contains(2));
        FT_REQUIRE(!interval.contains(9));

        FT_REQUIRE(interval.overlaps({8, 10}));
        FT_REQUIRE(interval.overlaps({0, 3}));
        FT_REQUIRE(interval.overlaps({4, 6}));
        FT_REQUIRE(interval.overlaps({0, 100}));
        FT_REQUIRE(!interval.overlaps({9, 12}));
        FT_REQUIRE(!interval.overlaps({-2, 2}));
    });

    tests.add("interval tree empty answers negatively", [] {
        const auto tree = ft::interval_tree<int>{};

        FT_REQUIRE(tree.empty());
        FT_REQUIRE_EQUAL(tree.size(), static_cast<std::size_t>(0));
        FT_REQUIRE(!tree.try_find_overlap({0, 10}).has_value());
        FT_REQUIRE(!tree.try_find_containing(5).has_value());
        FT_REQUIRE(tree.find_overlaps({0, 10}).empty());
        FT_REQUIRE_EQUAL(tree.count_overlaps({0, 10}), static_cast<std::size_t>(0));
        FT_REQUIRE(tree.coalesce().empty());
    });

    tests.add("interval tree insert keeps low endpoint order", [] {
        auto rng = deterministic_rng{7};
        auto tree = ft::interval_tree<int>{};
        auto intervals = std::vector<ft::interval<int>>{};

        for (auto index = 0; index != 200; ++index) {
            const auto low = rng.next_int(0, 999);
            const auto interval = ft::interval<int>{low, low + rng.next_int(0, 49)};
            tree = tree.insert(interval);
            intervals.push_back(interval);
        }

        FT_REQUIRE_EQUAL(tree.size(), intervals.size());
        const auto values = tree.to_vector();
        for (std::size_t index = 1; index < values.size(); ++index) {
            FT_REQUIRE(values[index - 1].low <= values[index].low);
        }

        auto iterated = std::vector<ft::interval<int>>{};
        for (const auto& interval : tree) {
            iterated.push_back(interval);
        }
        FT_REQUIRE(iterated == values);

        auto copied = std::vector<ft::interval<int>>{};
        tree.copy_to(std::back_inserter(copied));
        FT_REQUIRE(copied == values);
    });

    tests.add("interval tree single overlap agrees with brute force", [] {
        auto rng = deterministic_rng{202};
        auto intervals = std::vector<ft::interval<int>>{};
        for (auto index = 0; index != 150; ++index) {
            const auto low = rng.next_int(0, 500);
            intervals.push_back({low, low + rng.next_int(0, 30)});
        }

        const auto tree = ft::interval_tree<int>::from_range(intervals);
        for (auto query_index = 0; query_index != 300; ++query_index) {
            const auto low = rng.next_int(-10, 520);
            const auto query = ft::interval<int>{low, low + rng.next_int(0, 30)};
            const auto expected = std::ranges::any_of(intervals, [&](const auto& interval) {
                return interval.overlaps(query);
            });

            const auto found = tree.try_find_overlap(query);
            FT_REQUIRE_EQUAL(found.has_value(), expected);
            if (found.has_value()) {
                FT_REQUIRE(found->overlaps(query));
                FT_REQUIRE(std::ranges::find(intervals, *found) != intervals.end());
            }
        }
    });

    tests.add("interval tree all overlaps match brute force in low order", [] {
        auto rng = deterministic_rng{303};
        auto intervals = std::vector<ft::interval<int>>{};
        for (auto index = 0; index != 120; ++index) {
            const auto low = rng.next_int(0, 300);
            intervals.push_back({low, low + rng.next_int(0, 40)});
        }

        const auto tree = ft::interval_tree<int>::from_range(intervals);
        for (auto query_index = 0; query_index != 200; ++query_index) {
            const auto low = rng.next_int(-10, 320);
            const auto query = ft::interval<int>{low, low + rng.next_int(0, 40)};
            auto expected = sorted_overlaps(intervals, query);
            auto actual = tree.find_overlaps(query);
            auto sorted_actual = actual;
            std::ranges::sort(sorted_actual, {}, [](const auto& interval) {
                return std::pair{interval.low, interval.high};
            });

            require_vector_equal(sorted_actual, expected);
            FT_REQUIRE_EQUAL(tree.count_overlaps(query), expected.size());
            for (std::size_t index = 1; index < actual.size(); ++index) {
                FT_REQUIRE(actual[index - 1].low <= actual[index].low);
            }
        }
    });

    tests.add("interval tree point stabbing finds containing intervals", [] {
        const auto tree = ft::interval_tree<int>{}
            .insert(1, 5)
            .insert(10, 15)
            .insert(3, 8)
            .insert(20, 20);

        const auto a = tree.try_find_containing(4);
        FT_REQUIRE(a.has_value());
        FT_REQUIRE(a->contains(4));
        const auto b = tree.try_find_containing(20);
        FT_REQUIRE(b.has_value());
        FT_REQUIRE(*b == (ft::interval<int>{20, 20}));
        FT_REQUIRE(!tree.try_find_containing(9).has_value());
        FT_REQUIRE(!tree.try_find_containing(16).has_value());
    });

    tests.add("interval tree contains and remove handle duplicates", [] {
        const auto tree = ft::interval_tree<int>{}
            .insert(1, 5)
            .insert(3, 8)
            .insert(3, 8)
            .insert(10, 15);

        FT_REQUIRE_EQUAL(tree.size(), static_cast<std::size_t>(4));
        FT_REQUIRE(tree.contains({3, 8}));
        FT_REQUIRE(!tree.contains({3, 9}));
        FT_REQUIRE(!tree.contains({2, 5}));

        const auto once = tree.try_remove({3, 8});
        FT_REQUIRE(once.has_value());
        FT_REQUIRE_EQUAL(once->size(), static_cast<std::size_t>(3));
        FT_REQUIRE(once->contains({3, 8}));

        const auto twice = once->try_remove({3, 8});
        FT_REQUIRE(twice.has_value());
        FT_REQUIRE_EQUAL(twice->size(), static_cast<std::size_t>(2));
        FT_REQUIRE(!twice->contains({3, 8}));

        FT_REQUIRE(!tree.try_remove({0, 0}).has_value());
        require_vector_equal(tree.remove({99, 100}).to_vector(), tree.to_vector());
    });

    tests.add("interval tree coalesce matches sweep merge model", [] {
        auto rng = deterministic_rng{404};
        auto intervals = std::vector<ft::interval<int>>{};
        for (auto index = 0; index != 80; ++index) {
            const auto low = rng.next_int(0, 100);
            intervals.push_back({low, low + rng.next_int(0, 20)});
        }

        const auto tree = ft::interval_tree<int>::from_range(intervals);
        auto expected_source = intervals;
        std::ranges::sort(expected_source, {}, [](const auto& interval) {
            return std::pair{interval.low, interval.high};
        });

        auto expected = std::vector<ft::interval<int>>{};
        for (const auto& interval : expected_source) {
            if (!expected.empty() && interval.low <= expected.back().high) {
                expected.back().high = (std::max)(expected.back().high, interval.high);
            } else {
                expected.push_back(interval);
            }
        }

        const auto actual = tree.coalesce().to_vector();
        require_vector_equal(actual, expected);
        for (std::size_t index = 1; index < actual.size(); ++index) {
            FT_REQUIRE(actual[index].low > actual[index - 1].high);
        }
    });

    tests.add("interval tree comparer equality can differ from value equality", [] {
        using tree_type = ft::interval_tree<keyed_endpoint, keyed_endpoint_comparison>;
        const auto stored = ft::interval<keyed_endpoint>{{5, 1}, {10, 1}};
        const auto query = ft::interval<keyed_endpoint>{{5, 2}, {10, 2}};
        const auto tree = tree_type{}.insert(stored);

        FT_REQUIRE(!(stored == query));
        FT_REQUIRE(tree.contains(query));
        const auto removed = tree.try_remove(query);
        FT_REQUIRE(removed.has_value());
        FT_REQUIRE(removed->empty());
    });
}

} // namespace

void add_interval_tree_tests(suite& tests)
{
    add_interval_tree_tests_impl(tests);
}
