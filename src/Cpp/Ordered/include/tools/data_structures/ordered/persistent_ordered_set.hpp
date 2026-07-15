#pragma once

#include <Tools/DataStructures/Hamt/persistent_hash_map.hpp>
#include <tools/data_structures/finger_tree/persistent_deque.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::ordered {

/// An immutable insertion-ordered set with comparer-defined membership and
/// explicit positional reordering.
///
/// The set owns a neutral dual index: a public CHAMP map records the sparse
/// order-maintenance label for each equality class, while a public persistent
/// deque stores representatives in label order. Adding an equivalent value is
/// a no-op; only the move operations change the position of an existing class.
template <
    class T,
    class Hash = std::hash<T>,
    class KeyEqual = std::equal_to<T>>
    requires std::copyable<T>
        && std::copyable<Hash>
        && std::copyable<KeyEqual>
class persistent_ordered_set final {
private:
    static constexpr std::int64_t stamp_stride = std::int64_t{1} << 20;

    struct entry final {
        std::int64_t stamp = 0;
        // A disengaged item exists only in a private lower-bound probe. Every
        // entry in order_ has an engaged item, as validate_invariants checks.
        std::optional<T> item;
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
        T,
        std::int64_t,
        Hash,
        KeyEqual,
        std::equal_to<std::int64_t>>;
    using membership_map = tools::data_structures::hamt::persistent_hash_map<
        T,
        std::uint8_t,
        Hash,
        KeyEqual,
        std::equal_to<std::uint8_t>>;

    struct normalized_argument final {
        std::vector<T> items;
        membership_map membership;
    };

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using const_reference = const T&;

    class const_iterator final {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const
        {
            return persistent_ordered_set::item_of(*inner_);
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

        friend bool operator!=(const const_iterator& left, const const_iterator& right) noexcept
        {
            return !(left == right);
        }

    private:
        friend class persistent_ordered_set;

        explicit const_iterator(typename entry_deque::const_iterator inner)
            : inner_(std::move(inner))
        {
        }

        typename entry_deque::const_iterator inner_;
    };

    persistent_ordered_set() = default;
    persistent_ordered_set(const persistent_ordered_set&) = default;
    persistent_ordered_set(persistent_ordered_set&&) noexcept(
        std::is_nothrow_move_constructible_v<entry_deque>
        && std::is_nothrow_move_constructible_v<index_map>) = default;
    persistent_ordered_set& operator=(const persistent_ordered_set&) = default;
    persistent_ordered_set& operator=(persistent_ordered_set&&) noexcept(
        std::is_nothrow_move_assignable_v<entry_deque>
        && std::is_nothrow_move_assignable_v<index_map>) = default;
    ~persistent_ordered_set() = default;

    /// Returns an empty set with default-constructed policies.
    [[nodiscard]] static persistent_ordered_set empty_set()
    {
        return {};
    }

    /// Returns an empty set retaining the supplied policy values.
    [[nodiscard]] static persistent_ordered_set create(
        Hash hash = {},
        KeyEqual equal = {})
    {
        return persistent_ordered_set{
            entry_deque{},
            index_map::create(std::move(hash), std::move(equal))};
    }

    /// Enumerates a range once, retaining the first representative and first
    /// position of every receiver-policy equality class.
    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static persistent_ordered_set create_range(
        Range&& items,
        Hash hash = {},
        KeyEqual equal = {})
    {
        return create_range_impl(
            std::forward<Range>(items), std::move(hash), std::move(equal));
    }

    [[nodiscard]] static persistent_ordered_set create_range(
        std::initializer_list<T> items,
        Hash hash = {},
        KeyEqual equal = {})
    {
        return create_range_impl(items, std::move(hash), std::move(equal));
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return order_.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return order_.empty();
    }

    [[nodiscard]] size_type count() const noexcept
    {
        return size();
    }

    [[nodiscard]] bool is_empty() const noexcept
    {
        return empty();
    }

    [[nodiscard]] const Hash& hash_function() const noexcept
    {
        return stamps_.hash_function();
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept
    {
        return stamps_.key_eq();
    }

    [[nodiscard]] const_reference front() const
    {
        throw_if_empty();
        return item_of(order_.front());
    }

    [[nodiscard]] const_reference back() const
    {
        throw_if_empty();
        return item_of(order_.back());
    }

    [[nodiscard]] const_reference first() const
    {
        return front();
    }

    [[nodiscard]] const_reference last() const
    {
        return back();
    }

    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] const_reference at(const size_type index) const
    {
        check_element_index(index);
        return item_of(order_[index]);
    }

