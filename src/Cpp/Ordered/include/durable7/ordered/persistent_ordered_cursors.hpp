/// Gap cursors over the insertion-ordered collections.

#pragma once

#include <durable7/ordered/persistent_ordered_map.hpp>
#include <durable7/ordered/persistent_ordered_multimap.hpp>
#include <durable7/ordered/persistent_ordered_set.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace durable7::ordered {

/// Presence-discriminated result of an equality-class or grouped-pair search.
template <class Cursor>
struct ordered_cursor_search_result final {
    Cursor cursor;
    bool found;
};

/// Result of a non-overwriting insertion performed through a cursor.
template <class Cursor>
struct ordered_cursor_insert_result final {
    Cursor cursor;
    bool inserted;
};

namespace persistent_ordered_cursor_detail {

/// The next value, trapping on overflow rather than wrapping.
template <class Integer>
[[nodiscard]] Integer checked_successor(const Integer position)
{
    if (position == std::numeric_limits<Integer>::max()) {
        throw std::overflow_error("persistent ordered cursor position is exhausted");
    }
    return position + Integer{1};
}

/// The entry at the given rank.
template <class Multimap>
[[nodiscard]] std::optional<typename Multimap::value_type> entry_at(
    const Multimap& map,
    const std::int64_t rank)
{
    if (rank < 0 || rank >= map.pair_count()) {
        return std::nullopt;
    }

    auto result = std::optional<typename Multimap::value_type>{};
    auto position = std::int64_t{0};
    map.for_each_pair([&](const auto& key, const auto& value) {
        if (!result && position == rank) {
            result.emplace(key, value);
        }
        if (position < map.pair_count()) {
            ++position;
        }
    });
    return result;
}

/// The position of the given key-value pair.
template <class Multimap>
[[nodiscard]] std::int64_t pair_position(
    const Multimap& map,
    const typename Multimap::key_type& key,
    const typename Multimap::mapped_type& value)
{
    auto result = std::int64_t{-1};
    auto position = std::int64_t{0};
    map.for_each_pair([&](const auto& stored_key, const auto& stored_value) {
        if (result < 0
            && std::invoke(map.key_eq(), stored_key, key)
            && std::invoke(map.value_eq(), stored_value, value)) {
            result = position;
        }
        if (position < map.pair_count()) {
            ++position;
        }
    });
    return result;
}

/// The position where the key's group of values begins.
template <class Multimap>
[[nodiscard]] std::int64_t group_position(
    const Multimap& map,
    const typename Multimap::key_type& key)
{
    auto result = std::int64_t{-1};
    auto position = std::int64_t{0};
    map.for_each_pair([&](const auto& stored_key, const auto&) {
        if (result < 0 && std::invoke(map.key_eq(), stored_key, key)) {
            result = position;
        }
        if (position < map.pair_count()) {
            ++position;
        }
    });
    return result;
}

/// Pair rank of the gap immediately after the last pair of the key-equivalent
/// group, or the end gap when no such group is present.
///
/// The pairs of a key group are contiguous in the flattened enumeration, so the
/// last matching pair fixes the group end.  Consulting only the key policy keeps
/// this total: a value that is not reflexive under the value policy, such as a
/// `NaN` under `std::equal_to<double>`, is one the collection accepts but a
/// content re-scan can never find again. Because `add` appends the value to the
/// end of the key's group (or appends a fresh last group), this gap is exactly
/// where the newly inserted pair lands.
template <class Multimap>
[[nodiscard]] std::int64_t group_end(
    const Multimap& map,
    const typename Multimap::key_type& key)
{
    auto result = std::int64_t{-1};
    auto position = std::int64_t{0};
    map.for_each_pair([&](const auto& stored_key, const auto&) {
        if (std::invoke(map.key_eq(), stored_key, key)) {
            result = position + 1;
        }
        if (position < map.pair_count()) {
            ++position;
        }
    });
    return result < 0 ? map.pair_count() : result;
}

} // namespace persistent_ordered_cursor_detail

/// Immutable root-plus-explicit-order-position gap cursor over an ordered set.
template <
    class T,
    class Hash = std::hash<T>,
    class KeyEqual = std::equal_to<T>>
