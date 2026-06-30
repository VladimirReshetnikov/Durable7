#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>

#include <concepts>
#include <cstddef>

namespace tools::data_structures::finger_tree {

template <class Policy>
concept monoid_policy = requires(
    const typename Policy::measure_type& left,
    const typename Policy::measure_type& right) {
    typename Policy::measure_type;
    { Policy::empty() } -> std::same_as<typename Policy::measure_type>;
    { Policy::combine(left, right) } -> std::same_as<typename Policy::measure_type>;
};

template <class Policy, class Element>
concept measure_policy =
    monoid_policy<Policy>
    && requires(const Element& element) {
           { Policy::measure(element) } -> std::same_as<typename Policy::measure_type>;
       };

template <class Element>
struct size_measure {
    using element_type = Element;
    using measure_type = std::size_t;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type&) noexcept
    {
        return 1;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right)
    {
        return checked_add(left, right);
    }
};

} // namespace tools::data_structures::finger_tree
