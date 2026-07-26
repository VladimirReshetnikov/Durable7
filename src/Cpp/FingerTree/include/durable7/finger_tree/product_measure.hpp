/// The product of two measures, carried side by side over the same elements.
///
/// The product of two monoids is a monoid, so one tree can be searched by either component --
/// counting elements and summing weights at once, for instance.

#pragma once

#include <durable7/finger_tree/built_in_measures.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>
#include <durable7/finger_tree/measures.hpp>

#include <concepts>
#include <optional>
#include <stdexcept>
#include <utility>

namespace durable7::finger_tree {

/// The measure computed by a product_measure: two measures carried together.
template <class First, class Second>
struct measure_pair final {
    First first;
    Second second;

    [[nodiscard]] constexpr bool operator==(const measure_pair&) const = default;
};

/// Runs two measure policies side by side over the same elements.
template <class Element, class FirstPolicy, class SecondPolicy>
    requires measure_policy<FirstPolicy, Element> && measure_policy<SecondPolicy, Element>
struct product_measure {
    using element_type = Element;
    using first_measure_type = typename FirstPolicy::measure_type;
    using second_measure_type = typename SecondPolicy::measure_type;
    using measure_type = measure_pair<first_measure_type, second_measure_type>;

    /// The identity: the measure of an empty tree.
    [[nodiscard]] static constexpr measure_type empty()
    {
        return measure_type{FirstPolicy::empty(), SecondPolicy::empty()};
    }

    /// The measure of one element.
    [[nodiscard]] static constexpr measure_type measure(const element_type& element)
    {
        return measure_type{FirstPolicy::measure(element), SecondPolicy::measure(element)};
    }

    /// Combines two measures in order. Must be associative; it need not be commutative.
    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            FirstPolicy::combine(left.first, right.first),
            SecondPolicy::combine(left.second, right.second)};
    }
};

template <class Predicate, class Measure>
concept product_component_predicate =
    std::copy_constructible<Predicate>
    && requires(const Predicate& predicate, const Measure& measure) {
           { predicate(measure) } -> std::convertible_to<bool>;
       };

template <class Element, class FirstPolicy, class SecondPolicy, class Predicate>
    requires measure_policy<FirstPolicy, Element>
        && measure_policy<SecondPolicy, Element>
        && product_component_predicate<Predicate, typename FirstPolicy::measure_type>
/// Splits by the product measure's first component.
[[nodiscard]] finger_tree_split<Element, product_measure<Element, FirstPolicy, SecondPolicy>> split_by_first(
    const finger_tree<Element, product_measure<Element, FirstPolicy, SecondPolicy>>& tree,
    Predicate predicate)
{
    using first_measure = typename FirstPolicy::measure_type;
    using second_measure = typename SecondPolicy::measure_type;
    return tree.split([predicate = std::move(predicate)](const measure_pair<first_measure, second_measure>& measure) {
        return predicate(measure.first);
    });
}

template <class Element, class FirstPolicy, class SecondPolicy, class Predicate>
    requires measure_policy<FirstPolicy, Element>
        && measure_policy<SecondPolicy, Element>
        && product_component_predicate<Predicate, typename SecondPolicy::measure_type>
/// Splits by the product measure's second component.
[[nodiscard]] finger_tree_split<Element, product_measure<Element, FirstPolicy, SecondPolicy>> split_by_second(
    const finger_tree<Element, product_measure<Element, FirstPolicy, SecondPolicy>>& tree,
    Predicate predicate)
{
    using first_measure = typename FirstPolicy::measure_type;
    using second_measure = typename SecondPolicy::measure_type;
    return tree.split([predicate = std::move(predicate)](const measure_pair<first_measure, second_measure>& measure) {
        return predicate(measure.second);
    });
}

template <class Element, class FirstPolicy, class SecondPolicy, class Predicate>
    requires measure_policy<FirstPolicy, Element>
        && measure_policy<SecondPolicy, Element>
        && product_component_predicate<Predicate, typename FirstPolicy::measure_type>
