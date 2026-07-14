#pragma once

#include "persistent_hash_map.hpp"

#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

template <
    class T,
    class Hash = std::hash<T>,
    class KeyEqual = std::equal_to<T>>
class persistent_hash_set {
private:
    struct unit {
    };

    struct unit_equal {
        bool operator()(unit, unit) const noexcept {
            return true;
        }
    };

    using map_type = persistent_hash_map<T, unit, Hash, KeyEqual, unit_equal>;

public:
    using key_type = T;
    using value_type = T;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;

    class transient;

    persistent_hash_set() = default;

    static persistent_hash_set empty() {
        return {};
    }

    static persistent_hash_set create(Hash hash = {}, KeyEqual equal = {}) {
        return persistent_hash_set(map_type::create(std::move(hash), std::move(equal), unit_equal{}));
    }

    [[nodiscard]] static transient create_transient(Hash hash = {}, KeyEqual equal = {});

    [[nodiscard]] transient to_transient() const &;

    [[nodiscard]] transient to_transient() &&;

    static persistent_hash_set create_range(
        std::initializer_list<T> items,
        Hash hash = {},
        KeyEqual equal = {}) {
        auto builder = map_type::create_bulk_builder(std::move(hash), std::move(equal), unit_equal{});
        for (const auto& item : items) {
            builder.set_item(item, unit{});
        }

        return persistent_hash_set(builder.to_immutable());
    }

    template <class Range>
    static persistent_hash_set create_range(
        const Range& items,
        Hash hash = {},
        KeyEqual equal = {}) {
        auto builder = map_type::create_bulk_builder(std::move(hash), std::move(equal), unit_equal{});
        for (const auto& item : items) {
            builder.set_item(item, unit{});
        }

        return persistent_hash_set(builder.to_immutable());
    }

    [[nodiscard]] size_type count() const noexcept {
        return map_.count();
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return map_.is_empty();
    }

    [[nodiscard]] const Hash& hash_function() const noexcept {
        return map_.hash_function();
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept {
        return map_.key_eq();
    }

    [[nodiscard]] bool contains(const T& item) const {
        return map_.contains_key(item);
    }

    [[nodiscard]] const T* try_get_value(const T& equal_value) const {
        return map_.try_get_key(equal_value);
    }

    [[nodiscard]] persistent_hash_set add(const T& item) const {
        return with_map(map_.set_item(item, unit{}));
    }

    [[nodiscard]] std::pair<persistent_hash_set, bool> try_add(const T& item) const {
        auto [map, added] = map_.try_add(item, unit{});
        if (!added) {
            return {*this, false};
        }

        return {with_map(std::move(map)), true};
    }

    [[nodiscard]] persistent_hash_set remove(const T& item) const {
        return with_map(map_.remove(item));
    }

    [[nodiscard]] std::pair<persistent_hash_set, bool> try_remove(const T& item) const {
        auto [map, removed, value] = map_.try_remove(item);
        (void)value;
        if (!removed) {
            return {*this, false};
        }

        return {with_map(std::move(map)), true};
    }

    [[nodiscard]] persistent_hash_set clear() const {
        return with_map(map_.clear());
    }

    [[nodiscard]] persistent_hash_set union_with(const persistent_hash_set& other) const {
        return with_map(map_.union_with(other.map_));
    }

    [[nodiscard]] persistent_hash_set union_with(std::initializer_list<T> items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.add(item);
        }

        return result;
    }

    template <class Range>
    [[nodiscard]] persistent_hash_set union_with(const Range& items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.add(item);
        }

