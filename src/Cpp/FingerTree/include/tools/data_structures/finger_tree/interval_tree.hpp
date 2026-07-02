#pragma once

#include <tools/data_structures/finger_tree/built_in_measures.hpp>
#include <tools/data_structures/finger_tree/comparisons.hpp>
#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class interval_tree final {
public:
    using value_type = T;
    using comparison_type = Comparison;
    using interval_type = interval<value_type>;
    using measure_type = interval_measure<value_type, comparison_type>;
    using annotation_type = typename measure_type::measure_type;
    using tree_type = finger_tree<interval_type, measure_type>;
    using size_type = std::size_t;

    interval_tree() = default;

    interval_tree(std::initializer_list<interval_type> intervals)
    {
        auto current = interval_tree{};
        for (const auto& item : intervals) {
            current = current.insert(item);
        }

        tree_ = std::move(current.tree_);
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    interval_tree(Iterator first, Sentinel last)
    {
        auto current = interval_tree{};
        for (; first != last; ++first) {
            current = current.insert(*first);
        }

        tree_ = std::move(current.tree_);
    }

    [[nodiscard]] static interval_tree empty_tree()
    {
        return interval_tree{};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static interval_tree from_range(Range&& intervals)
    {
        return interval_tree{std::ranges::begin(intervals), std::ranges::end(intervals)};
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    [[nodiscard]] size_type size() const
    {
        return tree_.measure().count;
    }

    [[nodiscard]] interval_tree insert(interval_type item) const
    {
        auto split = tree_.split(last_low_at_least{item.low});
        return interval_tree{split.left.append(std::move(item)).concat(split.right)};
    }

    [[nodiscard]] interval_tree insert(value_type low, value_type high) const
    {
        return insert(interval_type{std::move(low), std::move(high)});
    }

    [[nodiscard]] std::optional<interval_type> try_find_overlap(const interval_type& query) const
    {
        auto located = tree_.try_locate(max_high_at_least{query.low});
        if (!located.item.has_value()) {
            return std::nullopt;
        }

        if (comparison_type::compare(located.item->low, query.high) <= 0) {
            return located.item;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<interval_type> try_find_overlap(const value_type& low, const value_type& high) const
    {
        return try_find_overlap(interval_type{low, high});
    }

    [[nodiscard]] std::optional<interval_type> try_find_containing(const value_type& point) const
    {
        return try_find_overlap(interval_type{point, point});
    }

    [[nodiscard]] std::vector<interval_type> find_overlaps(const interval_type& query) const
    {
        auto results = std::vector<interval_type>{};
        auto candidate_split = tree_.split(last_low_above{query.high});
        auto candidates = candidate_split.left;

        for (;;) {
            auto split = candidates.try_split_find(max_high_at_least{query.low});
            if (!split.has_value()) {
                break;
            }

            results.push_back(split->item);
            candidates = split->right;
        }

        return results;
    }

    [[nodiscard]] size_type count_overlaps(const interval_type& query) const
    {
        return find_overlaps(query).size();
    }

    [[nodiscard]] bool contains(const interval_type& item) const
    {
        auto split = tree_.split(last_low_at_least{item.low});
        auto current = split.right;

        while (auto view = current.try_view_left()) {
            if (comparison_type::compare(view->item.low, item.low) != 0) {
                break;
            }

            if (comparison_type::compare(view->item.high, item.high) == 0) {
                return true;
            }

            current = view->right;
        }

        return false;
    }

    [[nodiscard]] std::optional<interval_tree> try_remove(const interval_type& item) const
    {
        auto split = tree_.split(last_low_at_least{item.low});
        auto skipped = tree_type{};
        auto current = split.right;

        while (auto view = current.try_view_left()) {
            if (comparison_type::compare(view->item.low, item.low) != 0) {
                break;
            }

            if (comparison_type::compare(view->item.high, item.high) == 0) {
                return interval_tree{split.left.concat(skipped).concat(view->right)};
            }

            skipped = skipped.append(view->item);
            current = view->right;
        }

        return std::nullopt;
    }

    [[nodiscard]] interval_tree remove(const interval_type& item) const
    {
        auto removed = try_remove(item);
        return removed.has_value() ? *removed : *this;
    }

    [[nodiscard]] interval_tree coalesce() const
    {
        auto values = to_vector();
        if (values.empty()) {
            return *this;
        }

        auto merged = std::vector<interval_type>{};
        auto current = values.front();
        for (std::size_t index = 1; index < values.size(); ++index) {
            const auto& item = values[index];
            if (comparison_type::compare(item.low, current.high) <= 0) {
                if (comparison_type::compare(item.high, current.high) > 0) {
                    current.high = item.high;
                }
            } else {
                merged.push_back(current);
                current = item;
            }
        }

        merged.push_back(current);
        auto rebuilt = tree_type{};
        for (const auto& item : merged) {
            rebuilt = rebuilt.append(item);
        }

        return interval_tree{std::move(rebuilt)};
    }

    [[nodiscard]] std::vector<interval_type> to_vector() const
    {
        return tree_.to_vector();
    }

private:
    struct last_low_at_least final {
        value_type low;

        [[nodiscard]] bool operator()(const annotation_type& measure) const
        {
            return measure.last_low.has_value() && comparison_type::compare(measure.last_low.value(), low) >= 0;
        }
    };

    struct last_low_above final {
        value_type low;

        [[nodiscard]] bool operator()(const annotation_type& measure) const
        {
            return measure.last_low.has_value() && comparison_type::compare(measure.last_low.value(), low) > 0;
        }
    };

    struct max_high_at_least final {
        value_type low;

        [[nodiscard]] bool operator()(const annotation_type& measure) const
        {
            return measure.max_high.has_value() && comparison_type::compare(measure.max_high.value(), low) >= 0;
        }
    };

    explicit interval_tree(tree_type tree)
        : tree_(std::move(tree))
    {
    }

    tree_type tree_;
};

} // namespace tools::data_structures::finger_tree
