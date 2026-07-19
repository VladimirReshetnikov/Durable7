#pragma once

#include <tools/data_structures/finger_tree/canonical_sorted_set.hpp>
#include <tools/data_structures/finger_tree/interval_tree.hpp>
#include <tools/data_structures/finger_tree/persistent_chunked_bit_set.hpp>
#include <tools/data_structures/finger_tree/persistent_interval_map.hpp>
#include <tools/data_structures/finger_tree/priority_search_queue.hpp>
#include <tools/data_structures/finger_tree/sorted_bag.hpp>
#include <tools/data_structures/finger_tree/sorted_map.hpp>
#include <tools/data_structures/finger_tree/sorted_set.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tools::data_structures::finger_tree {

/// Presence-discriminated result of an ordered or interval cursor search.
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

namespace ordered_search_cursor_detail {

/// Narrow friendship bridge for occurrence-sensitive rank erasure.
struct access final {
    template <class T, class Less>
    [[nodiscard]] static sorted_bag<T, Less> erase_at(
        const sorted_bag<T, Less>& bag,
        const std::size_t rank)
    {
        return bag.cursor_erase_at(rank);
    }

    template <class T, class Comparison>
    [[nodiscard]] static interval_tree<T, Comparison> erase_at(
        const interval_tree<T, Comparison>& tree,
        const std::size_t rank)
    {
        return tree.cursor_erase_at(rank);
    }
};

template <class Derived, class Collection>
class rank_cursor_base {
public:
    using collection_type = Collection;
    using size_type = std::size_t;

    [[nodiscard]] size_type size() const noexcept { return snapshot_.size(); }
    [[nodiscard]] bool empty() const noexcept { return snapshot_.empty(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == size(); }

    [[nodiscard]] Derived move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("ordered cursor is already at the start");
        }
        return Derived{snapshot_, position_ - 1};
    }

    [[nodiscard]] Derived move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("ordered cursor is already at the end");
        }
        return Derived{snapshot_, position_ + 1};
    }

    [[nodiscard]] Derived seek_rank(const size_type rank) const
    {
        if (rank > size()) {
            throw std::out_of_range("ordered cursor rank is outside the collection bounds");
        }
        return rank == position_ ? static_cast<const Derived&>(*this) : Derived{snapshot_, rank};
    }

    [[nodiscard]] Collection snapshot() const { return snapshot_; }

protected:
    rank_cursor_base(Collection snapshot, const size_type position)
        : snapshot_(std::move(snapshot))
        , position_(position)
    {
        if (position_ > snapshot_.size()) {
            throw std::out_of_range("ordered cursor position is outside the collection bounds");
        }
    }

    Collection snapshot_;
    size_type position_;
};

} // namespace ordered_search_cursor_detail

/// Immutable root-plus-rank gap cursor over a persistent sorted bag.
template <class T, class Less = std::less<>>
    requires strict_weak_less_for<Less, T, T>
class sorted_bag_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          sorted_bag_cursor<T, Less>,
          sorted_bag<T, Less>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        sorted_bag_cursor<T, Less>,
        sorted_bag<T, Less>>;

public:
    using typename base_type::size_type;
    using value_type = T;
    using collection_type = sorted_bag<value_type, Less>;

    sorted_bag_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return this->is_at_start() ? nullptr : std::addressof(this->snapshot_.at(this->position_ - 1));
    }
    const value_type* try_peek_previous() const && = delete;

    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return this->is_at_end() ? nullptr : std::addressof(this->snapshot_.at(this->position_));
    }
    const value_type* try_peek_next() const && = delete;

    [[nodiscard]] sorted_bag_cursor add(value_type item) const
    {
        const auto insertion_rank = this->snapshot_.count_at_most(item);
        return {this->snapshot_.add(std::move(item)), checked_add(insertion_rank, size_type{1})};
    }

    [[nodiscard]] sorted_bag_cursor delete_previous() const
    {
        if (this->is_at_start()) {
            throw std::logic_error("sorted bag cursor has no previous occurrence");
        }
        return {
            ordered_search_cursor_detail::access::erase_at(
                this->snapshot_,
                this->position_ - 1),
            this->position_ - 1};
    }

    [[nodiscard]] sorted_bag_cursor delete_next() const
    {
        if (this->is_at_end()) {
            throw std::logic_error("sorted bag cursor has no next occurrence");
        }
        return {
            ordered_search_cursor_detail::access::erase_at(this->snapshot_, this->position_),
            this->position_};
    }
};

