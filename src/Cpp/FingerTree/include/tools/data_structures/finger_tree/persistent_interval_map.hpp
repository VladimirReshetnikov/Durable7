#pragma once

#include <tools/data_structures/finger_tree/built_in_measures.hpp>
#include <tools/data_structures/finger_tree/comparisons.hpp>
#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class Endpoint, class Value>
struct interval_map_entry final {
    interval<Endpoint> key;
    Value value;

    [[nodiscard]] bool operator==(const interval_map_entry&) const = default;
};

template <class Endpoint>
struct interval_map_annotation final {
    std::size_t count = 0;
    optional_measure<interval<Endpoint>> last_interval =
        optional_measure<interval<Endpoint>>::none();
    optional_measure<Endpoint> max_high = optional_measure<Endpoint>::none();
};

template <
    class Endpoint,
    class Value,
    class Comparison = default_comparison<Endpoint>>
    requires static_comparison_policy<Comparison, Endpoint>
struct interval_map_measure final {
    using element_type = interval_map_entry<Endpoint, Value>;
    using measure_type = interval_map_annotation<Endpoint>;

    [[nodiscard]] static measure_type empty()
    {
        return {};
    }

    [[nodiscard]] static measure_type measure(const element_type& element)
    {
        return {
            1,
            optional_measure<interval<Endpoint>>::some(element.key),
            optional_measure<Endpoint>::some(element.key.high)};
    }

    [[nodiscard]] static measure_type combine(
        const measure_type& left,
        const measure_type& right)
    {
        return {
            checked_add(left.count, right.count),
            right.last_interval.has_value() ? right.last_interval : left.last_interval,
            combine_max(left.max_high, right.max_high)};
    }

private:
    [[nodiscard]] static optional_measure<Endpoint> combine_max(
        const optional_measure<Endpoint>& left,
        const optional_measure<Endpoint>& right)
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

/// Immutable map from validated closed intervals to payload values.
///
/// Keys are unique and ordered lexicographically by (low, high). One measured
/// finger tree caches both the complete rightmost key and maximum high endpoint,
/// supporting logarithmic positioning and pruned overlap search without a second index.
template <
    class Endpoint,
    class Value,
    class Comparison = default_comparison<Endpoint>,
    class ValueEqual = std::equal_to<Value>>
    requires static_comparison_policy<Comparison, Endpoint>
        && std::copyable<Endpoint>
        && std::copyable<Value>
        && std::copyable<ValueEqual>
class persistent_interval_map final {
public:
    using interval_type = interval<Endpoint>;
    using value_type = interval_map_entry<Endpoint, Value>;
    using measure_type = interval_map_measure<Endpoint, Value, Comparison>;
    using tree_type = finger_tree<value_type, measure_type>;
    using size_type = std::size_t;
    using const_iterator = typename tree_type::const_iterator;

    persistent_interval_map() = default;

    [[nodiscard]] static persistent_interval_map empty_map()
    {
        return {};
    }