/// A gap cursor over one insertion-ordered set version.
class persistent_ordered_set_cursor final {
public:
    using collection_type = persistent_ordered_set<T, Hash, KeyEqual>;
    using value_type = T;
    using size_type = typename collection_type::size_type;

    /// A cursor over the given set version at the given position.
    persistent_ordered_set_cursor(collection_type snapshot, const size_type position)
        : snapshot_(std::move(snapshot)), position_(position)
    {
        if (position_ > snapshot_.size()) {
            throw std::out_of_range("ordered-set cursor position is outside the collection");
        }
    }

    /// Whether the gap follows the last element.
    /// Whether the gap precedes the first element.
    /// The cursor's gap position.
    /// Number of elements in the set version the cursor is positioned in.
    [[nodiscard]] size_type size() const noexcept { return snapshot_.size(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == size(); }

    /// Reads the element immediately before the gap, or nothing at the start.
    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return is_at_start() ? nullptr : std::addressof(snapshot_.at(position_ - 1));
    }
    /// Reads the element immediately before the gap, or nothing at the start.
    const value_type* try_peek_previous() const && = delete;

    /// Reads the element immediately after the gap, or nothing at the end.
    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return is_at_end() ? nullptr : std::addressof(snapshot_.at(position_));
    }
    /// Reads the element immediately after the gap, or nothing at the end.
    const value_type* try_peek_next() const && = delete;

    /// A cursor one position earlier. The receiver is unchanged; movement produces a new cursor
    /// over the same version.
    [[nodiscard]] persistent_ordered_set_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("ordered-set cursor is already at the start");
        }
        return {snapshot_, position_ - 1};
    }

    /// A cursor one position later. The receiver is unchanged.
    [[nodiscard]] persistent_ordered_set_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("ordered-set cursor is already at the end");
        }
        return {snapshot_, position_ + 1};
    }

    /// A cursor at the given position within the same set version.
    [[nodiscard]] persistent_ordered_set_cursor seek(const size_type position) const
    {
        return position == position_ ? *this : persistent_ordered_set_cursor{snapshot_, position};
    }

    /// Inserts at the gap; an equivalent representative preserves this cursor.
    [[nodiscard]] persistent_ordered_set_cursor insert(const value_type& item) const
    {
        if (snapshot_.contains(item)) {
            return *this;
        }
        return {
            snapshot_.insert(position_, item),
            persistent_ordered_cursor_detail::checked_successor(position_)};
    }

    /// Inserts the element unless an equivalent one is present, reporting which happened.
    [[nodiscard]] ordered_cursor_insert_result<persistent_ordered_set_cursor> try_insert(
        const value_type& item) const
    {
        if (snapshot_.contains(item)) {
            return {*this, false};
        }
        return {insert(item), true};
    }

    /// Removes the element before the gap, producing a new version the returned cursor is
    /// positioned in.
    [[nodiscard]] persistent_ordered_set_cursor delete_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("ordered-set cursor has no previous representative");
        }
        return {snapshot_.remove_at(position_ - 1), position_ - 1};
    }

    /// Removes the element after the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] persistent_ordered_set_cursor delete_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("ordered-set cursor has no next representative");
        }
        return {snapshot_.remove_at(position_), position_};
    }

    /// The set version this cursor is positioned in.
    [[nodiscard]] collection_type snapshot() const { return snapshot_; }

private:
    collection_type snapshot_;
    size_type position_;
};

/// A cursor at the given gap of the collection.
template <class T, class Hash, class KeyEqual>
[[nodiscard]] auto get_cursor(
    const persistent_ordered_set<T, Hash, KeyEqual>& set,
    const std::size_t position = 0) -> persistent_ordered_set_cursor<T, Hash, KeyEqual>
{
    return {set, position};
}

/// A cursor at the element together with an exact-match discriminator; on a miss it sits at the
/// insertion point.
template <class T, class Hash, class KeyEqual>
[[nodiscard]] auto get_cursor_at_item(
    const persistent_ordered_set<T, Hash, KeyEqual>& set,
    const T& item) -> ordered_cursor_search_result<persistent_ordered_set_cursor<T, Hash, KeyEqual>>
{
    const auto index = set.index_of(item);
    return {
        persistent_ordered_set_cursor<T, Hash, KeyEqual>{
            set,
            index < 0 ? set.size() : static_cast<std::size_t>(index)},
        index >= 0};
}