template <class T, class Less>
[[nodiscard]] auto get_cursor(
    const sorted_bag<T, Less>& bag,
    const std::size_t position = 0) -> sorted_bag_cursor<T, Less>
{
    return {bag, position};
}

template <class T, class Less>
[[nodiscard]] auto get_cursor_lower_bound(
    const sorted_bag<T, Less>& bag,
    const T& item) -> sorted_bag_cursor<T, Less>
{
    return {bag, bag.count_less_than(item)};
}

template <class T, class Less>
[[nodiscard]] auto get_cursor_upper_bound(
    const sorted_bag<T, Less>& bag,
    const T& item) -> sorted_bag_cursor<T, Less>
{
    return {bag, bag.count_at_most(item)};
}

template <class T, class Less>
[[nodiscard]] auto get_cursor_at_item(
    const sorted_bag<T, Less>& bag,
    const T& item) -> ordered_cursor_search_result<sorted_bag_cursor<T, Less>>
{
    return {get_cursor_lower_bound(bag, item), bag.contains(item)};
}

/// Immutable root-plus-rank gap cursor over a persistent sorted set.
template <class T, class Less = std::less<>>
    requires strict_weak_less_for<Less, T, T>
class sorted_set_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          sorted_set_cursor<T, Less>,
          sorted_set<T, Less>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        sorted_set_cursor<T, Less>,
        sorted_set<T, Less>>;

public:
    using typename base_type::size_type;
    using value_type = T;
    using collection_type = sorted_set<value_type, Less>;

    sorted_set_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return this->is_at_start() ? nullptr : std::addressof(this->snapshot_.at(this->position_ - 1));
    }
    const value_type* try_peek_previous() const && = delete;

    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return this->is_at_end() ? nullptr : std::addressof(this->snapshot_.at(this->position_));
    }
    const value_type* try_peek_next() const && = delete;

    [[nodiscard]] sorted_set_cursor add(value_type item) const
    {
        const auto insertion_rank = get_cursor_lower_bound(this->snapshot_, item).position();
        return {this->snapshot_.add(std::move(item)), checked_add(insertion_rank, size_type{1})};
    }

    [[nodiscard]] sorted_set_cursor delete_previous() const
    {
        const auto* item = try_peek_previous();
        if (item == nullptr) {
            throw std::logic_error("sorted set cursor has no previous item");
        }
        return {this->snapshot_.remove(*item), this->position_ - 1};
    }

    [[nodiscard]] sorted_set_cursor delete_next() const
    {
        const auto* item = try_peek_next();
        if (item == nullptr) {
            throw std::logic_error("sorted set cursor has no next item");
        }
        return {this->snapshot_.remove(*item), this->position_};
    }
};

template <class T, class Less>
[[nodiscard]] auto get_cursor(
    const sorted_set<T, Less>& set,
    const std::size_t position = 0) -> sorted_set_cursor<T, Less>
{
    return {set, position};
}

template <class T, class Less>
[[nodiscard]] auto get_cursor_lower_bound(
    const sorted_set<T, Less>& set,
    const T& item) -> sorted_set_cursor<T, Less>
{
    return {set, set.count_less_than(item)};
}

template <class T, class Less>
[[nodiscard]] auto get_cursor_upper_bound(
    const sorted_set<T, Less>& set,
    const T& item) -> sorted_set_cursor<T, Less>
{
    return {set, set.count_at_most(item)};
}

template <class T, class Less>
[[nodiscard]] auto get_cursor_at_item(
    const sorted_set<T, Less>& set,
    const T& item) -> ordered_cursor_search_result<sorted_set_cursor<T, Less>>
{
    return {get_cursor_lower_bound(set, item), set.contains(item)};
}

/// Immutable key-order root-plus-rank gap cursor over a persistent sorted map.
template <class Key, class T, class Less = std::less<>>
    requires strict_weak_less_for<Less, Key, Key>
class sorted_map_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          sorted_map_cursor<Key, T, Less>,
          sorted_map<Key, T, Less>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        sorted_map_cursor<Key, T, Less>,
        sorted_map<Key, T, Less>>;

