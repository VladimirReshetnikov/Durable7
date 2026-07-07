#pragma once

#include <Tools/DataStructures/Hamt/persistent_hash_map.hpp>
#include <tools/data_structures/finger_tree/persistent_deque.hpp>

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::tungsten {

template <
    class Key,
    class T,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueEqual = std::equal_to<T>>
class persistent_association final {
private:
    static constexpr std::int64_t stamp_gap = std::int64_t{1} << 20;

    struct slot final {
        std::int64_t stamp = 0;
        T value;

        slot() = default;

        slot(const std::int64_t stamp_value, T slot_value)
            : stamp(stamp_value)
            , value(std::move(slot_value))
        {
        }
    };

    struct slot_equal final {
        ValueEqual equal{};

        [[nodiscard]] bool operator()(const slot& left, const slot& right) const
        {
            return std::invoke(equal, left.value, right.value);
        }
    };

    struct entry final {
        std::int64_t stamp = 0;
        std::optional<Key> key;
        std::optional<T> value;

        entry() = default;

        entry(const std::int64_t stamp_value, Key entry_key, T entry_value)
            : stamp(stamp_value)
            , key(std::move(entry_key))
            , value(std::move(entry_value))
        {
        }

        [[nodiscard]] static entry stamp_probe(const std::int64_t stamp_value)
        {
            auto result = entry{};
            result.stamp = stamp_value;
            return result;
        }

        [[nodiscard]] const Key& key_ref() const
        {
            return *key;
        }

        [[nodiscard]] const T& value_ref() const
        {
            return *value;
        }

        [[nodiscard]] std::pair<Key, T> to_pair() const
        {
            return {key_ref(), value_ref()};
        }
    };

    struct stamp_less final {
        [[nodiscard]] bool operator()(const entry& left, const entry& right) const noexcept
        {
            return left.stamp < right.stamp;
        }
    };

    using entry_deque = finger_tree::persistent_deque<entry>;
    using index_map = hamt::persistent_hash_map<Key, slot, Hash, KeyEqual, slot_equal>;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<Key, T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using value_equal = ValueEqual;

    class const_iterator;

    persistent_association()
        : persistent_association(Hash{}, KeyEqual{}, ValueEqual{})
    {
    }

    persistent_association(std::initializer_list<value_type> items)
        : persistent_association(create_range(items))
    {
    }

    [[nodiscard]] static persistent_association empty_association()
    {
        return {};
    }

    [[nodiscard]] static persistent_association create(
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {})
    {
        return persistent_association(std::move(hash), std::move(equal), std::move(values_equal));
    }

    [[nodiscard]] static persistent_association create_range(
        std::initializer_list<value_type> items,
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {})
    {
        return create(std::move(hash), std::move(equal), std::move(values_equal)).set_items(items);
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_association create_range(
        Range&& items,
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {})
    {
        return create(std::move(hash), std::move(equal), std::move(values_equal)).set_items(std::forward<Range>(items));
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return entries_.size();
    }

    [[nodiscard]] bool is_empty() const noexcept
    {
        return entries_.empty();
    }

    [[nodiscard]] const Hash& hash_function() const noexcept
    {
        return hash_;
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept
    {
        return key_equal_;
    }

    [[nodiscard]] const ValueEqual& value_eq() const noexcept
    {
        return value_equal_;
    }

    [[nodiscard]] bool contains_key(const Key& key) const
    {
        return index_.contains_key(key);
    }

    [[nodiscard]] const T* try_get(const Key& key) const
    {
        const auto* found = index_.try_get(key);
        return found == nullptr ? nullptr : std::addressof(found->value);
    }

    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const
    {
        return index_.try_get_key(equal_key);
    }

    [[nodiscard]] const T& at(const Key& key) const
    {
        if (const auto* found = try_get(key)) {
            return *found;
        }

        throw std::out_of_range("key is absent from the persistent_association");
    }

    [[nodiscard]] value_type first() const
    {
        throw_if_empty();
        return entries_.front().to_pair();
    }

    [[nodiscard]] value_type last() const
    {
        throw_if_empty();
        return entries_.back().to_pair();
    }

    [[nodiscard]] value_type get_at(const size_type index) const
    {
        return entries_.at(index).to_pair();
    }

    [[nodiscard]] difference_type index_of_key(const Key& key) const
    {
        const auto* found = index_.try_get(key);
        return found == nullptr ? difference_type{-1} : checked_difference(index_of_stamp(found->stamp));
    }

    [[nodiscard]] persistent_association set_item(const Key& key, const T& value) const
    {
        const auto* found = index_.try_get(key);
        if (found == nullptr) {
            return append_new(key, value);
        }

        if (std::invoke(value_equal_, found->value, value)) {
            return *this;
        }

        const auto position = index_of_stamp(found->stamp);
        auto updated_entries = entries_.update_at(position, [&](const entry& stored) {
            return entry{stored.stamp, stored.key_ref(), value};
        });

        return make(std::move(updated_entries), index_.set_item(key, slot{found->stamp, value}));
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_association set_items(Range&& pairs) const
    {
        auto result = *this;
        for (const auto& pair : pairs) {
            result = result.set_item(pair.first, pair.second);
        }

        return result;
    }

    [[nodiscard]] persistent_association set_items(std::initializer_list<value_type> pairs) const
    {
        auto result = *this;
        for (const auto& pair : pairs) {
            result = result.set_item(pair.first, pair.second);
        }

        return result;
    }

    [[nodiscard]] persistent_association join(const persistent_association& other) const
    {
        if (other.is_empty()) {
            return *this;
        }

        if constexpr (std::is_empty_v<Hash> && std::is_empty_v<KeyEqual> && std::is_empty_v<ValueEqual>) {
            if (is_empty()) {
                return other;
            }
        }

        return set_items(other);
    }

    [[nodiscard]] persistent_association append(const Key& key, const T& value) const
    {
        const auto* found = index_.try_get(key);
        if (found == nullptr) {
            return append_new(key, value);
        }

        const auto position = index_of_stamp(found->stamp);
        if (position == size() - 1 && std::invoke(value_equal_, found->value, value)) {
            return *this;
        }

        auto entries = entries_.remove_at(position);
        return with_entry_inserted(std::move(entries), index_.remove(key), entries.size(), key, value);
    }

    [[nodiscard]] persistent_association prepend(const Key& key, const T& value) const
    {
        const auto* found = index_.try_get(key);
        if (found != nullptr) {
            const auto position = index_of_stamp(found->stamp);
            if (position == 0 && std::invoke(value_equal_, found->value, value)) {
                return *this;
            }

            auto entries = entries_.remove_at(position);
            return with_entry_inserted(std::move(entries), index_.remove(key), 0, key, value);
        }

        return with_entry_inserted(entries_, index_, 0, key, value);
    }

    [[nodiscard]] persistent_association insert(const size_type index, const Key& key, const T& value) const
    {
        throw_if_insert_index_out_of_range(index);

        auto entries = entries_;
        auto index_side = index_;
        auto insert_at = index;
        if (const auto* found = index_side.try_get(key)) {
            const auto old_position = index_of_stamp(found->stamp);
            entries = entries.remove_at(old_position);
            index_side = index_side.remove(key);
            if (old_position < insert_at) {
                --insert_at;
            }
        }

        return with_entry_inserted(std::move(entries), std::move(index_side), insert_at, key, value);
    }

    [[nodiscard]] persistent_association remove(const Key& key) const
    {
        auto [result, removed, value] = try_remove(key);
        (void)value;
        return removed ? result : *this;
    }

    [[nodiscard]] std::tuple<persistent_association, bool, std::optional<T>> try_remove(const Key& key) const
    {
        auto [trimmed_index, removed, removed_slot] = index_.try_remove(key);
        if (!removed) {
            return {*this, false, std::nullopt};
        }

        auto entries = entries_.remove_at(index_of_stamp(removed_slot->stamp));
        auto result = entries.empty()
            ? persistent_association{hash_, key_equal_, value_equal_}
            : make(std::move(entries), std::move(trimmed_index));
        return {std::move(result), true, std::move(removed_slot->value)};
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_association remove_range(Range&& keys) const
    {
        auto result = *this;
        for (const auto& key : keys) {
            result = result.remove(key);
        }

        return result;
    }

    template <std::ranges::input_range Range>
    [[nodiscard]] persistent_association key_take(Range&& keys) const
    {
        auto result = persistent_association{hash_, key_equal_, value_equal_};
        for (const auto& key : keys) {
            const auto* found = index_.try_get(key);
            if (found == nullptr || result.contains_key(key)) {
                continue;
            }

            const auto* stored_key = index_.try_get_key(key);
            result = result.append_new(*stored_key, found->value);
        }

        return result;
    }

    [[nodiscard]] persistent_association remove_at(const size_type index) const
    {
        const auto removed = entries_.at(index);
        auto entries = entries_.remove_at(index);
        auto trimmed_index = index_.remove(removed.key_ref());
        return entries.empty()
            ? persistent_association{hash_, key_equal_, value_equal_}
            : make(std::move(entries), std::move(trimmed_index));
    }

    [[nodiscard]] persistent_association remove_first() const
    {
        throw_if_empty();
        return remove_at(0);
    }

    [[nodiscard]] persistent_association remove_last() const
    {
        throw_if_empty();
        return remove_at(size() - 1);
    }

    [[nodiscard]] persistent_association get_range(const size_type index, const size_type count) const
    {
        auto split = entries_.split_range(index, count);
        auto kept = std::move(split.range);
        if (kept.size() == size()) {
            return *this;
        }

        if (kept.empty()) {
            return persistent_association{hash_, key_equal_, value_equal_};
        }

        const auto removed_count = size() - kept.size();
        auto index_side = index_map::create(hash_, key_equal_, slot_equal{value_equal_});
        if (kept.size() <= removed_count) {
            for (const auto& kept_entry : kept) {
                index_side = index_side.set_item(
                    kept_entry.key_ref(),
                    slot{kept_entry.stamp, kept_entry.value_ref()});
            }
        } else {
            index_side = index_;
            for (const auto& removed : split.before) {
                index_side = index_side.remove(removed.key_ref());
            }

            for (const auto& removed : split.after) {
                index_side = index_side.remove(removed.key_ref());
            }
        }

        return make(std::move(kept), std::move(index_side));
    }

    [[nodiscard]] persistent_association take(const size_type count) const
    {
        throw_if_count_out_of_range(count);
        return get_range(0, count);
    }

    [[nodiscard]] persistent_association drop(const size_type count) const
    {
        throw_if_count_out_of_range(count);
        return get_range(count, size() - count);
    }

    [[nodiscard]] persistent_association reverse() const
    {
        if (size() <= 1) {
            return *this;
        }

        auto ordered = entries_.to_vector();
        std::ranges::reverse(ordered);
        return rebuilt(std::move(ordered));
    }

    template <class Compare = std::less<>>
    [[nodiscard]] persistent_association key_sort(Compare compare = {}) const
    {
        if (size() <= 1) {
            return *this;
        }

        auto ordered = entries_.to_vector();
        std::stable_sort(ordered.begin(), ordered.end(), [&](const entry& left, const entry& right) {
            return std::invoke(compare, left.key_ref(), right.key_ref());
        });
        return rebuilt(std::move(ordered));
    }

    template <class Compare = std::less<>>
    [[nodiscard]] persistent_association sort(Compare compare = {}) const
    {
        if (size() <= 1) {
            return *this;
        }

        auto ordered = entries_.to_vector();
        std::stable_sort(ordered.begin(), ordered.end(), [&](const entry& left, const entry& right) {
            return std::invoke(compare, left.value_ref(), right.value_ref());
        });
        return rebuilt(std::move(ordered));
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        for (const auto& item : entries_) {
            result.push_back(item.to_pair());
        }

        return result;
    }

    [[nodiscard]] std::vector<Key> keys() const
    {
        auto result = std::vector<Key>{};
        result.reserve(size());
        for (const auto& item : entries_) {
            result.push_back(item.key_ref());
        }

        return result;
    }

    [[nodiscard]] std::vector<T> values() const
    {
        auto result = std::vector<T>{};
        result.reserve(size());
        for (const auto& item : entries_) {
            result.push_back(item.value_ref());
        }

        return result;
    }

    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{entries_.begin()};
    }

    [[nodiscard]] const_iterator end() const
    {
        return const_iterator{entries_.end()};
    }

    class const_iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = persistent_association::value_type;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;

        [[nodiscard]] value_type operator*() const
        {
            return (*inner_).to_pair();
        }

        const_iterator& operator++()
        {
            ++inner_;
            return *this;
        }

        const_iterator operator++(int)
        {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right)
        {
            return left.inner_ == right.inner_;
        }

        friend bool operator!=(const const_iterator& left, const const_iterator& right)
        {
            return !(left == right);
        }

    private:
        friend class persistent_association;

        explicit const_iterator(typename entry_deque::const_iterator inner)
            : inner_(std::move(inner))
        {
        }

        typename entry_deque::const_iterator inner_{};
    };

private:
    persistent_association(Hash hash, KeyEqual equal, ValueEqual values_equal)
        : hash_(std::move(hash))
        , key_equal_(std::move(equal))
        , value_equal_(std::move(values_equal))
        , entries_()
        , index_(index_map::create(hash_, key_equal_, slot_equal{value_equal_}))
    {
    }

    persistent_association(entry_deque entries, index_map index, Hash hash, KeyEqual equal, ValueEqual values_equal)
        : hash_(std::move(hash))
        , key_equal_(std::move(equal))
        , value_equal_(std::move(values_equal))
        , entries_(std::move(entries))
        , index_(std::move(index))
    {
        if (entries_.size() != index_.count()) {
            throw std::logic_error("persistent_association entry sequence and keyed index differ in size");
        }
    }

    [[nodiscard]] persistent_association make(entry_deque entries, index_map index) const
    {
        return persistent_association{
            std::move(entries),
            std::move(index),
            hash_,
            key_equal_,
            value_equal_};
    }

    [[nodiscard]] persistent_association append_new(const Key& key, const T& value) const
    {
        return with_entry_inserted(entries_, index_, entries_.size(), key, value);
    }

    [[nodiscard]] persistent_association with_entry_inserted(
        entry_deque entries,
        index_map index_side,
        const size_type insert_at,
        const Key& key,
        const T& value) const
    {
        auto stamp = std::int64_t{0};
        if (!try_pick_stamp(entries, insert_at, stamp)) {
            return relabeled(std::move(entries), insert_at, entry{0, key, value});
        }

        auto inserted = entry{stamp, key, value};
        auto grown = entry_deque{};
        if (insert_at == entries.size()) {
            grown = entries.push_back(inserted);
        } else if (insert_at == 0) {
            grown = entries.push_front(inserted);
        } else {
            grown = entries.insert_at(insert_at, inserted);
        }

        return make(std::move(grown), index_side.set_item(key, slot{stamp, value}));
    }

    [[nodiscard]] static bool try_pick_stamp(const entry_deque& entries, const size_type insert_at, std::int64_t& stamp)
    {
        stamp = 0;
        if (entries.empty()) {
            return true;
        }

        if (insert_at == 0) {
            const auto first = entries.front().stamp;
            if (first < (std::numeric_limits<std::int64_t>::min)() + stamp_gap) {
                return false;
            }

            stamp = first - stamp_gap;
            return true;
        }

        if (insert_at == entries.size()) {
            const auto last = entries.back().stamp;
            if (last > (std::numeric_limits<std::int64_t>::max)() - stamp_gap) {
                return false;
            }

            stamp = last + stamp_gap;
            return true;
        }

        const auto left = entries[insert_at - 1].stamp;
        const auto right = entries[insert_at].stamp;
        if (left + 1 >= right) {
            return false;
        }

        stamp = std::midpoint(left, right);
        return true;
    }

    [[nodiscard]] persistent_association relabeled(entry_deque entries, const size_type insert_at, entry inserted) const
    {
        auto ordered = std::vector<entry>{};
        ordered.reserve(entries.size() + 1);

        auto index = size_type{0};
        for (const auto& item : entries) {
            if (index == insert_at) {
                ordered.push_back(inserted);
            }

            ordered.push_back(item);
            ++index;
        }

        if (insert_at == entries.size()) {
            ordered.push_back(std::move(inserted));
        }

        return rebuilt(std::move(ordered));
    }

    [[nodiscard]] persistent_association rebuilt(std::vector<entry> ordered) const
    {
        auto index_side = index_map::create(hash_, key_equal_, slot_equal{value_equal_});
        for (auto index = size_type{0}; index != ordered.size(); ++index) {
            if (index > static_cast<size_type>((std::numeric_limits<std::int64_t>::max)() / stamp_gap)) {
                throw std::overflow_error("persistent_association is too large to relabel");
            }

            const auto stamp = static_cast<std::int64_t>(index) * stamp_gap;
            ordered[index] = entry{stamp, ordered[index].key_ref(), ordered[index].value_ref()};
            index_side = index_side.set_item(ordered[index].key_ref(), slot{stamp, ordered[index].value_ref()});
        }

        return make(entry_deque{ordered.begin(), ordered.end()}, std::move(index_side));
    }

    [[nodiscard]] size_type index_of_stamp(const std::int64_t stamp) const
    {
        const auto found = entries_.sorted_binary_search(entry::stamp_probe(stamp), stamp_less{});
        if (found < 0) {
            throw std::logic_error("stamp recorded in keyed index is absent from association order");
        }

        return static_cast<size_type>(found);
    }

    void throw_if_empty() const
    {
        if (is_empty()) {
            throw std::logic_error("persistent_association is empty");
        }
    }

    void throw_if_count_out_of_range(const size_type count) const
    {
        if (count > size()) {
            throw std::out_of_range("persistent_association count extends past the end");
        }
    }

    void throw_if_insert_index_out_of_range(const size_type index) const
    {
        if (index > size()) {
            throw std::out_of_range("persistent_association insertion index extends past the end");
        }
    }

    [[nodiscard]] static difference_type checked_difference(const size_type value)
    {
        if (value > static_cast<size_type>((std::numeric_limits<difference_type>::max)())) {
            throw std::overflow_error("persistent_association index does not fit in difference_type");
        }

        return static_cast<difference_type>(value);
    }

    Hash hash_{};
    KeyEqual key_equal_{};
    ValueEqual value_equal_{};
    entry_deque entries_;
    index_map index_;
};

} // namespace tools::data_structures::tungsten
