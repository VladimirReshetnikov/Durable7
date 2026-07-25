#pragma once

#include <durable7/finger_tree/built_in_measures.hpp>
#include <durable7/finger_tree/comparisons.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

namespace ordered_search_cursor_detail {
struct access;
}

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
    using const_iterator = typename tree_type::const_iterator;

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

    /// Returns the number of stored intervals whose low endpoint orders strictly before `low`,
    /// which is also the rank of its lower-bound gap. One O(log n) measured descent.
    [[nodiscard]] size_type count_low_less_than(const value_type& low) const
    {
        return tree_.try_locate(last_low_at_least{low}).measure_before.count;
    }

    /// Returns the number of stored intervals whose low endpoint does not order after `low`,
    /// which is also the rank of its upper-bound gap. One O(log n) measured descent.
    [[nodiscard]] size_type count_low_at_most(const value_type& low) const
    {
        return tree_.try_locate(last_low_above{low}).measure_before.count;
    }

    /// Returns the interval at a low-endpoint-order rank, or nullptr when the rank is at or
    /// past the end. One O(log n) measured descent; no endpoint comparisons are made. The
    /// reference follows this snapshot's lifetime or that of storage sharing the located node.
    [[nodiscard]] const interval_type* try_interval_at_rank(const size_type rank) const
    {
        auto located = tree_.try_locate_reference([rank](const annotation_type& annotation) {
            return annotation.count > rank;
        });
        return located.has_value() ? located.item : nullptr;
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
        auto count = size_type{0};
        auto candidate_split = tree_.split(last_low_above{query.high});
        auto candidates = candidate_split.left;

        for (;;) {
            auto split = candidates.try_split_find(max_high_at_least{query.low});
            if (!split.has_value()) {
                break;
            }

            count = checked_add(count, size_type{1});
            candidates = std::move(split->right);
        }

        return count;
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
        if (empty()) {
            return *this;
        }

        auto rebuilt = tree_type{};
        auto current = std::optional<interval_type>{};

        tree_.for_each([&rebuilt, &current](const interval_type& item) {
            if (!current.has_value()) {
                current = item;
                return;
            }

            if (comparison_type::compare(item.low, current->high) <= 0) {
                if (comparison_type::compare(item.high, current->high) > 0) {
                    current->high = item.high;
                }
            } else {
                rebuilt = rebuilt.append(std::move(*current));
                current = item;
            }
        });

        rebuilt = rebuilt.append(std::move(*current));

        return interval_tree{std::move(rebuilt)};
    }

    [[nodiscard]] std::vector<interval_type> to_vector() const
    {
        return tree_.to_vector();
    }

    template <std::output_iterator<const interval_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        tree_.copy_to(std::move(output));
    }

    [[nodiscard]] const_iterator begin() const { return tree_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return tree_.end(); }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    friend struct ordered_search_cursor_detail::access;

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

    /// Removes the exact occurrence at an order-statistic rank for the cursor layer.
    [[nodiscard]] interval_tree cursor_erase_at(const size_type rank) const
    {
        throw_if_index_out_of_range(rank, size());
        auto split = tree_.split([rank](const annotation_type& annotation) {
            return annotation.count > rank;
        });
        auto view = split.right.try_view_left();
        if (!view.has_value()) {
            throw std::logic_error("interval_tree cursor rank erase failed");
        }
        return interval_tree{split.left.concat(view->right)};
    }

    tree_type tree_;
};

} // namespace durable7::finger_tree