public:
    using typename base_type::size_type;
    using key_type = Key;
    using mapped_type = T;
    using collection_type = sorted_map<key_type, mapped_type, Less>;
    using entry_type = typename collection_type::entry_type;

    sorted_map_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const entry_type* try_peek_previous() const &
    {
        return this->is_at_start()
            ? nullptr
            : std::addressof(this->snapshot_.entry_at(this->position_ - 1));
    }
    const entry_type* try_peek_previous() const && = delete;

    [[nodiscard]] const entry_type* try_peek_next() const &
    {
        return this->is_at_end()
            ? nullptr
            : std::addressof(this->snapshot_.entry_at(this->position_));
    }
    const entry_type* try_peek_next() const && = delete;

    [[nodiscard]] sorted_map_cursor insert(key_type key, mapped_type value) const
    {
        const auto location = get_cursor_lower_bound(this->snapshot_, key).position();
        return {
            this->snapshot_.insert(std::move(key), std::move(value)),
            checked_add(location, size_type{1})};
    }

    [[nodiscard]] ordered_cursor_insert_result<sorted_map_cursor> try_insert(
        key_type key,
        mapped_type value) const
    {
        auto location = get_cursor_lower_bound(this->snapshot_, key);
        auto result = this->snapshot_.try_insert(std::move(key), std::move(value));
        if (!result.has_value()) {
            return {std::move(location), false};
        }
        return {
            sorted_map_cursor{std::move(*result), checked_add(location.position(), size_type{1})},
            true};
    }

    [[nodiscard]] sorted_map_cursor set_item(key_type key, mapped_type value) const
    {
        const auto location = get_cursor_at_key(this->snapshot_, key);
        return {
            this->snapshot_.set_item(std::move(key), std::move(value)),
            location.found
                ? location.cursor.position()
                : checked_add(location.cursor.position(), size_type{1})};
    }

    [[nodiscard]] sorted_map_cursor set_next_value(mapped_type value) const
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("sorted map cursor has no next entry");
        }
        return {this->snapshot_.set_item(entry->first, std::move(value)), this->position_};
    }

    [[nodiscard]] sorted_map_cursor delete_previous() const
    {
        const auto* entry = try_peek_previous();
        if (entry == nullptr) {
            throw std::logic_error("sorted map cursor has no previous entry");
        }
        return {this->snapshot_.remove(entry->first), this->position_ - 1};
    }

    [[nodiscard]] sorted_map_cursor delete_next() const
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("sorted map cursor has no next entry");
        }
        return {this->snapshot_.remove(entry->first), this->position_};
    }
};

template <class Key, class T, class Less>
[[nodiscard]] auto get_cursor(
    const sorted_map<Key, T, Less>& map,
    const std::size_t position = 0) -> sorted_map_cursor<Key, T, Less>
{
    return {map, position};
}

template <class Key, class T, class Less>
[[nodiscard]] auto get_cursor_lower_bound(
    const sorted_map<Key, T, Less>& map,
    const Key& key) -> sorted_map_cursor<Key, T, Less>
{
    return {map, map.count_keys_less_than(key)};
}

template <class Key, class T, class Less>
[[nodiscard]] auto get_cursor_upper_bound(
    const sorted_map<Key, T, Less>& map,
    const Key& key) -> sorted_map_cursor<Key, T, Less>
{
    return {map, map.count_keys_at_most(key)};
}

template <class Key, class T, class Less>
[[nodiscard]] auto get_cursor_at_key(
    const sorted_map<Key, T, Less>& map,
    const Key& key) -> ordered_cursor_search_result<sorted_map_cursor<Key, T, Less>>
{
    return {get_cursor_lower_bound(map, key), map.contains_key(key)};
}

/// Immutable policy-preserving root-plus-rank gap cursor over a canonical sorted set.
template <class T>
class canonical_sorted_set_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          canonical_sorted_set_cursor<T>,
          canonical_sorted_set<T>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        canonical_sorted_set_cursor<T>,
        canonical_sorted_set<T>>;

