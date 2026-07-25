#pragma once

#include <durable7/hamt/persistent_hash_map.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace durable7::hamt {

enum class bimap_conflict {
    none,
    key,
    value,
};

class bimap_conflict_error final : public std::invalid_argument {
public:
    explicit bimap_conflict_error(bimap_conflict conflict)
        : std::invalid_argument(
            conflict == bimap_conflict::key
                ? "An equivalent key is already present in the persistent_bi_map."
                : "An equivalent value is already present in the persistent_bi_map."),
          conflict_(conflict) {
    }

    [[nodiscard]] bimap_conflict conflict() const noexcept {
        return conflict_;
    }

private:
    bimap_conflict conflict_;
};

// A strict immutable bijection backed by independent forward and inverse CHAMP maps. The six
// policy template arguments make equality and hashing in the two domains independent. As with the
// other native persistent facades, observable identity is shared-root identity rather than object
// address identity.
template <
    class Key,
    class T,
    class KeyHash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueHash = std::hash<T>,
    class ValueEqual = std::equal_to<T>>
class persistent_bi_map {
private:
    using forward_map_type = persistent_hash_map<Key, T, KeyHash, KeyEqual, ValueEqual>;
    using inverse_map_type = persistent_hash_map<T, Key, ValueHash, ValueEqual, KeyEqual>;

    template <class, class, class, class, class, class>
    friend class persistent_bi_map;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = typename forward_map_type::value_type;
    using size_type = std::size_t;
    using key_hasher = KeyHash;
    using key_equal = KeyEqual;
    using value_hasher = ValueHash;
    using value_equal = ValueEqual;
    using inverse_type = persistent_bi_map<T, Key, ValueHash, ValueEqual, KeyHash, KeyEqual>;
    using const_iterator = typename forward_map_type::const_iterator;

    persistent_bi_map() = default;

    [[nodiscard]] static persistent_bi_map empty() {
        return {};
    }

    [[nodiscard]] static persistent_bi_map create(
        KeyHash key_hash = {},
        KeyEqual keys_equal = {},
        ValueHash value_hash = {},
        ValueEqual values_equal = {}) {
        return persistent_bi_map(
            forward_map_type::create(key_hash, keys_equal, values_equal),
            inverse_map_type::create(value_hash, values_equal, keys_equal));
    }

    template <class Range>
    [[nodiscard]] static persistent_bi_map create_range(
        const Range& entries,
        KeyHash key_hash = {},
        KeyEqual keys_equal = {},
        ValueHash value_hash = {},
        ValueEqual values_equal = {}) {
        auto result = create(
            std::move(key_hash),
            std::move(keys_equal),
            std::move(value_hash),
            std::move(values_equal));
        for (const auto& [key, value] : entries) {
            result = result.add(key, value);
        }
        return result;
    }

    [[nodiscard]] size_type count() const noexcept {
        return forward_.count();
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return forward_.is_empty();
    }