        return result;
    }

    [[nodiscard]] persistent_hash_set intersect_with(std::initializer_list<T> items) const {
        return intersect_with_range(items);
    }

    [[nodiscard]] persistent_hash_set intersect_with(const persistent_hash_set& other) const {
        return with_map(map_.intersect_with(other.map_));
    }

    template <class Range>
    [[nodiscard]] persistent_hash_set intersect_with(const Range& items) const {
        return intersect_with_range(items);
    }

    [[nodiscard]] persistent_hash_set except_with(std::initializer_list<T> items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.remove(item);
        }

        return result;
    }

    [[nodiscard]] persistent_hash_set except_with(const persistent_hash_set& other) const {
        return with_map(map_.except_with(other.map_));
    }

    template <class Range>
    [[nodiscard]] persistent_hash_set except_with(const Range& items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.remove(item);
        }

        return result;
    }

    [[nodiscard]] persistent_hash_set symmetric_except_with(std::initializer_list<T> items) const {
        return symmetric_except_with_range(items);
    }

    [[nodiscard]] persistent_hash_set symmetric_except_with(
        const persistent_hash_set& other) const {
        return with_map(map_.symmetric_except_with(other.map_));
    }

    template <class Range>
    [[nodiscard]] persistent_hash_set symmetric_except_with(const Range& items) const {
        return symmetric_except_with_range(items);
    }

    [[nodiscard]] bool is_subset_of(std::initializer_list<T> items) const {
        return is_subset_of_range(items);
    }

    [[nodiscard]] bool is_subset_of(const persistent_hash_set& other) const {
        if (!map_.shares_policy_with(other.map_)) {
            return is_subset_of_range(other);
        }
        return count() <= other.count()
            && map_.intersect_with(other.map_).shares_root_with(map_);
    }

    template <class Range>
    [[nodiscard]] bool is_subset_of(const Range& items) const {
        return is_subset_of_range(items);
    }

    [[nodiscard]] bool is_proper_subset_of(std::initializer_list<T> items) const {
        return is_proper_subset_of_range(items);
    }

    [[nodiscard]] bool is_proper_subset_of(const persistent_hash_set& other) const {
        return count() < other.count() && is_subset_of(other);
    }

    template <class Range>
    [[nodiscard]] bool is_proper_subset_of(const Range& items) const {
        return is_proper_subset_of_range(items);
    }

    [[nodiscard]] bool is_superset_of(std::initializer_list<T> items) const {
        return is_superset_of_range(items);
    }

    [[nodiscard]] bool is_superset_of(const persistent_hash_set& other) const {
        return other.is_subset_of(*this);
    }

    template <class Range>
    [[nodiscard]] bool is_superset_of(const Range& items) const {
        return is_superset_of_range(items);
    }

    [[nodiscard]] bool is_proper_superset_of(std::initializer_list<T> items) const {
        return is_proper_superset_of_range(items);
    }

    [[nodiscard]] bool is_proper_superset_of(const persistent_hash_set& other) const {
        return count() > other.count() && other.is_subset_of(*this);
    }

    template <class Range>
    [[nodiscard]] bool is_proper_superset_of(const Range& items) const {
        return is_proper_superset_of_range(items);
    }

    [[nodiscard]] bool overlaps(std::initializer_list<T> items) const {
        return overlaps_range(items);
    }

    [[nodiscard]] bool overlaps(const persistent_hash_set& other) const {
        if (!map_.shares_policy_with(other.map_)) {
            return overlaps_range(other);
        }
        return !map_.intersect_with(other.map_).is_empty();
    }

    template <class Range>
    [[nodiscard]] bool overlaps(const Range& items) const {
        return overlaps_range(items);
    }

    [[nodiscard]] bool set_equals(std::initializer_list<T> items) const {
        return set_equals_range(items);
    }

    [[nodiscard]] bool set_equals(const persistent_hash_set& other) const {
        return shares_root_with(other)
            || (count() == other.count() && is_subset_of(other));
    }

    template <class Range>
    [[nodiscard]] bool set_equals(const Range& items) const {
        return set_equals_range(items);
    }

    class const_iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = persistent_hash_set::value_type;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        const_iterator() = default;

        reference operator*() const {
            return inner_->first;
        }

        pointer operator->() const {
            return std::addressof(inner_->first);
        }

        const_iterator& operator++() {
            ++inner_;
            return *this;
        }

        const_iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& iterator, std::default_sentinel_t sentinel) noexcept {
            return iterator.inner_ == sentinel;
        }

        friend bool operator==(std::default_sentinel_t sentinel, const const_iterator& iterator) noexcept {
            return iterator == sentinel;
        }

        friend bool operator!=(const const_iterator& iterator, std::default_sentinel_t sentinel) noexcept {
            return !(iterator == sentinel);
        }

        friend bool operator!=(std::default_sentinel_t sentinel, const const_iterator& iterator) noexcept {
            return !(iterator == sentinel);
        }

    private:
        friend class persistent_hash_set;

        explicit const_iterator(typename map_type::const_iterator inner)
            : inner_(std::move(inner)) {
        }

        typename map_type::const_iterator inner_;
    };

    [[nodiscard]] const_iterator begin() const {
        return const_iterator(map_.begin());
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return {};
    }

    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }

    [[nodiscard]] std::default_sentinel_t cend() const noexcept {
        return {};
    }

    [[nodiscard]] std::vector<T> to_vector() const {
        std::vector<T> items;
        items.reserve(count());
        for (const auto& item : *this) {
            items.push_back(item);
        }

        return items;
    }

    [[nodiscard]] bool shares_root_with(const persistent_hash_set& other) const noexcept {
        return map_.shares_root_with(other.map_);
    }

    [[nodiscard]] const void* debug_root_identity() const noexcept {
        return map_.debug_root_identity();
    }

    [[nodiscard]] persistent_hamt_node_kind debug_root_kind() const noexcept {
        return map_.debug_root_kind();
    }

