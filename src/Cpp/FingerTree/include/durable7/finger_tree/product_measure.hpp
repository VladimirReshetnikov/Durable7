#pragma once

#include <durable7/finger_tree/built_in_measures.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>
#include <durable7/finger_tree/measures.hpp>

#include <concepts>
#include <optional>
#include <stdexcept>
#include <utility>

namespace durable7::finger_tree {

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
