#pragma once

#include <tools/data_structures/finger_tree/measures.hpp>

#include <concepts>

namespace tools::data_structures::finger_tree {

template <class T>
concept addable_monoid_value =
    std::default_initializable<T>
    && requires(const T& left, const T& right) {
           { left + right } -> std::same_as<T>;
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

} // namespace tools::data_structures::finger_tree
