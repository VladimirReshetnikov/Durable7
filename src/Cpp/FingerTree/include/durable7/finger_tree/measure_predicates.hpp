#pragma once

#include <durable7/finger_tree/built_in_measures.hpp>
#include <durable7/finger_tree/comparisons.hpp>
#include <durable7/finger_tree/product_measure.hpp>

#include <concepts>
#include <functional>
#include <utility>

namespace durable7::finger_tree {

template <class Predicate, class Measure>
concept measure_predicate =
    std::copy_constructible<Predicate>
    && requires(const Predicate& predicate, const Measure& measure) {
           { predicate(measure) } -> std::convertible_to<bool>;
       };

template <class Function>
class function_measure_predicate final {
public:
    explicit constexpr function_measure_predicate(Function function)
        : function_(std::move(function))
    {
    }

    template <class Measure>
    [[nodiscard]] constexpr bool operator()(const Measure& measure) const
        requires requires(const Function& function, const Measure& value) {
            { std::invoke(function, value) } -> std::convertible_to<bool>;
        }
    {
        return std::invoke(function_, measure);
    }

private:
    Function function_;
};

template <class T>
class count_above_predicate final {
public:
    explicit constexpr count_above_predicate(const std::size_t rank) noexcept
        : rank_(rank)
    {
    }

    [[nodiscard]] constexpr bool operator()(const ranked_key<T>& measure) const noexcept
    {
        return measure.count > rank_;
    }

private:
    std::size_t rank_;
};

class size_above_predicate final {
public:
    explicit constexpr size_above_predicate(const std::size_t index) noexcept
        : index_(index)
    {
    }

    [[nodiscard]] constexpr bool operator()(const std::size_t count) const noexcept
    {
        return count > index_;
    }

private:
    std::size_t index_;
};

template <class T, class Less = std::less<>>
class key_at_least_predicate final {
public:
    constexpr key_at_least_predicate(T key, Less less = Less{})
        : key_(std::move(key))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const ranked_key<T>& measure) const
    {
        return measure.key.has_value() && compare_with(less_, measure.key.value(), key_) >= 0;
    }

private:
    T key_;
    Less less_;
};

template <class T, class Less = std::less<>>
class key_above_predicate final {
public:
    constexpr key_above_predicate(T key, Less less = Less{})
        : key_(std::move(key))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const ranked_key<T>& measure) const
    {
        return measure.key.has_value() && compare_with(less_, measure.key.value(), key_) > 0;
    }

private:
    T key_;
    Less less_;
};

template <class T, class Less = std::less<>>
class optional_at_least_predicate final {
public:
    constexpr optional_at_least_predicate(T target, Less less = Less{})
        : target_(std::move(target))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const optional_measure<T>& measure) const
    {
        return measure.has_value() && compare_with(less_, measure.value(), target_) >= 0;
    }

private:
    T target_;
    Less less_;
};

template <class T, class Less = std::less<>>
class optional_at_most_predicate final {
public:
    constexpr optional_at_most_predicate(T target, Less less = Less{})
        : target_(std::move(target))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const optional_measure<T>& measure) const
    {
        return measure.has_value() && compare_with(less_, measure.value(), target_) <= 0;
    }

private:
    T target_;
    Less less_;
};

template <class T, class Less = std::less<>>
class optional_above_predicate final {
public:
    constexpr optional_above_predicate(T target, Less less = Less{})
        : target_(std::move(target))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const optional_measure<T>& measure) const
    {
        return measure.has_value() && compare_with(less_, measure.value(), target_) > 0;
    }

