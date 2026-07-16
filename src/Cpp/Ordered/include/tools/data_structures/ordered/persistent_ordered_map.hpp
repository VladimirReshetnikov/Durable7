#pragma once

#include <Tools/DataStructures/Hamt/persistent_hash_map.hpp>
#include <tools/data_structures/finger_tree/persistent_deque.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::ordered {

template <class Key, class Value>
struct ordered_map_entry final {
    Key key;
    Value value;

    [[nodiscard]] bool operator==(const ordered_map_entry&) const = default;
};

/// Immutable insertion-ordered map with comparer-defined keyed lookup and
/// explicit positional reordering.
///
/// Payloads occur only in the deque. The CHAMP index maps retained key
/// representatives to sparse order labels, so replacing a payload does not
/// duplicate it in the hash index and never changes the entry's position.
template <
    class Key,
    class Value,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueEqual = std::equal_to<Value>>
    requires std::copyable<Key>
        && std::copyable<Value>
        && std::copyable<Hash>
        && std::copyable<KeyEqual>
        && std::copyable<ValueEqual>
class persistent_ordered_map final {
private:
    static constexpr std::int64_t stamp_stride = std::int64_t{1} << 20;

    using public_entry = ordered_map_entry<Key, Value>;

    struct entry final {
        std::int64_t stamp = 0;
        // A disengaged item is used only as a private label-search probe.
        // Every entry stored in order_ is engaged, as validation recomputes.
        std::optional<public_entry> item;
    };

    struct stamp_less final {
        [[nodiscard]] bool operator()(const entry& left, const entry& right) const noexcept
        {
            return left.stamp < right.stamp;
        }
    };

    using entry_deque =
        tools::data_structures::finger_tree::persistent_deque<entry>;
    using index_map = tools::data_structures::hamt::persistent_hash_map<
        Key,
        std::int64_t,
        Hash,
        KeyEqual,
        std::equal_to<std::int64_t>>;

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = public_entry;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using value_equal = ValueEqual;

    class const_iterator final {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = public_entry;
        using difference_type = std::ptrdiff_t;
        using pointer = const public_entry*;
        using reference = const public_entry&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const
        {
            return persistent_ordered_map::item_of(*inner_);
        }

        [[nodiscard]] pointer operator->() const
        {
            return std::addressof(operator*());
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

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            return left.inner_ == right.inner_;
        }

    private:
        friend class persistent_ordered_map;

        explicit const_iterator(typename entry_deque::const_iterator inner)
            : inner_(std::move(inner))
        {
        }

        typename entry_deque::const_iterator inner_;
    };

    persistent_ordered_map() = default;

    [[nodiscard]] static persistent_ordered_map empty_map()
    {
        return {};
    }

    [[nodiscard]] static persistent_ordered_map create(
        Hash hash = {},
        KeyEqual key_equal = {},
        ValueEqual value_equal = {})
    {
        return persistent_ordered_map{
            entry_deque{},
            index_map::create(std::move(hash), std::move(key_equal)),
            std::move(value_equal)};
    }

    /// Enumerates once. The first key representative and position win while
    /// the last comparer-distinct payload for each key wins.
    template <std::ranges::input_range Range>
    [[nodiscard]] static persistent_ordered_map create_range(
        Range&& items,
        Hash hash = {},
        KeyEqual key_equal = {},
        ValueEqual value_equal = {})
    {
        auto result = create(
            std::move(hash), std::move(key_equal), std::move(value_equal));
        for (auto&& source : items) {
            auto&& [key, value] = source;
            result = result.set_item(key, value);
        }
        return result;
    }

    [[nodiscard]] size_type size() const noexcept { return order_.size(); }
    [[nodiscard]] size_type count() const noexcept { return size(); }
    [[nodiscard]] bool empty() const noexcept { return order_.empty(); }
    [[nodiscard]] bool is_empty() const noexcept { return empty(); }

