/// A persistent priority queue caching the minimum-priority entry as its measure.
///
/// Peeking is a cached read and removal is a measure-directed descent. Equal priorities are served
/// in enqueue order. Every operation returns a new version and leaves its inputs valid, sharing
/// unchanged structure, so an edit copies a path rather than the whole collection.

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

template <class Element, class Priority, class Comparison = default_comparison<Priority>>
    requires static_comparison_policy<Comparison, Priority>
class priority_queue;

/// The entry a dequeue removed, together with the queue remaining.
template <class Element, class Priority, class Comparison = default_comparison<Priority>>
    requires static_comparison_policy<Comparison, Priority>
struct priority_queue_dequeue final {
    Element element;
    Priority priority;
    priority_queue<Element, Priority, Comparison> rest;
};

/// A persistent priority queue caching the minimum-priority entry as its measure, so peeking is a
/// cached read and removal is a measure-directed descent.
template <class Element, class Priority, class Comparison>
    requires static_comparison_policy<Comparison, Priority>
class priority_queue final {
public:
    using element_type = Element;
    using priority_type = Priority;
    using comparison_type = Comparison;
    using entry_type = priority_entry<element_type, priority_type>;
    using measure_type = priority_measure<element_type, priority_type, comparison_type>;
    using aggregate_type = typename measure_type::measure_type;
    using tree_type = finger_tree<entry_type, measure_type>;
    using size_type = std::size_t;
    using const_iterator = typename tree_type::const_iterator;

    /// An empty queue.
    priority_queue() = default;

    /// A queue holding the listed entries.
    priority_queue(std::initializer_list<entry_type> entries)
        : tree_(tree_type::from_range(entries))
    {
    }

    /// A queue holding the entries an iterator pair yields.
    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    priority_queue(Iterator first, Sentinel last)
        : tree_(tree_type{std::move(first), std::move(last)})
    {
    }

    /// The shared empty queue.
    [[nodiscard]] static priority_queue empty_queue()
    {
        return priority_queue{};
    }

    /// A queue holding a range's entries, built in bulk rather than by repeated insertion.
    template <std::ranges::input_range Range>
    [[nodiscard]] static priority_queue from_range(Range&& entries)
    {
        return priority_queue{std::ranges::begin(entries), std::ranges::end(entries)};
    }

    /// Whether the queue holds no entries.
    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    /// Number of entries in the queue.
    [[nodiscard]] size_type size() const
    {
        return tree_.measure().count;
    }

    /// A queue with the entry added.
    [[nodiscard]] priority_queue enqueue(element_type element, priority_type priority) const
    {
        return priority_queue{tree_.append(entry_type{std::move(element), std::move(priority)})};
    }

    /// The minimum priority present, or nothing when empty.
    [[nodiscard]] std::optional<priority_type> try_peek_priority() const
    {
        const auto min = tree_.measure().min;
        if (!min.has_value()) {
            return std::nullopt;
        }

        return min.value();
    }

    /// Reads the next entry without removing it, or nothing when empty.
    [[nodiscard]] std::optional<entry_type> try_peek() const
    {
        return try_find_front();
    }

    /// Removes the front entry, or nothing when empty.
    [[nodiscard]] std::optional<priority_queue_dequeue<element_type, priority_type, comparison_type>> try_dequeue() const
    {
        const auto min = tree_.measure().min;
        if (!min.has_value()) {
            return std::nullopt;
        }

        auto split = tree_.try_split_find(front_predicate{min.value()});
        if (!split.has_value()) {
            throw std::logic_error("priority queue measure indicated a front entry but split did not find it");
        }

        return priority_queue_dequeue<element_type, priority_type, comparison_type>{
            split->item.element,
            split->item.priority,
            priority_queue{split->left.concat(split->right)}};
    }

    /// The queue holding both operands' entries.
    [[nodiscard]] priority_queue meld(const priority_queue& other) const
    {
        if (other.empty()) {
            return *this;
        }

        if (empty()) {
            return other;
        }

        return priority_queue{tree_.concat(other.tree_)};
    }

    /// Copies the entries out into a vector, in the queue's own order.
    [[nodiscard]] std::vector<entry_type> to_vector() const
    {
        return tree_.to_vector();
    }

    /// Copies the entries into the destination.
    template <std::output_iterator<const entry_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        tree_.copy_to(std::move(output));
    }

    /// The const iterator one past the last element.
    /// A const iterator over the entries.
    /// The iterator one past the last element.
    /// An iterator over the entries, in the queue's own order.
    [[nodiscard]] const_iterator begin() const { return tree_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return tree_.end(); }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct front_predicate final {
        priority_type target;

        [[nodiscard]] bool operator()(const aggregate_type& measure) const
        {
            return measure.min.has_value() && comparison_type::compare(measure.min.value(), target) <= 0;
        }
    };

    explicit priority_queue(tree_type tree)
        : tree_(std::move(tree))
    {
    }

    [[nodiscard]] std::optional<entry_type> try_find_front() const
    {
        const auto min = tree_.measure().min;
        if (!min.has_value()) {
            return std::nullopt;
        }

        auto located = tree_.try_locate(front_predicate{min.value()});
        if (!located.item.has_value()) {
            throw std::logic_error("priority queue measure indicated a front entry but locate did not find it");
        }

        return located.item;
    }

    tree_type tree_;
};

template <class Element, class Priority, class Comparison>
    requires static_comparison_policy<Comparison, Priority>
        && equality_comparable_value<Element>
        && equality_comparable_value<Priority>
[[nodiscard]] bool operator==(
    const priority_queue_dequeue<Element, Priority, Comparison>& left,
    const priority_queue_dequeue<Element, Priority, Comparison>& right)
{
    return left.element == right.element
        && left.priority == right.priority
        && detail::sequence_equal(left.rest, right.rest);
}

} // namespace durable7::finger_tree
