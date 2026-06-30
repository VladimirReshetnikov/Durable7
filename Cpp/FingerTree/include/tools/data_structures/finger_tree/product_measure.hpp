#pragma once

#include <tools/data_structures/finger_tree/measures.hpp>

namespace tools::data_structures::finger_tree {

template <class First, class Second>
struct measure_pair final {
    First first;
    Second second;

    [[nodiscard]] constexpr bool operator==(const measure_pair&) const = default;
};

template <class Element, class FirstPolicy, class SecondPolicy>
    requires measure_policy<FirstPolicy, Element> && measure_policy<SecondPolicy, Element>
struct product_measure {
    using element_type = Element;
    using first_measure_type = typename FirstPolicy::measure_type;
    using second_measure_type = typename SecondPolicy::measure_type;
    using measure_type = measure_pair<first_measure_type, second_measure_type>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return measure_type{FirstPolicy::empty(), SecondPolicy::empty()};
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type& element)
    {
        return measure_type{FirstPolicy::measure(element), SecondPolicy::measure(element)};
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            FirstPolicy::combine(left.first, right.first),
            SecondPolicy::combine(left.second, right.second)};
    }
};

} // namespace tools::data_structures::finger_tree
