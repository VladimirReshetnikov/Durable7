#pragma once

#include <tools/data_structures/finger_tree/built_in_measures.hpp>
#include <tools/data_structures/finger_tree/comparisons.hpp>
#include <tools/data_structures/finger_tree/measure_predicates.hpp>
#include <tools/data_structures/finger_tree/measured_finger_tree.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

template <class T, class Less = std::less<>>
    requires strict_weak_less_for<Less, T, T>
class sorted_set final {
public:
    using value_type = T;
    using comparison_type = Less;
    using tree_type = finger_tree<value_type, order_statistic_measure<value_type>>;
    using size_type = std::size_t;
    using const_iterator = typename tree_type::const_iterator;

    sorted_set() = default;

    explicit sorted_set(Less less)
        : less_(std::move(less))
    {
    }

    sorted_set(std::initializer_list<value_type> values, Less less = Less{})
        : sorted_set(from_range(values, std::move(less)))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    sorted_set(Iterator first, Sentinel last, Less less = Less{})
        : sorted_set(from_range(std::ranges::subrange(first, last), std::move(less)))
    {
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static sorted_set from_range(Range&& values, Less less = Less{})
    {
        auto sorted = std::vector<value_type>{};
        for (auto&& value : values) {
            sorted.push_back(value);
        }

        std::stable_sort(sorted.begin(), sorted.end(), less);

        auto unique = std::vector<value_type>{};
        unique.reserve(sorted.size());
        for (const auto& value : sorted) {
            if (unique.empty() || compare_with(less, unique.back(), value) != 0) {
                unique.push_back(value);
            }
        }

        return from_sorted_unique_values(unique, std::move(less));
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return tree_.empty();
    }

    [[nodiscard]] size_type size() const
    {
        return tree_.measure().count;
    }

    [[nodiscard]] const Less& comparison() const noexcept
    {
        return less_;
    }

    [[nodiscard]] const value_type& min() const
    {
        throw_if_empty();
        return tree_.front();
    }

    [[nodiscard]] const value_type& max() const
    {
        throw_if_empty();
        return tree_.back();
    }

    [[nodiscard]] const value_type& at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return element_at(index);
    }

