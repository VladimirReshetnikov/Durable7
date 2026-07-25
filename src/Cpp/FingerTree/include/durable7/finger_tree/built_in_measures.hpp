#pragma once

#include <durable7/finger_tree/comparisons.hpp>
#include <durable7/finger_tree/measured_finger_tree.hpp>
#include <durable7/finger_tree/measures.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace durable7::finger_tree {

template <class T>
class optional_measure final {
public:
    constexpr optional_measure() noexcept = default;

    [[nodiscard]] static constexpr optional_measure none() noexcept
    {
        return optional_measure{};
    }

    [[nodiscard]] static constexpr optional_measure some(T value)
    {
        return optional_measure{std::move(value)};
    }

    [[nodiscard]] constexpr bool has_value() const noexcept
    {
        return value_.has_value();
    }

    [[nodiscard]] constexpr const T& value() const&
    {
        return *value_;
    }

    [[nodiscard]] constexpr T&& value() &&
    {
        return std::move(*value_);
    }

    [[nodiscard]] constexpr const std::optional<T>& as_optional() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr bool operator==(const optional_measure& other) const = default;

private:
    explicit constexpr optional_measure(T value)
        : value_(std::move(value))
    {
    }

    std::optional<T> value_;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
struct max_measure {
    using element_type = T;
    using measure_type = optional_measure<T>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return measure_type::none();
    }

    [[nodiscard]] static constexpr measure_type measure(T element)
    {
        return measure_type::some(std::move(element));
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        if (!left.has_value()) {
            return right;
        }

        if (!right.has_value()) {
            return left;
        }

        return Comparison::compare(left.value(), right.value()) >= 0 ? left : right;
    }
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
struct min_measure {
    using element_type = T;
    using measure_type = optional_measure<T>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return measure_type::none();
    }

    [[nodiscard]] static constexpr measure_type measure(T element)
    {
        return measure_type::some(std::move(element));
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        if (!left.has_value()) {
            return right;
        }

        if (!right.has_value()) {
            return left;
        }

        return Comparison::compare(left.value(), right.value()) <= 0 ? left : right;
    }
};

template <class T>
struct key_measure {
    using element_type = T;
    using measure_type = optional_measure<T>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return measure_type::none();
    }

    [[nodiscard]] static constexpr measure_type measure(T element)
    {
        return measure_type::some(std::move(element));
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return right.has_value() ? right : left;
    }
};

template <class T>
struct ranked_key final {
    std::size_t count = 0;
    optional_measure<T> key = optional_measure<T>::none();

    [[nodiscard]] constexpr bool operator==(const ranked_key&) const = default;
};

template <class T>
struct order_statistic_measure {
    using element_type = T;
    using measure_type = ranked_key<T>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return {};
    }

    [[nodiscard]] static constexpr measure_type measure(T element)
    {
        return measure_type{1, optional_measure<T>::some(std::move(element))};
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            checked_add(left.count, right.count),
            right.key.has_value() ? right.key : left.key};
    }
};

template <class Priority>
struct priority_aggregate final {
    std::size_t count = 0;
    optional_measure<Priority> min = optional_measure<Priority>::none();

    [[nodiscard]] constexpr bool operator==(const priority_aggregate&) const = default;
};

template <class Element, class Priority>
struct priority_entry final {
    Element element;
    Priority priority;

    [[nodiscard]] constexpr bool operator==(const priority_entry&) const = default;
};

template <class Element, class Priority, class Comparison = default_comparison<Priority>>
    requires static_comparison_policy<Comparison, Priority>
struct priority_measure {
    using element_type = priority_entry<Element, Priority>;
    using measure_type = priority_aggregate<Priority>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return {};
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type& element)
    {
        return measure_type{1, optional_measure<Priority>::some(element.priority)};
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            checked_add(left.count, right.count),
            combine_min(left.min, right.min)};
    }

private:
    [[nodiscard]] static constexpr optional_measure<Priority> combine_min(
        const optional_measure<Priority>& left,
        const optional_measure<Priority>& right)
    {
        if (!left.has_value()) {
            return right;
        }

        if (!right.has_value()) {
            return left;
        }

        return Comparison::compare(left.value(), right.value()) <= 0 ? left : right;
    }
};

template <class T>
struct interval final {
    T low;
    T high;

    // A hand-written dependent body remains constexpr when endpoint equality is constexpr,
    // without rejecting otherwise valid runtime-only endpoint equality during class instantiation.
    [[nodiscard]] constexpr bool operator==(const interval& other) const
        requires requires(const T& left, const T& right) {
            { left == right } -> std::convertible_to<bool>;
        }
    {
        return low == other.low && high == other.high;
    }

    /// Tests closed-interval membership under `Comparison`, which defaults to
    /// `default_comparison<T>` and is NOT inferred from any owning tree: an
    /// interval returned by an `interval_tree` with a custom comparison policy
    /// must be queried with that same policy passed explicitly, or the answer
    /// can contradict the tree's own queries.
    template <class Comparison = default_comparison<T>>
        requires static_comparison_policy<Comparison, T>
    [[nodiscard]] constexpr bool contains(const T& point) const
    {
        return Comparison::compare(low, point) <= 0 && Comparison::compare(point, high) <= 0;
    }