    [[nodiscard]] const_reference get_at(const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] bool contains(const T& item) const
    {
        return stamps_.contains_key(item);
    }

    /// Returns the first stored representative equivalent to equal_value, or
    /// null when the class is absent. The pointer remains valid while a set
    /// version retaining the corresponding HAMT node remains alive.
    [[nodiscard]] const T* try_get_value(const T& equal_value) const
    {
        return stamps_.try_get_key(equal_value);
    }

    [[nodiscard]] const T* try_get(const T& equal_value) const
    {
        return try_get_value(equal_value);
    }

    [[nodiscard]] difference_type index_of(const T& equal_value) const
    {
        const auto* stamp = stamps_.try_get(equal_value);
        return stamp == nullptr ? difference_type{-1} : checked_difference(index_of_stamp(*stamp));
    }

    /// Appends an absent class. An equivalent present value is an identity
    /// no-op and does not replace or move its stored representative.
    [[nodiscard]] persistent_ordered_set add(const T& item) const
    {
        return insert_absent(size(), item);
    }

    [[nodiscard]] persistent_ordered_set add_first(const T& item) const
    {
        return insert_absent(0, item);
    }

    [[nodiscard]] persistent_ordered_set insert(const size_type index, const T& item) const
    {
        check_insert_index(index);
        return insert_absent(index, item);
    }

    [[nodiscard]] persistent_ordered_set move_to_first(const T& equal_value) const
    {
        return move_existing(0, equal_value);
    }

    [[nodiscard]] persistent_ordered_set move_to_last(const T& equal_value) const
    {
        return move_existing(empty() ? 0 : size() - 1, equal_value);
    }

    /// Moves an existing class to final_index, interpreted as its position in
    /// the result after removal from the old position.
    [[nodiscard]] persistent_ordered_set move_to(
        const size_type final_index,
        const T& equal_value) const
    {
        check_element_index(final_index);
        return move_existing(final_index, equal_value);
    }

    [[nodiscard]] persistent_ordered_set remove(const T& equal_value) const
    {
        const auto* stamp = stamps_.try_get(equal_value);
        if (stamp == nullptr) {
            return *this;
        }

        const auto index = index_of_stamp(*stamp);
        return persistent_ordered_set{
            order_.remove_at(index),
            stamps_.remove(equal_value)};
    }

    [[nodiscard]] std::pair<persistent_ordered_set, bool> try_remove(
        const T& equal_value) const
    {
        const auto* stamp = stamps_.try_get(equal_value);
        if (stamp == nullptr) {
            return {*this, false};
        }

        const auto index = index_of_stamp(*stamp);
        return {
            persistent_ordered_set{
                order_.remove_at(index),
                stamps_.remove(equal_value)},
            true};
    }

    [[nodiscard]] persistent_ordered_set remove_at(const size_type index) const
    {
        check_element_index(index);
        const auto& removed = item_of(order_[index]);
        return persistent_ordered_set{
            order_.remove_at(index),
            stamps_.remove(removed)};
    }

    [[nodiscard]] persistent_ordered_set remove_first() const
    {
        throw_if_empty();
        return remove_at(0);
    }

    [[nodiscard]] persistent_ordered_set remove_last() const
    {
        throw_if_empty();
        return remove_at(size() - 1);
    }

    [[nodiscard]] persistent_ordered_set clear() const
    {
        if (empty()) {
            return *this;
        }

        return persistent_ordered_set{entry_deque{}, stamps_.clear()};
    }

    [[nodiscard]] persistent_ordered_set get_range(
        const size_type index,
        const size_type count) const
    {
        check_range(index, count);
        if (count == size()) {
            return *this;
        }
        if (count == 0) {
            return comparer_preserving_empty();
        }

        auto split = order_.split_range(index, count);
        const auto removed_count = size() - count;
        if (count <= removed_count) {
            auto index_result = build_index(split.range, hash_function(), key_eq());
            return persistent_ordered_set{
                std::move(split.range),
                std::move(index_result)};
        }

        auto index_result = stamps_;
        index_result = remove_entries(std::move(index_result), split.before);
        index_result = remove_entries(std::move(index_result), split.after);
        return persistent_ordered_set{std::move(split.range), std::move(index_result)};
    }