private:
    T target_;
    Less less_;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class static_optional_at_least_predicate final {
public:
    explicit constexpr static_optional_at_least_predicate(T target)
        : target_(std::move(target))
    {
    }

    [[nodiscard]] constexpr bool operator()(const optional_measure<T>& measure) const
    {
        return measure.has_value() && Comparison::compare(measure.value(), target_) >= 0;
    }

private:
    T target_;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class static_optional_at_most_predicate final {
public:
    explicit constexpr static_optional_at_most_predicate(T target)
        : target_(std::move(target))
    {
    }

    [[nodiscard]] constexpr bool operator()(const optional_measure<T>& measure) const
    {
        return measure.has_value() && Comparison::compare(measure.value(), target_) <= 0;
    }

private:
    T target_;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class static_optional_above_predicate final {
public:
    explicit constexpr static_optional_above_predicate(T target)
        : target_(std::move(target))
    {
    }

    [[nodiscard]] constexpr bool operator()(const optional_measure<T>& measure) const
    {
        return measure.has_value() && Comparison::compare(measure.value(), target_) > 0;
    }

private:
    T target_;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class ranked_key_at_least_predicate final {
public:
    explicit constexpr ranked_key_at_least_predicate(T key)
        : key_(std::move(key))
    {
    }

    [[nodiscard]] constexpr bool operator()(const ranked_key<T>& measure) const
    {
        return measure.key.has_value() && Comparison::compare(measure.key.value(), key_) >= 0;
    }

private:
    T key_;
};

template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class ranked_key_above_predicate final {
public:
    explicit constexpr ranked_key_above_predicate(T key)
        : key_(std::move(key))
    {
    }

    [[nodiscard]] constexpr bool operator()(const ranked_key<T>& measure) const
    {
        return measure.key.has_value() && Comparison::compare(measure.key.value(), key_) > 0;
    }

private:
    T key_;
};

template <class Priority, class Less = std::less<>>
class priority_front_predicate final {
public:
    constexpr priority_front_predicate(Priority target, Less less = Less{})
        : target_(std::move(target))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const priority_aggregate<Priority>& measure) const
    {
        return measure.min.has_value() && compare_with(less_, measure.min.value(), target_) <= 0;
    }

private:
    Priority target_;
    Less less_;
};

template <class T, class Less = std::less<>>
class max_high_at_least_predicate final {
public:
    constexpr max_high_at_least_predicate(T low, Less less = Less{})
        : low_(std::move(low))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const interval_annotation<T>& measure) const
    {
        return measure.max_high.has_value() && compare_with(less_, measure.max_high.value(), low_) >= 0;
    }

private:
    T low_;
    Less less_;
};

template <class T, class Less = std::less<>>
class last_low_at_least_predicate final {
public:
    constexpr last_low_at_least_predicate(T low, Less less = Less{})
        : low_(std::move(low))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const interval_annotation<T>& measure) const
    {
        return measure.last_low.has_value() && compare_with(less_, measure.last_low.value(), low_) >= 0;
    }

private:
    T low_;
    Less less_;
};

template <class T, class Less = std::less<>>
class last_low_above_predicate final {
public:
    constexpr last_low_above_predicate(T low, Less less = Less{})
        : low_(std::move(low))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] constexpr bool operator()(const interval_annotation<T>& measure) const
    {
        return measure.last_low.has_value() && compare_with(less_, measure.last_low.value(), low_) > 0;
    }

private:
    T low_;
    Less less_;
};

template <class T>
class sum_above_predicate final {
public:
    explicit constexpr sum_above_predicate(T threshold)
        : threshold_(std::move(threshold))
    {
    }

    [[nodiscard]] constexpr bool operator()(const T& sum) const
    {
        return sum > threshold_;
    }

private:
    T threshold_;
};

template <class Inner, class First, class Second>
class first_component_predicate final {
public:
    explicit constexpr first_component_predicate(Inner inner)
        : inner_(std::move(inner))
    {
    }

    [[nodiscard]] constexpr bool operator()(const measure_pair<First, Second>& measure) const
        requires measure_predicate<Inner, First>
    {
        return inner_(measure.first);
    }

private:
    Inner inner_;
};

template <class Inner, class First, class Second>
class second_component_predicate final {
public:
    explicit constexpr second_component_predicate(Inner inner)
        : inner_(std::move(inner))
    {
    }

    [[nodiscard]] constexpr bool operator()(const measure_pair<First, Second>& measure) const
        requires measure_predicate<Inner, Second>
    {
        return inner_(measure.second);
    }

private:
    Inner inner_;
};

} // namespace durable7::finger_tree
