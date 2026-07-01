#pragma once

#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>
#include <tools/data_structures/finger_tree/product_measure.hpp>

#include <concepts>
#include <cstddef>
#include <optional>
#include <utility>

namespace tools::data_structures::finger_tree {

template <class T>
concept addable_monoid_value =
    std::default_initializable<T>
    && requires(const T& left, const T& right) {
           { left + right } -> std::same_as<T>;
       };

template <class T>
concept cumulative_weight_value =
    addable_monoid_value<T>
    && requires(const T& left, const T& right) {
           { left > right } -> std::convertible_to<bool>;
       };

template <class T>
    requires addable_monoid_value<T>
struct sum_measure {
    using element_type = T;
    using measure_type = T;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return T{};
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type& element)
    {
        return element;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return left + right;
    }
};

template <class T>
struct cumulative_weight_selection final {
    T value;
    T measure_before;

    [[nodiscard]] constexpr bool operator==(const cumulative_weight_selection&) const = default;
};

template <class T>
struct indexed_cumulative_weight_selection final {
    T value;
    std::size_t index_before = 0;
    T measure_before;

    [[nodiscard]] constexpr bool operator==(const indexed_cumulative_weight_selection&) const = default;
};

template <class T>
    requires cumulative_weight_value<T>
[[nodiscard]] finger_tree_split<T, sum_measure<T>> split_by_cumulative_weight(
    const finger_tree<T, sum_measure<T>>& tree,
    T threshold)
{
    return tree.split([threshold = std::move(threshold)](const T& sum) {
        return sum > threshold;
    });
}

template <class T>
    requires cumulative_weight_value<T>
[[nodiscard]] std::optional<cumulative_weight_selection<T>> try_select_by_cumulative_weight(
    const finger_tree<T, sum_measure<T>>& tree,
    T threshold)
{
    auto located = tree.try_locate([threshold = std::move(threshold)](const T& sum) {
        return sum > threshold;
    });
    if (!located.item.has_value()) {
        return std::nullopt;
    }

    return cumulative_weight_selection<T>{*located.item, located.measure_before};
}

template <class T>
    requires cumulative_weight_value<T>
[[nodiscard]] finger_tree_split<T, product_measure<T, size_measure<T>, sum_measure<T>>> split_by_cumulative_weight(
    const finger_tree<T, product_measure<T, size_measure<T>, sum_measure<T>>>& tree,
    T threshold)
{
    return split_by_second(tree, [threshold = std::move(threshold)](const T& sum) {
        return sum > threshold;
    });
}

template <class T>
    requires cumulative_weight_value<T>
[[nodiscard]] std::optional<indexed_cumulative_weight_selection<T>> try_select_by_cumulative_weight(
    const finger_tree<T, product_measure<T, size_measure<T>, sum_measure<T>>>& tree,
    T threshold)
{
    auto located = tree.try_locate([threshold = std::move(threshold)](
                                       const measure_pair<std::size_t, T>& measure) {
        return measure.second > threshold;
    });
    if (!located.item.has_value()) {
        return std::nullopt;
    }

    return indexed_cumulative_weight_selection<T>{
        *located.item,
        located.measure_before.first,
        located.measure_before.second};
}

} // namespace tools::data_structures::finger_tree
