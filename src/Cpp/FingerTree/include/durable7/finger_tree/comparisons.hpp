/// Ordering policies and the strict_weak_less_for concept the ordered collections require.
///
/// Orderings are expressed through `<` alone and compared with compare_with, so a caller need only
/// supply a strict weak ordering rather than a three-way comparison.

#pragma once

#include <concepts>
#include <functional>
#include <utility>

namespace durable7::finger_tree {

/// A comparison policy carrying no state, so two instances are interchangeable and a compatibility
/// check must accept distinct ones.
template <class Comparison, class T>
concept static_comparison_policy = requires(const T& left, const T& right) {
    { Comparison::compare(left, right) } -> std::convertible_to<int>;
};

/// A comparison usable as a strict weak ordering over the given types. Required wherever an ordered
/// collection stores a comparison, since a comparison that is not a strict weak ordering silently
/// corrupts the ordering invariants.
template <class Less, class T, class U>
concept strict_weak_less_for = requires(Less less, const T& left, const U& right) {
    { less(left, right) } -> std::convertible_to<bool>;
    { less(right, left) } -> std::convertible_to<bool>;
};

/// Orders two values under the given comparison, returning negative, zero, or positive. Derived
/// from `<` alone, so a comparison need only be a strict weak ordering.
template <class Less, class T, class U>
[[nodiscard]] constexpr int compare_with(Less&& less, const T& left, const U& right)
    requires strict_weak_less_for<Less, T, U>
{
    if (std::invoke(less, left, right)) {
        return -1;
    }

    if (std::invoke(less, right, left)) {
        return 1;
    }

    return 0;
}

/// The default ordering, using the platform's own `<`.
template <class T>
struct default_comparison {
    /// Orders two elements.
    [[nodiscard]] static constexpr int compare(const T& left, const T& right)
    {
        return compare_with(std::less<>{}, left, right);
    }
};

/// An ordering built from `<`.
template <class T, class Less>
struct less_comparison {
    /// Orders two elements.
    [[nodiscard]] static constexpr int compare(const T& left, const T& right)
    {
        return compare_with(Less{}, left, right);
    }
};

/// An ordering that reverses another.
template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
struct reverse_comparison {
    /// Orders two elements.
    [[nodiscard]] static constexpr int compare(const T& left, const T& right)
    {
        return Comparison::compare(right, left);
    }
};

} // namespace durable7::finger_tree