/// Immutable root-plus-explicit-order-position gap cursor over an ordered map.
template <
    class Key,
    class Value,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueEqual = std::equal_to<Value>>
/// A gap cursor over one insertion-ordered map version.
class persistent_ordered_map_cursor final {
public:
    using collection_type = persistent_ordered_map<Key, Value, Hash, KeyEqual, ValueEqual>;
    using key_type = Key;
    using mapped_type = Value;
    using entry_type = typename collection_type::value_type;
    using size_type = typename collection_type::size_type;

    /// A cursor over the given map version at the given position.
    persistent_ordered_map_cursor(collection_type snapshot, const size_type position)
        : snapshot_(std::move(snapshot)), position_(position)
    {
        if (position_ > snapshot_.size()) {
            throw std::out_of_range("ordered-map cursor position is outside the collection");
        }
    }

    /// Whether the gap follows the last entry.
    /// Whether the gap precedes the first entry.
    /// The cursor's gap position.
    /// Number of entries in the map version the cursor is positioned in.
    [[nodiscard]] size_type size() const noexcept { return snapshot_.size(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == size(); }

    /// Reads the entry immediately before the gap, or nothing at the start.
    [[nodiscard]] const entry_type* try_peek_previous() const &
    {
        return is_at_start() ? nullptr : std::addressof(snapshot_.entry_at(position_ - 1));
    }
    /// Reads the entry immediately before the gap, or nothing at the start.
    const entry_type* try_peek_previous() const && = delete;

    /// Reads the entry immediately after the gap, or nothing at the end.
    [[nodiscard]] const entry_type* try_peek_next() const &
    {
        return is_at_end() ? nullptr : std::addressof(snapshot_.entry_at(position_));
    }
    /// Reads the entry immediately after the gap, or nothing at the end.
    const entry_type* try_peek_next() const && = delete;

    /// A cursor one position earlier. The receiver is unchanged; movement produces a new cursor
    /// over the same version.
    [[nodiscard]] persistent_ordered_map_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("ordered-map cursor is already at the start");
        }
        return {snapshot_, position_ - 1};
    }

    /// A cursor one position later. The receiver is unchanged.
    [[nodiscard]] persistent_ordered_map_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("ordered-map cursor is already at the end");
        }
        return {snapshot_, position_ + 1};
    }

    /// A cursor at the given position within the same map version.
    [[nodiscard]] persistent_ordered_map_cursor seek(const size_type position) const
    {
        return position == position_ ? *this : persistent_ordered_map_cursor{snapshot_, position};
    }

    /// A map with the entry inserted.
    [[nodiscard]] persistent_ordered_map_cursor insert(
        const key_type& key,
        const mapped_type& value) const
    {
        return {
            snapshot_.insert(position_, key, value),
            persistent_ordered_cursor_detail::checked_successor(position_)};
    }

    /// A duplicate focuses its stored entry without replacing the payload.
    [[nodiscard]] ordered_cursor_insert_result<persistent_ordered_map_cursor> try_insert(
        const key_type& key,
        const mapped_type& value) const
    {
        const auto index = snapshot_.index_of_key(key);
        if (index >= 0) {
            return {
                persistent_ordered_map_cursor{snapshot_, static_cast<size_type>(index)},
                false};
        }
        return {insert(key, value), true};
    }

    /// Replaces the value of the entry after the gap, producing a new version the returned cursor
    /// is positioned in.
    [[nodiscard]] persistent_ordered_map_cursor set_next_value(
        const mapped_type& value) const
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("ordered-map cursor has no next entry");
        }
        return {snapshot_.set_item(entry->key, value), position_};
    }

    /// Removes the entry before the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] persistent_ordered_map_cursor delete_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("ordered-map cursor has no previous entry");
        }
        return {snapshot_.remove_at(position_ - 1), position_ - 1};
    }

    /// Removes the entry after the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] persistent_ordered_map_cursor delete_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("ordered-map cursor has no next entry");
        }
        return {snapshot_.remove_at(position_), position_};
    }

    /// The map version this cursor is positioned in.
    [[nodiscard]] collection_type snapshot() const { return snapshot_; }