public:
    using typename base_type::size_type;
    using value_type = T;
    using collection_type = canonical_sorted_set<value_type>;

    canonical_sorted_set_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return this->is_at_start() ? nullptr : this->snapshot_.try_select(this->position_ - 1);
    }
    const value_type* try_peek_previous() const && = delete;

    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return this->snapshot_.try_select(this->position_);
    }
    const value_type* try_peek_next() const && = delete;

    [[nodiscard]] canonical_sorted_set_cursor add(value_type item) const
    {
        const auto location = get_cursor_lower_bound(this->snapshot_, item).position();
        return {
            this->snapshot_.add(std::move(item)),
            checked_add(location, size_type{1})};
    }

    [[nodiscard]] canonical_sorted_set_cursor delete_previous() const
    {
        const auto* item = try_peek_previous();
        if (item == nullptr) {
            throw std::logic_error("canonical sorted-set cursor has no previous item");
        }
        return {this->snapshot_.remove(*item), this->position_ - 1};
    }

    [[nodiscard]] canonical_sorted_set_cursor delete_next() const
    {
        const auto* item = try_peek_next();
        if (item == nullptr) {
            throw std::logic_error("canonical sorted-set cursor has no next item");
        }
        return {this->snapshot_.remove(*item), this->position_};
    }
};

template <class T>
[[nodiscard]] auto get_cursor(
    const canonical_sorted_set<T>& set,
    const std::size_t position = 0) -> canonical_sorted_set_cursor<T>
{
    return {set, position};
}

template <class T>
[[nodiscard]] auto get_cursor_lower_bound(
    const canonical_sorted_set<T>& set,
    const T& item) -> canonical_sorted_set_cursor<T>
{
    return {set, set.count_less_than(item)};
}

template <class T>
[[nodiscard]] auto get_cursor_upper_bound(
    const canonical_sorted_set<T>& set,
    const T& item) -> canonical_sorted_set_cursor<T>
{
    return {set, set.count_at_most(item)};
}

template <class T>
[[nodiscard]] auto get_cursor_at_item(
    const canonical_sorted_set<T>& set,
    const T& item) -> ordered_cursor_search_result<canonical_sorted_set_cursor<T>>
{
    return {get_cursor_lower_bound(set, item), set.contains(item)};
}

/// Immutable key-order root-plus-rank gap cursor over a persistent priority-search queue.
template <
    class Key,
    class Priority,
    class T,
    class KeyLess = std::less<Key>,
    class PriorityLess = std::less<Priority>>
class priority_search_queue_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>,
          priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>,
        priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>>;

public:
    using typename base_type::size_type;
    using key_type = Key;
    using priority_type = Priority;
    using mapped_type = T;
    using collection_type = priority_search_queue<
        key_type,
        priority_type,
        mapped_type,
        KeyLess,
        PriorityLess>;
    using entry_type = typename collection_type::entry_type;

    priority_search_queue_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const entry_type* try_peek_previous() const &
    {
        return this->is_at_start()
            ? nullptr
            : this->snapshot_.try_entry_at_rank(this->position_ - 1);
    }
    const entry_type* try_peek_previous() const && = delete;

    [[nodiscard]] const entry_type* try_peek_next() const &
    {
        return this->snapshot_.try_entry_at_rank(this->position_);
    }
    const entry_type* try_peek_next() const && = delete;

    [[nodiscard]] priority_search_queue_cursor insert(
        key_type key,
        priority_type priority,
        mapped_type value) const
    {
        auto result = try_insert(std::move(key), std::move(priority), std::move(value));
        if (!result.inserted) {
            throw std::invalid_argument("priority-search cursor cannot insert a duplicate key");
        }
        return std::move(result.cursor);
    }

    [[nodiscard]] ordered_cursor_insert_result<priority_search_queue_cursor> try_insert(
        key_type key,
        priority_type priority,
        mapped_type value) const
    {
        auto location = get_cursor_lower_bound(this->snapshot_, key);
        auto result = this->snapshot_.try_add(
            std::move(key),
            std::move(priority),
            std::move(value));
        if (!result.added) {
            return {std::move(location), false};
        }
        return {
            priority_search_queue_cursor{
                std::move(result.queue),
                checked_add(location.position(), size_type{1})},
            true};
    }

    [[nodiscard]] priority_search_queue_cursor set_item(
        key_type key,
        priority_type priority,
        mapped_type value) const
        requires priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<mapped_type>
    {
        const auto location = get_cursor_at_key(this->snapshot_, key);
        return {
            this->snapshot_.set_item(
                std::move(key),
                std::move(priority),
                std::move(value)),
            location.found
                ? location.cursor.position()
                : checked_add(location.cursor.position(), size_type{1})};
    }

    [[nodiscard]] priority_search_queue_cursor set_next(
        priority_type priority,
        mapped_type value) const
        requires std::copy_constructible<key_type>
            && priority_search_equality_testable<priority_type>
            && priority_search_equality_testable<mapped_type>
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("priority-search cursor has no next entry");
        }
        return {
            this->snapshot_.set_item(entry->key(), std::move(priority), std::move(value)),
            this->position_};
    }

    [[nodiscard]] priority_search_queue_cursor delete_previous() const
    {
        const auto* entry = try_peek_previous();
        if (entry == nullptr) {
            throw std::logic_error("priority-search cursor has no previous entry");
        }
        return {this->snapshot_.remove(entry->key()), this->position_ - 1};
    }

    [[nodiscard]] priority_search_queue_cursor delete_next() const
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("priority-search cursor has no next entry");
        }
        return {this->snapshot_.remove(entry->key()), this->position_};
    }
};