/// Splits around a match on the product measure's first component, or nothing when there is none.
[[nodiscard]] std::optional<finger_tree_item_split<Element, product_measure<Element, FirstPolicy, SecondPolicy>>>
try_split_find_by_first(
    const finger_tree<Element, product_measure<Element, FirstPolicy, SecondPolicy>>& tree,
    Predicate predicate)
{
    using first_measure = typename FirstPolicy::measure_type;
    using second_measure = typename SecondPolicy::measure_type;
    return tree.try_split_find(
        [predicate = std::move(predicate)](const measure_pair<first_measure, second_measure>& measure) {
            return predicate(measure.first);
        });
}

template <class Element, class FirstPolicy, class SecondPolicy, class Predicate>
    requires measure_policy<FirstPolicy, Element>
        && measure_policy<SecondPolicy, Element>
        && product_component_predicate<Predicate, typename SecondPolicy::measure_type>
/// Splits around a match on the product measure's second component, or nothing when there is none.
[[nodiscard]] std::optional<finger_tree_item_split<Element, product_measure<Element, FirstPolicy, SecondPolicy>>>
try_split_find_by_second(
    const finger_tree<Element, product_measure<Element, FirstPolicy, SecondPolicy>>& tree,
    Predicate predicate)
{
    using first_measure = typename FirstPolicy::measure_type;
    using second_measure = typename SecondPolicy::measure_type;
    return tree.try_split_find(
        [predicate = std::move(predicate)](const measure_pair<first_measure, second_measure>& measure) {
            return predicate(measure.second);
        });
}

/// Splits at the given index.
template <class Element, class SecondPolicy>
    requires measure_policy<SecondPolicy, Element>
[[nodiscard]] finger_tree_split<Element, product_measure<Element, size_measure<Element>, SecondPolicy>> split_at_index(
    const finger_tree<Element, product_measure<Element, size_measure<Element>, SecondPolicy>>& tree,
    const std::size_t index)
{
    return split_by_first(tree, [index](const std::size_t count) {
        return count > index;
    });
}

/// The largest element, or nothing when empty.
template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<T> try_peek_max(
    const finger_tree<T, product_measure<T, size_measure<T>, max_measure<T, Comparison>>>& tree)
{
    const auto measure = tree.measure().second;
    return measure.has_value() ? std::optional<T>{measure.value()} : std::nullopt;
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<
    finger_tree_extract_result<T, product_measure<T, size_measure<T>, max_measure<T, Comparison>>>>
/// The largest element together with the structure remaining, or nothing when empty.
try_extract_max(const finger_tree<T, product_measure<T, size_measure<T>, max_measure<T, Comparison>>>& tree)
{
    const auto measure = tree.measure().second;
    if (!measure.has_value()) {
        return std::nullopt;
    }

    const auto target = measure.value();
    auto split = try_split_find_by_second(tree, [target](const optional_measure<T>& current) {
        return current.has_value() && Comparison::compare(current.value(), target) >= 0;
    });
    if (!split.has_value()) {
        throw std::logic_error("size+max tree reported a maximum but split did not find it");
    }

    return finger_tree_extract_result<T, product_measure<T, size_measure<T>, max_measure<T, Comparison>>>{
        split->item,
        split->left.concat(split->right)};
}

/// The smallest element, or nothing when empty.
template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<T> try_peek_min(
    const finger_tree<T, product_measure<T, size_measure<T>, min_measure<T, Comparison>>>& tree)
{
    const auto measure = tree.measure().second;
    return measure.has_value() ? std::optional<T>{measure.value()} : std::nullopt;
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<
    finger_tree_extract_result<T, product_measure<T, size_measure<T>, min_measure<T, Comparison>>>>
/// The smallest element together with the structure remaining, or nothing when empty.
try_extract_min(const finger_tree<T, product_measure<T, size_measure<T>, min_measure<T, Comparison>>>& tree)
{
    const auto measure = tree.measure().second;
    if (!measure.has_value()) {
        return std::nullopt;
    }

    const auto target = measure.value();
    auto split = try_split_find_by_second(tree, [target](const optional_measure<T>& current) {
        return current.has_value() && Comparison::compare(current.value(), target) <= 0;
    });
    if (!split.has_value()) {
        throw std::logic_error("size+min tree reported a minimum but split did not find it");
    }

    return finger_tree_extract_result<T, product_measure<T, size_measure<T>, min_measure<T, Comparison>>>{
        split->item,
        split->left.concat(split->right)};
}

} // namespace durable7::finger_tree
