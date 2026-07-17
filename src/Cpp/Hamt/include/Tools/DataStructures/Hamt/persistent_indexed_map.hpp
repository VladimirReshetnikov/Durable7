#pragma once

#include "persistent_hash_map.hpp"
#include "persistent_hash_multimap.hpp"

#include <concepts>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

/// Immutable primary map with one automatically maintained nonunique secondary index.
template <
    class Key,
    class Value,
    class IndexKey,
    class Selector = std::function<IndexKey(const Key&, const Value&)>,
    class KeyHash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueEqual = std::equal_to<Value>,
    class IndexHash = std::hash<IndexKey>,
    class IndexEqual = std::equal_to<IndexKey>>
    requires std::copyable<Key>
        && std::copyable<Value>
        && std::copyable<IndexKey>
        && std::copyable<Selector>
        && std::invocable<Selector&, const Key&, const Value&>
        && std::convertible_to<
            std::invoke_result_t<Selector&, const Key&, const Value&>, IndexKey>
        && std::copyable<KeyHash>
        && std::copyable<KeyEqual>
        && std::copyable<ValueEqual>
        && std::copyable<IndexHash>
        && std::copyable<IndexEqual>
class persistent_indexed_map final {
private:
    struct entry final {
        Value value;
        IndexKey index_key;
    };

    struct entry_equal final {
        ValueEqual value_equal{};
        IndexEqual index_equal{};

        [[nodiscard]] bool operator()(const entry& left, const entry& right) const
        {
            return std::invoke(value_equal, left.value, right.value)
                && std::invoke(index_equal, left.index_key, right.index_key);
        }
    };

    using primary_map = persistent_hash_map<
        Key, entry, KeyHash, KeyEqual, entry_equal>;
    using index_map = persistent_hash_multimap<
        IndexKey, Key, IndexHash, IndexEqual, KeyHash, KeyEqual>;

public:
    using key_type = Key;
    using mapped_type = Value;
    using index_key_type = IndexKey;
    using value_type = std::pair<Key, Value>;
    using key_set = typename index_map::value_set;
    using size_type = std::size_t;

    persistent_indexed_map() = delete;

    [[nodiscard]] static persistent_indexed_map create(
        Selector selector,
        KeyHash key_hash = {},
        KeyEqual key_equal = {},
        ValueEqual value_equal = {},
        IndexHash index_hash = {},
        IndexEqual index_equal = {})
    {
        validate_selector(selector);
        auto retained_key_hash = key_hash;
        auto retained_key_equal = key_equal;
        auto retained_value_equal = value_equal;
        auto retained_index_equal = index_equal;
        return persistent_indexed_map{
            primary_map::create(
                std::move(key_hash), std::move(key_equal),
                entry_equal{retained_value_equal, retained_index_equal}),
            index_map::create(
                std::move(index_hash), std::move(index_equal),
                std::move(retained_key_hash), std::move(retained_key_equal)),
            std::move(selector),
            std::move(value_equal)};
    }

    template <class Range>
    [[nodiscard]] static persistent_indexed_map create_range(
        const Range& items,
        Selector selector,
        KeyHash key_hash = {},
        KeyEqual key_equal = {},
        ValueEqual value_equal = {},
        IndexHash index_hash = {},
        IndexEqual index_equal = {})
    {
        auto result = create(
            std::move(selector), std::move(key_hash), std::move(key_equal),
            std::move(value_equal), std::move(index_hash), std::move(index_equal));
        for (const auto& [key, value] : items) {
            result = result.set_item(key, value);
        }
        return result;
    }

    [[nodiscard]] size_type count() const noexcept { return primary_.count(); }
    [[nodiscard]] bool is_empty() const noexcept { return primary_.is_empty(); }
    [[nodiscard]] size_type index_key_count() const noexcept { return index_.key_count(); }
    [[nodiscard]] const KeyHash& key_hash_function() const noexcept { return primary_.hash_function(); }
    [[nodiscard]] const KeyEqual& key_eq() const noexcept { return primary_.key_eq(); }
    [[nodiscard]] const ValueEqual& value_eq() const noexcept { return value_equal_; }
    [[nodiscard]] const IndexHash& index_hash_function() const noexcept
    {
        return index_.key_hash_function();
    }
    [[nodiscard]] const IndexEqual& index_eq() const noexcept { return index_.key_eq(); }
    [[nodiscard]] const Selector& selector() const noexcept { return selector_; }

    [[nodiscard]] bool contains_key(const Key& key) const { return primary_.contains_key(key); }
    [[nodiscard]] const Value* try_get(const Key& key) const
    {
        const auto* stored = primary_.try_get(key);
        return stored == nullptr ? nullptr : std::addressof(stored->value);
    }
    [[nodiscard]] const Value& at(const Key& key) const
    {
        const auto* value = try_get(key);
        if (value == nullptr) {
            throw std::out_of_range("persistent_indexed_map key was not present");
        }
        return *value;
    }
    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const
    {
        return primary_.try_get_key(equal_key);
    }
    [[nodiscard]] const IndexKey* try_get_index_key(const Key& key) const
    {
        const auto* stored = primary_.try_get(key);
        return stored == nullptr ? nullptr : std::addressof(stored->index_key);
    }
    [[nodiscard]] bool contains_index_key(const IndexKey& index_key) const
    {
        return index_.contains_key(index_key);
    }
    [[nodiscard]] size_type count_by_index(const IndexKey& index_key) const
    {
        const auto* keys = index_.try_get_values(index_key);
        return keys == nullptr ? size_type{0} : keys->count();
    }
    [[nodiscard]] const key_set* try_get_keys_by_index(const IndexKey& index_key) const
    {
        return index_.try_get_values(index_key);
    }
    [[nodiscard]] key_set keys_by_index_or_empty(const IndexKey& index_key) const
    {
        return index_.values_or_empty(index_key);
    }