template <class Key, class Priority, class T, class KeyLess, class PriorityLess>
[[nodiscard]] auto get_cursor(
    const priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>& queue,
    const std::size_t position = 0)
    -> priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>
{
    return {queue, position};
}

template <class Key, class Priority, class T, class KeyLess, class PriorityLess>
[[nodiscard]] auto get_cursor_lower_bound(
    const priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>& queue,
    const Key& key)
    -> priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>
{
    return {queue, queue.count_keys_less_than(key)};
}

template <class Key, class Priority, class T, class KeyLess, class PriorityLess>
[[nodiscard]] auto get_cursor_upper_bound(
    const priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>& queue,
    const Key& key)
    -> priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>
{
    return {queue, queue.count_keys_at_most(key)};
}

template <class Key, class Priority, class T, class KeyLess, class PriorityLess>
[[nodiscard]] auto get_cursor_at_key(
    const priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>& queue,
    const Key& key)
    -> ordered_cursor_search_result<
        priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>>
{
    return {get_cursor_lower_bound(queue, key), queue.contains_key(key)};
}

template <class Key, class Priority, class T, class KeyLess, class PriorityLess>
[[nodiscard]] auto get_cursor_at_minimum_priority(
    const priority_search_queue<Key, Priority, T, KeyLess, PriorityLess>& queue)
    -> priority_search_queue_cursor<Key, Priority, T, KeyLess, PriorityLess>
{
    return queue.empty()
        ? get_cursor(queue)
        : get_cursor_lower_bound(queue, queue.minimum().key());
}

/// Immutable low-endpoint-order root-plus-rank gap cursor over an interval tree.
template <class T, class Comparison = default_comparison<T>>
    requires static_comparison_policy<Comparison, T>
class interval_tree_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          interval_tree_cursor<T, Comparison>,
          interval_tree<T, Comparison>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        interval_tree_cursor<T, Comparison>,
        interval_tree<T, Comparison>>;

public:
    using typename base_type::size_type;
    using value_type = T;
    using comparison_type = Comparison;
    using collection_type = interval_tree<value_type, comparison_type>;
    using interval_type = typename collection_type::interval_type;

    interval_tree_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const interval_type* try_peek_previous() const &
    {
        return this->is_at_start()
            ? nullptr
            : this->snapshot_.try_interval_at_rank(this->position_ - 1);
    }
    const interval_type* try_peek_previous() const && = delete;

    [[nodiscard]] const interval_type* try_peek_next() const &
    {
        return this->snapshot_.try_interval_at_rank(this->position_);
    }
    const interval_type* try_peek_next() const && = delete;

    [[nodiscard]] interval_tree_cursor insert(interval_type item) const
    {
        const auto location = get_cursor_lower_bound(this->snapshot_, item.low).position();
        return {
            this->snapshot_.insert(std::move(item)),
            checked_add(location, size_type{1})};
    }

    [[nodiscard]] interval_tree_cursor delete_previous() const
    {
        if (this->is_at_start()) {
            throw std::logic_error("interval-tree cursor has no previous occurrence");
        }
        return {
            ordered_search_cursor_detail::access::erase_at(
                this->snapshot_,
                this->position_ - 1),
            this->position_ - 1};
    }

    [[nodiscard]] interval_tree_cursor delete_next() const
    {
        if (this->is_at_end()) {
            throw std::logic_error("interval-tree cursor has no next occurrence");
        }
        return {
            ordered_search_cursor_detail::access::erase_at(this->snapshot_, this->position_),
            this->position_};
    }

    [[nodiscard]] ordered_cursor_search_result<interval_tree_cursor> seek_next_overlap(
        const interval_type& query) const
    {
        return find_overlap_cursor_from(
            this->snapshot_,
            this->is_at_end() ? this->size() : this->position_ + 1,
            query);
    }
};