    [[nodiscard]] const KeyHash& key_hash_function() const noexcept {
        return forward_.hash_function();
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept {
        return forward_.key_eq();
    }

    [[nodiscard]] const ValueHash& value_hash_function() const noexcept {
        return inverse_.hash_function();
    }

    [[nodiscard]] const ValueEqual& value_eq() const noexcept {
        return inverse_.key_eq();
    }

    [[nodiscard]] bool contains_key(const Key& key) const {
        return forward_.contains_key(key);
    }

    [[nodiscard]] bool contains_value(const T& value) const {
        return inverse_.contains_key(value);
    }

    // Returned pointers retain the same snapshot-bound lifetime as persistent_hash_map::try_get.
    [[nodiscard]] const T* try_get(const Key& key) const {
        return forward_.try_get(key);
    }

    [[nodiscard]] const Key* try_get_key(const T& value) const {
        return inverse_.try_get(value);
    }

    [[nodiscard]] const T& at(const Key& key) const {
        return forward_.at(key);
    }

    [[nodiscard]] persistent_bi_map add(const Key& key, const T& value) const {
        auto [result, added, conflict] = try_add(key, value);
        if (!added) {
            throw bimap_conflict_error(conflict);
        }
        return result;
    }

    // Key conflict takes precedence when both domains are already represented. A conflict returns
    // a value sharing both roots with the receiver.
    [[nodiscard]] std::tuple<persistent_bi_map, bool, bimap_conflict> try_add(
        const Key& key,
        const T& value) const {
        if (forward_.contains_key(key)) {
            return {*this, false, bimap_conflict::key};
        }
        if (inverse_.contains_key(value)) {
            return {*this, false, bimap_conflict::value};
        }

        auto next_forward = forward_.add(key, value);
        auto next_inverse = inverse_.add(value, key);
        return {
            persistent_bi_map(std::move(next_forward), std::move(next_inverse)),
            true,
            bimap_conflict::none,
        };
    }

    // Adds a missing key or replaces its value, but never silently displaces a different key.
    // Replacement removes and reinserts both directions to retain the stored key representative
    // while making the configured value policy—not an incidental mapped-value relation—normative.
    [[nodiscard]] persistent_bi_map set_item(const Key& key, const T& value) const {
        const auto* previous = forward_.try_get(key);
        if (previous == nullptr) {
            if (inverse_.contains_key(value)) {
                throw bimap_conflict_error(bimap_conflict::value);
            }
            return persistent_bi_map(forward_.add(key, value), inverse_.add(value, key));
        }
        if (std::invoke(value_eq(), *previous, value)) {
            return *this;
        }
        if (inverse_.contains_key(value)) {
            throw bimap_conflict_error(bimap_conflict::value);
        }

        const auto* stored_key = forward_.try_get_key(key);
        if (stored_key == nullptr || inverse_.try_get(*previous) == nullptr) {
            throw std::logic_error("persistent_bi_map invariant failure");
        }

        auto next_forward = forward_.remove(*stored_key).add(*stored_key, value);
        auto next_inverse = inverse_.remove(*previous).add(value, *stored_key);
        return persistent_bi_map(std::move(next_forward), std::move(next_inverse));
    }

    [[nodiscard]] persistent_bi_map remove_key(const Key& key) const {
        auto [result, removed, value] = try_remove_key(key);
        (void)value;
        return removed ? std::move(result) : *this;
    }

    [[nodiscard]] std::tuple<persistent_bi_map, bool, std::optional<T>> try_remove_key(
        const Key& key) const {
        const auto* stored_key = forward_.try_get_key(key);
        const auto* stored_value = forward_.try_get(key);
        if (stored_key == nullptr || stored_value == nullptr) {
            return {*this, false, std::nullopt};
        }
        if (inverse_.try_get(*stored_value) == nullptr) {
            throw std::logic_error("persistent_bi_map invariant failure");
        }

        auto removed_value = std::optional<T>(*stored_value);
        auto next_forward = forward_.remove(*stored_key);
        auto next_inverse = inverse_.remove(*stored_value);
        return {
            persistent_bi_map(std::move(next_forward), std::move(next_inverse)),
            true,
            std::move(removed_value),
        };
    }

    [[nodiscard]] persistent_bi_map remove_value(const T& value) const {
        auto [result, removed, key] = try_remove_value(value);
        (void)key;
        return removed ? std::move(result) : *this;
    }

    [[nodiscard]] std::tuple<persistent_bi_map, bool, std::optional<Key>> try_remove_value(
        const T& value) const {
        const auto* stored_value = inverse_.try_get_key(value);
        const auto* stored_key = inverse_.try_get(value);
        if (stored_value == nullptr || stored_key == nullptr) {
            return {*this, false, std::nullopt};
        }
        if (forward_.try_get(*stored_key) == nullptr) {
            throw std::logic_error("persistent_bi_map invariant failure");
        }

        auto removed_key = std::optional<Key>(*stored_key);
        auto next_forward = forward_.remove(*stored_key);
        auto next_inverse = inverse_.remove(*stored_value);
        return {
            persistent_bi_map(std::move(next_forward), std::move(next_inverse)),
            true,
            std::move(removed_key),
        };
    }

    [[nodiscard]] persistent_bi_map clear() const {
        if (is_empty()) {
            return *this;
        }
        return persistent_bi_map(forward_.clear(), inverse_.clear());
    }

    // Value-semantic O(1) inversion copies two small HAMT facades and swaps their shared roots.
    [[nodiscard]] inverse_type inverse() const {
        return inverse_type(inverse_, forward_);
    }

    [[nodiscard]] const_iterator begin() const {
        return forward_.begin();
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return {};
    }

    [[nodiscard]] std::vector<Key> keys() const {
        return forward_.keys();
    }

    [[nodiscard]] std::vector<T> values() const {
        return forward_.values();
    }

    [[nodiscard]] bool shares_roots_with(const persistent_bi_map& other) const noexcept {
        return forward_.shares_root_with(other.forward_)
            && inverse_.shares_root_with(other.inverse_);
    }

    [[nodiscard]] bool validate_structure() const {
        if (forward_.count() != inverse_.count()) {
            return false;
        }
        for (const auto& [key, value] : forward_) {
            const auto* inverse_key = inverse_.try_get(value);
            if (inverse_key == nullptr || !std::invoke(key_eq(), key, *inverse_key)) {
                return false;
            }
        }
        for (const auto& [value, key] : inverse_) {
            const auto* forward_value = forward_.try_get(key);
            if (forward_value == nullptr || !std::invoke(value_eq(), value, *forward_value)) {
                return false;
            }
        }
        return true;
    }

private:
    persistent_bi_map(forward_map_type forward, inverse_map_type inverse)
        : forward_(std::move(forward)),
          inverse_(std::move(inverse)) {
    }

    forward_map_type forward_;
    inverse_map_type inverse_;
};

} // namespace durable7::hamt
