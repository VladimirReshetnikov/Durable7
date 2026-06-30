#pragma once

#include <tools/data_structures/finger_tree/comparisons.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace tools::data_structures::finger_tree {

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

    [[nodiscard]] constexpr bool operator==(const interval&) const = default;

    template <class Comparison = default_comparison<T>>
        requires static_comparison_policy<Comparison, T>
    [[nodiscard]] constexpr bool contains(const T& point) const
    {
        return Comparison::compare(low, point) <= 0 && Comparison::compare(point, high) <= 0;
    }

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

} // namespace tools::data_structures::finger_tree

namespace std {

template <class T>
struct hash<tools::data_structures::finger_tree::interval<T>> {
    [[nodiscard]] std::size_t operator()(const tools::data_structures::finger_tree::interval<T>& value) const
        noexcept(noexcept(std::hash<T>{}(value.low)) && noexcept(std::hash<T>{}(value.high)))
    {
        const auto left = std::hash<T>{}(value.low);
        const auto right = std::hash<T>{}(value.high);
        return left ^ (right + 0x9e3779b97f4a7c15ULL + (left << 6U) + (left >> 2U));
    }
};

} // namespace std
