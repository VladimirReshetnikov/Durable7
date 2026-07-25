#pragma once

#include <concepts>
#include <functional>
#include <utility>

namespace durable7::finger_tree {

template <class Comparison, class T>
concept static_comparison_policy = requires(const T& left, const T& right) {
    { Comparison::compare(left, right) } -> std::convertible_to<int>;
};

template <class Less, class T, class U>
concept strict_weak_less_for = requires(Less less, const T& left, const U& right) {
    { less(left, right) } -> std::convertible_to<bool>;
    { less(right, left) } -> std::convertible_to<bool>;
};

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

template <class T>
struct default_comparison {
    [[nodiscard]] static constexpr int compare(const T& left, const T& right)
    {
        return compare_with(std::less<>{}, left, right);
    }
};

template <class T, class Less>
struct less_comparison {
    [[nodiscard]] static constexpr int compare(const T& left, const T& right)
    {
        return compare_with(Less{}, left, right);
    }
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
struct reverse_comparison {
    [[nodiscard]] static constexpr int compare(const T& left, const T& right)
    {
        return Comparison::compare(right, left);
    }
};

} // namespace durable7::finger_tree