    [[nodiscard]] const value_type& operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] std::optional<size_type> index_of(const value_type& item) const
    {
        auto located = tree_.try_locate(key_at_least_predicate<value_type, Less>{item, less_});
        if (located.item.has_value() && equivalent(*located.item, item)) {
            return located.measure_before.count;
        }

        return std::nullopt;
    }

    [[nodiscard]] sorted_set add(value_type item) const
    {
        auto split = tree_.split(key_at_least_predicate<value_type, Less>{item, less_});
        if (auto view = split.right.try_view_left(); view.has_value() && equivalent(view->item, item)) {
            return *this;
        }

        return wrap(split.left.append(std::move(item)).concat(split.right));
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] sorted_set add_range(Range&& values) const
    {
        auto result = *this;
        for (auto&& value : values) {
            result = result.add(value);
        }

        return result;
    }

    [[nodiscard]] bool contains(const value_type& item) const
    {
        return index_of(item).has_value();
    }

    [[nodiscard]] std::optional<sorted_set> try_remove(const value_type& item) const
    {
        auto split = tree_.split(key_at_least_predicate<value_type, Less>{item, less_});
        auto view = split.right.try_view_left();
        if (view.has_value() && equivalent(view->item, item)) {
            return wrap(split.left.concat(view->right));
        }

        return std::nullopt;
    }

    [[nodiscard]] sorted_set remove(const value_type& item) const
    {
        auto removed = try_remove(item);
        return removed.has_value() ? *removed : *this;
    }

    [[nodiscard]] std::optional<value_type> try_floor(const value_type& item) const
    {
        const auto at_most = count_before(key_above_predicate<value_type, Less>{item, less_});
        if (at_most == 0) {
            return std::nullopt;
        }

        return element_at(at_most - 1);
    }

    [[nodiscard]] std::optional<value_type> try_ceiling(const value_type& item) const
    {
        auto located = tree_.try_locate(key_at_least_predicate<value_type, Less>{item, less_});
        return located.item;
    }

    [[nodiscard]] std::optional<value_type> try_lower(const value_type& item) const
    {
        const auto less = count_before(key_at_least_predicate<value_type, Less>{item, less_});
        if (less == 0) {
            return std::nullopt;
        }

        return element_at(less - 1);
    }

    [[nodiscard]] std::optional<value_type> try_higher(const value_type& item) const
    {
        auto located = tree_.try_locate(key_above_predicate<value_type, Less>{item, less_});
        return located.item;
    }

    [[nodiscard]] sorted_set get_range(const value_type& low, const value_type& high) const
    {
        auto split = tree_.split(key_at_least_predicate<value_type, Less>{low, less_});
        auto range_split = split.right.split(key_above_predicate<value_type, Less>{high, less_});
        return wrap(range_split.left);
    }

    [[nodiscard]] sorted_set union_with(const sorted_set& other) const
    {
        return merge(other, true, true, true);
    }

    [[nodiscard]] sorted_set intersect(const sorted_set& other) const
    {
        return merge(other, false, true, false);
    }

    [[nodiscard]] sorted_set except(const sorted_set& other) const
    {
        return merge(other, true, false, false);
    }

    [[nodiscard]] sorted_set symmetric_except(const sorted_set& other) const
    {
        return merge(other, true, false, true);
    }

    [[nodiscard]] bool is_subset_of(const sorted_set& other) const
    {
        return merge_counts(other).only_this == 0;
    }

    [[nodiscard]] bool is_superset_of(const sorted_set& other) const
    {
        return merge_counts(other).only_other == 0;
    }

    [[nodiscard]] bool is_proper_subset_of(const sorted_set& other) const
    {
        const auto counts = merge_counts(other);
        return counts.only_this == 0 && counts.only_other > 0;
    }

    [[nodiscard]] bool is_proper_superset_of(const sorted_set& other) const
    {
        const auto counts = merge_counts(other);
        return counts.only_other == 0 && counts.only_this > 0;
    }

    [[nodiscard]] bool overlaps(const sorted_set& other) const
    {
        return merge_counts(other).both > 0;
    }

    [[nodiscard]] bool set_equals(const sorted_set& other) const
    {
        const auto counts = merge_counts(other);
        return counts.only_this == 0 && counts.only_other == 0;
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        return tree_.to_vector();
    }

    template <std::output_iterator<const value_type&> OutputIterator>
    void copy_to(OutputIterator output) const
    {
        tree_.copy_to(std::move(output));
    }

    [[nodiscard]] const_iterator begin() const { return tree_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return tree_.end(); }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct merge_count_result final {
        size_type only_this = 0;
        size_type both = 0;
        size_type only_other = 0;
    };

    explicit sorted_set(tree_type tree, Less less = Less{})
        : tree_(std::move(tree))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] static sorted_set from_sorted_unique_values(const std::vector<value_type>& values, Less less)
    {
        auto tree = tree_type{};
        for (const auto& value : values) {
            tree = tree.append(value);
        }

        return sorted_set{std::move(tree), std::move(less)};
    }

    [[nodiscard]] sorted_set wrap(tree_type tree) const
    {
        return sorted_set{std::move(tree), less_};
    }

    [[nodiscard]] bool equivalent(const value_type& left, const value_type& right) const
    {
        return compare_with(less_, left, right) == 0;
    }

    [[nodiscard]] const value_type& element_at(const size_type rank) const
    {
        auto located = tree_.try_locate_reference(count_above_predicate<value_type>{rank});
        if (!located.has_value()) {
            throw std::logic_error("sorted_set rank locate failed");
        }

        return *located.item;
    }

    template <class Predicate>
    [[nodiscard]] size_type count_before(Predicate predicate) const
    {
        auto located = tree_.try_locate(std::move(predicate));
        return located.measure_before.count;
    }

    [[nodiscard]] sorted_set merge(
        const sorted_set& other,
        const bool emit_only_this,
        const bool emit_both,
        const bool emit_only_other) const
    {
        if (this == &other) {
            return emit_both ? *this : wrap(tree_type{});
        }

        auto reordered_other = std::optional<sorted_set>{};
        if (!comparators_compatible(other)) {
            reordered_other.emplace(from_range(other, less_));
        }
        const auto& right_set = reordered_other.has_value() ? *reordered_other : other;

        if (empty()) {
            return emit_only_other ? wrap(right_set.tree_) : wrap(tree_type{});
        }

        if (right_set.empty()) {
            return emit_only_this ? *this : wrap(tree_type{});
        }

        const auto left_before_right = compare_with(less_, max(), right_set.min()) < 0;
        const auto right_before_left = compare_with(less_, right_set.max(), min()) < 0;
        if (left_before_right || right_before_left) {
            if (emit_only_this && emit_only_other) {
                return left_before_right
                    ? wrap(tree_.concat(right_set.tree_))
                    : wrap(right_set.tree_.concat(tree_));
            }

            if (emit_only_this) {
                return *this;
            }

            if (emit_only_other) {
                return wrap(right_set.tree_);
            }

            return wrap(tree_type{});
        }

        auto left = begin();
        const auto left_end = end();
        auto right = right_set.begin();
        const auto right_end = right_set.end();

        auto output = tree_type{};
        auto emit = [&output](const value_type& value) {
            output = output.append(value);
        };

        while (left != left_end && right != right_end) {
            const auto order = compare_with(less_, *left, *right);
            if (order < 0) {
                if (emit_only_this) {
                    emit(*left);
                }
                ++left;
            } else if (order > 0) {
                if (emit_only_other) {
                    emit(*right);
                }
                ++right;
            } else {
                if (emit_both) {
                    emit(*left);
                }
                ++left;
                ++right;
            }
        }

        if (emit_only_this) {
            while (left != left_end) {
                emit(*left);
                ++left;
            }
        }

        if (emit_only_other) {
            while (right != right_end) {
                emit(*right);
                ++right;
            }
        }

        return wrap(std::move(output));
    }

    [[nodiscard]] merge_count_result merge_counts(const sorted_set& other) const
    {
        if (this == &other) {
            return merge_count_result{0, size(), 0};
        }

        auto reordered_other = std::optional<sorted_set>{};
        if (!comparators_compatible(other)) {
            reordered_other.emplace(from_range(other, less_));
        }
        const auto& right_set = reordered_other.has_value() ? *reordered_other : other;

        if (empty()) {
            return merge_count_result{0, 0, right_set.size()};
        }

        if (right_set.empty()) {
            return merge_count_result{size(), 0, 0};
        }

        if (compare_with(less_, max(), right_set.min()) < 0
            || compare_with(less_, right_set.max(), min()) < 0) {
            return merge_count_result{size(), 0, right_set.size()};
        }

        auto left = begin();
        const auto left_end = end();
        auto right = right_set.begin();
        const auto right_end = right_set.end();
        auto counts = merge_count_result{};

        while (left != left_end && right != right_end) {
            const auto order = compare_with(less_, *left, *right);
            if (order < 0) {
                counts.only_this = checked_add(counts.only_this, size_type{1});
                ++left;
            } else if (order > 0) {
                counts.only_other = checked_add(counts.only_other, size_type{1});
                ++right;
            } else {
                counts.both = checked_add(counts.both, size_type{1});
                ++left;
                ++right;
            }
        }

        while (left != left_end) {
            counts.only_this = checked_add(counts.only_this, size_type{1});
            ++left;
        }
        while (right != right_end) {
            counts.only_other = checked_add(counts.only_other, size_type{1});
            ++right;
        }
        return counts;
    }

    [[nodiscard]] bool comparators_compatible(const sorted_set& other) const
    {
        if constexpr (std::is_empty_v<Less>) {
            return true;
        } else if constexpr (std::equality_comparable<Less>) {
            return less_ == other.less_;
        } else {
            return false;
        }
    }

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("sorted_set is empty");
        }
    }

    tree_type tree_;
    Less less_{};
};

} // namespace tools::data_structures::finger_tree
