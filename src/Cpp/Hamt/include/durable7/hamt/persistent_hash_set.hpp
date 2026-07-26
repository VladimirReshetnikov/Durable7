/// A persistent hash set backed by a CHAMP trie.
///
/// Branching is 32-way and bitmap-indexed with immutable collision buckets, so operations are
/// constant time in expectation while versions share every unchanged node. The set retains its
/// hashing and equivalence policy and keeps the first representative of each equivalence class.
/// Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
/// so an edit copies a path rather than the whole collection.

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

namespace durable7::hamt {

template <
    class T,
    class Hash = std::hash<T>,
    class KeyEqual = std::equal_to<T>>
/// A persistent hash set backed by the same CHAMP trie, retaining the first representative of each
/// equivalence class.
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

    /// An empty set.
    persistent_hash_set() = default;

    /// Whether the set holds no elements.
    static persistent_hash_set empty() {
        return {};
    }

    /// An empty set using the supplied policies, which it retains.
    static persistent_hash_set create(Hash hash = {}, KeyEqual equal = {}) {
        return persistent_hash_set(map_type::create(std::move(hash), std::move(equal), unit_equal{}));
    }

    /// Opens a mutable session seeded from this set.
    [[nodiscard]] static transient create_transient(Hash hash = {}, KeyEqual equal = {});

    /// Opens a mutable session seeded from this set. The set itself is unaffected.
    [[nodiscard]] transient to_transient() const &;

    /// Opens a mutable session seeded from this set. The set itself is unaffected.
    [[nodiscard]] transient to_transient() &&;

    /// A set holding a range's elements, built in bulk rather than by repeated insertion.
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

    /// A set holding a range's elements, built in bulk rather than by repeated insertion.
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

    /// Number of elements in the set.
    [[nodiscard]] size_type count() const noexcept {
        return map_.count();
    }

    /// Whether the set holds no elements.
    [[nodiscard]] bool is_empty() const noexcept {
        return map_.is_empty();
    }

    /// The retained hashing policy. Keys the policy treats as equivalent must hash identically.
    [[nodiscard]] const Hash& hash_function() const noexcept {
        return map_.hash_function();
    }

    /// The retained key equivalence policy.
    [[nodiscard]] const KeyEqual& key_eq() const noexcept {
        return map_.key_eq();
    }

    /// Whether the element is present.
    [[nodiscard]] bool contains(const T& item) const {
        return map_.contains_key(item);
    }

    /// Reads the value stored for the key, or nothing when absent.
    [[nodiscard]] const T* try_get_value(const T& equal_value) const {
        return map_.try_get_key(equal_value);
    }

    /// A set containing the given element; returns the receiver when already present.
    [[nodiscard]] persistent_hash_set add(const T& item) const {
        return with_map(map_.set_item(item, unit{}));
    }

    /// Adds the element unless an equivalent one is present, reporting which happened.
    [[nodiscard]] std::pair<persistent_hash_set, bool> try_add(const T& item) const {
        auto [map, added] = map_.try_add(item, unit{});
        if (!added) {
            return {*this, false};
        }

        return {with_map(std::move(map)), true};
    }

    /// A set without that element; returns the receiver when absent.
    [[nodiscard]] persistent_hash_set remove(const T& item) const {
        return with_map(map_.remove(item));
    }

    /// Removes the element, reporting whether it was present.
    [[nodiscard]] std::pair<persistent_hash_set, bool> try_remove(const T& item) const {
        auto [map, removed, value] = map_.try_remove(item);
        (void)value;
        if (!removed) {
            return {*this, false};
        }

        return {with_map(std::move(map)), true};
    }

    /// An empty set retaining the same policies; returns the receiver when already empty.
    [[nodiscard]] persistent_hash_set clear() const {
        return with_map(map_.clear());
    }

    /// The elements of both sets. Subtrees the operands already share are adopted whole rather than
    /// re-entered.
    [[nodiscard]] persistent_hash_set union_with(const persistent_hash_set& other) const {
        return with_map(map_.union_with(other.map_));
    }

    /// The elements of both sets. Subtrees the operands already share are adopted whole rather than
    /// re-entered.
    [[nodiscard]] persistent_hash_set union_with(std::initializer_list<T> items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.add(item);
        }

