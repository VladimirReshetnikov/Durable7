/// A persistent multimap that remembers insertion order, both across keys and within a key.
///
/// Every operation returns a new version and leaves its inputs valid, sharing unchanged structure,
/// so an edit copies a path rather than the whole collection.

#pragma once

#include <durable7/ordered/persistent_ordered_map.hpp>
#include <durable7/ordered/persistent_ordered_set.hpp>

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::ordered {

/// Immutable set-valued multimap preserving first key-group order and the
/// first-insertion order of distinct values within each group.
template <
    class Key,
    class Value,
    class KeyHash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueHash = std::hash<Value>,
    class ValueEqual = std::equal_to<Value>>
    requires std::copyable<Key>
        && std::copyable<Value>
        && std::copyable<KeyHash>
        && std::copyable<KeyEqual>
        && std::copyable<ValueHash>
        && std::copyable<ValueEqual>
/// A persistent multimap that remembers insertion order, both across keys and within a key.
class persistent_ordered_multimap final {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using value_set = persistent_ordered_set<Value, ValueHash, ValueEqual>;
    using size_type = std::size_t;

private:
    struct group_identity_equal final {
        [[nodiscard]] bool operator()(const value_set& left, const value_set& right) const noexcept
        {
            return left.shares_index_with(right);
        }
    };

    using group_map = persistent_ordered_map<
        Key, value_set, KeyHash, KeyEqual, group_identity_equal>;

public:
    /// An empty multimap.
    persistent_ordered_multimap() = default;

    /// Whether the multimap holds no pairs.
    [[nodiscard]] static persistent_ordered_multimap empty()
    {
        return {};
    }

    /// An empty multimap using the supplied policies, which it retains.
    [[nodiscard]] static persistent_ordered_multimap create(
        KeyHash key_hash = {},
        KeyEqual key_equal = {},
        ValueHash value_hash = {},
        ValueEqual value_equal = {})
    {
        return persistent_ordered_multimap{
            group_map::create(
                std::move(key_hash), std::move(key_equal), group_identity_equal{}),
            std::move(value_hash),
            std::move(value_equal),
            0};
    }

    /// A multimap holding a range's pairs, built in bulk rather than by repeated insertion.
    template <class Range>
    [[nodiscard]] static persistent_ordered_multimap create_range(
        const Range& pairs,
        KeyHash key_hash = {},
        KeyEqual key_equal = {},
        ValueHash value_hash = {},
        ValueEqual value_equal = {})
    {
        auto result = create(
            std::move(key_hash), std::move(key_equal),
            std::move(value_hash), std::move(value_equal));
        for (const auto& [key, value] : pairs) {
            result = result.add(key, value);
        }
        return result;
    }

    /// Whether the multimap holds no pairs.
    /// How many pairs are present in total.
    /// How many distinct keys are present.
    [[nodiscard]] size_type key_count() const noexcept { return groups_.size(); }
    [[nodiscard]] std::int64_t pair_count() const noexcept { return pair_count_; }
    [[nodiscard]] bool is_empty() const noexcept { return pair_count_ == 0; }

    /// The retained key hashing policy.
    [[nodiscard]] const KeyHash& key_hash_function() const noexcept
    {
        return groups_.hash_function();
    }

    /// The retained value equivalence policy.
    /// The retained value hashing policy.
    /// The retained key equivalence policy.
    [[nodiscard]] const KeyEqual& key_eq() const noexcept { return groups_.key_eq(); }
    [[nodiscard]] const ValueHash& value_hash_function() const noexcept { return value_hash_; }
    [[nodiscard]] const ValueEqual& value_eq() const noexcept { return value_equal_; }

    /// Whether the key is present.
    [[nodiscard]] bool contains_key(const Key& key) const { return groups_.contains_key(key); }

    /// Whether the pair is present.
    [[nodiscard]] bool contains(const Key& key, const Value& value) const
    {
        const auto* values = groups_.try_get(key);
        return values != nullptr && values->contains(value);
    }

    /// How many values the key is bound to.
    [[nodiscard]] size_type count_values(const Key& key) const
    {
        const auto* values = groups_.try_get(key);
        return values == nullptr ? size_type{0} : values->size();
    }

    /// Reads the stored key representative, or nothing when absent.
    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const
    {
        return groups_.try_get_key(equal_key);
    }

    /// Reads the values bound to the key, or nothing when the key is absent.
    [[nodiscard]] const value_set* try_get_values(const Key& key) const
    {
        return groups_.try_get(key);
    }

    /// Reads the value stored for the key, or nothing when absent.
    [[nodiscard]] const Value* try_get_value(const Key& key, const Value& equal_value) const
    {
        const auto* values = groups_.try_get(key);
        return values == nullptr ? nullptr : values->try_get_value(equal_value);
    }

    /// The values bound to the key, empty when the key is absent.
    [[nodiscard]] value_set values_or_empty(const Key& key) const
    {
        if (const auto* values = try_get_values(key)) {
            return *values;
        }
        return value_set::create(value_hash_, value_equal_);
    }

    /// A multimap containing the given pair; returns the receiver when already present.
    [[nodiscard]] persistent_ordered_multimap add(const Key& key, const Value& value) const
    {
        if (const auto* stored_values = groups_.try_get(key)) {
            const auto values = stored_values->add(value);
            if (values.shares_index_with(*stored_values)) {
                return *this;
            }
            ensure_room_for_pair();
            const auto* stored_key = groups_.try_get_key(key);
            if (stored_key == nullptr) {
                throw std::logic_error("persistent_ordered_multimap key lookup is inconsistent");
            }
            return persistent_ordered_multimap{
                groups_.set_item(*stored_key, values),
                value_hash_, value_equal_, pair_count_ + 1};
        }

        ensure_room_for_pair();
        const auto values = value_set::create(value_hash_, value_equal_).add(value);
        return persistent_ordered_multimap{
            groups_.add(key, values), value_hash_, value_equal_, pair_count_ + 1};
    }

    /// Adds the pair unless an equivalent one is present, reporting which happened.
    [[nodiscard]] std::pair<persistent_ordered_multimap, bool> try_add(
        const Key& key,
        const Value& value) const
    {
        auto result = add(key, value);
        const auto changed = !result.shares_index_with(*this);
        return {std::move(result), changed};
    }

    /// A multimap without that pair; returns the receiver when absent.
    [[nodiscard]] persistent_ordered_multimap remove(
        const Key& key,
        const Value& value) const
    {
        const auto* stored_values = groups_.try_get(key);
        const auto* stored_key = groups_.try_get_key(key);
        if (stored_values == nullptr || stored_key == nullptr) {
            return *this;
        }
        auto [values, removed] = stored_values->try_remove(value);
        if (!removed) {
            return *this;
        }
        auto groups = values.is_empty()
            ? groups_.remove(*stored_key)
            : groups_.set_item(*stored_key, values);
        return persistent_ordered_multimap{
            std::move(groups), value_hash_, value_equal_, pair_count_ - 1};
    }

    /// A multimap without that key.
    [[nodiscard]] persistent_ordered_multimap remove_key(const Key& key) const
    {
        const auto* values = groups_.try_get(key);
        if (values == nullptr) {
            return *this;
        }
        return persistent_ordered_multimap{
            groups_.remove(key), value_hash_, value_equal_,
            pair_count_ - static_cast<std::int64_t>(values->size())};
    }

    /// An empty multimap retaining the same policies; returns the receiver when already empty.
    [[nodiscard]] persistent_ordered_multimap clear() const
    {
        return is_empty()
            ? *this
            : create(groups_.hash_function(), groups_.key_eq(), value_hash_, value_equal_);
    }

    template <class Function>
        requires std::invocable<Function&, const Key&, const Value&>
    void for_each_pair(Function&& function) const
    {
        for (const auto& entry : groups_) {
            for (const auto& value : entry.value) {
                std::invoke(function, entry.key, value);
            }
        }
    }

    [[nodiscard]] std::vector<Key> keys_to_vector() const
    {
        return groups_.keys_to_vector();
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(static_cast<size_type>(pair_count_));
        for_each_pair([&result](const Key& key, const Value& value) {
            result.emplace_back(key, value);
        });
        return result;
    }

    [[nodiscard]] bool shares_index_with(const persistent_ordered_multimap& other) const noexcept
    {
        return groups_.shares_index_with(other.groups_);
    }

    void validate_invariants() const
    {
        groups_.validate_invariants();
        auto pairs = std::int64_t{0};
        for (const auto& entry : groups_) {
            entry.value.validate_invariants();
            if (entry.value.is_empty()) {
                throw std::logic_error("persistent_ordered_multimap stores an empty group");
            }
            if (entry.value.size()
                > static_cast<size_type>(std::numeric_limits<std::int64_t>::max() - pairs)) {
                throw std::logic_error("persistent_ordered_multimap pair count overflows");
            }
            pairs += static_cast<std::int64_t>(entry.value.size());
        }
        if (pairs != pair_count_) {
            throw std::logic_error("persistent_ordered_multimap cached count disagrees");
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

private:
    persistent_ordered_multimap(
        group_map groups,
        ValueHash value_hash,
        ValueEqual value_equal,
        std::int64_t pair_count)
        : groups_(std::move(groups)),
          value_hash_(std::move(value_hash)),
          value_equal_(std::move(value_equal)),
          pair_count_(pair_count)
    {
    }

    void ensure_room_for_pair() const
    {
        if (pair_count_ == std::numeric_limits<std::int64_t>::max()) {
            throw std::overflow_error("persistent_ordered_multimap pair count is exhausted");
        }
    }

    group_map groups_;
    [[no_unique_address]] ValueHash value_hash_{};
    [[no_unique_address]] ValueEqual value_equal_{};
    std::int64_t pair_count_ = 0;
};

} // namespace durable7::ordered