    /// Tests closed-interval overlap under `Comparison`; the same explicit-policy
    /// caveat as `contains` applies when the interval came from a tree with a
    /// non-default comparison policy.
    template <class Comparison = default_comparison<T>>
        requires static_comparison_policy<Comparison, T>
    [[nodiscard]] constexpr bool overlaps(const interval& other) const
    {
        return Comparison::compare(low, other.high) <= 0 && Comparison::compare(other.low, high) <= 0;
    }
};

template <class T>
struct interval_annotation final {
    std::size_t count = 0;
    optional_measure<T> last_low = optional_measure<T>::none();
    optional_measure<T> max_high = optional_measure<T>::none();

    [[nodiscard]] constexpr bool operator==(const interval_annotation&) const = default;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
struct interval_measure {
    using element_type = interval<T>;
    using measure_type = interval_annotation<T>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return {};
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type& element)
    {
        return measure_type{
            1,
            optional_measure<T>::some(element.low),
            optional_measure<T>::some(element.high)};
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            checked_add(left.count, right.count),
            right.last_low.has_value() ? right.last_low : left.last_low,
            combine_max(left.max_high, right.max_high)};
    }

private:
    [[nodiscard]] static constexpr optional_measure<T> combine_max(
        const optional_measure<T>& left,
        const optional_measure<T>& right)
    {
        if (!left.has_value()) {
            return right;
        }

        if (!right.has_value()) {
            return left;
        }

        return Comparison::compare(left.value(), right.value()) >= 0 ? left : right;
    }
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<T> try_peek_max(const finger_tree<T, max_measure<T, Comparison>>& tree)
{
    const auto measure = tree.measure();
    return measure.has_value() ? std::optional<T>{measure.value()} : std::nullopt;
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<finger_tree_extract_result<T, max_measure<T, Comparison>>> try_extract_max(
    const finger_tree<T, max_measure<T, Comparison>>& tree)
{
    const auto measure = tree.measure();
    if (!measure.has_value()) {
        return std::nullopt;
    }

    const auto target = measure.value();
    auto split = tree.try_split_find([target](const optional_measure<T>& current) {
        return current.has_value() && Comparison::compare(current.value(), target) >= 0;
    });
    if (!split.has_value()) {
        throw std::logic_error("max-measured tree reported a maximum but split did not find it");
    }

    return finger_tree_extract_result<T, max_measure<T, Comparison>>{
        split->item,
        split->left.concat(split->right)};
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<T> try_peek_min(const finger_tree<T, min_measure<T, Comparison>>& tree)
{
    const auto measure = tree.measure();
    return measure.has_value() ? std::optional<T>{measure.value()} : std::nullopt;
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] std::optional<finger_tree_extract_result<T, min_measure<T, Comparison>>> try_extract_min(
    const finger_tree<T, min_measure<T, Comparison>>& tree)
{
    const auto measure = tree.measure();
    if (!measure.has_value()) {
        return std::nullopt;
    }

    const auto target = measure.value();
    auto split = tree.try_split_find([target](const optional_measure<T>& current) {
        return current.has_value() && Comparison::compare(current.value(), target) <= 0;
    });
    if (!split.has_value()) {
        throw std::logic_error("min-measured tree reported a minimum but split did not find it");
    }

    return finger_tree_extract_result<T, min_measure<T, Comparison>>{
        split->item,
        split->left.concat(split->right)};
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] finger_tree_split<T, key_measure<T>> split_by_lower_bound(
    const finger_tree<T, key_measure<T>>& tree,
    T key)
{
    return tree.split([key = std::move(key)](const optional_measure<T>& measure) {
        return measure.has_value() && Comparison::compare(measure.value(), key) >= 0;
    });
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] finger_tree_split<T, key_measure<T>> split_by_upper_bound(
    const finger_tree<T, key_measure<T>>& tree,
    T key)
{
    return tree.split([key = std::move(key)](const optional_measure<T>& measure) {
        return measure.has_value() && Comparison::compare(measure.value(), key) > 0;
    });
}

template <class T>
[[nodiscard]] finger_tree_split<T, order_statistic_measure<T>> split_at_index(
    const finger_tree<T, order_statistic_measure<T>>& tree,
    const std::size_t index)
{
    return tree.split([index](const ranked_key<T>& measure) {
        return measure.count > index;
    });
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] finger_tree_split<T, order_statistic_measure<T>> split_by_lower_bound(
    const finger_tree<T, order_statistic_measure<T>>& tree,
    T key)
{
    return tree.split([key = std::move(key)](const ranked_key<T>& measure) {
        return measure.key.has_value() && Comparison::compare(measure.key.value(), key) >= 0;
    });
}

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
[[nodiscard]] finger_tree_split<T, order_statistic_measure<T>> split_by_upper_bound(
    const finger_tree<T, order_statistic_measure<T>>& tree,
    T key)
{
    return tree.split([key = std::move(key)](const ranked_key<T>& measure) {
        return measure.key.has_value() && Comparison::compare(measure.key.value(), key) > 0;
    });
}

} // namespace durable7::finger_tree

namespace std {

template <class T>
struct hash<durable7::finger_tree::interval<T>> {
    [[nodiscard]] std::size_t operator()(const durable7::finger_tree::interval<T>& value) const
        noexcept(noexcept(std::hash<T>{}(value.low)) && noexcept(std::hash<T>{}(value.high)))
    {
        const auto left = std::hash<T>{}(value.low);
        const auto right = std::hash<T>{}(value.high);
        return left ^ (right + 0x9e3779b97f4a7c15ULL + (left << 6U) + (left >> 2U));
    }
};

} // namespace std