    [[nodiscard]] persistent_indexed_map add(const Key& key, const Value& value) const
    {
        if (primary_.contains_key(key)) {
            throw std::invalid_argument("persistent_indexed_map primary key already exists");
        }
        const auto selected = static_cast<IndexKey>(std::invoke(selector_, key, value));
        auto index = index_.add(selected, key);
        const auto* actual_index = index.try_get_key(selected);
        if (actual_index == nullptr) {
            throw std::logic_error("persistent_indexed_map secondary key lookup is inconsistent");
        }
        return persistent_indexed_map{
            primary_.add(key, entry{value, *actual_index}),
            std::move(index), selector_, value_equal_};
    }

    [[nodiscard]] std::pair<persistent_indexed_map, bool> try_add(
        const Key& key,
        const Value& value) const
    {
        if (primary_.contains_key(key)) {
            return {*this, false};
        }
        return {add(key, value), true};
    }

    [[nodiscard]] persistent_indexed_map set_item(const Key& key, const Value& value) const
    {
        const auto* current = primary_.try_get(key);
        const auto* stored_key = primary_.try_get_key(key);
        if (current == nullptr || stored_key == nullptr) {
            return add(key, value);
        }
        if (std::invoke(value_equal_, current->value, value)) {
            return *this;
        }

        const auto selected = static_cast<IndexKey>(std::invoke(selector_, *stored_key, value));
        auto index = index_;
        auto actual_index = current->index_key;
        if (!std::invoke(index_.key_eq(), current->index_key, selected)) {
            index = index.remove(current->index_key, *stored_key).add(selected, *stored_key);
            const auto* retained = index.try_get_key(selected);
            if (retained == nullptr) {
                throw std::logic_error("persistent_indexed_map secondary update is inconsistent");
            }
            actual_index = *retained;
        }
        return persistent_indexed_map{
            primary_.set_item(*stored_key, entry{value, actual_index}),
            std::move(index), selector_, value_equal_};
    }

    [[nodiscard]] persistent_indexed_map remove(const Key& key) const
    {
        const auto* current = primary_.try_get(key);
        const auto* stored_key = primary_.try_get_key(key);
        if (current == nullptr || stored_key == nullptr) {
            return *this;
        }
        return persistent_indexed_map{
            primary_.remove(*stored_key),
            index_.remove(current->index_key, *stored_key),
            selector_, value_equal_};
    }

    [[nodiscard]] persistent_indexed_map clear() const
    {
        return is_empty()
            ? *this
            : persistent_indexed_map{
                primary_.clear(), index_.clear(), selector_, value_equal_};
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(count());
        for (const auto& [key, stored] : primary_) {
            result.emplace_back(key, stored.value);
        }
        return result;
    }

    [[nodiscard]] bool shares_roots_with(const persistent_indexed_map& other) const noexcept
    {
        return primary_.shares_root_with(other.primary_)
            && index_.shares_root_with(other.index_);
    }

    void validate_invariants() const
    {
        if (!primary_.debug_validate_canonical()) {
            throw std::logic_error("persistent_indexed_map primary HAMT is not canonical");
        }
        index_.validate_invariants();
        if (primary_.count() != static_cast<size_type>(index_.pair_count())) {
            throw std::logic_error("persistent_indexed_map counts disagree");
        }
        for (const auto& [key, stored] : primary_) {
            if (!index_.contains(stored.index_key, key)) {
                throw std::logic_error("persistent_indexed_map primary row is not indexed");
            }
        }
        index_.for_each_pair([this](const IndexKey& index_key, const Key& key) {
            const auto* stored = primary_.try_get(key);
            if (stored == nullptr
                || !std::invoke(index_.key_eq(), index_key, stored->index_key)) {
                throw std::logic_error("persistent_indexed_map secondary membership disagrees");
            }
        });
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
    persistent_indexed_map(
        primary_map primary,
        index_map index,
        Selector selector,
        ValueEqual value_equal)
        : primary_(std::move(primary)),
          index_(std::move(index)),
          selector_(std::move(selector)),
          value_equal_(std::move(value_equal))
    {
    }

    static void validate_selector(const Selector& selector)
    {
        if constexpr (requires { static_cast<bool>(selector); }) {
            if (!static_cast<bool>(selector)) {
                throw std::invalid_argument("persistent_indexed_map selector must not be empty");
            }
        }
    }

    primary_map primary_;
    index_map index_;
    [[no_unique_address]] Selector selector_;
    [[no_unique_address]] ValueEqual value_equal_{};
};

} // namespace tools::data_structures::hamt
