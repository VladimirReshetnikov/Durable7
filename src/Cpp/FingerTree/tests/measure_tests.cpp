#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

struct by_length {
    [[nodiscard]] static int compare(const std::string& left, const std::string& right)
    {
        return ft::compare_with(std::less<>{}, left.size(), right.size());
    }
};

template <class Policy, class Range>
auto accumulate_measure(const Range& values)
{
    auto result = Policy::empty();
    for (const auto& value : values) {
        result = Policy::combine(result, Policy::measure(value));
    }

    return result;
}

template <class Policy, class Range, class Predicate>
std::size_t first_prefix_satisfying(const Range& values, const Predicate& predicate)
{
    auto measure = Policy::empty();
    for (std::size_t index = 0; index < values.size(); ++index) {
        measure = Policy::combine(measure, Policy::measure(values[index]));
        if (predicate(measure)) {
            return index;
        }
    }

    return values.size();
}

void add_measure_tests_impl(suite& tests)
{
    tests.add("measure concepts accept built-in policies", [] {
        static_assert(ft::measure_policy<ft::size_measure<int>, int>);
        static_assert(ft::measure_policy<ft::sum_measure<int>, int>);
        static_assert(ft::measure_policy<ft::max_measure<int>, int>);
        static_assert(ft::measure_policy<ft::min_measure<int>, int>);
        static_assert(ft::measure_policy<ft::key_measure<int>, int>);
        static_assert(ft::measure_policy<ft::order_statistic_measure<int>, int>);
        static_assert(ft::measure_policy<
                      ft::product_measure<int, ft::size_measure<int>, ft::sum_measure<int>>,
                      int>);
    });

    tests.add("optional measure mirrors C# none and some", [] {
        FT_REQUIRE(!ft::optional_measure<int>::none().has_value());
        const auto some = ft::optional_measure<std::string>::some("x");
        FT_REQUIRE(some.has_value());
        FT_REQUIRE_EQUAL(some.value(), std::string{"x"});
        FT_REQUIRE(ft::optional_measure<int>::some(1) == ft::optional_measure<int>::some(1));
    });

    tests.add("size and sum measures satisfy identity and accumulation contracts", [] {
        using size = ft::size_measure<int>;
        using sum = ft::sum_measure<int>;

        FT_REQUIRE_EQUAL(size::empty(), static_cast<std::size_t>(0));
        FT_REQUIRE_EQUAL(size::combine(size::empty(), size::measure(42)), static_cast<std::size_t>(1));

        constexpr std::array values{3, 1, 4, 1, 5};
        FT_REQUIRE_EQUAL(accumulate_measure<size>(values), values.size());
        FT_REQUIRE_EQUAL(accumulate_measure<sum>(values), std::accumulate(values.begin(), values.end(), 0));
        FT_REQUIRE_THROWS(
            std::overflow_error,
            size::combine((std::numeric_limits<std::size_t>::max)(), 1));
    });

    tests.add("comparison policies match default and reversed order", [] {
        FT_REQUIRE(ft::default_comparison<int>::compare(1, 2) < 0);
        FT_REQUIRE(ft::default_comparison<int>::compare(2, 1) > 0);
        FT_REQUIRE_EQUAL(ft::default_comparison<int>::compare(5, 5), 0);
        FT_REQUIRE(ft::reverse_comparison<int>::compare(1, 2) > 0);
        FT_REQUIRE_EQUAL(ft::reverse_comparison<int>::compare(5, 5), 0);
    });

    tests.add("max and min measures keep the earlier equal extremum", [] {
        using max_by_length = ft::max_measure<std::string, by_length>;
        using min_by_length = ft::min_measure<std::string, by_length>;

        const auto aa = ft::optional_measure<std::string>::some("aa");
        const auto bb = ft::optional_measure<std::string>::some("bb");
        const auto c = ft::optional_measure<std::string>::some("c");

        FT_REQUIRE_EQUAL(max_by_length::combine(aa, bb).value(), std::string{"aa"});
        FT_REQUIRE_EQUAL(min_by_length::combine(aa, bb).value(), std::string{"aa"});
        FT_REQUIRE_EQUAL(max_by_length::combine(c, aa).value(), std::string{"aa"});
        FT_REQUIRE_EQUAL(min_by_length::combine(c, aa).value(), std::string{"c"});
    });

    tests.add("key and order-statistic measures are right-biased on keys", [] {
        using key = ft::key_measure<int>;
        using ranked = ft::order_statistic_measure<int>;

        const auto combined_key = key::combine(key::measure(10), key::measure(20));
        FT_REQUIRE(combined_key.has_value());
        FT_REQUIRE_EQUAL(combined_key.value(), 20);

        const auto combined_ranked = ranked::combine(ranked::measure(10), ranked::measure(20));
        FT_REQUIRE_EQUAL(combined_ranked.count, static_cast<std::size_t>(2));
        FT_REQUIRE(combined_ranked.key.has_value());
        FT_REQUIRE_EQUAL(combined_ranked.key.value(), 20);
    });

    tests.add("runtime lower and upper bound predicates match binary-search models", [] {
        using ranked = ft::order_statistic_measure<int>;
        constexpr std::array sorted{1, 3, 3, 3, 7, 9, 9, 12};

        for (const auto key : {0, 1, 2, 3, 4, 9, 12, 13}) {
            const auto lower = static_cast<std::size_t>(
                std::ranges::lower_bound(sorted, key) - sorted.begin());
            const auto upper = static_cast<std::size_t>(
                std::ranges::upper_bound(sorted, key) - sorted.begin());

            FT_REQUIRE_EQUAL(
                first_prefix_satisfying<ranked>(sorted, ft::key_at_least_predicate<int>{key}),
                lower);
            FT_REQUIRE_EQUAL(
                first_prefix_satisfying<ranked>(sorted, ft::key_above_predicate<int>{key}),
                upper);
        }
    });

    tests.add("static reversed comparison searches descending order", [] {
        using ranked = ft::order_statistic_measure<int>;
        using descending = ft::reverse_comparison<int>;
        constexpr std::array values{12, 9, 9, 7, 3, 3, 3, 1};

        for (const auto key : {13, 12, 10, 9, 4, 3, 1, 0}) {
            const auto expected = std::ranges::find_if(values, [key](const int value) {
                                      return value <= key;
                                  })
                - values.begin();

            FT_REQUIRE_EQUAL(
                first_prefix_satisfying<ranked>(
                    values,
                    ft::ranked_key_at_least_predicate<int, descending>{key}),
                static_cast<std::size_t>(expected));
        }
    });

    tests.add("product measure combines components independently", [] {
        using product = ft::product_measure<int, ft::size_measure<int>, ft::sum_measure<int>>;
        constexpr std::array values{3, 1, 4, 1, 5, 9};
        const auto measure = accumulate_measure<product>(values);

        FT_REQUIRE_EQUAL(measure.first, values.size());
        FT_REQUIRE_EQUAL(measure.second, std::accumulate(values.begin(), values.end(), 0));

        const auto a = ft::measure_pair<std::size_t, int>{2, 4};
        const auto b = ft::measure_pair<std::size_t, int>{3, 8};
        const auto c = ft::measure_pair<std::size_t, int>{5, 16};
        FT_REQUIRE(product::combine(product::combine(a, b), c) == product::combine(a, product::combine(b, c)));
    });

    tests.add("named max and min operations peek and extract extrema", [] {
        auto max_tree = ft::finger_tree<int, ft::max_measure<int>>{3, 9, 1, 9, 4};
        auto min_tree = ft::finger_tree<int, ft::min_measure<int>>{3, 9, 1, 9, 4};

        const auto max = ft::try_peek_max(max_tree);
        const auto min = ft::try_peek_min(min_tree);
        FT_REQUIRE(max.has_value());
        FT_REQUIRE(min.has_value());
        FT_REQUIRE_EQUAL(*max, 9);
        FT_REQUIRE_EQUAL(*min, 1);

        auto descending = std::vector<int>{};
        while (auto extracted = ft::try_extract_max(max_tree)) {
            descending.push_back(extracted->item);
            max_tree = extracted->rest;
        }

        auto ascending = std::vector<int>{};
        while (auto extracted = ft::try_extract_min(min_tree)) {
            ascending.push_back(extracted->item);
            min_tree = extracted->rest;
        }

        FT_REQUIRE(descending == (std::vector<int>{9, 9, 4, 3, 1}));
        FT_REQUIRE(ascending == (std::vector<int>{1, 3, 4, 9, 9}));
        FT_REQUIRE(!ft::try_peek_max(max_tree).has_value());
        FT_REQUIRE(!ft::try_extract_min(min_tree).has_value());
    });

    tests.add("named key and order-statistic splits match binary-search models", [] {
        constexpr std::array sorted{1, 3, 3, 5, 8, 13};
        const auto key_tree = ft::finger_tree<int, ft::key_measure<int>>::from_range(sorted);
        const auto ranked_tree = ft::finger_tree<int, ft::order_statistic_measure<int>>::from_range(sorted);

        const auto key_lower = ft::split_by_lower_bound(key_tree, 4);
        const auto key_upper = ft::split_by_upper_bound(key_tree, 3);
        FT_REQUIRE(key_lower.left.to_vector() == (std::vector<int>{1, 3, 3}));
        FT_REQUIRE(key_lower.right.to_vector() == (std::vector<int>{5, 8, 13}));
        FT_REQUIRE(key_upper.left.to_vector() == (std::vector<int>{1, 3, 3}));
        FT_REQUIRE(key_upper.right.to_vector() == (std::vector<int>{5, 8, 13}));

        const auto ranked_index = ft::split_at_index(ranked_tree, 4);
        const auto ranked_lower = ft::split_by_lower_bound(ranked_tree, 8);
        const auto ranked_upper = ft::split_by_upper_bound(ranked_tree, 3);
        FT_REQUIRE(ranked_index.left.to_vector() == (std::vector<int>{1, 3, 3, 5}));
        FT_REQUIRE(ranked_index.right.to_vector() == (std::vector<int>{8, 13}));
        FT_REQUIRE(ranked_lower.left.to_vector() == (std::vector<int>{1, 3, 3, 5}));
        FT_REQUIRE(ranked_lower.right.to_vector() == (std::vector<int>{8, 13}));
        FT_REQUIRE(ranked_upper.left.to_vector() == (std::vector<int>{1, 3, 3}));
        FT_REQUIRE(ranked_upper.right.to_vector() == (std::vector<int>{5, 8, 13}));
    });

    tests.add("named sum operations split and select by cumulative weight", [] {
        auto weights = ft::finger_tree<int, ft::sum_measure<int>>{5, 1, 4};

        const auto split = ft::split_by_cumulative_weight(weights, 5);
        FT_REQUIRE(split.left.to_vector() == (std::vector<int>{5}));
        FT_REQUIRE(split.right.to_vector() == (std::vector<int>{1, 4}));

        const auto selected = ft::try_select_by_cumulative_weight(weights, 6);
        FT_REQUIRE(selected.has_value());
        FT_REQUIRE_EQUAL(selected->value, 4);
        FT_REQUIRE_EQUAL(selected->measure_before, 6);

        const auto missed = ft::try_select_by_cumulative_weight(weights, 10);
        FT_REQUIRE(!missed.has_value());
    });

    tests.add("named product operations project components and support size-plus helpers", [] {
        using size_sum = ft::product_measure<int, ft::size_measure<int>, ft::sum_measure<int>>;
        auto tree = ft::finger_tree<int, size_sum>{5, 1, 4, 2};

        const auto by_index = ft::split_at_index(tree, 2);
        const auto by_weight = ft::split_by_cumulative_weight(tree, 6);
        const auto by_first = ft::split_by_first(tree, [](const std::size_t count) {
            return count > 3;
        });
        const auto found_by_second = ft::try_split_find_by_second(tree, [](const int sum) {
            return sum > 6;
        });

        FT_REQUIRE(by_index.left.to_vector() == (std::vector<int>{5, 1}));
        FT_REQUIRE(by_index.right.to_vector() == (std::vector<int>{4, 2}));
        FT_REQUIRE(by_weight.left.to_vector() == (std::vector<int>{5, 1}));
        FT_REQUIRE(by_weight.right.to_vector() == (std::vector<int>{4, 2}));
        FT_REQUIRE(by_first.left.to_vector() == (std::vector<int>{5, 1, 4}));
        FT_REQUIRE(by_first.right.to_vector() == (std::vector<int>{2}));
        FT_REQUIRE(found_by_second.has_value());
        FT_REQUIRE_EQUAL(found_by_second->item, 4);

        const auto selected = ft::try_select_by_cumulative_weight(tree, 6);
        FT_REQUIRE(selected.has_value());
        FT_REQUIRE_EQUAL(selected->value, 4);
        FT_REQUIRE_EQUAL(selected->index_before, static_cast<std::size_t>(2));
        FT_REQUIRE_EQUAL(selected->measure_before, 6);

        using size_max = ft::product_measure<int, ft::size_measure<int>, ft::max_measure<int>>;
        auto max_tree = ft::finger_tree<int, size_max>{3, 9, 1, 9, 4};
        const auto peeked = ft::try_peek_max(max_tree);
        FT_REQUIRE(peeked.has_value());
        FT_REQUIRE_EQUAL(*peeked, 9);

        const auto extracted = ft::try_extract_max(max_tree);
        FT_REQUIRE(extracted.has_value());
        FT_REQUIRE_EQUAL(extracted->item, 9);
        FT_REQUIRE(extracted->rest.to_vector() == (std::vector<int>{3, 1, 9, 4}));
    });

    tests.add("component predicates project without allocation", [] {
        using pair = ft::measure_pair<std::size_t, int>;
        const auto first = ft::first_component_predicate<ft::size_above_predicate, std::size_t, int>{
            ft::size_above_predicate{2}};
        const auto second = ft::second_component_predicate<ft::sum_above_predicate<int>, std::size_t, int>{
            ft::sum_above_predicate<int>{10}};

        allocation_counting_scope allocations;
        FT_REQUIRE(first(pair{3, 9}));
        FT_REQUIRE(!first(pair{2, 100}));
        FT_REQUIRE(second(pair{1, 11}));
        FT_REQUIRE(!second(pair{4, 10}));
        FT_REQUIRE_EQUAL(allocations.allocations(), static_cast<std::size_t>(0));
    });

    tests.add("priority aggregate counts entries and keeps FIFO tie signpost", [] {
        using measure = ft::priority_measure<std::string, int>;

        const auto left = measure::measure({"first", 1});
        const auto right = measure::measure({"second", 1});
        const auto combined = measure::combine(left, right);

        FT_REQUIRE_EQUAL(combined.count, static_cast<std::size_t>(2));
        FT_REQUIRE(combined.min.has_value());
        FT_REQUIRE_EQUAL(combined.min.value(), 1);
        FT_REQUIRE(ft::priority_front_predicate<int>{1}(combined));
    });

    tests.add("interval primitive and measure match closed-interval semantics", [] {
        const auto value = ft::interval<int>{3, 8};
        FT_REQUIRE(value.contains(3));
        FT_REQUIRE(value.contains(8));
        FT_REQUIRE(value.overlaps(ft::interval<int>{8, 10}));
        FT_REQUIRE(!value.overlaps(ft::interval<int>{9, 12}));

        using measure = ft::interval_measure<int>;
        const auto combined = measure::combine(
            measure::measure(ft::interval<int>{1, 5}),
            measure::measure(ft::interval<int>{3, 8}));

        FT_REQUIRE_EQUAL(combined.count, static_cast<std::size_t>(2));
        FT_REQUIRE_EQUAL(combined.last_low.value(), 3);
        FT_REQUIRE_EQUAL(combined.max_high.value(), 8);
        FT_REQUIRE(ft::max_high_at_least_predicate<int>{6}(combined));
        FT_REQUIRE(ft::last_low_at_least_predicate<int>{3}(combined));
        FT_REQUIRE(!ft::last_low_above_predicate<int>{3}(combined));
    });

    tests.add("interval structural equality and hash support unordered containers", [] {
        std::unordered_set<ft::interval<int>> intervals;
        intervals.insert(ft::interval<int>{1, 5});
        intervals.insert(ft::interval<int>{1, 5});
        intervals.insert(ft::interval<int>{3, 8});

        FT_REQUIRE_EQUAL(intervals.size(), static_cast<std::size_t>(2));
        FT_REQUIRE(intervals.contains(ft::interval<int>{1, 5}));
        FT_REQUIRE(!intervals.contains(ft::interval<int>{5, 1}));
    });
}

} // namespace

void add_measure_tests(suite& tests)
{
    add_measure_tests_impl(tests);
}