private:
    explicit persistent_hash_set(map_type map)
        : map_(std::move(map)) {
    }

    [[nodiscard]] persistent_hash_set with_map(map_type map) const {
        if (map.shares_root_with(map_)) {
            return *this;
        }

        return persistent_hash_set(std::move(map));
    }

    template <class Range>
    [[nodiscard]] std::unordered_set<T, Hash, KeyEqual> materialize_probe(const Range& items) const {
        std::unordered_set<T, Hash, KeyEqual> probe(0, hash_function(), key_eq());
        for (const auto& item : items) {
            probe.insert(item);
        }

        return probe;
    }

    template <class Range>
    [[nodiscard]] persistent_hash_set intersect_with_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        auto builder = map_type::create_bulk_builder(hash_function(), key_eq(), unit_equal{});
        for (const auto& item : *this) {
            if (probe.find(item) != probe.end()) {
                builder.set_item(item, unit{});
            }
        }

        return persistent_hash_set(builder.to_immutable());
    }

    template <class Range>
    [[nodiscard]] persistent_hash_set symmetric_except_with_range(const Range& items) const {
        const auto toggles = materialize_probe(items);
        auto result = *this;
        for (const auto& item : toggles) {
            result = result.contains(item) ? result.remove(item) : result.add(item);
        }

        return result;
    }

    template <class Range>
    [[nodiscard]] bool is_subset_of_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        for (const auto& item : *this) {
            if (probe.find(item) == probe.end()) {
                return false;
            }
        }

        return true;
    }

    template <class Range>
    [[nodiscard]] bool is_proper_subset_of_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        if (count() >= probe.size()) {
            return false;
        }

        for (const auto& item : *this) {
            if (probe.find(item) == probe.end()) {
                return false;
            }
        }

        return true;
    }

    template <class Range>
    [[nodiscard]] bool is_superset_of_range(const Range& items) const {
        for (const auto& item : items) {
            if (!contains(item)) {
                return false;
            }
        }

        return true;
    }

    template <class Range>
    [[nodiscard]] bool is_proper_superset_of_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        if (probe.size() >= count()) {
            return false;
        }

        for (const auto& item : probe) {
            if (!contains(item)) {
                return false;
            }
        }

        return true;
    }

    template <class Range>
    [[nodiscard]] bool overlaps_range(const Range& items) const {
        for (const auto& item : items) {
            if (contains(item)) {
                return true;
            }
        }

        return false;
    }

    template <class Range>
    [[nodiscard]] bool set_equals_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        if (probe.size() != count()) {
            return false;
        }

        for (const auto& item : *this) {
            if (probe.find(item) == probe.end()) {
                return false;
            }
        }

        return true;
    }

    map_type map_;
};