        return result;
    }

    /// The elements of both sets. Subtrees the operands already share are adopted whole rather than
    /// re-entered.
    template <class Range>
    [[nodiscard]] persistent_hash_set union_with(const Range& items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.add(item);
        }

        return result;
    }

    /// The elements present in both sets.
    [[nodiscard]] persistent_hash_set intersect_with(std::initializer_list<T> items) const {
        return intersect_with_range(items);
    }

    /// The elements present in both sets.
    [[nodiscard]] persistent_hash_set intersect_with(const persistent_hash_set& other) const {
        return with_map(map_.intersect_with(other.map_));
    }

    /// The elements present in both sets.
    template <class Range>
    [[nodiscard]] persistent_hash_set intersect_with(const Range& items) const {
        return intersect_with_range(items);
    }

    /// This set's elements that are absent from the other.
    [[nodiscard]] persistent_hash_set except_with(std::initializer_list<T> items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.remove(item);
        }

        return result;
    }

    /// This set's elements that are absent from the other.
    [[nodiscard]] persistent_hash_set except_with(const persistent_hash_set& other) const {
        return with_map(map_.except_with(other.map_));
    }

    /// This set's elements that are absent from the other.
    template <class Range>
    [[nodiscard]] persistent_hash_set except_with(const Range& items) const {
        auto result = *this;
        for (const auto& item : items) {
            result = result.remove(item);
        }

        return result;
    }

    /// The elements present in exactly one of the two sets.
    [[nodiscard]] persistent_hash_set symmetric_except_with(std::initializer_list<T> items) const {
        return symmetric_except_with_range(items);
    }

    /// The elements present in exactly one of the two sets.
    [[nodiscard]] persistent_hash_set symmetric_except_with(
        const persistent_hash_set& other) const {
        return with_map(map_.symmetric_except_with(other.map_));
    }

    /// The elements present in exactly one of the two sets.
    template <class Range>
    [[nodiscard]] persistent_hash_set symmetric_except_with(const Range& items) const {
        return symmetric_except_with_range(items);
    }

    /// Whether every element of this set also occurs in the other.
    [[nodiscard]] bool is_subset_of(std::initializer_list<T> items) const {
        return is_subset_of_range(items);
    }

    /// Whether every element of this set also occurs in the other.
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

        /// An unpositioned cursor, holding no version.
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

    /// Opens a mutable session seeded from this set. The set itself is unaffected; the session's
    /// edits are visible only through it.
    transient(const transient&) = delete;
    transient& operator=(const transient&) = delete;
    /// Opens a mutable session seeded from this set. The set itself is unaffected; the session's
    /// edits are visible only through it.
    transient(transient&&) noexcept(std::is_nothrow_move_constructible_v<map_transient_type>) = default;
    transient& operator=(transient&&) noexcept(
        std::is_nothrow_move_assignable_v<map_transient_type>) = default;
    /// Opens a mutable session seeded from this set. The set itself is unaffected; the session's
    /// edits are visible only through it.
    ~transient() = default;

    /// Number of elements in the set.
    [[nodiscard]] size_type count() const {
        return map_.count();
    }

    /// Whether the set holds no elements.
    [[nodiscard]] bool is_empty() const {
        return map_.is_empty();
    }

    /// The retained hashing policy. Keys the policy treats as equivalent must hash identically.
    [[nodiscard]] const Hash& hash_function() const {
        return map_.hash_function();
    }

    /// The retained key equivalence policy.
    [[nodiscard]] const KeyEqual& key_eq() const {
        return map_.key_eq();
    }

    /// Whether the element is present.
    [[nodiscard]] bool contains(const T& item) const {
        return map_.contains_key(item);
    }

    /// Reads the value stored for the key, or nothing when absent.
    [[nodiscard]] const T* try_get_value(const T& equal_value) const {
        return map_.try_get_key(equal_value);
    }

    /// A set containing the given element; returns the receiver when already present.
    [[nodiscard]] bool add(const T& item) {
        return map_.try_add(item, unit{});
    }

    /// A set without that element; returns the receiver when absent.
    [[nodiscard]] bool remove(const T& item) {
        return map_.remove(item);
    }

    /// An empty set retaining the same policies; returns the receiver when already empty.
    void clear() {
        map_.clear();
    }

    /// Whether every element of this set also occurs in the other.
    [[nodiscard]] bool is_subset_of(std::initializer_list<T> items) const {
        return is_subset_of_range(items);
    }

    /// Whether every element of this set also occurs in the other.
    [[nodiscard]] bool is_subset_of(const set_type& other) const {
        return is_subset_of_range(other);
    }

    /// Whether every element of this set also occurs in the other.
    template <class Range>
    [[nodiscard]] bool is_subset_of(const Range& items) const {
        return is_subset_of_range(items);
    }

    /// Whether this set is a subset of the other and the other holds an element it lacks.
    [[nodiscard]] bool is_proper_subset_of(std::initializer_list<T> items) const {
        return is_proper_subset_of_range(items);
    }

    /// Whether this set is a subset of the other and the other holds an element it lacks.
    [[nodiscard]] bool is_proper_subset_of(const set_type& other) const {
        return is_proper_subset_of_range(other);
    }

    /// Whether this set is a subset of the other and the other holds an element it lacks.
    template <class Range>
    [[nodiscard]] bool is_proper_subset_of(const Range& items) const {
        return is_proper_subset_of_range(items);
    }

    /// Whether every element of the other occurs in this set.
    [[nodiscard]] bool is_superset_of(std::initializer_list<T> items) const {
        return is_superset_of_range(items);
    }

    /// Whether every element of the other occurs in this set.
    [[nodiscard]] bool is_superset_of(const set_type& other) const {
        return is_superset_of_range(other);
    }

    /// Whether every element of the other occurs in this set.
    template <class Range>
    [[nodiscard]] bool is_superset_of(const Range& items) const {
        return is_superset_of_range(items);
    }

    /// Whether this set is a superset of the other and holds an element the other lacks.
    [[nodiscard]] bool is_proper_superset_of(std::initializer_list<T> items) const {
        return is_proper_superset_of_range(items);
    }

    /// Whether this set is a superset of the other and holds an element the other lacks.
    [[nodiscard]] bool is_proper_superset_of(const set_type& other) const {
        return is_proper_superset_of_range(other);
    }

    /// Whether this set is a superset of the other and holds an element the other lacks.
    template <class Range>
    [[nodiscard]] bool is_proper_superset_of(const Range& items) const {
        return is_proper_superset_of_range(items);
    }

    /// Whether the two sets share at least one element.
    [[nodiscard]] bool overlaps(std::initializer_list<T> items) const {
        return overlaps_range(items);
    }

    /// Whether the two sets share at least one element.
    [[nodiscard]] bool overlaps(const set_type& other) const {
        return overlaps_range(other);
    }

    /// Whether the two sets share at least one element.
    template <class Range>
    [[nodiscard]] bool overlaps(const Range& items) const {
        return overlaps_range(items);
    }

    /// Whether both sets hold the same elements.
    [[nodiscard]] bool set_equals(std::initializer_list<T> items) const {
        return set_equals_range(items);
    }

    /// Whether both sets hold the same elements.
    [[nodiscard]] bool set_equals(const set_type& other) const {
        return set_equals_range(other);
    }

    /// Whether both sets hold the same elements.
    template <class Range>
    [[nodiscard]] bool set_equals(const Range& items) const {
        return set_equals_range(items);
    }

    /// A forward const iterator over one set version's elements. It keeps that version alive, so it
    /// stays valid across later edits.
    class const_iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        /// An unpositioned cursor, holding no version.
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

    /// An iterator over the elements, in the set's own order.
    [[nodiscard]] const_iterator begin() const {
        return const_iterator(map_.begin());
    }

    /// The iterator one past the last element.
    [[nodiscard]] std::default_sentinel_t end() const {
        return map_.end();
    }

    /// A const iterator over the elements.
    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }

    /// The const iterator one past the last element.
    [[nodiscard]] std::default_sentinel_t cend() const {
        return end();
    }

    /// Copies the elements out into a vector, in the set's own order.
    [[nodiscard]] std::vector<T> to_vector() const {
        std::vector<T> items;
        items.reserve(count());
        for (const auto& item : *this) {
            items.push_back(item);
        }
        return items;
    }

    /// Checks that the structure is in its canonical shape, which must depend only on contents and
    /// not on edit history.
    [[nodiscard]] bool debug_validate_canonical() const {
        return map_.debug_validate_canonical();
    }

    /// Closes the session and produces the set holding its accumulated edits.
    [[nodiscard]] set_type persist() && {
        return set_type(std::move(map_).persist());
    }

    /// Closes the session and produces the set holding its accumulated edits.
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

} // namespace durable7::hamt