template <class T, class Comparison>
[[nodiscard]] auto get_cursor(
    const interval_tree<T, Comparison>& tree,
    const std::size_t position = 0) -> interval_tree_cursor<T, Comparison>
{
    return {tree, position};
}

template <class T, class Comparison>
[[nodiscard]] auto get_cursor_lower_bound(
    const interval_tree<T, Comparison>& tree,
    const T& low) -> interval_tree_cursor<T, Comparison>
{
    return {tree, tree.count_low_less_than(low)};
}

template <class T, class Comparison>
[[nodiscard]] auto get_cursor_upper_bound(
    const interval_tree<T, Comparison>& tree,
    const T& low) -> interval_tree_cursor<T, Comparison>
{
    return {tree, tree.count_low_at_most(low)};
}

template <class T, class Comparison>
[[nodiscard]] auto get_cursor_at_interval(
    const interval_tree<T, Comparison>& tree,
    const interval<T>& item) -> ordered_cursor_search_result<interval_tree_cursor<T, Comparison>>
{
    const auto start = get_cursor_lower_bound(tree, item.low).position();
    auto iterator = tree.begin();
    for (auto rank = std::size_t{0}; rank != start; ++rank) {
        ++iterator;
    }
    for (auto rank = start; iterator != tree.end(); ++rank, ++iterator) {
        if (Comparison::compare(iterator->low, item.low) != 0) {
            break;
        }
        if (Comparison::compare(iterator->high, item.high) == 0) {
            return {get_cursor(tree, rank), true};
        }
    }
    return {get_cursor(tree, start), false};
}

template <class T, class Comparison>
[[nodiscard]] auto find_overlap_cursor_from(
    const interval_tree<T, Comparison>& tree,
    const std::size_t start,
    const interval<T>& query)
    -> ordered_cursor_search_result<interval_tree_cursor<T, Comparison>>
{
    if (start > tree.size()) {
        throw std::out_of_range("interval cursor search start is outside the tree bounds");
    }
    auto iterator = tree.begin();
    for (auto rank = std::size_t{0}; rank != start; ++rank) {
        ++iterator;
    }
    for (auto rank = start; iterator != tree.end(); ++rank, ++iterator) {
        if (Comparison::compare(iterator->low, query.high) > 0) {
            break;
        }
        if (iterator->template overlaps<Comparison>(query)) {
            return {get_cursor(tree, rank), true};
        }
    }
    return {get_cursor(tree, tree.size()), false};
}

template <class T, class Comparison>
[[nodiscard]] auto find_overlap_cursor(
    const interval_tree<T, Comparison>& tree,
    const interval<T>& query)
    -> ordered_cursor_search_result<interval_tree_cursor<T, Comparison>>
{
    return find_overlap_cursor_from(tree, 0, query);
}

template <class T, class Comparison>
[[nodiscard]] auto find_containing_cursor(
    const interval_tree<T, Comparison>& tree,
    const T& point) -> ordered_cursor_search_result<interval_tree_cursor<T, Comparison>>
{
    return find_overlap_cursor(tree, interval<T>{point, point});
}

/// Immutable interval-key-order root-plus-rank gap cursor over an interval map.
template <
    class Endpoint,
    class T,
    class Comparison = default_comparison<Endpoint>,
    class ValueEqual = std::equal_to<T>>
    requires static_comparison_policy<Comparison, Endpoint>
        && std::copyable<Endpoint>
        && std::copyable<T>
        && std::copyable<ValueEqual>