// The set session is intentionally a zero-semantics facade over the map
// session. It inherits the map transient's one-way lifecycle, version-bound
// iteration, path-copy edit implementation, and policy/representative rules.
template <class T, class Hash, class KeyEqual>
class persistent_hash_set<T, Hash, KeyEqual>::transient final {
private:
    using set_type = persistent_hash_set<T, Hash, KeyEqual>;
    using map_type = typename set_type::map_type;
    using map_transient_type = typename map_type::transient;

public:
    using key_type = T;
    using value_type = T;
    using size_type = typename set_type::size_type;
    using hasher = Hash;
    using key_equal = KeyEqual;

    transient(const transient&) = delete;
    transient& operator=(const transient&) = delete;
    transient(transient&&) noexcept(std::is_nothrow_move_constructible_v<map_transient_type>) = default;
    transient& operator=(transient&&) noexcept(
        std::is_nothrow_move_assignable_v<map_transient_type>) = default;
    ~transient() = default;

    [[nodiscard]] size_type count() const {
        return map_.count();
    }

    [[nodiscard]] bool is_empty() const {
        return map_.is_empty();
    }

    [[nodiscard]] const Hash& hash_function() const {
        return map_.hash_function();
    }

    [[nodiscard]] const KeyEqual& key_eq() const {
        return map_.key_eq();
    }

    [[nodiscard]] bool contains(const T& item) const {
        return map_.contains_key(item);
    }

    [[nodiscard]] const T* try_get_value(const T& equal_value) const {
        return map_.try_get_key(equal_value);
    }

    [[nodiscard]] bool add(const T& item) {
        return map_.try_add(item, unit{});
    }

    [[nodiscard]] bool remove(const T& item) {
        return map_.remove(item);
    }

    void clear() {
        map_.clear();
    }

    [[nodiscard]] bool is_subset_of(std::initializer_list<T> items) const {
        return is_subset_of_range(items);
    }

    [[nodiscard]] bool is_subset_of(const set_type& other) const {
        return is_subset_of_range(other);
    }

    template <class Range>
    [[nodiscard]] bool is_subset_of(const Range& items) const {
        return is_subset_of_range(items);
    }

    [[nodiscard]] bool is_proper_subset_of(std::initializer_list<T> items) const {
        return is_proper_subset_of_range(items);
    }

    [[nodiscard]] bool is_proper_subset_of(const set_type& other) const {
        return is_proper_subset_of_range(other);
    }

    template <class Range>
    [[nodiscard]] bool is_proper_subset_of(const Range& items) const {
        return is_proper_subset_of_range(items);
    }

    [[nodiscard]] bool is_superset_of(std::initializer_list<T> items) const {
        return is_superset_of_range(items);
    }

    [[nodiscard]] bool is_superset_of(const set_type& other) const {
        return is_superset_of_range(other);
    }

    template <class Range>
    [[nodiscard]] bool is_superset_of(const Range& items) const {
        return is_superset_of_range(items);
    }

    [[nodiscard]] bool is_proper_superset_of(std::initializer_list<T> items) const {
        return is_proper_superset_of_range(items);
    }

    [[nodiscard]] bool is_proper_superset_of(const set_type& other) const {
        return is_proper_superset_of_range(other);
    }

    template <class Range>
    [[nodiscard]] bool is_proper_superset_of(const Range& items) const {
        return is_proper_superset_of_range(items);
    }

    [[nodiscard]] bool overlaps(std::initializer_list<T> items) const {
        return overlaps_range(items);
    }

    [[nodiscard]] bool overlaps(const set_type& other) const {
        return overlaps_range(other);
    }

    template <class Range>
    [[nodiscard]] bool overlaps(const Range& items) const {
        return overlaps_range(items);
    }

    [[nodiscard]] bool set_equals(std::initializer_list<T> items) const {
        return set_equals_range(items);
    }

    [[nodiscard]] bool set_equals(const set_type& other) const {
        return set_equals_range(other);
    }

    template <class Range>
    [[nodiscard]] bool set_equals(const Range& items) const {
        return set_equals_range(items);
    }

    class const_iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        const_iterator() = default;

        reference operator*() const {
            return inner_->first;
        }

        pointer operator->() const {
            return std::addressof(inner_->first);
        }

        const_iterator& operator++() {
            ++inner_;
            return *this;
        }