    [[nodiscard]] persistent_ordered_set take(const size_type count) const
    {
        check_count(count);
        return get_range(0, count);
    }

    [[nodiscard]] persistent_ordered_set drop(const size_type count) const
    {
        check_count(count);
        return get_range(count, size() - count);
    }

    [[nodiscard]] persistent_ordered_set reverse() const
    {
        if (size() <= 1) {
            return *this;
        }

        const auto entries = order_.to_vector();
        auto items = std::vector<T>{};
        items.reserve(size());
        for (auto iterator = entries.rbegin();
             iterator != entries.rend();
             ++iterator) {
            items.push_back(item_of(*iterator));
        }
        return build_from_items(items, hash_function(), key_eq());
    }

    /// Performs a stable one-shot sort. Ordering ties retain the old order;
    /// the set's hash/equality policy remains unchanged.
    template <class Compare = std::less<>>
        requires std::predicate<Compare&, const T&, const T&>
    [[nodiscard]] persistent_ordered_set sort(Compare compare = {}) const
    {
        if (size() <= 1) {
            return *this;
        }

        auto entries = order_.to_vector();
        std::stable_sort(entries.begin(), entries.end(), [&](const entry& left, const entry& right) {
            return std::invoke(compare, item_of(left), item_of(right));
        });

        auto unchanged = true;
        for (auto index = size_type{0}; index != entries.size(); ++index) {
            if (entries[index].stamp != order_[index].stamp) {
                unchanged = false;
                break;
            }
        }
        return unchanged ? *this : rebuild_entries(std::move(entries));
    }

    [[nodiscard]] persistent_ordered_set union_with(
        const persistent_ordered_set& other) const
    {
        return union_core(other);
    }

