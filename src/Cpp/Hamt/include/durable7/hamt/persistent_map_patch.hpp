#pragma once

#include "persistent_hash_map.hpp"

#include <concepts>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::hamt {

/// One presence-safe map change. An outer disengaged optional is absence;
/// an engaged optional remains presence even when Value is itself optional.
template <class Key, class Value>
struct map_patch_entry final {
    Key key;
    std::optional<Value> before;
    std::optional<Value> after;
};

/// Immutable, invertible strict changes between persistent hash maps.
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
class persistent_map_patch final {
private:
    struct change final {
        std::optional<Value> before;
        std::optional<Value> after;
    };

    struct change_equal final {
        ValueEqual value_equal{};

        [[nodiscard]] bool operator()(const change& left, const change& right) const
        {
            return states_equal(left.before, right.before, value_equal)
                && states_equal(left.after, right.after, value_equal);
        }
    };

    using change_map = persistent_hash_map<Key, change, Hash, KeyEqual, change_equal>;

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = map_patch_entry<Key, Value>;
    using size_type = std::size_t;
    using map_type = persistent_hash_map<Key, Value, Hash, KeyEqual, ValueEqual>;

    persistent_map_patch() = default;

    [[nodiscard]] static persistent_map_patch empty() { return {}; }

    [[nodiscard]] static persistent_map_patch create(
        Hash hash = {},
        KeyEqual key_equal = {},
        ValueEqual value_equal = {})
    {
        auto retained_equal = value_equal;
        return persistent_map_patch{
            change_map::create(
                std::move(hash), std::move(key_equal),
                change_equal{std::move(retained_equal)}),
            std::move(value_equal)};
    }

    template <class Range>
    [[nodiscard]] static persistent_map_patch create_range(
        const Range& entries,
        Hash hash = {},
        KeyEqual key_equal = {},
        ValueEqual value_equal = {})
    {
        auto result = create(
            std::move(hash), std::move(key_equal), std::move(value_equal));
        for (const auto& entry : entries) {
            result = result.add(entry);
        }
        return result;
    }

    [[nodiscard]] static persistent_map_patch between(
        const map_type& source,
        const map_type& target)
    {
        auto result = create(
            source.hash_function(), source.key_eq(), source.value_eq());
        for (const auto& [key, value] : source) {
            const auto* target_value = target.try_get(key);
            if (target_value == nullptr) {
                result = result.add(value_type{key, value, std::nullopt});
            } else if (!std::invoke(source.value_eq(), value, *target_value)) {
                result = result.add(value_type{key, value, *target_value});
            }
        }
        for (const auto& [key, value] : target) {
            if (!source.contains_key(key)) {
                result = result.add(value_type{key, std::nullopt, value});
            }
        }
        return result;
    }

    [[nodiscard]] size_type count() const noexcept { return changes_.count(); }
    [[nodiscard]] bool is_empty() const noexcept { return changes_.is_empty(); }
    [[nodiscard]] const Hash& hash_function() const noexcept { return changes_.hash_function(); }
    [[nodiscard]] const KeyEqual& key_eq() const noexcept { return changes_.key_eq(); }
    [[nodiscard]] const ValueEqual& value_eq() const noexcept { return value_equal_; }
    [[nodiscard]] bool contains_key(const Key& key) const { return changes_.contains_key(key); }

    [[nodiscard]] std::optional<value_type> try_get_entry(const Key& key) const
    {
        const auto* stored_key = changes_.try_get_key(key);
        const auto* stored = changes_.try_get(key);
        return stored_key == nullptr || stored == nullptr
            ? std::nullopt
            : std::optional<value_type>{value_type{*stored_key, stored->before, stored->after}};
    }

    [[nodiscard]] persistent_map_patch add(value_type entry) const
    {
        if (states_equal(entry.before, entry.after, value_equal_)) {
            return *this;
        }
        return persistent_map_patch{
            changes_.add(
                entry.key,
                change{std::move(entry.before), std::move(entry.after)}),
            value_equal_};
    }