private:
    collection_type snapshot_;
    size_type position_;
};

/// A cursor at the given gap of the collection.
template <class Key, class Value, class Hash, class KeyEqual, class ValueEqual>
[[nodiscard]] auto get_cursor(
    const persistent_ordered_map<Key, Value, Hash, KeyEqual, ValueEqual>& map,
    const std::size_t position = 0)
    -> persistent_ordered_map_cursor<Key, Value, Hash, KeyEqual, ValueEqual>
{
    return {map, position};
}

/// A cursor at the key together with an exact-match discriminator; on a miss it sits at the
/// insertion point.
template <class Key, class Value, class Hash, class KeyEqual, class ValueEqual>
[[nodiscard]] auto get_cursor_at_key(
    const persistent_ordered_map<Key, Value, Hash, KeyEqual, ValueEqual>& map,
    const Key& key)
    -> ordered_cursor_search_result<
        persistent_ordered_map_cursor<Key, Value, Hash, KeyEqual, ValueEqual>>
{
    const auto index = map.index_of_key(key);
    return {
        persistent_ordered_map_cursor<Key, Value, Hash, KeyEqual, ValueEqual>{
            map,
            index < 0 ? map.size() : static_cast<std::size_t>(index)},
        index >= 0};
}

/// Immutable root-plus-flattened-key-grouped-pair-rank multimap gap cursor.
template <
    class Key,
    class Value,
    class KeyHash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueHash = std::hash<Value>,
    class ValueEqual = std::equal_to<Value>>
/// A gap cursor over one insertion-ordered multimap version.
class persistent_ordered_multimap_cursor final {
public:
    using collection_type =
        persistent_ordered_multimap<Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>;
    using key_type = Key;
    using mapped_type = Value;
    using value_type = typename collection_type::value_type;
    using size_type = std::int64_t;

    /// A cursor over the given multimap version at the given position.
    persistent_ordered_multimap_cursor(collection_type snapshot, const size_type position)
        : snapshot_(std::move(snapshot)), position_(position)
    {
        if (position_ < 0 || position_ > snapshot_.pair_count()) {
            throw std::out_of_range("ordered-multimap cursor position is outside the collection");
        }
    }

    /// Whether the gap follows the last pair.
    /// Whether the gap precedes the first pair.
    /// The cursor's gap position.
    /// Number of pairs in the multimap version the cursor is positioned in.
    [[nodiscard]] size_type size() const noexcept { return snapshot_.pair_count(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == size(); }

    /// Reads the pair immediately before the gap, or nothing at the start.
    [[nodiscard]] std::optional<value_type> try_peek_previous() const
    {
        return persistent_ordered_cursor_detail::entry_at(snapshot_, position_ - 1);
    }

    /// Reads the pair immediately after the gap, or nothing at the end.
    [[nodiscard]] std::optional<value_type> try_peek_next() const
    {
        return persistent_ordered_cursor_detail::entry_at(snapshot_, position_);
    }

    /// A cursor one position earlier. The receiver is unchanged; movement produces a new cursor
    /// over the same version.
    [[nodiscard]] persistent_ordered_multimap_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("ordered-multimap cursor is already at the start");
        }
        return {snapshot_, position_ - 1};
    }

    /// A cursor one position later. The receiver is unchanged.
    [[nodiscard]] persistent_ordered_multimap_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("ordered-multimap cursor is already at the end");
        }
        return {snapshot_, position_ + 1};
    }

    /// A cursor at the given position within the same multimap version.
    [[nodiscard]] persistent_ordered_multimap_cursor seek(const size_type position) const
    {
        return position == position_
            ? *this
            : persistent_ordered_multimap_cursor{snapshot_, position};
    }

    /// Adds under grouped semantics; an equivalent pair preserves this gap.
    ///
    /// The published gap is derived from the group end using only the key
    /// policy rather than by re-scanning for the (key, value) pair, so a value
    /// that is not reflexive under the value policy (such as a `NaN` under
    /// `std::equal_to<double>`) is inserted without the search failing on a pair
    /// the collection just accepted.
    [[nodiscard]] persistent_ordered_multimap_cursor add(
        const key_type& key,
        const mapped_type& value) const
    {
        if (snapshot_.contains(key, value)) {
            return *this;
        }
        auto next = snapshot_.add(key, value);
        const auto following_gap = persistent_ordered_cursor_detail::group_end(next, key);
        return {std::move(next), following_gap};
    }

    /// Adds the pair unless an equivalent one is present, reporting which happened.
    [[nodiscard]] ordered_cursor_insert_result<persistent_ordered_multimap_cursor> try_add(
        const key_type& key,
        const mapped_type& value) const
    {
        if (snapshot_.contains(key, value)) {
            return {*this, false};
        }
        return {add(key, value), true};
    }

    /// Removes the pair before the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] persistent_ordered_multimap_cursor delete_previous() const
    {
        const auto pair = try_peek_previous();
        if (!pair) {
            throw std::logic_error("ordered-multimap cursor has no previous pair");
        }
        return {deleted_pair(pair->first, pair->second), position_ - 1};
    }

    /// Removes the pair after the gap, producing a new version the returned cursor is positioned
    /// in.
    [[nodiscard]] persistent_ordered_multimap_cursor delete_next() const
    {
        const auto pair = try_peek_next();
        if (!pair) {
            throw std::logic_error("ordered-multimap cursor has no next pair");
        }
        return {deleted_pair(pair->first, pair->second), position_};
    }

    /// The multimap version this cursor is positioned in.
    [[nodiscard]] collection_type snapshot() const { return snapshot_; }

