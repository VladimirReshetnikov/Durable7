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

template <class Key, class T>
struct sorted_map_entry_measure {
    using element_type = std::pair<Key, T>;
    using measure_type = ranked_key<Key>;

    [[nodiscard]] static constexpr measure_type empty()
    {
        return {};
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type& element)
    {
        return measure_type{1, optional_measure<Key>::some(element.first)};
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right)
    {
        return measure_type{
            checked_add(left.count, right.count),
            right.key.has_value() ? right.key : left.key};
    }
};

template <class Key, class T, class Less = std::less<>>
    requires strict_weak_less_for<Less, Key, Key>
class sorted_map final {
public:
    using key_type = Key;
    using mapped_type = T;
    using entry_type = std::pair<key_type, mapped_type>;
    using comparison_type = Less;
    using tree_type = finger_tree<entry_type, sorted_map_entry_measure<key_type, mapped_type>>;
    using size_type = std::size_t;

    sorted_map() = default;

    explicit sorted_map(Less less)
        : less_(std::move(less))
    {
    }

    sorted_map(std::initializer_list<entry_type> entries, Less less = Less{})
        : sorted_map(from_range(entries, std::move(less)))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    sorted_map(Iterator first, Sentinel last, Less less = Less{})
        : sorted_map(from_range(std::ranges::subrange(first, last), std::move(less)))
    {
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static sorted_map from_range(Range&& entries, Less less = Less{})
    {
        auto sorted = std::vector<entry_type>{};
        for (auto&& entry : entries) {
            sorted.push_back(entry);
        }

        std::stable_sort(sorted.begin(), sorted.end(), [&](const entry_type& left, const entry_type& right) {
            return std::invoke(less, left.first, right.first);
        });

        auto unique = std::vector<entry_type>{};
        unique.reserve(sorted.size());
        for (auto index = size_type{0}; index < sorted.size(); ++index) {
            if (index + 1 < sorted.size() && compare_with(less, sorted[index].first, sorted[index + 1].first) == 0) {
                continue;
            }

            unique.push_back(sorted[index]);
        }

        return from_sorted_unique_entries(unique, std::move(less));
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

    [[nodiscard]] const entry_type& min_entry() const
    {
        throw_if_empty();
        return tree_.front();
    }

    [[nodiscard]] const entry_type& max_entry() const
    {
        throw_if_empty();
        return tree_.back();
    }

    [[nodiscard]] bool contains_key(const key_type& key) const
    {
        return locate_key(key).has_value();
    }

    [[nodiscard]] std::optional<mapped_type> try_get(const key_type& key) const
    {
        auto located = locate_key(key);
        return located.has_value() ? std::optional<mapped_type>{located->second} : std::nullopt;
    }

    [[nodiscard]] mapped_type at(const key_type& key) const
    {
        auto value = try_get(key);
        if (!value.has_value()) {
            throw std::out_of_range("sorted_map key was not present");
        }

        return *value;
    }

    [[nodiscard]] const entry_type& entry_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return entry_at_rank(index);
    }

    [[nodiscard]] std::optional<size_type> index_of_key(const key_type& key) const
    {
        auto located = tree_.try_locate(key_at_least_predicate<key_type, Less>{key, less_});
        if (located.item.has_value() && equivalent_key(located.item->first, key)) {
            return located.measure_before.count;
        }

        return std::nullopt;
    }

    [[nodiscard]] sorted_map set_item(key_type key, mapped_type value) const
    {
        auto split = split_at_least(key);
        auto entry = entry_type{std::move(key), std::move(value)};
        auto view = split.right.try_view_left();
        if (view.has_value() && equivalent_key(view->item.first, entry.first)) {
            return wrap(split.left.append(std::move(entry)).concat(view->right));
        }

        return wrap(split.left.append(std::move(entry)).concat(split.right));
    }

    [[nodiscard]] sorted_map insert(key_type key, mapped_type value) const
    {
        auto split = split_at_least(key);
        if (auto view = split.right.try_view_left(); view.has_value() && equivalent_key(view->item.first, key)) {
            throw std::invalid_argument("sorted_map key already exists");
        }

        return wrap(split.left.append(entry_type{std::move(key), std::move(value)}).concat(split.right));
    }

    [[nodiscard]] std::optional<sorted_map> try_insert(key_type key, mapped_type value) const
    {
        auto split = split_at_least(key);
        if (auto view = split.right.try_view_left(); view.has_value() && equivalent_key(view->item.first, key)) {
            return std::nullopt;
        }

        return wrap(split.left.append(entry_type{std::move(key), std::move(value)}).concat(split.right));
    }

    [[nodiscard]] std::optional<sorted_map> try_remove(const key_type& key) const
    {
        auto split = split_at_least(key);
        auto view = split.right.try_view_left();
        if (view.has_value() && equivalent_key(view->item.first, key)) {
            return wrap(split.left.concat(view->right));
        }

        return std::nullopt;
    }

    [[nodiscard]] sorted_map remove(const key_type& key) const
    {
        auto removed = try_remove(key);
        return removed.has_value() ? *removed : *this;
    }

    [[nodiscard]] std::optional<entry_type> try_floor_entry(const key_type& key) const
    {
        const auto at_most = count_before(key_above_predicate<key_type, Less>{key, less_});
        if (at_most == 0) {
            return std::nullopt;
        }

        return entry_at_rank(at_most - 1);
    }

    [[nodiscard]] std::optional<entry_type> try_ceiling_entry(const key_type& key) const
    {
        auto located = tree_.try_locate(key_at_least_predicate<key_type, Less>{key, less_});
        return located.item;
    }

    [[nodiscard]] std::optional<entry_type> try_lower_entry(const key_type& key) const
    {
        const auto less = count_before(key_at_least_predicate<key_type, Less>{key, less_});
        if (less == 0) {
            return std::nullopt;
        }

        return entry_at_rank(less - 1);
    }

    [[nodiscard]] std::optional<entry_type> try_higher_entry(const key_type& key) const
    {
        auto located = tree_.try_locate(key_above_predicate<key_type, Less>{key, less_});
        return located.item;
    }

    [[nodiscard]] sorted_map get_range(const key_type& low, const key_type& high) const
    {
        auto split = split_at_least(low);
        auto range_split = split.right.split(key_above_predicate<key_type, Less>{high, less_});
        return wrap(range_split.left);
    }

    [[nodiscard]] std::vector<entry_type> to_vector() const
    {
        return tree_.to_vector();
    }

    [[nodiscard]] std::vector<key_type> keys_to_vector() const
    {
        auto keys = std::vector<key_type>{};
        keys.reserve(size());
        tree_.for_each([&keys](const entry_type& entry) {
            keys.push_back(entry.first);
        });

        return keys;
    }

    [[nodiscard]] std::vector<mapped_type> values_to_vector() const
    {
        auto values = std::vector<mapped_type>{};
        values.reserve(size());
        tree_.for_each([&values](const entry_type& entry) {
            values.push_back(entry.second);
        });

        return values;
    }

private:
    using split_type = finger_tree_split<entry_type, sorted_map_entry_measure<key_type, mapped_type>>;
    using locate_type = finger_tree_locate_result<entry_type, sorted_map_entry_measure<key_type, mapped_type>>;

    explicit sorted_map(tree_type tree, Less less = Less{})
        : tree_(std::move(tree))
        , less_(std::move(less))
    {
    }

    [[nodiscard]] static sorted_map from_sorted_unique_entries(const std::vector<entry_type>& entries, Less less)
    {
        auto tree = tree_type{};
        for (const auto& entry : entries) {
            tree = tree.append(entry);
        }

        return sorted_map{std::move(tree), std::move(less)};
    }

    [[nodiscard]] sorted_map wrap(tree_type tree) const
    {
        return sorted_map{std::move(tree), less_};
    }

    [[nodiscard]] bool equivalent_key(const key_type& left, const key_type& right) const
    {
        return compare_with(less_, left, right) == 0;
    }

    [[nodiscard]] split_type split_at_least(const key_type& key) const
    {
        return tree_.split(key_at_least_predicate<key_type, Less>{key, less_});
    }

    [[nodiscard]] std::optional<entry_type> locate_key(const key_type& key) const
    {
        auto located = tree_.try_locate(key_at_least_predicate<key_type, Less>{key, less_});
        if (located.item.has_value() && equivalent_key(located.item->first, key)) {
            return located.item;
        }

        return std::nullopt;
    }

    [[nodiscard]] const entry_type& entry_at_rank(const size_type rank) const
    {
        auto located = tree_.try_locate_reference(count_above_predicate<key_type>{rank});
        if (!located.has_value()) {
            throw std::logic_error("sorted_map rank locate failed");
        }

        return *located.item;
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
            throw std::logic_error("sorted_map is empty");
        }
    }

    tree_type tree_;
    Less less_{};
};

} // namespace tools::data_structures::finger_tree
