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
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

namespace ordered_search_cursor_detail {
struct access;
}

template <class T, class Less = std::less<>>
    requires strict_weak_less_for<Less, T, T>
class sorted_bag final {
public:
    using value_type = T;
    using comparison_type = Less;
    using tree_type = finger_tree<value_type, order_statistic_measure<value_type>>;
    using size_type = std::size_t;
    using const_iterator = typename tree_type::const_iterator;

    sorted_bag() = default;

    explicit sorted_bag(Less less)
        : less_(std::move(less))
    {
    }

    sorted_bag(std::initializer_list<value_type> values, Less less = Less{})
        : sorted_bag(from_range(values, std::move(less)))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    sorted_bag(Iterator first, Sentinel last, Less less = Less{})
        : sorted_bag(from_range(std::ranges::subrange(first, last), std::move(less)))
    {
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static sorted_bag from_range(Range&& values, Less less = Less{})
    {
        auto sorted = std::vector<value_type>{};
        for (auto&& value : values) {
            sorted.push_back(value);
        }

        std::stable_sort(sorted.begin(), sorted.end(), less);
        return from_sorted_values(sorted, std::move(less));
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
        auto located = tree_.try_locate_reference(count_above_predicate<value_type>{index});
        if (!located.has_value()) {
            throw std::logic_error("sorted_bag rank locate failed");
        }

        return *located.item;
    }

    [[nodiscard]] const value_type& operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] sorted_bag add(value_type item) const
    {
        auto split = tree_.split(key_above_predicate<value_type, Less>{item, less_});
        return wrap(split.left.append(std::move(item)).concat(split.right));
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] sorted_bag add_range(Range&& values) const
    {
        auto result = *this;
        for (auto&& value : values) {
            result = result.add(value);
        }

        return result;
    }

    [[nodiscard]] bool contains(const value_type& item) const
    {
        auto located = tree_.try_locate(key_at_least_predicate<value_type, Less>{item, less_});
        return located.item.has_value() && equivalent(*located.item, item);
    }

    [[nodiscard]] size_type count_less_than(const value_type& item) const
    {
        return count_before(key_at_least_predicate<value_type, Less>{item, less_});
    }

    [[nodiscard]] size_type count_at_most(const value_type& item) const
    {
        return count_before(key_above_predicate<value_type, Less>{item, less_});
    }

    [[nodiscard]] size_type count_of(const value_type& item) const
    {
        return count_at_most(item) - count_less_than(item);
    }

    [[nodiscard]] std::optional<sorted_bag> try_remove(const value_type& item) const
    {
        auto split = tree_.split(key_at_least_predicate<value_type, Less>{item, less_});
        auto view = split.right.try_view_left();
        if (view.has_value() && equivalent(view->item, item)) {
            return wrap(split.left.concat(view->right));
        }

        return std::nullopt;
    }

    [[nodiscard]] sorted_bag remove(const value_type& item) const
    {
        auto removed = try_remove(item);
        return removed.has_value() ? *removed : *this;
    }

    [[nodiscard]] sorted_bag remove_all(const value_type& item) const
    {
        auto split = tree_.split(key_at_least_predicate<value_type, Less>{item, less_});
        auto tail_split = split.right.split(key_above_predicate<value_type, Less>{item, less_});
        return wrap(split.left.concat(tail_split.right));
    }

    [[nodiscard]] sorted_bag get_range(const value_type& low, const value_type& high) const
    {
        auto split = tree_.split(key_at_least_predicate<value_type, Less>{low, less_});
        auto range_split = split.right.split(key_above_predicate<value_type, Less>{high, less_});
        return wrap(range_split.left);
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
    friend struct ordered_search_cursor_detail::access;

    explicit sorted_bag(tree_type tree, Less less = Less{})
        : tree_(std::move(tree))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] static sorted_bag from_sorted_values(const std::vector<value_type>& values, Less less)
    {
        auto tree = tree_type{};
        for (const auto& value : values) {
            tree = tree.append(value);
        }

        return sorted_bag{std::move(tree), std::move(less)};
    }

    [[nodiscard]] sorted_bag wrap(tree_type tree) const
    {
        return sorted_bag{std::move(tree), less_};
    }

    /// Removes the exact occurrence at an order-statistic rank for the cursor layer.
    [[nodiscard]] sorted_bag cursor_erase_at(const size_type rank) const
    {
        throw_if_index_out_of_range(rank, size());
        auto split = tree_.split(count_above_predicate<value_type>{rank});
        auto view = split.right.try_view_left();
        if (!view.has_value()) {
            throw std::logic_error("sorted_bag cursor rank erase failed");
        }
        return wrap(split.left.concat(view->right));
    }

    [[nodiscard]] bool equivalent(const value_type& left, const value_type& right) const
    {
        return compare_with(less_, left, right) == 0;
    }

    template <class Predicate>
    [[nodiscard]] size_type count_before(Predicate predicate) const
    {
        auto located = tree_.try_locate(std::move(predicate));
        return located.measure_before.count;
    }

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("sorted_bag is empty");
        }
    }

    tree_type tree_;
    Less less_{};
};

} // namespace tools::data_structures::finger_tree