private:
    /// Removes one peeked pair and returns the successor snapshot.
    ///
    /// `remove` locates the pair by content and returns its unchanged receiver
    /// on a miss, which a pair peeked by rank still hits whenever the stored
    /// value is not reflexive under the value policy (a `NaN`, for instance).
    /// Publishing that receiver would report a deletion that removed nothing and
    /// leave the peek returning the same pair forever, so the pair count
    /// validates the correspondence before a version is published, matching how
    /// the sibling cursors reject an impossible boundary edit.
    [[nodiscard]] collection_type deleted_pair(
        const key_type& key,
        const mapped_type& value) const
    {
        auto next = snapshot_.remove(key, value);
        if (next.pair_count() == snapshot_.pair_count()) {
            throw std::logic_error("ordered-multimap cursor deletion removed no pair");
        }
        return next;
    }

    collection_type snapshot_;
    size_type position_;
};

/// A cursor at the given gap of the collection.
template <class Key, class Value, class KeyHash, class KeyEqual, class ValueHash, class ValueEqual>
[[nodiscard]] auto get_cursor(
    const persistent_ordered_multimap<Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>& map,
    const std::int64_t position = 0)
    -> persistent_ordered_multimap_cursor<
        Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>
{
    return {map, position};
}

/// A cursor at the given key-value pair.
template <class Key, class Value, class KeyHash, class KeyEqual, class ValueHash, class ValueEqual>
[[nodiscard]] auto get_cursor_at_pair(
    const persistent_ordered_multimap<Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>& map,
    const Key& key,
    const Value& value)
    -> ordered_cursor_search_result<
        persistent_ordered_multimap_cursor<
            Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>>
{
    const auto position = persistent_ordered_cursor_detail::pair_position(map, key, value);
    return {
        persistent_ordered_multimap_cursor<
            Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>{
            map,
            position < 0 ? map.pair_count() : position},
        position >= 0};
}

/// A cursor at the start of the key's group of values.
template <class Key, class Value, class KeyHash, class KeyEqual, class ValueHash, class ValueEqual>
[[nodiscard]] auto get_cursor_at_group(
    const persistent_ordered_multimap<Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>& map,
    const Key& key)
    -> ordered_cursor_search_result<
        persistent_ordered_multimap_cursor<
            Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>>
{
    const auto position = persistent_ordered_cursor_detail::group_position(map, key);
    return {
        persistent_ordered_multimap_cursor<
            Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>{
            map,
            position < 0 ? map.pair_count() : position},
        position >= 0};
}

} // namespace durable7::ordered