        const_iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& iterator, std::default_sentinel_t sentinel) {
            return iterator.inner_ == sentinel;
        }

        friend bool operator==(std::default_sentinel_t sentinel, const const_iterator& iterator) {
            return iterator == sentinel;
        }

        friend bool operator!=(const const_iterator& iterator, std::default_sentinel_t sentinel) {
            return !(iterator == sentinel);
        }

        friend bool operator!=(std::default_sentinel_t sentinel, const const_iterator& iterator) {
            return !(iterator == sentinel);
        }

    private:
        friend class transient;

        explicit const_iterator(typename map_transient_type::const_iterator inner)
            : inner_(std::move(inner)) {
        }

        typename map_transient_type::const_iterator inner_;
    };

    [[nodiscard]] const_iterator begin() const {
        return const_iterator(map_.begin());
    }

    [[nodiscard]] std::default_sentinel_t end() const {
        return map_.end();
    }

    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }

    [[nodiscard]] std::default_sentinel_t cend() const {
        return end();
    }

    [[nodiscard]] std::vector<T> to_vector() const {
        std::vector<T> items;
        items.reserve(count());
        for (const auto& item : *this) {
            items.push_back(item);
        }
        return items;
    }

    [[nodiscard]] bool debug_validate_canonical() const {
        return map_.debug_validate_canonical();
    }

    [[nodiscard]] set_type persist() && {
        return set_type(std::move(map_).persist());
    }

    set_type persist() & = delete;

private:
    friend set_type;

    explicit transient(map_transient_type map)
        : map_(std::move(map)) {
    }

    void ensure_active() const {
        (void)map_.count();
    }

    template <class Range>
    [[nodiscard]] std::unordered_set<T, Hash, KeyEqual> materialize_probe(
        const Range& items) const {
        ensure_active();
        std::unordered_set<T, Hash, KeyEqual> probe(0, hash_function(), key_eq());
        for (const auto& item : items) {
            probe.insert(item);
        }
        return probe;
    }

    template <class Range>
    [[nodiscard]] bool is_subset_of_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        for (const auto& item : *this) {
            if (probe.find(item) == probe.end()) {
                return false;
            }
        }
        return true;
    }

    template <class Range>
    [[nodiscard]] bool is_proper_subset_of_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        if (count() >= probe.size()) {
            return false;
        }
        for (const auto& item : *this) {
            if (probe.find(item) == probe.end()) {
                return false;
            }
        }
        return true;
    }

    template <class Range>
    [[nodiscard]] bool is_superset_of_range(const Range& items) const {
        ensure_active();
        for (const auto& item : items) {
            if (!contains(item)) {
                return false;
            }
        }
        return true;
    }

    template <class Range>
    [[nodiscard]] bool is_proper_superset_of_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        if (probe.size() >= count()) {
            return false;
        }
        for (const auto& item : probe) {
            if (!contains(item)) {
                return false;
            }
        }
        return true;
    }

    template <class Range>
    [[nodiscard]] bool overlaps_range(const Range& items) const {
        ensure_active();
        for (const auto& item : items) {
            if (contains(item)) {
                return true;
            }
        }
        return false;
    }

    template <class Range>
    [[nodiscard]] bool set_equals_range(const Range& items) const {
        const auto probe = materialize_probe(items);
        if (probe.size() != count()) {
            return false;
        }
        for (const auto& item : *this) {
            if (probe.find(item) == probe.end()) {
                return false;
            }
        }
        return true;
    }

    map_transient_type map_;
};

template <class T, class Hash, class KeyEqual>
[[nodiscard]] auto persistent_hash_set<T, Hash, KeyEqual>::create_transient(
    Hash hash,
    KeyEqual equal) -> transient {
    return transient(map_type::create_transient(
        std::move(hash), std::move(equal), unit_equal{}));
}

template <class T, class Hash, class KeyEqual>
[[nodiscard]] auto persistent_hash_set<T, Hash, KeyEqual>::to_transient() const &
    -> transient {
    return transient(map_.to_transient());
}

template <class T, class Hash, class KeyEqual>
[[nodiscard]] auto persistent_hash_set<T, Hash, KeyEqual>::to_transient() &&
    -> transient {
    return transient(std::move(map_).to_transient());
}

} // namespace tools::data_structures::hamt