    [[nodiscard]] std::pair<persistent_map_patch, bool> try_add(value_type entry) const
    {
        if (states_equal(entry.before, entry.after, value_equal_)
            || changes_.contains_key(entry.key)) {
            return {*this, false};
        }
        return {
            persistent_map_patch{
                changes_.add(
                    entry.key,
                    change{std::move(entry.before), std::move(entry.after)}),
                value_equal_},
            true};
    }

    [[nodiscard]] persistent_map_patch remove(const Key& key) const
    {
        auto updated = changes_.remove(key);
        return updated.shares_root_with(changes_)
            ? *this
            : persistent_map_patch{std::move(updated), value_equal_};
    }

    [[nodiscard]] persistent_map_patch clear() const
    {
        return is_empty()
            ? *this
            : persistent_map_patch{changes_.clear(), value_equal_};
    }

    /// Validates every expectation before publishing any edit.
    [[nodiscard]] std::pair<map_type, std::optional<Key>> try_apply(
        const map_type& source) const
    {
        for (const auto& [key, expected] : changes_) {
            const auto* current = source.try_get(key);
            const auto matches = expected.before.has_value()
                ? current != nullptr
                    && std::invoke(value_equal_, *current, *expected.before)
                : current == nullptr;
            if (!matches) {
                return {source, key};
            }
        }

        auto result = source;
        for (const auto& [key, replacement] : changes_) {
            result = replacement.after.has_value()
                ? result.set_item(key, *replacement.after)
                : result.remove(key);
        }
        return {std::move(result), std::nullopt};
    }

    [[nodiscard]] map_type apply(const map_type& source) const
    {
        auto [result, conflict] = try_apply(source);
        if (conflict.has_value()) {
            throw std::logic_error("source map conflicts with persistent_map_patch");
        }
        return result;
    }

    [[nodiscard]] persistent_map_patch invert() const
    {
        if (is_empty()) {
            return *this;
        }
        auto result = create(hash_function(), key_eq(), value_equal_);
        for (const auto& [key, stored] : changes_) {
            result = result.add(value_type{key, stored.after, stored.before});
        }
        return result;
    }

    [[nodiscard]] persistent_map_patch compose(const persistent_map_patch& next) const
    {
        auto result = *this;
        for (const auto& [next_key, next_change] : next.changes_) {
            const auto* current = result.changes_.try_get(next_key);
            if (current == nullptr) {
                result = result.add(value_type{
                    next_key, next_change.before, next_change.after});
                continue;
            }
            const auto* stored_key = result.changes_.try_get_key(next_key);
            if (stored_key == nullptr
                || !states_equal(current->after, next_change.before, value_equal_)) {
                throw std::invalid_argument(
                    "persistent_map_patch values disagree at composition boundary");
            }
            const auto before = current->before;
            result = result.remove(*stored_key);
            result = result.add(value_type{*stored_key, before, next_change.after});
        }
        return result;
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(count());
        for (const auto& [key, stored] : changes_) {
            result.push_back(value_type{key, stored.before, stored.after});
        }
        return result;
    }

    [[nodiscard]] bool shares_root_with(const persistent_map_patch& other) const noexcept
    {
        return changes_.shares_root_with(other.changes_);
    }

    void validate_invariants() const
    {
        if (!changes_.debug_validate_canonical()) {
            throw std::logic_error("persistent_map_patch HAMT is not canonical");
        }
        for (const auto& [key, stored] : changes_) {
            (void)key;
            if (states_equal(stored.before, stored.after, value_equal_)) {
                throw std::logic_error("persistent_map_patch stores a semantic no-op");
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

private:
    persistent_map_patch(change_map changes, ValueEqual value_equal)
        : changes_(std::move(changes)), value_equal_(std::move(value_equal))
    {
    }

    [[nodiscard]] static bool states_equal(
        const std::optional<Value>& left,
        const std::optional<Value>& right,
        const ValueEqual& value_equal)
    {
        return left.has_value() == right.has_value()
            && (!left.has_value() || std::invoke(value_equal, *left, *right));
    }

    change_map changes_;
    [[no_unique_address]] ValueEqual value_equal_{};
};

} // namespace durable7::hamt