class persistent_interval_map_cursor final
    : public ordered_search_cursor_detail::rank_cursor_base<
          persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>,
          persistent_interval_map<Endpoint, T, Comparison, ValueEqual>> {
private:
    using base_type = ordered_search_cursor_detail::rank_cursor_base<
        persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>,
        persistent_interval_map<Endpoint, T, Comparison, ValueEqual>>;

public:
    using typename base_type::size_type;
    using endpoint_type = Endpoint;
    using mapped_type = T;
    using collection_type = persistent_interval_map<
        endpoint_type,
        mapped_type,
        Comparison,
        ValueEqual>;
    using interval_type = typename collection_type::interval_type;
    using entry_type = typename collection_type::value_type;

    persistent_interval_map_cursor(collection_type snapshot, const size_type position)
        : base_type(std::move(snapshot), position)
    {
    }

    [[nodiscard]] const entry_type* try_peek_previous() const &
    {
        return this->is_at_start()
            ? nullptr
            : this->snapshot_.try_entry_at_rank(this->position_ - 1);
    }
    const entry_type* try_peek_previous() const && = delete;

    [[nodiscard]] const entry_type* try_peek_next() const &
    {
        return this->snapshot_.try_entry_at_rank(this->position_);
    }
    const entry_type* try_peek_next() const && = delete;

    [[nodiscard]] persistent_interval_map_cursor insert(
        interval_type key,
        mapped_type value) const
    {
        const auto location = get_cursor_lower_bound(this->snapshot_, key).position();
        return {
            this->snapshot_.add(std::move(key), std::move(value)),
            checked_add(location, size_type{1})};
    }

    [[nodiscard]] ordered_cursor_insert_result<persistent_interval_map_cursor> try_insert(
        interval_type key,
        mapped_type value) const
    {
        auto location = get_cursor_lower_bound(this->snapshot_, key);
        auto [updated, inserted] = this->snapshot_.try_add(std::move(key), std::move(value));
        if (!inserted) {
            return {std::move(location), false};
        }
        return {
            persistent_interval_map_cursor{
                std::move(updated),
                checked_add(location.position(), size_type{1})},
            true};
    }

    [[nodiscard]] persistent_interval_map_cursor set_next_value(mapped_type value) const
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("interval-map cursor has no next entry");
        }
        return {this->snapshot_.set_item(entry->key, std::move(value)), this->position_};
    }

    [[nodiscard]] persistent_interval_map_cursor delete_previous() const
    {
        const auto* entry = try_peek_previous();
        if (entry == nullptr) {
            throw std::logic_error("interval-map cursor has no previous entry");
        }
        return {this->snapshot_.remove(entry->key), this->position_ - 1};
    }

    [[nodiscard]] persistent_interval_map_cursor delete_next() const
    {
        const auto* entry = try_peek_next();
        if (entry == nullptr) {
            throw std::logic_error("interval-map cursor has no next entry");
        }
        return {this->snapshot_.remove(entry->key), this->position_};
    }

    [[nodiscard]] ordered_cursor_search_result<persistent_interval_map_cursor>
    seek_next_overlap(const interval_type& query) const
    {
        return find_overlap_cursor_from(
            this->snapshot_,
            this->is_at_end() ? this->size() : this->position_ + 1,
            query);
    }
};

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto get_cursor(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const std::size_t position = 0)
    -> persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>
{
    return {map, position};
}

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto get_cursor_lower_bound(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const interval<Endpoint>& key)
    -> persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>
{
    return {map, map.count_keys_less_than(key)};
}

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto get_cursor_upper_bound(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const interval<Endpoint>& key)
    -> persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>
{
    return {map, map.count_keys_at_most(key)};
}

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto get_cursor_at_key(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const interval<Endpoint>& key)
    -> ordered_cursor_search_result<
        persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>>
{
    return {get_cursor_lower_bound(map, key), map.contains_key(key)};
}

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto find_overlap_cursor_from(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const std::size_t start,
    const interval<Endpoint>& query)
    -> ordered_cursor_search_result<
        persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>>
{
    if (start > map.size()) {
        throw std::out_of_range("interval-map cursor search start is outside the map bounds");
    }
    auto iterator = map.begin();
    for (auto rank = std::size_t{0}; rank != start; ++rank) {
        ++iterator;
    }
    for (auto rank = start; iterator != map.end(); ++rank, ++iterator) {
        if (Comparison::compare(iterator->key.low, query.high) > 0) {
            break;
        }
        if (iterator->key.template overlaps<Comparison>(query)) {
            return {get_cursor(map, rank), true};
        }
    }
    return {get_cursor(map, map.size()), false};
}

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto find_overlap_cursor(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const interval<Endpoint>& query)
    -> ordered_cursor_search_result<
        persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>>
{
    return find_overlap_cursor_from(map, 0, query);
}

