/// The Monoid and MeasurePolicy concepts every measured structure is parameterized on.
///
/// A measure must be a monoid -- associative with a two-sided identity -- because the tree combines
/// cached measures in whatever grouping its shape happens to have, and a non-associative combine
/// would make the answer depend on that shape.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>

#include <concepts>
#include <cstddef>

namespace durable7::finger_tree {

/// Static policy for a monoid annotation. The concept checks the callable
/// surface; callers remain responsible for the monoid laws (`empty` is a
/// two-sided identity and `combine` is associative), deterministic results,
/// and any thread-safety required by concurrent readers. Policy exceptions are
/// propagated and are never published as cached measures.
template <class Policy>
concept monoid_policy = requires(
    const typename Policy::measure_type& left,
    const typename Policy::measure_type& right) {
    typename Policy::measure_type;
    { Policy::empty() } -> std::same_as<typename Policy::measure_type>;
    { Policy::combine(left, right) } -> std::same_as<typename Policy::measure_type>;
};

/// A monoid policy that additionally maps each stored element to its leaf
/// annotation. `measure(element)` must remain stable for the immutable value;
/// mutating aliased state observed by the policy violates tree invariants.
template <class Policy, class Element>
concept measure_policy =
    monoid_policy<Policy>
    && requires(const Element& element) {
           { Policy::measure(element) } -> std::same_as<typename Policy::measure_type>;
       };

/// Counts elements, making a measured tree indexable by position.
template <class Element>
struct size_measure {
    using element_type = Element;
    using measure_type = std::size_t;

    /// The identity: the measure of an empty tree.
    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    /// The measure of one element.
    [[nodiscard]] static constexpr measure_type measure(const element_type&) noexcept
    {
        return 1;
    }

    /// Combines two measures in order. Must be associative; it need not be commutative.
    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right)
    {
        return checked_add(left, right);
    }
};

} // namespace durable7::finger_tree