    [[nodiscard]] static persistent_interval_map create(ValueEqual value_equal = {})
    {
        return persistent_interval_map{tree_type{}, std::move(value_equal)};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_interval_map create_range(
        Range&& entries,
        ValueEqual value_equal = {})
    {
        auto result = create(std::move(value_equal));
        for (auto&& entry : entries) {
            result = result.set_item(entry.key, entry.value);
        }
        return result;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    [[nodiscard]] size_type size() const
    {
        return tree_.measure().count;
    }

    [[nodiscard]] const ValueEqual& value_eq() const noexcept
    {
        return value_equal_;
    }

    [[nodiscard]] const value_type* try_get_entry(const interval_type& key) const
    {
        validate_interval(key);
        const auto located = tree_.try_locate_reference([&key](const auto& annotation) {
            return annotation.last_interval.has_value()
                && compare_intervals(annotation.last_interval.value(), key) >= 0;
        });
        return located.item != nullptr && compare_intervals(located.item->key, key) == 0
            ? located.item
            : nullptr;
    }

    [[nodiscard]] const Value* try_get(const interval_type& key) const
    {
        const auto* entry = try_get_entry(key);
        return entry == nullptr ? nullptr : std::addressof(entry->value);
    }

    [[nodiscard]] bool contains_key(const interval_type& key) const
    {
        return try_get_entry(key) != nullptr;
    }

    /// Returns the number of entries whose interval keys order strictly before `key`, which is
    /// also the rank of its lower-bound gap. Keys order lexicographically by low endpoint then
    /// high endpoint, matching the iteration order. One O(log n) measured descent.
    [[nodiscard]] size_type count_keys_less_than(const interval_type& key) const
    {
        return tree_
            .try_locate([&key](const auto& annotation) {
                return annotation.last_interval.has_value()
                    && compare_intervals(annotation.last_interval.value(), key) >= 0;
            })
            .measure_before.count;
    }

    /// Returns the number of entries whose interval keys do not order after `key`, which is
    /// also the rank of its upper-bound gap. One O(log n) measured descent.
    [[nodiscard]] size_type count_keys_at_most(const interval_type& key) const
    {
        return tree_
            .try_locate([&key](const auto& annotation) {
                return annotation.last_interval.has_value()
                    && compare_intervals(annotation.last_interval.value(), key) > 0;
            })
            .measure_before.count;
    }

    /// Returns the entry at a key-order rank, or nullptr when the rank is at or past the end.
    /// One O(log n) measured descent; no endpoint comparisons are made. The reference follows
    /// this snapshot's lifetime or that of storage sharing the located node.
    [[nodiscard]] const value_type* try_entry_at_rank(const size_type rank) const
    {
        const auto located = tree_.try_locate_reference([rank](const auto& annotation) {
            return annotation.count > rank;
        });
        return located.has_value() ? located.item : nullptr;
    }

    [[nodiscard]] const Value& at(const interval_type& key) const
    {
        const auto* value = try_get(key);
        if (value == nullptr) {
            throw std::out_of_range("persistent_interval_map key was not present");
        }
        return *value;
    }

    [[nodiscard]] persistent_interval_map add(interval_type key, Value value) const
    {
        validate_interval(key);
        auto split = split_at(key);
        if (auto view = split.right.try_view_left();
            view.has_value() && compare_intervals(view->item.key, key) == 0) {
            throw std::invalid_argument("an equivalent interval key is already present");
        }
        return persistent_interval_map{
            split.left.append(value_type{std::move(key), std::move(value)}).concat(split.right),
            value_equal_};
    }

    [[nodiscard]] std::pair<persistent_interval_map, bool> try_add(
        interval_type key,
        Value value) const
    {
        validate_interval(key);
        auto split = split_at(key);
        if (auto view = split.right.try_view_left();
            view.has_value() && compare_intervals(view->item.key, key) == 0) {
            return {*this, false};
        }
        return {
            persistent_interval_map{
                split.left.append(value_type{std::move(key), std::move(value)}).concat(split.right),
                value_equal_},
            true};
    }

    /// Adds or replaces a payload while retaining the first interval representative.
    [[nodiscard]] persistent_interval_map set_item(interval_type key, Value value) const
    {
        validate_interval(key);
        auto split = split_at(key);
        if (auto view = split.right.try_view_left();
            view.has_value() && compare_intervals(view->item.key, key) == 0) {
            if (std::invoke(value_equal_, view->item.value, value)) {
                return *this;
            }
            return persistent_interval_map{
                split.left
                    .append(value_type{view->item.key, std::move(value)})
                    .concat(view->right),
                value_equal_};
        }
        return persistent_interval_map{
            split.left.append(value_type{std::move(key), std::move(value)}).concat(split.right),
            value_equal_};
    }

    [[nodiscard]] persistent_interval_map remove(const interval_type& key) const
    {
        validate_interval(key);
        auto split = split_at(key);
        if (auto view = split.right.try_view_left();
            view.has_value() && compare_intervals(view->item.key, key) == 0) {
            return persistent_interval_map{split.left.concat(view->right), value_equal_};
        }
        return *this;
    }

    [[nodiscard]] persistent_interval_map clear() const
    {
        return empty() ? *this : create(value_equal_);
    }

    [[nodiscard]] std::optional<value_type> try_find_overlap(const interval_type& query) const
    {
        validate_interval(query);
        auto candidates = tree_.split([&query](const auto& annotation) {
            return annotation.last_interval.has_value()
                && Comparison::compare(annotation.last_interval.value().low, query.high) > 0;
        }).left;
        auto located = candidates.try_locate_reference([&query](const auto& annotation) {
            return annotation.max_high.has_value()
                && Comparison::compare(annotation.max_high.value(), query.low) >= 0;
        });
        return located.item == nullptr ? std::nullopt : std::optional<value_type>{*located.item};
    }

    [[nodiscard]] std::optional<value_type> try_find_containing(const Endpoint& point) const
    {
        return try_find_overlap(interval_type{point, point});
    }

    [[nodiscard]] std::vector<value_type> find_overlaps(const interval_type& query) const
    {
        validate_interval(query);
        auto candidates = tree_.split([&query](const auto& annotation) {
            return annotation.last_interval.has_value()
                && Comparison::compare(annotation.last_interval.value().low, query.high) > 0;
        }).left;
        auto result = std::vector<value_type>{};
        for (;;) {
            auto split = candidates.try_split_find([&query](const auto& annotation) {
                return annotation.max_high.has_value()
                    && Comparison::compare(annotation.max_high.value(), query.low) >= 0;
            });
            if (!split.has_value()) {
                break;
            }
            result.push_back(split->item);
            candidates = std::move(split->right);
        }
        return result;
    }

    [[nodiscard]] size_type count_overlaps(const interval_type& query) const
    {
        return find_overlaps(query).size();
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        return tree_.to_vector();
    }

    [[nodiscard]] std::vector<interval_type> keys_to_vector() const
    {
        auto keys = std::vector<interval_type>{};
        keys.reserve(size());
        tree_.for_each([&keys](const value_type& entry) {
            keys.push_back(entry.key);
        });
        return keys;
    }

    [[nodiscard]] std::vector<Value> values_to_vector() const
    {
        auto values = std::vector<Value>{};
        values.reserve(size());
        tree_.for_each([&values](const value_type& entry) {
            values.push_back(entry.value);
        });
        return values;
    }

    [[nodiscard]] const_iterator begin() const
    {
        return tree_.begin();
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return tree_.end();
    }

    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    [[nodiscard]] const_iterator cend() const noexcept
    {
        return end();
    }

    void validate_invariants() const
    {
        auto previous = std::optional<interval_type>{};
        auto maximum = optional_measure<Endpoint>::none();
        auto count = size_type{0};
        for (const auto& entry : tree_) {
            count = checked_add(count, size_type{1});
            validate_interval(entry.key);
            if (previous.has_value() && compare_intervals(*previous, entry.key) >= 0) {
                throw std::logic_error("persistent_interval_map keys are not strictly ordered");
            }
            previous = entry.key;
            if (!maximum.has_value()
                || Comparison::compare(entry.key.high, maximum.value()) > 0) {
                maximum = optional_measure<Endpoint>::some(entry.key.high);
            }
        }
        const auto annotation = tree_.measure();
        if (annotation.count != count
            || annotation.last_interval.has_value() != previous.has_value()
            || (previous.has_value()
                && compare_intervals(annotation.last_interval.value(), *previous) != 0)
            || annotation.max_high.has_value() != maximum.has_value()
            || (maximum.has_value()
                && Comparison::compare(annotation.max_high.value(), maximum.value()) != 0)) {
            throw std::logic_error("persistent_interval_map cached annotation is inconsistent");
        }
    }

    [[nodiscard]] bool debug_validate() const noexcept
    {
        try {
            validate_invariants();
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    persistent_interval_map(tree_type tree, ValueEqual value_equal)
        : tree_(std::move(tree)), value_equal_(std::move(value_equal))
    {
    }

    [[nodiscard]] static int compare_intervals(
        const interval_type& left,
        const interval_type& right)
    {
        const auto low = Comparison::compare(left.low, right.low);
        return low != 0 ? low : Comparison::compare(left.high, right.high);
    }

    static void validate_interval(const interval_type& key)
    {
        if (Comparison::compare(key.low, key.high) > 0) {
            throw std::invalid_argument("interval low endpoint exceeds high endpoint");
        }
    }

    [[nodiscard]] finger_tree_split<value_type, measure_type> split_at(
        const interval_type& key) const
    {
        return tree_.split([&key](const auto& annotation) {
            return annotation.last_interval.has_value()
                && compare_intervals(annotation.last_interval.value(), key) >= 0;
        });
    }

    tree_type tree_;
    [[no_unique_address]] ValueEqual value_equal_{};
};

} // namespace tools::data_structures::finger_tree
