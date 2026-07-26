/// A persistent bag of closed intervals, with overlap and containment queries.
///
/// Each subtree caches the maximum high endpoint below it, so a query visits only the subtrees that
/// can still hold a match. Every operation returns a new version and leaves its inputs valid,
/// sharing unchanged structure, so an edit copies a path rather than the whole collection.

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

/// A persistent bag of closed intervals. Each subtree's maximum high endpoint is cached, so a query
/// visits only the subtrees that can still contain a match.
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

    /// An empty tree.
    interval_tree() = default;

    /// A tree holding the listed intervals.
    interval_tree(std::initializer_list<interval_type> intervals)
    {
        auto current = interval_tree{};
        for (const auto& item : intervals) {
            current = current.insert(item);
        }

        tree_ = std::move(current.tree_);
    }

    /// A tree holding the intervals an iterator pair yields.
    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    interval_tree(Iterator first, Sentinel last)
    {
        auto current = interval_tree{};
        for (; first != last; ++first) {
            current = current.insert(*first);
        }

        tree_ = std::move(current.tree_);
    }

    /// The shared empty tree.
    [[nodiscard]] static interval_tree empty_tree()
    {
        return interval_tree{};
    }

    /// A tree holding a range's intervals, built in bulk rather than by repeated insertion.
    template <std::ranges::input_range Range>
    [[nodiscard]] static interval_tree from_range(Range&& intervals)
    {
        return interval_tree{std::ranges::begin(intervals), std::ranges::end(intervals)};
    }

    /// Whether the tree holds no intervals.
    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    /// Number of intervals in the tree.
    [[nodiscard]] size_type size() const
    {
        return tree_.measure().count;
    }

    /// A tree with the interval inserted.
    [[nodiscard]] interval_tree insert(interval_type item) const
    {
        auto split = tree_.split(last_low_at_least{item.low});
        return interval_tree{split.left.append(std::move(item)).concat(split.right)};
    }

    /// A tree with the interval inserted.
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

    /// An interval overlapping the probe, or nothing when none does.
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

    /// An interval overlapping the probe, or nothing when none does.
    [[nodiscard]] std::optional<interval_type> try_find_overlap(const value_type& low, const value_type& high) const
    {
        return try_find_overlap(interval_type{low, high});
    }

    /// An interval containing the point, or nothing when none does.
    [[nodiscard]] std::optional<interval_type> try_find_containing(const value_type& point) const
    {
        return try_find_overlap(interval_type{point, point});
    }

    /// Every interval overlapping the probe. Subtrees whose cached maximum endpoint falls short of
    /// the probe are skipped whole.
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

    /// How many stored intervals overlap the probe.
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

    /// Whether the interval is present.
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

    /// Removes the interval, reporting whether it was present.
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

    /// A tree without that interval; returns the receiver when absent.
    [[nodiscard]] interval_tree remove(const interval_type& item) const
    {
        auto removed = try_remove(item);
        return removed.has_value() ? *removed : *this;
    }

    /// Merges adjacent pieces that can be represented as one.
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

    /// Copies the intervals out into a vector, in the tree's own order.
    [[nodiscard]] std::vector<interval_type> to_vector() const
    {
        return tree_.to_vector();
    }

    /// Copies the intervals into the destination.
    template <std::output_iterator<const interval_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        tree_.copy_to(std::move(output));
    }

    /// The const iterator one past the last element.
    /// A const iterator over the intervals.
    /// The iterator one past the last element.
    /// An iterator over the intervals, in the tree's own order.
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