    [[nodiscard]] persistent_ordered_set union_with(
        std::initializer_list<T> other) const
    {
        return union_core(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set union_with(Range&& other) const
    {
        return union_core(std::forward<Range>(other));
    }

    [[nodiscard]] persistent_ordered_set intersect_with(
        const persistent_ordered_set& other) const
    {
        return intersect_core(other);
    }

    [[nodiscard]] persistent_ordered_set intersect_with(
        std::initializer_list<T> other) const
    {
        return intersect_core(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set intersect_with(Range&& other) const
    {
        return intersect_core(std::forward<Range>(other));
    }

    [[nodiscard]] persistent_ordered_set except_with(
        const persistent_ordered_set& other) const
    {
        return except_core(other);
    }

    [[nodiscard]] persistent_ordered_set except_with(
        std::initializer_list<T> other) const
    {
        return except_core(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set except_with(Range&& other) const
    {
        return except_core(std::forward<Range>(other));
    }

    [[nodiscard]] persistent_ordered_set symmetric_except_with(
        const persistent_ordered_set& other) const
    {
        return symmetric_except_core(other);
    }

    [[nodiscard]] persistent_ordered_set symmetric_except_with(
        std::initializer_list<T> other) const
    {
        return symmetric_except_core(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set symmetric_except_with(Range&& other) const
    {
        return symmetric_except_core(std::forward<Range>(other));
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] bool is_subset_of(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        return size() <= argument.items.size()
            && every_receiver_occurs_in(argument.membership);
    }

    [[nodiscard]] bool is_subset_of(std::initializer_list<T> other) const
    {
        return is_subset_of<std::initializer_list<T>&>(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] bool is_proper_subset_of(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        return size() < argument.items.size()
            && every_receiver_occurs_in(argument.membership);
    }

    [[nodiscard]] bool is_proper_subset_of(std::initializer_list<T> other) const
    {
        return is_proper_subset_of<std::initializer_list<T>&>(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] bool is_superset_of(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        return size() >= argument.items.size()
            && every_argument_occurs_here(argument.items);
    }

    [[nodiscard]] bool is_superset_of(std::initializer_list<T> other) const
    {
        return is_superset_of<std::initializer_list<T>&>(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] bool is_proper_superset_of(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        return size() > argument.items.size()
            && every_argument_occurs_here(argument.items);
    }

    [[nodiscard]] bool is_proper_superset_of(std::initializer_list<T> other) const
    {
        return is_proper_superset_of<std::initializer_list<T>&>(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] bool overlaps(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        return std::ranges::any_of(argument.items, [&](const T& item) {
            return contains(item);
        });
    }

    [[nodiscard]] bool overlaps(std::initializer_list<T> other) const
    {
        return overlaps<std::initializer_list<T>&>(other);
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] bool set_equals(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        return size() == argument.items.size()
            && every_argument_occurs_here(argument.items);
    }

    [[nodiscard]] bool set_equals(std::initializer_list<T> other) const
    {
        return set_equals<std::initializer_list<T>&>(other);
    }

    [[nodiscard]] std::vector<T> to_vector() const
    {
        auto result = std::vector<T>{};
        result.reserve(size());
        for (const auto& item : *this) {
            result.push_back(item);
        }
        return result;
    }

    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{order_.begin()};
    }

    [[nodiscard]] const_iterator end() const
    {
        return const_iterator{order_.end()};
    }

    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    [[nodiscard]] const_iterator cend() const
    {
        return end();
    }

    /// Recomputes the complete dual-index invariant. This diagnostic is
    /// intentionally public because native consumers do not have an
    /// InternalsVisibleTo-style test hook.
    void validate_invariants() const
    {
        order_.validate_invariants();
        if (!stamps_.debug_validate_canonical()) {
            throw std::logic_error("persistent_ordered_set HAMT index is not canonical");
        }
        if (size() != stamps_.count()) {
            throw std::logic_error("persistent_ordered_set indexes have different counts");
        }

        auto has_previous = false;
        auto previous = std::int64_t{0};
        for (const auto& ordered : order_) {
            const auto& item = item_of(ordered);
            if (has_previous && ordered.stamp <= previous) {
                throw std::logic_error("persistent_ordered_set labels are not strictly ascending");
            }
            has_previous = true;
            previous = ordered.stamp;

            const auto* indexed_stamp = stamps_.try_get(item);
            const auto* indexed_item = stamps_.try_get_key(item);
            if (indexed_stamp == nullptr || *indexed_stamp != ordered.stamp
                || indexed_item == nullptr
                || !std::invoke(key_eq(), *indexed_item, item)
                || !same_representative(*indexed_item, item)) {
                throw std::logic_error("persistent_ordered_set ordered entry is missing from its index");
            }
        }

        for (const auto& [indexed_item, stamp] : stamps_) {
            const auto position = index_of_stamp(stamp);
            const auto& ordered_item = item_of(order_[position]);
            if (!std::invoke(key_eq(), indexed_item, ordered_item)
                || !same_representative(indexed_item, ordered_item)) {
                throw std::logic_error("persistent_ordered_set indexed entry is missing from its order");
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

    /// Reports whether the two facades retain the same CHAMP root. It is a
    /// diagnostic for persistence/no-op tests, not an ownership guarantee.
    [[nodiscard]] bool shares_index_with(const persistent_ordered_set& other) const noexcept
    {
        return stamps_.shares_root_with(other.stamps_);
    }

private:
    persistent_ordered_set(entry_deque order, index_map stamps)
        : order_(std::move(order)), stamps_(std::move(stamps))
    {
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static persistent_ordered_set create_range_impl(
        Range&& items,
        Hash hash,
        KeyEqual equal)
    {
        auto seen = membership_map::create(hash, equal);
        auto distinct = std::vector<T>{};
        for (auto&& source_item : items) {
            auto item = T(std::forward<decltype(source_item)>(source_item));
            auto [candidate, added] = seen.try_add(item, std::uint8_t{0});
            if (!added) {
                continue;
            }
            seen = std::move(candidate);
            distinct.push_back(std::move(item));
        }

        return build_from_items(distinct, seen.hash_function(), seen.key_eq());
    }

    [[nodiscard]] static const T& item_of(const entry& value)
    {
        if (!value.item.has_value()) {
            throw std::logic_error("persistent_ordered_set contains a label probe as data");
        }
        return *value.item;
    }

    [[nodiscard]] static bool same_representative(const T& left, const T& right)
    {
        if constexpr (std::equality_comparable<T>) {
            return left == right;
        } else {
            // C++ has no general identity predicate for copied value-semantic
            // objects. Equality-comparable values receive the stronger check;
            // other values are still checked through the retained KeyEqual.
            return true;
        }
    }

    [[nodiscard]] persistent_ordered_set comparer_preserving_empty() const
    {
        return persistent_ordered_set{entry_deque{}, stamps_.clear()};
    }

    [[nodiscard]] persistent_ordered_set insert_absent(
        const size_type index,
        const T& item) const
    {
        auto [provisional, added] = stamps_.try_add(item, std::int64_t{0});
        if (!added) {
            return *this;
        }
        ensure_can_add();

        auto stamp = std::int64_t{0};
        if (!try_pick_stamp(order_, index, stamp)) {
            return rebuild_inserted(order_, index, item);
        }

        auto ordered = insert_entry(order_, index, entry{stamp, item});
        return persistent_ordered_set{
            std::move(ordered),
            provisional.set_item(item, stamp)};
    }

    [[nodiscard]] persistent_ordered_set move_existing(
        const size_type final_index,
        const T& equal_value) const
    {
        const auto* old_stamp = stamps_.try_get(equal_value);
        if (old_stamp == nullptr) {
            throw std::out_of_range("value is absent from persistent_ordered_set");
        }

        const auto old_index = index_of_stamp(*old_stamp);
        if (old_index == final_index) {
            return *this;
        }

        const auto stored = item_of(order_[old_index]);
        auto trimmed = order_.remove_at(old_index);
        auto stamp = std::int64_t{0};
        if (!try_pick_stamp(trimmed, final_index, stamp)) {
            return rebuild_inserted(trimmed, final_index, stored);
        }

        auto ordered = insert_entry(trimmed, final_index, entry{stamp, stored});
        return persistent_ordered_set{
            std::move(ordered),
            stamps_.set_item(stored, stamp)};
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
            throw std::logic_error("persistent_ordered_set indexes disagree");
        }
        return index;
    }

    [[nodiscard]] persistent_ordered_set rebuild_inserted(
        const entry_deque& order,
        const size_type index,
        const T& item) const
    {
        ensure_can_add(order.size());
        auto entries = order.to_vector();
        entries.insert(
            entries.begin() + checked_difference(index),
            entry{0, item});
        return rebuild_entries(std::move(entries));
    }

    [[nodiscard]] persistent_ordered_set rebuild_entries(std::vector<entry> entries) const
    {
        if (entries.empty()) {
            return comparer_preserving_empty();
        }

        auto builder = index_map::create_bulk_builder(hash_function(), key_eq());
        for (auto index = size_type{0}; index != entries.size(); ++index) {
            entries[index].stamp = checked_stamp(index);
            builder.set_item(item_of(entries[index]), entries[index].stamp);
        }

        return persistent_ordered_set{
            entry_deque{entries.begin(), entries.end()},
            builder.to_immutable()};
    }

    [[nodiscard]] static persistent_ordered_set build_from_items(
        const std::vector<T>& items,
        const Hash& hash,
        const KeyEqual& equal)
    {
        if (items.empty()) {
            return create(hash, equal);
        }

        auto entries = std::vector<entry>{};
        entries.reserve(items.size());
        auto builder = index_map::create_bulk_builder(hash, equal);
        for (auto index = size_type{0}; index != items.size(); ++index) {
            const auto stamp = checked_stamp(index);
            entries.push_back(entry{stamp, items[index]});
            builder.set_item(items[index], stamp);
        }

        return persistent_ordered_set{
            entry_deque{entries.begin(), entries.end()},
            builder.to_immutable()};
    }

    [[nodiscard]] static index_map build_index(
        const entry_deque& order,
        const Hash& hash,
        const KeyEqual& equal)
    {
        auto builder = index_map::create_bulk_builder(hash, equal);
        for (const auto& value : order) {
            builder.set_item(item_of(value), value.stamp);
        }
        return builder.to_immutable();
    }

    [[nodiscard]] static index_map remove_entries(index_map index, const entry_deque& removed)
    {
        for (const auto& value : removed) {
            index = index.remove(item_of(value));
        }
        return index;
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] normalized_argument normalize(Range&& other) const
    {
        auto membership = membership_map::create(hash_function(), key_eq());
        auto items = std::vector<T>{};
        for (auto&& source_item : other) {
            auto item = T(std::forward<decltype(source_item)>(source_item));
            auto [candidate, added] = membership.try_add(item, std::uint8_t{0});
            if (!added) {
                continue;
            }
            membership = std::move(candidate);
            items.push_back(std::move(item));
        }
        return normalized_argument{std::move(items), std::move(membership)};
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set union_core(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        auto result = to_vector();
        auto changed = false;
        for (const auto& item : argument.items) {
            if (contains(item)) {
                continue;
            }
            result.push_back(item);
            changed = true;
        }
        return changed ? build_from_items(result, hash_function(), key_eq()) : *this;
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set intersect_core(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        auto result = std::vector<T>{};
        result.reserve((std::min)(size(), argument.items.size()));
        for (const auto& value : order_) {
            const auto& item = item_of(value);
            if (argument.membership.contains_key(item)) {
                result.push_back(item);
            }
        }
        return result.size() == size()
            ? *this
            : build_from_items(result, hash_function(), key_eq());
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set except_core(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        auto result = std::vector<T>{};
        result.reserve(size());
        for (const auto& value : order_) {
            const auto& item = item_of(value);
            if (!argument.membership.contains_key(item)) {
                result.push_back(item);
            }
        }
        return result.size() == size()
            ? *this
            : build_from_items(result, hash_function(), key_eq());
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
    [[nodiscard]] persistent_ordered_set symmetric_except_core(Range&& other) const
    {
        auto argument = normalize(std::forward<Range>(other));
        if (argument.items.empty()) {
            return *this;
        }

        auto result = std::vector<T>{};
        if (argument.items.size()
            > (std::numeric_limits<size_type>::max)() - size()) {
            throw std::overflow_error("persistent_ordered_set algebra result is too large");
        }
        result.reserve(size() + argument.items.size());
        for (const auto& value : order_) {
            const auto& item = item_of(value);
            if (!argument.membership.contains_key(item)) {
                result.push_back(item);
            }
        }
        for (const auto& item : argument.items) {
            if (!contains(item)) {
                result.push_back(item);
            }
        }
        return build_from_items(result, hash_function(), key_eq());
    }

    [[nodiscard]] bool every_receiver_occurs_in(const membership_map& membership) const
    {
        for (const auto& value : order_) {
            if (!membership.contains_key(item_of(value))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool every_argument_occurs_here(const std::vector<T>& items) const
    {
        return std::ranges::all_of(items, [&](const T& item) {
            return contains(item);
        });
    }

    void ensure_can_add() const
    {
        ensure_can_add(size());
    }

    static void ensure_can_add(const size_type current_count)
    {
        constexpr auto maximum_index =
            static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)())
            / static_cast<std::uintmax_t>(stamp_stride);
        if (static_cast<std::uintmax_t>(current_count) > maximum_index) {
            throw std::overflow_error("persistent_ordered_set label capacity is exhausted");
        }
    }

    [[nodiscard]] static std::int64_t checked_stamp(const size_type index)
    {
        constexpr auto maximum_index =
            static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)())
            / static_cast<std::uintmax_t>(stamp_stride);
        if (static_cast<std::uintmax_t>(index) > maximum_index) {
            throw std::overflow_error("persistent_ordered_set label capacity is exhausted");
        }
        return static_cast<std::int64_t>(
            static_cast<std::uintmax_t>(index)
            * static_cast<std::uintmax_t>(stamp_stride));
    }

    [[nodiscard]] static difference_type checked_difference(const size_type value)
    {
        if (value > static_cast<size_type>((std::numeric_limits<difference_type>::max)())) {
            throw std::overflow_error("persistent_ordered_set index does not fit in difference_type");
        }
        return static_cast<difference_type>(value);
    }

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("persistent_ordered_set is empty");
        }
    }

    void check_element_index(const size_type index) const
    {
        if (index >= size()) {
            throw std::out_of_range("persistent_ordered_set index does not identify an element");
        }
    }

    void check_insert_index(const size_type index) const
    {
        if (index > size()) {
            throw std::out_of_range("persistent_ordered_set insertion index is past the end");
        }
    }

    void check_range(const size_type index, const size_type count) const
    {
        if (index > size() || count > size() - index) {
            throw std::out_of_range("persistent_ordered_set range extends past the end");
        }
    }

    void check_count(const size_type count) const
    {
        if (count > size()) {
            throw std::out_of_range("persistent_ordered_set count exceeds its size");
        }
    }

    entry_deque order_;
    index_map stamps_;
};

} // namespace tools::data_structures::ordered