template <class Endpoint, class T, class Comparison, class ValueEqual>
[[nodiscard]] auto find_containing_cursor(
    const persistent_interval_map<Endpoint, T, Comparison, ValueEqual>& map,
    const Endpoint& point)
    -> ordered_cursor_search_result<
        persistent_interval_map_cursor<Endpoint, T, Comparison, ValueEqual>>
{
    return find_overlap_cursor(map, interval<Endpoint>{point, point});
}

/// Immutable root-plus-population-rank gap cursor over the present set bits.
class persistent_chunked_bit_set_cursor final {
public:
    using collection_type = persistent_chunked_bit_set;
    using value_type = collection_type::value_type;
    using size_type = collection_type::size_type;

    persistent_chunked_bit_set_cursor(collection_type snapshot, const size_type position)
        : snapshot_(std::move(snapshot))
        , position_(position)
    {
        if (position_ > snapshot_.count()) {
            throw std::out_of_range("chunked-bit-set cursor position is outside the population");
        }
    }

    [[nodiscard]] size_type count() const { return snapshot_.count(); }
    [[nodiscard]] bool empty() const noexcept { return snapshot_.is_empty(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const { return position_ == count(); }

    [[nodiscard]] std::optional<value_type> try_peek_previous() const
    {
        return is_at_start() ? std::nullopt : snapshot_.try_select(position_ - 1);
    }

    [[nodiscard]] std::optional<value_type> try_peek_next() const
    {
        return snapshot_.try_select(position_);
    }

    [[nodiscard]] persistent_chunked_bit_set_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("chunked-bit-set cursor is already at the start");
        }
        return {snapshot_, position_ - 1};
    }

    [[nodiscard]] persistent_chunked_bit_set_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("chunked-bit-set cursor is already at the end");
        }
        return {snapshot_, position_ + 1};
    }

    [[nodiscard]] persistent_chunked_bit_set_cursor seek_rank(const size_type rank) const
    {
        return rank == position_ ? *this : persistent_chunked_bit_set_cursor{snapshot_, rank};
    }

    [[nodiscard]] persistent_chunked_bit_set_cursor add(const value_type bit_index) const
    {
        if (snapshot_.contains(bit_index)) {
            return *this;
        }
        const auto insertion_rank = bit_index <= 0 ? size_type{0} : snapshot_.rank(bit_index - 1);
        return {snapshot_.add(bit_index), checked_add(insertion_rank, size_type{1})};
    }

    [[nodiscard]] persistent_chunked_bit_set_cursor delete_previous() const
    {
        const auto bit = try_peek_previous();
        if (!bit.has_value()) {
            throw std::logic_error("chunked-bit-set cursor has no previous bit");
        }
        return {snapshot_.remove(*bit), position_ - 1};
    }

    [[nodiscard]] persistent_chunked_bit_set_cursor delete_next() const
    {
        const auto bit = try_peek_next();
        if (!bit.has_value()) {
            throw std::logic_error("chunked-bit-set cursor has no next bit");
        }
        return {snapshot_.remove(*bit), position_};
    }

    [[nodiscard]] collection_type snapshot() const { return snapshot_; }

private:
    collection_type snapshot_;
    size_type position_;
};

[[nodiscard]] inline persistent_chunked_bit_set_cursor get_cursor(
    const persistent_chunked_bit_set& set,
    const persistent_chunked_bit_set::size_type position = 0)
{
    return {set, position};
}

[[nodiscard]] inline persistent_chunked_bit_set_cursor get_cursor_at_or_after(
    const persistent_chunked_bit_set& set,
    const persistent_chunked_bit_set::value_type bit_index)
{
    const auto position = bit_index <= 0
        ? persistent_chunked_bit_set::size_type{0}
        : set.rank(bit_index - 1);
    return {set, position};
}

[[nodiscard]] inline ordered_cursor_search_result<persistent_chunked_bit_set_cursor>
get_cursor_at_item(
    const persistent_chunked_bit_set& set,
    const persistent_chunked_bit_set::value_type bit_index)
{
    return {get_cursor_at_or_after(set, bit_index), set.contains(bit_index)};
}

} // namespace tools::data_structures::finger_tree
