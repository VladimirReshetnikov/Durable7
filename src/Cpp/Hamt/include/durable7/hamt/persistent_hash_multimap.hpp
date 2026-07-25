#pragma once

#include "persistent_hash_map.hpp"
#include "persistent_hash_set.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace durable7::hamt {

/// Immutable set-valued hash multimap composed from the public CHAMP map and set.
///
/// Both domains retain their first representatives. Empty value groups are never
/// stored, so key_count() and pair_count() describe the same flattened relation.
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
class persistent_hash_multimap final {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using value_set = persistent_hash_set<Value, ValueHash, ValueEqual>;
    using size_type = std::size_t;

private:
    struct group_identity_equal final {
        [[nodiscard]] bool operator()(const value_set& left, const value_set& right) const noexcept
        {
            return left.shares_root_with(right);
        }
    };

    using group_map = persistent_hash_map<
        Key,
        value_set,
        KeyHash,
        KeyEqual,
        group_identity_equal>;

public:
    persistent_hash_multimap() = default;

    [[nodiscard]] static persistent_hash_multimap empty()
    {
        return {};
    }

    [[nodiscard]] static persistent_hash_multimap create(
        KeyHash key_hash = {},
        KeyEqual key_equal = {},
        ValueHash value_hash = {},
        ValueEqual value_equal = {})
    {
        return persistent_hash_multimap{
            group_map::create(std::move(key_hash), std::move(key_equal), group_identity_equal{}),
            std::move(value_hash),
            std::move(value_equal),
            0};
    }

    template <class Range>
    [[nodiscard]] static persistent_hash_multimap create_range(
        const Range& pairs,
        KeyHash key_hash = {},
        KeyEqual key_equal = {},
        ValueHash value_hash = {},
        ValueEqual value_equal = {})
    {
        auto result = create(
            std::move(key_hash),
            std::move(key_equal),
            std::move(value_hash),
            std::move(value_equal));
        for (const auto& [key, value] : pairs) {
            result = result.add(key, value);
        }
        return result;
    }

    [[nodiscard]] size_type key_count() const noexcept
    {
        return groups_.count();
    }

    [[nodiscard]] std::int64_t pair_count() const noexcept
    {
        return pair_count_;
    }

    [[nodiscard]] bool is_empty() const noexcept
    {
        return pair_count_ == 0;
    }

    [[nodiscard]] const KeyHash& key_hash_function() const noexcept
    {
        return groups_.hash_function();
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept
    {
        return groups_.key_eq();
    }

    [[nodiscard]] const ValueHash& value_hash_function() const noexcept
    {
        return value_hash_;
    }

    [[nodiscard]] const ValueEqual& value_eq() const noexcept
    {
        return value_equal_;
    }

    [[nodiscard]] bool contains_key(const Key& key) const
    {
        return groups_.contains_key(key);
    }

    [[nodiscard]] bool contains(const Key& key, const Value& value) const
    {
        const auto* values = groups_.try_get(key);
        return values != nullptr && values->contains(value);
    }

    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const
    {
        return groups_.try_get_key(equal_key);
    }

    [[nodiscard]] const value_set* try_get_values(const Key& key) const
    {
        return groups_.try_get(key);
    }

    [[nodiscard]] value_set values_or_empty(const Key& key) const
    {
        if (const auto* values = try_get_values(key)) {
            return *values;
        }
        return value_set::create(value_hash_, value_equal_);
    }

    /// Adds one pair; a duplicate returns a facade sharing the exact outer root.
    [[nodiscard]] persistent_hash_multimap add(const Key& key, const Value& value) const
    {
        if (const auto* stored_values = groups_.try_get(key)) {
            auto [values, added] = stored_values->try_add(value);
            if (!added) {
                return *this;
            }
            ensure_room_for_pair();
            const auto* stored_key = groups_.try_get_key(key);
            if (stored_key == nullptr) {
                throw std::logic_error("persistent_hash_multimap key lookup is inconsistent");
            }
            return persistent_hash_multimap{
                groups_.set_item(*stored_key, values),
                value_hash_,
                value_equal_,
                pair_count_ + 1};
        }

        ensure_room_for_pair();
        auto values = value_set::create(value_hash_, value_equal_).add(value);
        return persistent_hash_multimap{
            groups_.add(key, values),
            value_hash_,
            value_equal_,
            pair_count_ + 1};
    }

    /// Removes one pair and contracts its outer key when the final value disappears.
    [[nodiscard]] persistent_hash_multimap remove(const Key& key, const Value& value) const
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
        return persistent_hash_multimap{
            std::move(groups), value_hash_, value_equal_, pair_count_ - 1};
    }

    [[nodiscard]] persistent_hash_multimap remove_key(const Key& key) const
    {
        const auto* values = groups_.try_get(key);
        if (values == nullptr) {
            return *this;
        }
        return persistent_hash_multimap{
            groups_.remove(key),
            value_hash_,
            value_equal_,
            pair_count_ - static_cast<std::int64_t>(values->count())};
    }

    [[nodiscard]] persistent_hash_multimap clear() const
    {
        if (is_empty()) {
            return *this;
        }
        return persistent_hash_multimap{
            groups_.clear(), value_hash_, value_equal_, 0};
    }

    template <class Function>
        requires std::invocable<Function&, const Key&, const Value&>
    void for_each_pair(Function&& function) const
    {
        for (const auto& [key, values] : groups_) {
            for (const auto& value : values) {
                std::invoke(function, key, value);
            }
        }
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

    [[nodiscard]] bool shares_root_with(const persistent_hash_multimap& other) const noexcept
    {
        return groups_.shares_root_with(other.groups_);
    }

    void validate_invariants() const
    {
        if (!groups_.debug_validate_canonical()) {
            throw std::logic_error("persistent_hash_multimap outer HAMT is not canonical");
        }
        auto pairs = std::int64_t{0};
        for (const auto& [key, values] : groups_) {
            (void)key;
            if (values.is_empty()) {
                throw std::logic_error("persistent_hash_multimap stores an empty group");
            }
            if (values.count() > static_cast<size_type>(std::numeric_limits<std::int64_t>::max() - pairs)) {
                throw std::logic_error("persistent_hash_multimap pair count overflows");
            }
            pairs += static_cast<std::int64_t>(values.count());
        }
        if (pairs != pair_count_ || (pairs == 0) != groups_.is_empty()) {
            throw std::logic_error("persistent_hash_multimap cached counts disagree");
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
    persistent_hash_multimap(
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
            throw std::overflow_error("persistent_hash_multimap pair count is exhausted");
        }
    }

    group_map groups_;
    [[no_unique_address]] ValueHash value_hash_{};
    [[no_unique_address]] ValueEqual value_equal_{};
    std::int64_t pair_count_ = 0;
};

} // namespace durable7::hamt