    [[nodiscard]] const Hash& hash_function() const noexcept
    {
        return stamps_.hash_function();
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept
    {
        return stamps_.key_eq();
    }

    [[nodiscard]] const ValueEqual& value_eq() const noexcept
    {
        return value_equal_;
    }

    [[nodiscard]] bool contains_key(const Key& key) const
    {
        return stamps_.contains_key(key);
    }

    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const
    {
        return stamps_.try_get_key(equal_key);
    }

    [[nodiscard]] const Value* try_get(const Key& key) const
    {
        const auto* stamp = stamps_.try_get(key);
        if (stamp == nullptr) {
            return nullptr;
        }
        return std::addressof(item_of(order_[index_of_stamp(*stamp)]).value);
    }

    [[nodiscard]] const Value& at(const Key& key) const
    {
        const auto* value = try_get(key);
        if (value == nullptr) {
            throw std::out_of_range("persistent_ordered_map key was not present");
        }
        return *value;
    }

    [[nodiscard]] const value_type& entry_at(const size_type index) const
    {
        check_element_index(index);
        return item_of(order_[index]);
    }

    [[nodiscard]] const value_type& front() const
    {
        throw_if_empty();
        return item_of(order_.front());
    }

    [[nodiscard]] const value_type& back() const
    {
        throw_if_empty();
        return item_of(order_.back());
    }

    [[nodiscard]] difference_type index_of_key(const Key& key) const
    {
        const auto* stamp = stamps_.try_get(key);
        return stamp == nullptr
            ? difference_type{-1}
            : checked_difference(index_of_stamp(*stamp));
    }

    [[nodiscard]] persistent_ordered_map add(const Key& key, const Value& value) const
    {
        auto [result, added] = try_insert(size(), key, value);
        if (!added) {
            throw std::invalid_argument("persistent_ordered_map key already exists");
        }
        return result;
    }

    [[nodiscard]] std::pair<persistent_ordered_map, bool> try_add(
        const Key& key,
        const Value& value) const
    {
        return try_insert(size(), key, value);
    }

    [[nodiscard]] persistent_ordered_map add_first(
        const Key& key,
        const Value& value) const
    {
        auto [result, added] = try_insert(0, key, value);
        if (!added) {
            throw std::invalid_argument("persistent_ordered_map key already exists");
        }
        return result;
    }

    [[nodiscard]] persistent_ordered_map insert(
        const size_type index,
        const Key& key,
        const Value& value) const
    {
        check_insert_index(index);
        auto [result, added] = try_insert(index, key, value);
        if (!added) {
            throw std::invalid_argument("persistent_ordered_map key already exists");
        }
        return result;
    }

    /// Adds or replaces a payload while retaining the first key and position.
    [[nodiscard]] persistent_ordered_map set_item(
        const Key& key,
        const Value& value) const
    {
        const auto* stamp = stamps_.try_get(key);
        if (stamp == nullptr) {
            return add(key, value);
        }

        const auto index = index_of_stamp(*stamp);
        const auto& old = item_of(order_[index]);
        if (std::invoke(value_equal_, old.value, value)) {
            return *this;
        }

        return persistent_ordered_map{
            order_.set_item(index, entry{*stamp, public_entry{old.key, value}}),
            stamps_,
            value_equal_};
    }

    [[nodiscard]] persistent_ordered_map move_to_first(const Key& key) const
    {
        return move_existing(0, key);
    }

    [[nodiscard]] persistent_ordered_map move_to_last(const Key& key) const
    {
        if (empty()) {
            throw std::out_of_range("key is absent from persistent_ordered_map");
        }
        return move_existing(size() - 1, key);
    }

    [[nodiscard]] persistent_ordered_map move_to(
        const size_type final_index,
        const Key& key) const
    {
        check_element_index(final_index);
        return move_existing(final_index, key);
    }

    [[nodiscard]] persistent_ordered_map remove(const Key& key) const
    {
        return try_remove(key).first;
    }

    [[nodiscard]] std::pair<persistent_ordered_map, bool> try_remove(
        const Key& key) const
    {
        const auto* stamp = stamps_.try_get(key);
        if (stamp == nullptr) {
            return {*this, false};
        }
        return {
            wrap(order_.remove_at(index_of_stamp(*stamp)), stamps_.remove(key)),
            true};
    }

    [[nodiscard]] persistent_ordered_map remove_at(const size_type index) const
    {
        check_element_index(index);
        const auto& removed = item_of(order_[index]);
        return wrap(order_.remove_at(index), stamps_.remove(removed.key));
    }

    [[nodiscard]] persistent_ordered_map remove_first() const
    {
        throw_if_empty();
        return remove_at(0);
    }

    [[nodiscard]] persistent_ordered_map remove_last() const
    {
        throw_if_empty();
        return remove_at(size() - 1);
    }

    [[nodiscard]] persistent_ordered_map clear() const
    {
        return empty() ? *this : comparer_preserving_empty();
    }

    [[nodiscard]] persistent_ordered_map get_range(
        const size_type index,
        const size_type range_count) const
    {
        check_range(index, range_count);
        if (range_count == size()) {
            return *this;
        }
        if (range_count == 0) {
            return comparer_preserving_empty();
        }

        auto split = order_.split_range(index, range_count);
        const auto removed_count = size() - range_count;
        if (range_count <= removed_count) {
            auto index_result = build_index(split.range, hash_function(), key_eq());
            return persistent_ordered_map{
                std::move(split.range),
                std::move(index_result),
                value_equal_};
        }

        auto index_result = stamps_;
        index_result = remove_entries(std::move(index_result), split.before);
        index_result = remove_entries(std::move(index_result), split.after);
        return persistent_ordered_map{
            std::move(split.range), std::move(index_result), value_equal_};
    }

    [[nodiscard]] persistent_ordered_map take(const size_type range_count) const
    {
        check_count(range_count);
        return get_range(0, range_count);
    }

    [[nodiscard]] persistent_ordered_map drop(const size_type range_count) const
    {
        check_count(range_count);
        return get_range(range_count, size() - range_count);
    }

    [[nodiscard]] persistent_ordered_map reverse() const
    {
        if (size() <= 1) {
            return *this;
        }
        auto values = to_vector();
        std::ranges::reverse(values);
        return build_from_distinct(values, hash_function(), key_eq(), value_equal_);
    }

    /// Performs a stable one-shot sort; ties retain the previous explicit order.
    template <class Compare>
        requires std::predicate<Compare&, const value_type&, const value_type&>
    [[nodiscard]] persistent_ordered_map sort(Compare compare) const
    {
        if (size() <= 1) {
            return *this;
        }
        auto entries = order_.to_vector();
        std::stable_sort(entries.begin(), entries.end(), [&](const entry& left, const entry& right) {
            return std::invoke(compare, item_of(left), item_of(right));
        });
        for (auto index = size_type{0}; index != entries.size(); ++index) {
            if (entries[index].stamp != order_[index].stamp) {
                return rebuild_entries(std::move(entries));
            }
        }
        return *this;
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        for (const auto& item : *this) {
            result.push_back(item);
        }
        return result;
    }

    [[nodiscard]] std::vector<Key> keys_to_vector() const
    {
        auto result = std::vector<Key>{};
        result.reserve(size());
        for (const auto& item : *this) {
            result.push_back(item.key);
        }
        return result;
    }

    [[nodiscard]] std::vector<Value> values_to_vector() const
    {
        auto result = std::vector<Value>{};
        result.reserve(size());
        for (const auto& item : *this) {
            result.push_back(item.value);
        }
        return result;
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{order_.begin()}; }
    [[nodiscard]] const_iterator end() const { return const_iterator{order_.end()}; }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const { return end(); }

    void validate_invariants() const
    {
        order_.validate_invariants();
        if (!stamps_.debug_validate_canonical()) {
            throw std::logic_error("persistent_ordered_map HAMT index is not canonical");
        }
        if (size() != stamps_.count()) {
            throw std::logic_error("persistent_ordered_map indexes have different counts");
        }

        auto previous = std::optional<std::int64_t>{};
        for (const auto& ordered : order_) {
            const auto& item = item_of(ordered);
            if (previous.has_value() && ordered.stamp <= *previous) {
                throw std::logic_error("persistent_ordered_map labels are not strictly ascending");
            }
            previous = ordered.stamp;
            const auto* indexed_stamp = stamps_.try_get(item.key);
            const auto* indexed_key = stamps_.try_get_key(item.key);
            if (indexed_stamp == nullptr || *indexed_stamp != ordered.stamp
                || indexed_key == nullptr
                || !std::invoke(key_eq(), *indexed_key, item.key)
                || !same_representative(*indexed_key, item.key)) {
                throw std::logic_error("persistent_ordered_map ordered entry is missing from its index");
            }
        }

        for (const auto& [indexed_key, stamp] : stamps_) {
            const auto& item = item_of(order_[index_of_stamp(stamp)]);
            if (!std::invoke(key_eq(), indexed_key, item.key)
                || !same_representative(indexed_key, item.key)) {
                throw std::logic_error("persistent_ordered_map indexed entry is missing from its order");
            }
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

    [[nodiscard]] bool shares_index_with(const persistent_ordered_map& other) const noexcept
    {
        return stamps_.shares_root_with(other.stamps_);
    }

private:
    persistent_ordered_map(entry_deque order, index_map stamps, ValueEqual value_equal)
        : order_(std::move(order))
        , stamps_(std::move(stamps))
        , value_equal_(std::move(value_equal))
    {
    }

    [[nodiscard]] static const value_type& item_of(const entry& value)
    {
        if (!value.item.has_value()) {
            throw std::logic_error("persistent_ordered_map contains a label probe as data");
        }
        return *value.item;
    }

    [[nodiscard]] static bool same_representative(const Key& left, const Key& right)
    {
        if constexpr (std::equality_comparable<Key>) {
            return left == right;
        }
        return true;
    }

    [[nodiscard]] persistent_ordered_map comparer_preserving_empty() const
    {
        return persistent_ordered_map{entry_deque{}, stamps_.clear(), value_equal_};
    }

    [[nodiscard]] persistent_ordered_map wrap(entry_deque order, index_map stamps) const
    {
        return order.empty()
            ? persistent_ordered_map{entry_deque{}, stamps.clear(), value_equal_}
            : persistent_ordered_map{std::move(order), std::move(stamps), value_equal_};
    }

    [[nodiscard]] std::pair<persistent_ordered_map, bool> try_insert(
        const size_type index,
        const Key& key,
        const Value& value) const
    {
        auto [provisional, added] = stamps_.try_add(key, std::int64_t{0});
        if (!added) {
            return {*this, false};
        }
        ensure_can_add(size());

        auto stamp = std::int64_t{0};
        if (!try_pick_stamp(order_, index, stamp)) {
            return {rebuild_inserted(index, key, value), true};
        }
        auto ordered = insert_entry(
            order_, index, entry{stamp, public_entry{key, value}});
        return {
            persistent_ordered_map{
                std::move(ordered),
                provisional.set_item(key, stamp),
                value_equal_},
            true};
    }

    [[nodiscard]] persistent_ordered_map move_existing(
        const size_type final_index,
        const Key& key) const
    {
        const auto* old_stamp = stamps_.try_get(key);
        if (old_stamp == nullptr) {
            throw std::out_of_range("key is absent from persistent_ordered_map");
        }
        const auto old_index = index_of_stamp(*old_stamp);
        if (old_index == final_index) {
            return *this;
        }

        const auto stored = item_of(order_[old_index]);
        auto trimmed = order_.remove_at(old_index);
        auto stamp = std::int64_t{0};
        if (!try_pick_stamp(trimmed, final_index, stamp)) {
            auto entries = trimmed.to_vector();
            entries.insert(
                entries.begin() + checked_difference(final_index),
                entry{0, stored});
            return rebuild_entries(std::move(entries));
        }

        auto ordered = insert_entry(trimmed, final_index, entry{stamp, stored});
        return persistent_ordered_map{
            std::move(ordered), stamps_.set_item(stored.key, stamp), value_equal_};
    }

    [[nodiscard]] static entry_deque insert_entry(
        const entry_deque& order,
        const size_type index,
        entry value)
    {
        if (index == 0) {
            return order.add_first(std::move(value));
        }
        if (index == order.size()) {
            return order.add_last(std::move(value));
        }
        return order.insert_at(index, std::move(value));
    }

    [[nodiscard]] static bool try_pick_stamp(
        const entry_deque& order,
        const size_type index,
        std::int64_t& stamp)
    {
        stamp = 0;
        if (order.empty()) {
            return true;
        }
        if (index == 0) {
            const auto first = order.front().stamp;
            if (first < (std::numeric_limits<std::int64_t>::min)() + stamp_stride) {
                return false;
            }
            stamp = first - stamp_stride;
            return true;
        }
        if (index == order.size()) {
            const auto last = order.back().stamp;
            if (last > (std::numeric_limits<std::int64_t>::max)() - stamp_stride) {
                return false;
            }
            stamp = last + stamp_stride;
            return true;
        }
        const auto left = order[index - 1].stamp;
        const auto right = order[index].stamp;
        if (left >= right) {
            return false;
        }
        stamp = std::midpoint(left, right);
        return stamp != left && stamp != right;
    }

    [[nodiscard]] size_type index_of_stamp(const std::int64_t stamp) const
    {
        const auto index = order_.sorted_lower_bound(entry{stamp, std::nullopt}, stamp_less{});
        if (index >= size() || order_[index].stamp != stamp) {
            throw std::logic_error("persistent_ordered_map indexes disagree");
        }
        return index;
    }

    [[nodiscard]] persistent_ordered_map rebuild_inserted(
        const size_type index,
        const Key& key,
        const Value& value) const
    {
        auto entries = order_.to_vector();
        entries.insert(
            entries.begin() + checked_difference(index),
            entry{0, public_entry{key, value}});
        return rebuild_entries(std::move(entries));
    }

    [[nodiscard]] persistent_ordered_map rebuild_entries(std::vector<entry> entries) const
    {
        if (entries.empty()) {
            return comparer_preserving_empty();
        }
        auto builder = index_map::create_bulk_builder(hash_function(), key_eq());
        for (auto index = size_type{0}; index != entries.size(); ++index) {
            entries[index].stamp = checked_stamp(index);
            builder.set_item(item_of(entries[index]).key, entries[index].stamp);
        }
        return persistent_ordered_map{
            entry_deque{entries.begin(), entries.end()},
            builder.to_immutable(),
            value_equal_};
    }

    [[nodiscard]] static persistent_ordered_map build_from_distinct(
        const std::vector<value_type>& items,
        const Hash& hash,
        const KeyEqual& key_equal,
        const ValueEqual& value_equal)
    {
        if (items.empty()) {
            return create(hash, key_equal, value_equal);
        }
        auto entries = std::vector<entry>{};
        entries.reserve(items.size());
        auto builder = index_map::create_bulk_builder(hash, key_equal);
        for (auto index = size_type{0}; index != items.size(); ++index) {
            const auto stamp = checked_stamp(index);
            entries.push_back(entry{stamp, items[index]});
            builder.set_item(items[index].key, stamp);
        }
        return persistent_ordered_map{
            entry_deque{entries.begin(), entries.end()},
            builder.to_immutable(),
            value_equal};
    }

    [[nodiscard]] static index_map build_index(
        const entry_deque& order,
        const Hash& hash,
        const KeyEqual& key_equal)
    {
        auto builder = index_map::create_bulk_builder(hash, key_equal);
        for (const auto& stored : order) {
            builder.set_item(item_of(stored).key, stored.stamp);
        }
        return builder.to_immutable();
    }

    [[nodiscard]] static index_map remove_entries(index_map index, const entry_deque& removed)
    {
        for (const auto& stored : removed) {
            index = index.remove(item_of(stored).key);
        }
        return index;
    }

    static void ensure_can_add(const size_type current_count)
    {
        constexpr auto maximum_index =
            static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)())
            / static_cast<std::uintmax_t>(stamp_stride);
        if (static_cast<std::uintmax_t>(current_count) > maximum_index) {
            throw std::overflow_error("persistent_ordered_map label capacity is exhausted");
        }
    }

    [[nodiscard]] static std::int64_t checked_stamp(const size_type index)
    {
        constexpr auto maximum_index =
            static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)())
            / static_cast<std::uintmax_t>(stamp_stride);
        if (static_cast<std::uintmax_t>(index) > maximum_index) {
            throw std::overflow_error("persistent_ordered_map label capacity is exhausted");
        }
        return static_cast<std::int64_t>(
            static_cast<std::uintmax_t>(index)
            * static_cast<std::uintmax_t>(stamp_stride));
    }

    [[nodiscard]] static difference_type checked_difference(const size_type value)
    {
        if (value > static_cast<size_type>((std::numeric_limits<difference_type>::max)())) {
            throw std::overflow_error("persistent_ordered_map index does not fit difference_type");
        }
        return static_cast<difference_type>(value);
    }

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("persistent_ordered_map is empty");
        }
    }

    void check_element_index(const size_type index) const
    {
        if (index >= size()) {
            throw std::out_of_range("persistent_ordered_map index does not identify an entry");
        }
    }

    void check_insert_index(const size_type index) const
    {
        if (index > size()) {
            throw std::out_of_range("persistent_ordered_map insertion index is past the end");
        }
    }

    void check_range(const size_type index, const size_type range_count) const
    {
        if (index > size() || range_count > size() - index) {
            throw std::out_of_range("persistent_ordered_map range extends past the end");
        }
    }

    void check_count(const size_type range_count) const
    {
        if (range_count > size()) {
            throw std::out_of_range("persistent_ordered_map count exceeds its size");
        }
    }

    entry_deque order_;
    index_map stamps_;
    [[no_unique_address]] ValueEqual value_equal_{};
};

} // namespace tools::data_structures::ordered
