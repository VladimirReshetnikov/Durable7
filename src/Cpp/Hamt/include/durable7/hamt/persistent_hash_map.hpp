#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Plain MSVC accepts standard [[no_unique_address]] but ignores it for ABI
// stability; the effective spelling there is [[msvc::no_unique_address]].
#if defined(_MSC_VER)
#define DURABLE7_HAMT_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define DURABLE7_HAMT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace durable7::hamt {

/// Which kind of CHAMP node this is.
enum class persistent_hamt_node_kind {
    empty,
    leaf,
    collision,
    bitmap_indexed,
};

/// Which side of a diff an entry falls on.
enum class map_difference_kind {
    added,
    removed,
    changed,
};

/// One entry-level difference between two maps.
template<class Key, class T>
struct map_difference {
    map_difference_kind kind;
    Key key;
    std::optional<T> before;
    std::optional<T> after;
};

template <
    class Key,
    class T,
    class Hash = std::hash<Key>,
    class KeyEqual = std::equal_to<Key>,
    class ValueEqual = std::equal_to<T>>
/// A persistent hash map backed by a CHAMP trie. Branching is 32-way and bitmap-indexed with
/// immutable collision buckets, so operations are constant time in expectation while versions share
/// every unchanged node.
class persistent_hash_map {
private:
    static constexpr int bits_per_level = 5;
    static constexpr int branch_mask = (1 << bits_per_level) - 1;
    static constexpr int max_depth = 7;

    // The iterator keeps an inline stack of `max_depth` frames, which is only
    // sufficient while bitmap nodes are confined to shifts covering a 32-bit
    // hash. Guard the pairing so a future `bits_per_level` change cannot
    // silently overflow that stack.
    static_assert(
        max_depth * bits_per_level >= 32 && (max_depth - 1) * bits_per_level < 32,
        "max_depth must be exactly the number of bitmap levels needed to consume a 32-bit hash");

    struct node;
    struct hash_node;
    struct leaf_node;
    struct collision_node;
    struct bitmap_indexed_node;
    struct payload;
    struct entry_run_view;
    struct diff_operand;
    struct policy_identity final {};
    struct no_update_factory final {};

    using node_ptr = std::shared_ptr<const node>;
    using hash_node_ptr = std::shared_ptr<const hash_node>;
    using leaf_node_ptr = std::shared_ptr<const leaf_node>;

    struct mutable_node;
    struct mutable_hash_node;
    struct mutable_leaf_node;
    struct mutable_collision_node;
    struct mutable_bitmap_indexed_node;

    using mutable_node_ptr = std::unique_ptr<mutable_node>;
    using mutable_hash_node_ptr = std::unique_ptr<mutable_hash_node>;
    using mutable_leaf_node_ptr = std::unique_ptr<mutable_leaf_node>;

    using bulk_update_function = std::function<T(const T&, const T&)>;

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<Key, T>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using value_equal = ValueEqual;

    class transient;

private:
    struct payload {
        std::uint32_t hash;
        value_type entry;
    };

public:

    /// An empty map.
    persistent_hash_map() = default;

    /// An empty map.
    persistent_hash_map(const persistent_hash_map&) = default;
    persistent_hash_map& operator=(const persistent_hash_map&) = default;

    // Moved-from maps must still read as empty: the defaulted move operations
    // null `root_` but would leave `count_` stale, making `count()` disagree
    // with enumeration.
    persistent_hash_map(persistent_hash_map&& other) noexcept(
        std::is_nothrow_move_constructible_v<Hash>
        && std::is_nothrow_move_constructible_v<KeyEqual>
        && std::is_nothrow_move_constructible_v<ValueEqual>)
        : root_(std::move(other.root_)),
          count_(std::exchange(other.count_, 0)),
          hash_(std::move(other.hash_)),
          key_equal_(std::move(other.key_equal_)),
          value_equal_(std::move(other.value_equal_)),
          policy_identity_(std::move(other.policy_identity_)) {
    }

    persistent_hash_map& operator=(persistent_hash_map&& other) noexcept(
        std::is_nothrow_move_assignable_v<Hash>
        && std::is_nothrow_move_assignable_v<KeyEqual>
        && std::is_nothrow_move_assignable_v<ValueEqual>) {
        if (this != &other) {
            root_ = std::move(other.root_);
            count_ = std::exchange(other.count_, 0);
            hash_ = std::move(other.hash_);
            key_equal_ = std::move(other.key_equal_);
            value_equal_ = std::move(other.value_equal_);
            policy_identity_ = std::move(other.policy_identity_);
        }

        return *this;
    }

    /// An empty map.
    ~persistent_hash_map() = default;

    /// Whether the map holds no entries.
    static persistent_hash_map empty() {
        return {};
    }

    /// An empty map using the supplied policies, which it retains.
    static persistent_hash_map create(
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        return persistent_hash_map(nullptr, 0, std::move(hash), std::move(equal), std::move(values_equal));
    }

    // Builds an independent HAMT by mutating unpublished nodes in place and
    // freezing them into detached persistent nodes on demand. Each
    // `to_immutable` call copies every reachable node, so frozen maps never
    // share mutable storage with the builder and the builder stays usable
    // afterwards. Duplicate keys keep the first stored key; a value write is
    // skipped when the incoming value compares equal under the value-equality
    // policy, so the earlier stored value is retained (last distinct value
    // wins). Builders are move-only scratch state, not persistent values.
    class bulk_builder {
    public:
        /// Constructs the bulk builder from the given parts.
        explicit bulk_builder(Hash hash = {}, KeyEqual equal = {}, ValueEqual values_equal = {})
            : hash_(std::move(hash)),
              key_equal_(std::move(equal)),
              value_equal_(std::move(values_equal)),
              policy_identity_(std::make_shared<const policy_identity>()) {
        }

        /// Number of elements in the collection.
        [[nodiscard]] size_type count() const noexcept {
            return count_;
        }

        /// A collection with the key bound to the value, adding or replacing as needed.
        void set_item(const Key& key, const T& value) {
            const auto hash = static_cast<std::uint32_t>(std::invoke(hash_, key));
            if (!root_) {
                root_ = std::make_unique<mutable_leaf_node>(hash, key, value);
                count_ = 1;
                return;
            }

            bool added = false;
            if (auto replacement = root_->set(
                key,
                value,
                hash,
                0,
                key_equal_,
                value_equal_,
                nullptr,
                nullptr,
                added)) {
                root_ = std::move(replacement);
            }
            if (added) {
                ++count_;
            }
        }

        // Adds `add_value` for a missing key or combines it with the stored
        // value for a hit. The update callable is validated before hashing,
        // the key is hashed once, and exactly one equal-hash path is scanned.
        // The first equivalent key and any value-equal stored representative
        // are retained. This is construction-only staging: snapshots returned
        // by `to_immutable` remain detached from later builder mutations.
        template <class UpdateFactory>
            requires std::invocable<UpdateFactory&, const T&, const T&>
                && std::convertible_to<
                    std::invoke_result_t<UpdateFactory&, const T&, const T&>,
                    T>
        /// A collection with the key bound to the value, adding or replacing as needed.
        T add_or_update(
            const Key& key,
            const T& add_value,
            UpdateFactory&& update_factory) {
            bulk_update_function update(std::forward<UpdateFactory>(update_factory));
            if (!update) {
                throw std::invalid_argument(
                    "The bulk_builder update factory must not be empty.");
            }

            const auto hash = static_cast<std::uint32_t>(std::invoke(hash_, key));
            if (!root_) {
                root_ = std::make_unique<mutable_leaf_node>(hash, key, add_value);
                count_ = 1;
                return add_value;
            }

            auto selected = std::optional<T>{};
            bool added = false;
            if (auto replacement = root_->set(
                key,
                add_value,
                hash,
                0,
                key_equal_,
                value_equal_,
                std::addressof(update),
                std::addressof(selected),
                added)) {
                root_ = std::move(replacement);
            }
            if (added) {
                ++count_;
            }
            if (!selected) {
                throw std::logic_error(
                    "The bulk_builder update path did not select a value.");
            }
            return std::move(*selected);
        }

        /// The immutable collection holding the current contents.
        [[nodiscard]] persistent_hash_map to_immutable() const {
            if (!root_) {
                return persistent_hash_map(
                    nullptr, 0, hash_, key_equal_, value_equal_, policy_identity_);
            }

            return persistent_hash_map(
                root_->freeze(), count_, hash_, key_equal_, value_equal_, policy_identity_);
        }

    private:
        mutable_node_ptr root_;
        size_type count_ = 0;
        DURABLE7_HAMT_NO_UNIQUE_ADDRESS Hash hash_{};
        DURABLE7_HAMT_NO_UNIQUE_ADDRESS KeyEqual key_equal_{};
        DURABLE7_HAMT_NO_UNIQUE_ADDRESS ValueEqual value_equal_{};
        std::shared_ptr<const policy_identity> policy_identity_;
    };

    /// A builder for constructing in bulk, which is cheaper than repeated insertion.
    static bulk_builder create_bulk_builder(
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        return bulk_builder(std::move(hash), std::move(equal), std::move(values_equal));
    }

    /// Opens a mutable session seeded from this map.
    [[nodiscard]] static transient create_transient(
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {});

    /// Opens a mutable session seeded from this map. The map itself is unaffected.
    [[nodiscard]] transient to_transient() const &;

    /// Opens a mutable session seeded from this map. The map itself is unaffected.
    [[nodiscard]] transient to_transient() &&;

    /// A map holding a range's entries, built in bulk rather than by repeated insertion.
    static persistent_hash_map create_range(
        std::initializer_list<value_type> items,
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        auto builder = create_bulk_builder(std::move(hash), std::move(equal), std::move(values_equal));
        for (const auto& item : items) {
            builder.set_item(item.first, item.second);
        }

        return builder.to_immutable();
    }

    /// A map holding a range's entries, built in bulk rather than by repeated insertion.
    template <class Range>
    static persistent_hash_map create_range(
        const Range& items,
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        auto builder = create_bulk_builder(std::move(hash), std::move(equal), std::move(values_equal));
        for (const auto& item : items) {
            builder.set_item(item.first, item.second);
        }

        return builder.to_immutable();
    }

    /// Number of entries in the map.
    [[nodiscard]] size_type count() const noexcept {
        return count_;
    }

    /// Whether the map holds no entries.
    [[nodiscard]] bool is_empty() const noexcept {
        return count_ == 0;
    }

    /// The retained hashing policy. Keys the policy treats as equivalent must hash identically.
    [[nodiscard]] const Hash& hash_function() const noexcept {
        return hash_;
    }

    /// The retained key equivalence policy.
    [[nodiscard]] const KeyEqual& key_eq() const noexcept {
        return key_equal_;
    }

    /// The retained value equivalence policy.
    [[nodiscard]] const ValueEqual& value_eq() const noexcept {
        return value_equal_;
    }

    /// Whether the key is present.
    [[nodiscard]] bool contains_key(const Key& key) const {
        return try_get(key) != nullptr;
    }

    // The returned pointer aims into this map's shared nodes: it stays valid
    // while any map value retaining the containing version is alive, and in
    // particular does NOT outlive a temporary receiver
    // (get_map().try_get(k) dangles once the full expression ends).
    [[nodiscard]] const T* try_get(const Key& key) const {
        const Key* actual_key = nullptr;
        const T* value = nullptr;
        return try_get_entry(key, actual_key, value) ? value : nullptr;
    }

    // Same lifetime contract as try_get.
    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const {
        const Key* actual_key = nullptr;
        const T* value = nullptr;
        return try_get_entry(equal_key, actual_key, value) ? actual_key : nullptr;
    }

    /// The value stored for the key. Raises when the key is absent.
    [[nodiscard]] const T& at(const Key& key) const {
        if (const auto* value = try_get(key)) {
            return *value;
        }

        throw std::out_of_range("The key was not present in the persistent_hash_map.");
    }

    /// A map with the key bound to the value, adding or replacing as needed.
    [[nodiscard]] persistent_hash_map set_item(const Key& key, const T& value) const {
        const auto hash = get_hash(key);
        if (!root_) {
            return persistent_hash_map(
                make_leaf(hash, key, value),
                1,
                hash_,
                key_equal_,
                value_equal_,
                policy_identity_);
        }

        bool added = false;
        auto new_root = root_->set(key, value, hash, 0, key_equal_, value_equal_, true, added);
        if (new_root.get() == root_.get()) {
            return *this;
        }

        return persistent_hash_map(
            std::move(new_root),
            count_ + (added ? 1u : 0u),
            hash_,
            key_equal_,
            value_equal_,
            policy_identity_);
    }

    /// A map containing the given entry; returns the receiver when already present.
    [[nodiscard]] persistent_hash_map add(const Key& key, const T& value) const {
        auto [result, added] = try_add(key, value);
        if (!added) {
            throw std::invalid_argument("An equivalent key is already present in the persistent_hash_map.");
        }

        return result;
    }

    /// Adds the entry unless an equivalent one is present, reporting which happened.
    [[nodiscard]] std::pair<persistent_hash_map, bool> try_add(const Key& key, const T& value) const {
        const auto hash = get_hash(key);
        if (!root_) {
            return {
                persistent_hash_map(
                    make_leaf(hash, key, value),
                    1,
                    hash_,
                    key_equal_,
                    value_equal_,
                    policy_identity_),
                true,
            };
        }

        bool added = false;
        auto new_root = root_->set(key, value, hash, 0, key_equal_, value_equal_, false, added);
        if (!added) {
            return {*this, false};
        }

        return {
            persistent_hash_map(
                std::move(new_root),
                count_ + 1,
                hash_,
                key_equal_,
                value_equal_,
                policy_identity_),
            true,
        };
    }

    // Returns the current map and its stored value on a hit, or invokes the
    // add factory exactly once and returns the resulting successor on a miss.
    // Callable validation precedes hashing even when the key is present.
    template <class AddFactory>
        requires std::invocable<AddFactory&, const Key&>
            && std::convertible_to<std::invoke_result_t<AddFactory&, const Key&>, T>
    [[nodiscard]] std::pair<persistent_hash_map, T> get_or_add(
        const Key& key,
        AddFactory&& add_factory) const {
        validate_callable(add_factory, "The add factory must not be empty.");
        return apply_factory_update(
            key,
            add_factory,
            static_cast<no_update_factory*>(nullptr));
    }

    // Selects exactly one factory in one hashed trie descent. On a hit the
    // update factory receives the caller's lookup key and the stored value;
    // the stored key representative is retained. A value-equal update keeps
    // and returns the stored value representative and shares the current root.
    template <class AddFactory, class UpdateFactory>
        requires std::invocable<AddFactory&, const Key&>
            && std::convertible_to<std::invoke_result_t<AddFactory&, const Key&>, T>
            && std::invocable<UpdateFactory&, const Key&, const T&>
            && std::convertible_to<
                std::invoke_result_t<UpdateFactory&, const Key&, const T&>,
                T>
    [[nodiscard]] std::pair<persistent_hash_map, T> add_or_update(
        const Key& key,
        AddFactory&& add_factory,
        UpdateFactory&& update_factory) const {
        validate_callable(add_factory, "The add factory must not be empty.");
        validate_callable(update_factory, "The update factory must not be empty.");
        return apply_factory_update(key, add_factory, std::addressof(update_factory));
    }

    template <class Range>
    [[nodiscard]] persistent_hash_map set_items(const Range& items) const {
        auto map = *this;
        for (const auto& item : items) {
            map = map.set_item(item.first, item.second);
        }

        return map;
    }

    [[nodiscard]] persistent_hash_map set_items(std::initializer_list<value_type> items) const {
        auto map = *this;
        for (const auto& item : items) {
            map = map.set_item(item.first, item.second);
        }

        return map;
    }

    [[nodiscard]] persistent_hash_map remove(const Key& key) const {
        auto [result, removed, value] = try_remove(key);
        (void)value;
        return removed ? result : *this;
    }

    [[nodiscard]] std::tuple<persistent_hash_map, bool, std::optional<T>> try_remove(const Key& key) const {
        if (!root_) {
            return {*this, false, std::nullopt};
        }

        bool removed = false;
        std::optional<T> value;
        auto new_root = root_->remove(key, get_hash(key), 0, key_equal_, removed, value);
        if (!removed) {
            return {*this, false, std::nullopt};
        }

        return {
            from_root(
                std::move(new_root), count_ - 1, hash_, key_equal_, value_equal_, policy_identity_),
            true,
            std::move(value),
        };
    }

    [[nodiscard]] persistent_hash_map clear() const {
        if (count_ == 0) {
            return *this;
        }

        return persistent_hash_map(nullptr, 0, hash_, key_equal_, value_equal_, policy_identity_);
    }

    [[nodiscard]] persistent_hash_map union_with(const persistent_hash_map& other) const {
        return combine(other, set_operation::union_values);
    }

    [[nodiscard]] persistent_hash_map intersect_with(const persistent_hash_map& other) const {
        return combine(other, set_operation::intersect);
    }

    [[nodiscard]] persistent_hash_map except_with(const persistent_hash_map& other) const {
        return combine(other, set_operation::except);
    }

    [[nodiscard]] persistent_hash_map symmetric_except_with(
        const persistent_hash_map& other) const {
        return combine(other, set_operation::symmetric_except);
    }

    class const_iterator {
    private:
        struct frame {
            const bitmap_indexed_node* branch = nullptr;
            std::size_t data_index = 0;
            std::size_t child_index = 0;
        };

    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = persistent_hash_map::value_type;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        /// An unpositioned cursor, holding no version.
        const_iterator() = default;

        reference operator*() const {
            return *current_;
        }

        pointer operator->() const {
            return current_;
        }

        const_iterator& operator++() {
            at_end_ = !move_next();
            return *this;
        }

        const_iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& iterator, std::default_sentinel_t) noexcept {
            return iterator.at_end_;
        }

        friend bool operator==(std::default_sentinel_t sentinel, const const_iterator& iterator) noexcept {
            return iterator == sentinel;
        }

        friend bool operator!=(const const_iterator& iterator, std::default_sentinel_t sentinel) noexcept {
            return !(iterator == sentinel);
        }

        friend bool operator!=(std::default_sentinel_t sentinel, const const_iterator& iterator) noexcept {
            return !(iterator == sentinel);
        }

    private:
        friend class persistent_hash_map;

        explicit const_iterator(node_ptr root)
            : root_(std::move(root)),
              next_(root_.get()),
              at_end_(false) {
            at_end_ = !move_next();
        }

        bool move_next() noexcept {
            if (move_next_collision_entry()) {
                return true;
            }

            const node* current_node = next_;
            next_ = nullptr;

            while (true) {
                if (current_node == nullptr) {
                    if (depth_ == 0) {
                        current_ = nullptr;
                        return false;
                    }

                    auto& top = frames_[depth_ - 1];
                    if (top.data_index < top.branch->data_.size()) {
                        current_ = std::addressof(top.branch->data_[top.data_index++].entry);
                        return true;
                    }
                    if (top.child_index == top.branch->children_.size()) {
                        frames_[--depth_] = {};
                        continue;
                    }

                    current_node = top.branch->children_[top.child_index++].get();
                }

                switch (current_node->kind()) {
                case persistent_hamt_node_kind::leaf:
                    current_ = std::addressof(static_cast<const leaf_node*>(current_node)->entry_);
                    return true;
                case persistent_hamt_node_kind::collision:
                    collision_entries_ = std::addressof(
                        static_cast<const collision_node*>(current_node)->entries_);
                    collision_index_ = 0;
                    return move_next_collision_entry();
                default: {
                    const auto* branch = static_cast<const bitmap_indexed_node*>(current_node);
                    frames_[depth_++] = frame{branch, 0, 0};
                    current_node = nullptr;
                    break;
                }
                }
            }
        }

        bool move_next_collision_entry() noexcept {
            if (collision_entries_ == nullptr) {
                return false;
            }

            if (collision_index_ < collision_entries_->size()) {
                current_ = std::addressof((*collision_entries_)[collision_index_++]);
                return true;
            }

            collision_entries_ = nullptr;
            collision_index_ = 0;
            return false;
        }

        node_ptr root_;
        const node* next_ = nullptr;
        std::array<frame, max_depth> frames_{};
        std::size_t depth_ = 0;
        const std::vector<value_type>* collision_entries_ = nullptr;
        std::size_t collision_index_ = 0;
        const value_type* current_ = nullptr;
        bool at_end_ = true;
    };

    [[nodiscard]] const_iterator begin() const {
        return const_iterator(root_);
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return {};
    }

    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }

    [[nodiscard]] std::default_sentinel_t cend() const noexcept {
        return {};
    }

    [[nodiscard]] std::vector<value_type> to_vector() const {
        std::vector<value_type> items;
        items.reserve(count_);
        for (const auto& item : *this) {
            items.push_back(item);
        }

        return items;
    }

    [[nodiscard]] std::vector<Key> keys() const {
        std::vector<Key> result;
        result.reserve(count_);
        for (const auto& item : *this) {
            result.push_back(item.first);
        }

        return result;
    }

    [[nodiscard]] std::vector<T> values() const {
        std::vector<T> result;
        result.reserve(count_);
        for (const auto& item : *this) {
            result.push_back(item.second);
        }

        return result;
    }

    [[nodiscard]] bool shares_root_with(const persistent_hash_map& other) const noexcept {
        return root_.get() == other.root_.get();
    }

    [[nodiscard]] bool shares_policy_with(const persistent_hash_map& other) const noexcept {
        return policy_identity_.get() == other.policy_identity_.get();
    }

    // Precondition: both maps' stateful Hash/KeyEqual objects define compatible key semantics.
    // Descendants sharing this map's internal policy identity use canonical bitmap alignment with
    // no hashing and prune pointer-identical nodes before KeyEqual or ValueEqual. Independently
    // created policy identities retain semantic lookup comparison because compatible equality
    // objects may use different coherent hash states.
    [[nodiscard]] bool map_equals(const persistent_hash_map& other) const {
        if (root_.get() == other.root_.get()) {
            return true;
        }
        if (count_ != other.count_) {
            return false;
        }
        if (shares_policy_with(other)) {
            return nodes_equal(root_.get(), other.root_.get(), key_equal_, value_equal_);
        }
        for (const auto& [key, value] : *this) {
            const auto* actual_key = static_cast<const Key*>(nullptr);
            const auto* other_value = static_cast<const T*>(nullptr);
            if (!other.try_get_entry(key, actual_key, other_value)
                || !values_equal_by_policy(value, *other_value, value_equal_)) {
                return false;
            }
        }
        return true;
    }

    // The same policy-compatibility precondition, structural fast path, and semantic fallback as
    // map_equals apply.
    [[nodiscard]] std::vector<map_difference<Key, T>> diff(const persistent_hash_map& other) const {
        if (root_.get() == other.root_.get()) {
            return {};
        }
        std::vector<map_difference<Key, T>> result;
        if (shares_policy_with(other)) {
            diff_operands(
                diff_operand{root_.get(), nullptr},
                diff_operand{other.root_.get(), nullptr},
                0,
                key_equal_,
                value_equal_,
                result);
            return result;
        }
        for (const auto& [key, value] : *this) {
            const auto* actual_key = static_cast<const Key*>(nullptr);
            const auto* other_value = static_cast<const T*>(nullptr);
            if (!other.try_get_entry(key, actual_key, other_value)) {
                result.push_back({map_difference_kind::removed, key, value, std::nullopt});
            } else if (!values_equal_by_policy(value, *other_value, value_equal_)) {
                result.push_back({map_difference_kind::changed, key, value, *other_value});
            }
        }
        for (const auto& [key, value] : other) {
            const auto* actual_key = static_cast<const Key*>(nullptr);
            const auto* old_value = static_cast<const T*>(nullptr);
            if (!try_get_entry(key, actual_key, old_value)) {
                result.push_back({map_difference_kind::added, key, std::nullopt, value});
            }
        }
        return result;
    }

    [[nodiscard]] const void* debug_root_identity() const noexcept {
        return root_.get();
    }

    [[nodiscard]] persistent_hamt_node_kind debug_root_kind() const noexcept {
        return kind_of(root_.get());
    }

    [[nodiscard]] std::vector<const void*> debug_root_child_identities() const {
        std::vector<const void*> children;
        if (kind_of(root_.get()) != persistent_hamt_node_kind::bitmap_indexed) {
            return children;
        }

        const auto* branch = static_cast<const bitmap_indexed_node*>(root_.get());

        children.reserve(branch->children_.size());
        for (const auto& child : branch->children_) {
            children.push_back(child.get());
        }

        return children;
    }

    [[nodiscard]] bool debug_validate_canonical() const noexcept {
        auto entries = size_type{0};
        return debug_validate_node(root_.get(), 0, 0, 0, entries) && entries == count();
    }

    // Precondition: both maps' stateful KeyEqual objects define compatible key semantics.
    [[nodiscard]] bool debug_topology_equal(const persistent_hash_map& other) const {
        return debug_nodes_topology_equal(root_.get(), other.root_.get(), key_equal_);
    }

private:
    enum class set_operation {
        union_values,
        intersect,
        except,
        symmetric_except,
    };

    persistent_hash_map(
        node_ptr root,
        size_type count,
        Hash hash,
        KeyEqual equal,
        ValueEqual values_equal,
        std::shared_ptr<const policy_identity> policy_token = {})
        : root_(std::move(root)),
          count_(count),
          hash_(std::move(hash)),
          key_equal_(std::move(equal)),
          value_equal_(std::move(values_equal)),
          policy_identity_(policy_token
              ? std::move(policy_token)
              : std::make_shared<const policy_identity>()) {
    }

    static int index(std::uint32_t hash, int shift) noexcept {
        return static_cast<int>((hash >> shift) & branch_mask);
    }

    static std::uint32_t bit(int index) noexcept {
        return 1u << index;
    }

    static std::size_t slot(std::uint32_t bitmap, std::uint32_t bit) noexcept {
        return static_cast<std::size_t>(std::popcount(bitmap & (bit - 1u)));
    }

    std::uint32_t get_hash(const Key& key) const {
        return static_cast<std::uint32_t>(std::invoke(hash_, key));
    }

    static bool values_equal_by_policy(
        const T& left,
        const T& right,
        const ValueEqual& values_equal) {
        return std::addressof(left) == std::addressof(right)
            || std::invoke(values_equal, left, right);
    }

    static leaf_node_ptr make_leaf(std::uint32_t hash, const Key& key, const T& value) {
        return std::make_shared<leaf_node>(hash, key, value);
    }

    static persistent_hash_map from_root(
        node_ptr root,
        size_type count,
        Hash hash,
        KeyEqual equal,
        ValueEqual values_equal,
        std::shared_ptr<const policy_identity> policy_token) {
        if (count == 0) {
            return persistent_hash_map(
                nullptr,
                0,
                std::move(hash),
                std::move(equal),
                std::move(values_equal),
                std::move(policy_token));
        }

        return persistent_hash_map(
            std::move(root),
            count,
            std::move(hash),
            std::move(equal),
            std::move(values_equal),
            std::move(policy_token));
    }

    [[nodiscard]] persistent_hash_map combine(
        const persistent_hash_map& other,
        set_operation operation) const {
        if (policy_identity_.get() != other.policy_identity_.get()) {
            return combine_elementwise(other, operation);
        }

        auto root = combine_nodes(
            root_, other.root_, 0, operation, key_equal_, value_equal_);
        if (root.get() == root_.get()) {
            return *this;
        }
        if (root.get() == other.root_.get()) {
            return other;
        }
        const auto result_count = root ? root->entry_count() : 0;
        return persistent_hash_map(
            std::move(root),
            result_count,
            hash_,
            key_equal_,
            value_equal_,
            policy_identity_);
    }

    [[nodiscard]] persistent_hash_map combine_elementwise(
        const persistent_hash_map& other,
        set_operation operation) const {
        auto result = operation == set_operation::intersect ? clear() : *this;
        switch (operation) {
        case set_operation::union_values:
            for (const auto& [key, value] : other) {
                result = result.set_item(key, value);
            }
            break;
        case set_operation::intersect:
            for (const auto& [key, value] : *this) {
                if (other.contains_key(key)) {
                    result = result.set_item(key, value);
                }
            }
            break;
        case set_operation::except:
            for (const auto& [key, value] : other) {
                (void)value;
                result = result.remove(key);
            }
            break;
        case set_operation::symmetric_except:
            for (const auto& [key, value] : other) {
                result = result.contains_key(key)
                    ? result.remove(key)
                    : result.set_item(key, value);
            }
            break;
        }
        return result;
    }

    static node_ptr combine_nodes(
        const node_ptr& left,
        const node_ptr& right,
        int shift,
        set_operation operation,
        const KeyEqual& equal,
        const ValueEqual& values_equal) {
        if (left.get() == right.get()) {
            return operation == set_operation::union_values
                    || operation == set_operation::intersect
                ? left
                : nullptr;
        }
        if (!left) {
            return operation == set_operation::union_values
                    || operation == set_operation::symmetric_except
                ? right
                : nullptr;
        }
        if (!right) {
            return operation != set_operation::intersect ? left : nullptr;
        }

        const auto left_hash = std::dynamic_pointer_cast<const hash_node>(left);
        const auto right_hash = std::dynamic_pointer_cast<const hash_node>(right);
        if (left_hash && right_hash) {
            return combine_hash_nodes(
                left_hash, right_hash, shift, operation, equal, values_equal);
        }

        auto slots = std::array<node_ptr, 32>{};
        for (auto index_value = 0; index_value < 32; ++index_value) {
            slots[static_cast<std::size_t>(index_value)] = combine_nodes(
                logical_slot(left, index_value, shift),
                logical_slot(right, index_value, shift),
                shift + bits_per_level,
                operation,
                equal,
                values_equal);
        }
        return build_logical_node(slots, left, shift, equal, values_equal);
    }

    static node_ptr combine_hash_nodes(
        const hash_node_ptr& left,
        const hash_node_ptr& right,
        int shift,
        set_operation operation,
        const KeyEqual& equal,
        const ValueEqual& values_equal) {
        if (left->hash_ != right->hash_) {
            if (operation == set_operation::intersect) {
                return nullptr;
            }
            if (operation == set_operation::except) {
                return left;
            }
            auto slots = std::array<node_ptr, 32>{};
            const auto left_index = index(left->hash_, shift);
            const auto right_index = index(right->hash_, shift);
            if (left_index != right_index) {
                slots[static_cast<std::size_t>(left_index)] = left;
                slots[static_cast<std::size_t>(right_index)] = right;
            } else {
                slots[static_cast<std::size_t>(left_index)] = combine_hash_nodes(
                    left,
                    right,
                    shift + bits_per_level,
                    operation,
                    equal,
                    values_equal);
            }
            return build_logical_node(slots, left, shift, equal, values_equal);
        }

        const auto left_entries = hash_entries(left);
        const auto right_entries = hash_entries(right);
        auto result = std::vector<value_type>{};
        result.reserve(left_entries.size() + right_entries.size());
        for (const auto& left_entry : left_entries) {
            const auto right_iterator = std::find_if(
                right_entries.begin(), right_entries.end(), [&](const value_type& candidate) {
                    return std::invoke(equal, left_entry.first, candidate.first);
                });
            const auto found = right_iterator != right_entries.end();
            switch (operation) {
            case set_operation::union_values:
                result.emplace_back(
                    left_entry.first,
                    found && !values_equal_by_policy(left_entry.second, right_iterator->second, values_equal)
                        ? right_iterator->second
                        : left_entry.second);
                break;
            case set_operation::intersect:
                if (found) {
                    result.push_back(left_entry);
                }
                break;
            case set_operation::except:
            case set_operation::symmetric_except:
                if (!found) {
                    result.push_back(left_entry);
                }
                break;
            }
        }
        if (operation == set_operation::union_values
            || operation == set_operation::symmetric_except) {
            for (const auto& right_entry : right_entries) {
                const auto found = std::any_of(
                    left_entries.begin(), left_entries.end(), [&](const value_type& candidate) {
                        return std::invoke(equal, candidate.first, right_entry.first);
                    });
                if (!found) {
                    result.push_back(right_entry);
                }
            }
        }
        if (entries_match(left_entries, result, equal, values_equal)) {
            return left;
        }
        if (result.empty()) {
            return nullptr;
        }
        if (result.size() == 1) {
            return make_leaf(left->hash_, result[0].first, result[0].second);
        }
        return std::make_shared<collision_node>(left->hash_, std::move(result));
    }

    static std::vector<value_type> hash_entries(const hash_node_ptr& candidate) {
        if (candidate->kind() == persistent_hamt_node_kind::leaf) {
            return {std::static_pointer_cast<const leaf_node>(candidate)->entry_};
        }
        return std::static_pointer_cast<const collision_node>(candidate)->entries_;
    }

    static bool entries_match(
        const std::vector<value_type>& left,
        const std::vector<value_type>& right,
        const KeyEqual& equal,
        const ValueEqual& values_equal) {
        if (left.size() != right.size()) {
            return false;
        }
        for (auto i = std::size_t{0}; i < left.size(); ++i) {
            if (!std::invoke(equal, left[i].first, right[i].first)
                || !values_equal_by_policy(left[i].second, right[i].second, values_equal)) {
                return false;
            }
        }
        return true;
    }

    static node_ptr logical_slot(const node_ptr& candidate, int slot_index, int shift) {
        if (!candidate) {
            return nullptr;
        }
        if (candidate->kind() != persistent_hamt_node_kind::bitmap_indexed) {
            const auto hash = std::static_pointer_cast<const hash_node>(candidate);
            return index(hash->hash_, shift) == slot_index ? candidate : nullptr;
        }
        const auto branch = std::static_pointer_cast<const bitmap_indexed_node>(candidate);
        const auto selected_bit = bit(slot_index);
        if ((branch->data_map_ & selected_bit) != 0) {
            const auto& entry = branch->data_[slot(branch->data_map_, selected_bit)];
            return make_leaf(entry.hash, entry.entry.first, entry.entry.second);
        }
        return (branch->node_map_ & selected_bit) != 0
            ? branch->children_[slot(branch->node_map_, selected_bit)]
            : nullptr;
    }

    static node_ptr build_logical_node(
        const std::array<node_ptr, 32>& slots,
        const node_ptr& original_left,
        int shift,
        const KeyEqual& equal,
        const ValueEqual& values_equal) {
        if (logical_slots_match(slots, original_left, shift, equal, values_equal)) {
            return original_left;
        }
        auto data_map = std::uint32_t{0};
        auto node_map = std::uint32_t{0};
        auto data = std::vector<payload>{};
        auto children = std::vector<node_ptr>{};
        for (auto index_value = 0; index_value < 32; ++index_value) {
            const auto& candidate = slots[static_cast<std::size_t>(index_value)];
            if (!candidate) {
                continue;
            }
            const auto selected_bit = bit(index_value);
            if (candidate->kind() == persistent_hamt_node_kind::leaf) {
                const auto leaf = std::static_pointer_cast<const leaf_node>(candidate);
                data_map |= selected_bit;
                data.push_back(payload{leaf->hash_, leaf->entry_});
            } else {
                node_map |= selected_bit;
                children.push_back(candidate);
            }
        }
        return bitmap_indexed_node::rebuild(
            data_map, node_map, std::move(data), std::move(children));
    }

    static bool logical_slots_match(
        const std::array<node_ptr, 32>& slots,
        const node_ptr& original,
        int shift,
        const KeyEqual& equal,
        const ValueEqual& values_equal) {
        for (auto index_value = 0; index_value < 32; ++index_value) {
            const auto expected = logical_slot(original, index_value, shift);
            const auto& actual = slots[static_cast<std::size_t>(index_value)];
            if (expected.get() == actual.get()) {
                continue;
            }
            if (!expected || !actual
                || expected->kind() != persistent_hamt_node_kind::leaf
                || actual->kind() != persistent_hamt_node_kind::leaf) {
                return false;
            }
            const auto left = std::static_pointer_cast<const leaf_node>(expected);
            const auto right = std::static_pointer_cast<const leaf_node>(actual);
            if (left->hash_ != right->hash_
                || !std::invoke(equal, left->entry_.first, right->entry_.first)
                || !values_equal_by_policy(left->entry_.second, right->entry_.second, values_equal)) {
                return false;
            }
        }
        return true;
    }

    static persistent_hamt_node_kind kind_of(const node* candidate) noexcept {
        return candidate == nullptr ? persistent_hamt_node_kind::empty : candidate->kind();
    }

    bool try_get_entry(
        const Key& key,
        const Key*& actual_key,
        const T*& value) const {
        auto* current_node = root_.get();
        if (current_node == nullptr) {
            return false;
        }

        const auto hash = get_hash(key);
        auto shift = 0;
        while (current_node->kind() == persistent_hamt_node_kind::bitmap_indexed) {
            const auto* branch = static_cast<const bitmap_indexed_node*>(current_node);
            const auto selected_bit = bit(index(hash, shift));
            if ((branch->data_map_ & selected_bit) != 0) {
                const auto& candidate = branch->data_[slot(branch->data_map_, selected_bit)];
                if (candidate.hash == hash && std::invoke(key_equal_, candidate.entry.first, key)) {
                    actual_key = std::addressof(candidate.entry.first);
                    value = std::addressof(candidate.entry.second);
                    return true;
                }
                return false;
            }
            if ((branch->node_map_ & selected_bit) == 0) {
                return false;
            }
            current_node = branch->children_[slot(branch->node_map_, selected_bit)].get();
            shift += bits_per_level;
        }

        if (current_node->kind() == persistent_hamt_node_kind::leaf) {
            const auto* leaf = static_cast<const leaf_node*>(current_node);
            if (leaf->hash_ == hash && std::invoke(key_equal_, leaf->entry_.first, key)) {
                actual_key = std::addressof(leaf->entry_.first);
                value = std::addressof(leaf->entry_.second);
                return true;
            }

            return false;
        }

        const auto* collision = static_cast<const collision_node*>(current_node);
        if (collision->hash_ != hash) {
            return false;
        }

        for (const auto& collision_entry : collision->entries_) {
            if (std::invoke(key_equal_, collision_entry.first, key)) {
                actual_key = std::addressof(collision_entry.first);
                value = std::addressof(collision_entry.second);
                return true;
            }
        }

        return false;
    }

    static node_ptr merge_hash_nodes(hash_node_ptr left, leaf_node_ptr right, int shift) {
        if (left->hash_ == right->hash_) {
            return collision_node::create(std::move(left), std::move(right));
        }

        if (shift >= 32) {
            throw std::logic_error("Different 32-bit hashes cannot share every HAMT level.");
        }

        const auto left_index = index(left->hash_, shift);
        const auto right_index = index(right->hash_, shift);
        const auto left_bit = bit(left_index);
        const auto right_bit = bit(right_index);

        if (left_index == right_index) {
            std::vector<node_ptr> children;
            children.push_back(merge_hash_nodes(std::move(left), std::move(right), shift + bits_per_level));
            return std::make_shared<bitmap_indexed_node>(0, left_bit, std::vector<payload>{}, std::move(children));
        }

        std::vector<payload> data;
        std::vector<node_ptr> children;
        if (left->kind() == persistent_hamt_node_kind::leaf) {
            const auto leaf = std::static_pointer_cast<const leaf_node>(left);
            if (left_index < right_index) {
                data.push_back(payload{leaf->hash_, leaf->entry_});
                data.push_back(payload{right->hash_, right->entry_});
            } else {
                data.push_back(payload{right->hash_, right->entry_});
                data.push_back(payload{leaf->hash_, leaf->entry_});
            }
            return std::make_shared<bitmap_indexed_node>(
                left_bit | right_bit, 0, std::move(data), std::move(children));
        }

        data.push_back(payload{right->hash_, right->entry_});
        children.push_back(std::move(left));
        return std::make_shared<bitmap_indexed_node>(
            right_bit, left_bit, std::move(data), std::move(children));
    }

    struct node : std::enable_shared_from_this<node> {
        /// An empty node.
        virtual ~node() = default;

        /// Which case this value is.
        [[nodiscard]] virtual persistent_hamt_node_kind kind() const noexcept = 0;
        /// Number of entries.
        [[nodiscard]] virtual size_type entry_count() const noexcept = 0;

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] virtual node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool overwrite,
            bool& added) const = 0;

        /// A node without that element; returns the receiver when absent.
        [[nodiscard]] virtual node_ptr remove(
            const Key& key,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            bool& removed,
            std::optional<T>& value) const = 0;
    };

    struct hash_node : node {
        /// Constructs the hash node from the given parts.
        explicit hash_node(std::uint32_t hash)
            : hash_(hash) {
        }

        /// A shared handle on this object, for use where ownership must be kept alive.
        [[nodiscard]] hash_node_ptr shared_hash_from_this() const {
            return std::static_pointer_cast<const hash_node>(this->shared_from_this());
        }

        std::uint32_t hash_;
    };

    struct leaf_node final : hash_node {
        /// Constructs the leaf node from the given parts.
        leaf_node(std::uint32_t hash, Key key, T value)
            : hash_node(hash),
              entry_(std::move(key), std::move(value)) {
        }

        /// Which case this value is.
        [[nodiscard]] persistent_hamt_node_kind kind() const noexcept override {
            return persistent_hamt_node_kind::leaf;
        }

        /// Number of entries.
        [[nodiscard]] size_type entry_count() const noexcept override {
            return 1;
        }

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool overwrite,
            bool& added) const override {
            if (this->hash_ == hash && std::invoke(equal, entry_.first, key)) {
                added = false;
                if (!overwrite || values_equal_by_policy(entry_.second, value, values_equal)) {
                    return this->shared_from_this();
                }

                return make_leaf(this->hash_, entry_.first, value);
            }

            added = true;
            return merge_hash_nodes(
                this->shared_hash_from_this(),
                make_leaf(hash, key, value),
                shift);
        }

        /// A node without that element; returns the receiver when absent.
        [[nodiscard]] node_ptr remove(
            const Key& key,
            std::uint32_t hash,
            int,
            const KeyEqual& equal,
            bool& removed,
            std::optional<T>& value) const override {
            if (this->hash_ == hash && std::invoke(equal, entry_.first, key)) {
                removed = true;
                value = entry_.second;
                return nullptr;
            }

            removed = false;
            return this->shared_from_this();
        }

        value_type entry_;
    };

    struct collision_node final : hash_node {
        /// Constructs the collision node from the given parts.
        collision_node(std::uint32_t hash, std::vector<value_type> entries)
            : hash_node(hash),
              entries_(std::move(entries)) {
        }

        /// Which case this value is.
        [[nodiscard]] persistent_hamt_node_kind kind() const noexcept override {
            return persistent_hamt_node_kind::collision;
        }

        /// Number of entries.
        [[nodiscard]] size_type entry_count() const noexcept override {
            return entries_.size();
        }

        // Precondition: an equal-hash merge only ever combines two leaves whose
        // keys differ under the map's comparer. Equal-hash inserts into an
        // existing collision node are handled inside collision_node::set, so
        // appending right without a duplicate-key scan is safe only under that
        // precondition (mirrors the C# reference's Debug.Assert).
        static std::shared_ptr<const collision_node> create(hash_node_ptr left, leaf_node_ptr right) {
            assert(left->kind() == persistent_hamt_node_kind::leaf
                && "Equal-hash merges must combine two leaves.");
            const auto leaf = std::static_pointer_cast<const leaf_node>(left);
            std::vector<value_type> entries;
            entries.reserve(2);
            entries.emplace_back(leaf->entry_);
            entries.emplace_back(right->entry_);
            return std::make_shared<collision_node>(left->hash_, std::move(entries));
        }

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool overwrite,
            bool& added) const override {
            if (this->hash_ != hash) {
                added = true;
                return merge_hash_nodes(
                    this->shared_hash_from_this(),
                    make_leaf(hash, key, value),
                    shift);
            }

            for (std::size_t i = 0; i < entries_.size(); ++i) {
                if (!std::invoke(equal, entries_[i].first, key)) {
                    continue;
                }

                added = false;
                if (!overwrite || values_equal_by_policy(entries_[i].second, value, values_equal)) {
                    return this->shared_from_this();
                }

                auto replaced = entries_;
                replaced[i] = value_type(entries_[i].first, value);
                return std::make_shared<collision_node>(this->hash_, std::move(replaced));
            }

            auto entries = entries_;
            entries.emplace_back(key, value);
            added = true;
            return std::make_shared<collision_node>(this->hash_, std::move(entries));
        }

        /// A node without that element; returns the receiver when absent.
        [[nodiscard]] node_ptr remove(
            const Key& key,
            std::uint32_t hash,
            int,
            const KeyEqual& equal,
            bool& removed,
            std::optional<T>& value) const override {
            if (this->hash_ != hash) {
                removed = false;
                return this->shared_from_this();
            }

            for (std::size_t i = 0; i < entries_.size(); ++i) {
                if (!std::invoke(equal, entries_[i].first, key)) {
                    continue;
                }

                removed = true;
                value = entries_[i].second;

                if (entries_.size() == 2) {
                    const auto& remaining = entries_[1 - i];
                    return make_leaf(this->hash_, remaining.first, remaining.second);
                }

                std::vector<value_type> entries;
                entries.reserve(entries_.size() - 1);
                entries.insert(entries.end(), entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(i));
                entries.insert(entries.end(), entries_.begin() + static_cast<std::ptrdiff_t>(i) + 1, entries_.end());
                return std::make_shared<collision_node>(this->hash_, std::move(entries));
            }

            removed = false;
            return this->shared_from_this();
        }

        std::vector<value_type> entries_;
    };

    struct bitmap_indexed_node final : node {
        /// An empty node.
        bitmap_indexed_node(
            std::uint32_t data_map,
            std::uint32_t node_map,
            std::vector<payload> data,
            std::vector<node_ptr> children)
            : data_map_(data_map),
              node_map_(node_map),
              data_(std::move(data)),
              children_(std::move(children)),
              count_(data_.size()) {
            for (const auto& child : children_) {
                count_ += child->entry_count();
            }
        }

        /// Which case this value is.
        [[nodiscard]] persistent_hamt_node_kind kind() const noexcept override {
            return persistent_hamt_node_kind::bitmap_indexed;
        }

        /// Number of entries.
        [[nodiscard]] size_type entry_count() const noexcept override {
            return count_;
        }

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool overwrite,
            bool& added) const override {
            const auto selected_bit = bit(index(hash, shift));
            if ((data_map_ & selected_bit) != 0) {
                const auto data_slot = slot(data_map_, selected_bit);
                const auto& existing = data_[data_slot];
                if (existing.hash == hash && std::invoke(equal, existing.entry.first, key)) {
                    added = false;
                    if (!overwrite || values_equal_by_policy(existing.entry.second, value, values_equal)) {
                        return this->shared_from_this();
                    }
                    auto data = data_;
                    data[data_slot] = payload{hash, value_type(existing.entry.first, value)};
                    return std::make_shared<bitmap_indexed_node>(
                        data_map_, node_map_, std::move(data), children_);
                }

                auto child = merge_hash_nodes(
                    make_leaf(existing.hash, existing.entry.first, existing.entry.second),
                    make_leaf(hash, key, value),
                    shift + bits_per_level);
                auto data = data_;
                data.erase(data.begin() + static_cast<std::ptrdiff_t>(data_slot));
                auto children = children_;
                children.insert(
                    children.begin() + static_cast<std::ptrdiff_t>(slot(node_map_, selected_bit)),
                    std::move(child));
                added = true;
                return std::make_shared<bitmap_indexed_node>(
                    data_map_ & ~selected_bit,
                    node_map_ | selected_bit,
                    std::move(data),
                    std::move(children));
            }

            if ((node_map_ & selected_bit) == 0) {
                auto data = data_;
                data.insert(
                    data.begin() + static_cast<std::ptrdiff_t>(slot(data_map_, selected_bit)),
                    payload{hash, value_type(key, value)});
                added = true;
                return std::make_shared<bitmap_indexed_node>(
                    data_map_ | selected_bit, node_map_, std::move(data), children_);
            }

            const auto selected_slot = slot(node_map_, selected_bit);
            const auto& old_child = children_[selected_slot];
            auto new_child = old_child->set(
                key,
                value,
                hash,
                shift + bits_per_level,
                equal,
                values_equal,
                overwrite,
                added);
            if (new_child.get() == old_child.get()) {
                return this->shared_from_this();
            }

            auto replaced = children_;
            replaced[selected_slot] = std::move(new_child);
            return std::make_shared<bitmap_indexed_node>(data_map_, node_map_, data_, std::move(replaced));
        }

        /// A node without that element; returns the receiver when absent.
        [[nodiscard]] node_ptr remove(
            const Key& key,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            bool& removed,
            std::optional<T>& value) const override {
            const auto selected_bit = bit(index(hash, shift));
            if ((data_map_ & selected_bit) != 0) {
                const auto selected_slot = slot(data_map_, selected_bit);
                const auto& existing = data_[selected_slot];
                if (existing.hash != hash || !std::invoke(equal, existing.entry.first, key)) {
                    removed = false;
                    return this->shared_from_this();
                }
                removed = true;
                value = existing.entry.second;
                auto data = data_;
                data.erase(data.begin() + static_cast<std::ptrdiff_t>(selected_slot));
                return rebuild(data_map_ & ~selected_bit, node_map_, std::move(data), children_);
            }
            if ((node_map_ & selected_bit) == 0) {
                removed = false;
                return this->shared_from_this();
            }

            const auto selected_slot = slot(node_map_, selected_bit);
            const auto& old_child = children_[selected_slot];
            auto new_child = old_child->remove(
                key,
                hash,
                shift + bits_per_level,
                equal,
                removed,
                value);
            if (!removed) {
                return this->shared_from_this();
            }

            if (!new_child) {
                std::vector<node_ptr> children;
                children.reserve(children_.size() - 1);
                children.insert(children.end(), children_.begin(), children_.begin() + static_cast<std::ptrdiff_t>(selected_slot));
                children.insert(children.end(), children_.begin() + static_cast<std::ptrdiff_t>(selected_slot) + 1, children_.end());
                return rebuild(data_map_, node_map_ & ~selected_bit, data_, std::move(children));
            }

            if (new_child->kind() == persistent_hamt_node_kind::leaf) {
                const auto leaf = std::static_pointer_cast<const leaf_node>(new_child);
                auto data = data_;
                data.insert(
                    data.begin() + static_cast<std::ptrdiff_t>(slot(data_map_, selected_bit)),
                    payload{leaf->hash_, leaf->entry_});
                auto children = children_;
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(selected_slot));
                return rebuild(
                    data_map_ | selected_bit,
                    node_map_ & ~selected_bit,
                    std::move(data),
                    std::move(children));
            }

            auto replaced = children_;
            replaced[selected_slot] = std::move(new_child);
            return rebuild(data_map_, node_map_, data_, std::move(replaced));
        }

        /// Rebuilds the structure from its current contents.
        static node_ptr rebuild(
            std::uint32_t data_map,
            std::uint32_t node_map,
            std::vector<payload> data,
            std::vector<node_ptr> children) {
            if (data.empty() && children.empty()) {
                return nullptr;
            }
            if (data.size() == 1 && children.empty()) {
                return make_leaf(data[0].hash, data[0].entry.first, data[0].entry.second);
            }
            if (data.empty() && children.size() == 1
                && children[0]->kind() != persistent_hamt_node_kind::bitmap_indexed) {
                return std::move(children[0]);
            }

            return std::make_shared<bitmap_indexed_node>(
                data_map, node_map, std::move(data), std::move(children));
        }

        std::uint32_t data_map_;
        std::uint32_t node_map_;
        std::vector<payload> data_;
        std::vector<node_ptr> children_;
        size_type count_;
    };

    struct factory_update_result {
        node_ptr root;
        bool added;
        T selected;
    };

    struct factory_value_selection {
        std::optional<T> replacement;
        T selected;
    };

    template <class Callable>
    static void validate_callable(const Callable& callable, const char* message) {
        using callable_type = std::remove_cvref_t<Callable>;
        if constexpr (std::is_pointer_v<callable_type>
            || std::is_member_pointer_v<callable_type>) {
            if (callable == nullptr) {
                throw std::invalid_argument(message);
            }
        } else if constexpr (requires(const callable_type& candidate) {
            candidate.operator bool();
        }) {
            if (!callable.operator bool()) {
                throw std::invalid_argument(message);
            }
        }
    }

    template <class UpdateFactory>
    static factory_value_selection select_factory_value(
        const Key& lookup_key,
        const T& stored,
        const ValueEqual& values_equal,
        UpdateFactory* update_factory) {
        if constexpr (std::same_as<
                          std::remove_cv_t<UpdateFactory>,
                          no_update_factory>) {
            (void)lookup_key;
            (void)values_equal;
            (void)update_factory;
            return {std::nullopt, stored};
        } else {
            assert(update_factory != nullptr);
            auto selected = T(std::invoke(*update_factory, lookup_key, stored));
            if (values_equal_by_policy(stored, selected, values_equal)) {
                return {std::nullopt, stored};
            }

            return {std::optional<T>(selected), std::move(selected)};
        }
    }

    template <class AddFactory, class UpdateFactory>
    [[nodiscard]] std::pair<persistent_hash_map, T> apply_factory_update(
        const Key& key,
        AddFactory& add_factory,
        UpdateFactory* update_factory) const {
        const auto hash = get_hash(key);
        if (!root_) {
            auto selected = T(std::invoke(add_factory, key));
            return {
                persistent_hash_map(
                    make_leaf(hash, key, selected),
                    1,
                    hash_,
                    key_equal_,
                    value_equal_,
                    policy_identity_),
                std::move(selected),
            };
        }

        auto result = factory_update_node(
            root_,
            key,
            hash,
            0,
            key_equal_,
            value_equal_,
            add_factory,
            update_factory);
        if (result.root.get() == root_.get()) {
            return {*this, std::move(result.selected)};
        }

        if (result.added && count_ == std::numeric_limits<size_type>::max()) {
            throw std::length_error("persistent_hash_map count overflow");
        }

        return {
            persistent_hash_map(
                std::move(result.root),
                count_ + (result.added ? 1u : 0u),
                hash_,
                key_equal_,
                value_equal_,
                policy_identity_),
            std::move(result.selected),
        };
    }

    template <class AddFactory, class UpdateFactory>
    static factory_update_result factory_update_node(
        const node_ptr& current,
        const Key& key,
        std::uint32_t hash,
        int shift,
        const KeyEqual& equal,
        const ValueEqual& values_equal,
        AddFactory& add_factory,
        UpdateFactory* update_factory) {
        if (current->kind() != persistent_hamt_node_kind::bitmap_indexed) {
            return factory_update_hash_node(
                std::static_pointer_cast<const hash_node>(current),
                key,
                hash,
                shift,
                equal,
                values_equal,
                add_factory,
                update_factory);
        }

        const auto branch = std::static_pointer_cast<const bitmap_indexed_node>(current);
        const auto selected_bit = bit(index(hash, shift));
        if ((branch->data_map_ & selected_bit) != 0) {
            const auto data_slot = slot(branch->data_map_, selected_bit);
            const auto& existing = branch->data_[data_slot];
            if (existing.hash == hash && std::invoke(equal, existing.entry.first, key)) {
                auto selection = select_factory_value(
                    key, existing.entry.second, values_equal, update_factory);
                if (!selection.replacement) {
                    return {current, false, std::move(selection.selected)};
                }

                auto data = branch->data_;
                data[data_slot] = payload{
                    existing.hash,
                    value_type(existing.entry.first, *selection.replacement)};
                return {
                    std::make_shared<bitmap_indexed_node>(
                        branch->data_map_,
                        branch->node_map_,
                        std::move(data),
                        branch->children_),
                    false,
                    std::move(selection.selected),
                };
            }

            auto selected = T(std::invoke(add_factory, key));
            auto child = merge_hash_nodes(
                make_leaf(existing.hash, existing.entry.first, existing.entry.second),
                make_leaf(hash, key, selected),
                shift + bits_per_level);
            auto data = branch->data_;
            data.erase(data.begin() + static_cast<std::ptrdiff_t>(data_slot));
            auto children = branch->children_;
            children.insert(
                children.begin()
                    + static_cast<std::ptrdiff_t>(slot(branch->node_map_, selected_bit)),
                std::move(child));
            return {
                std::make_shared<bitmap_indexed_node>(
                    branch->data_map_ & ~selected_bit,
                    branch->node_map_ | selected_bit,
                    std::move(data),
                    std::move(children)),
                true,
                std::move(selected),
            };
        }

        if ((branch->node_map_ & selected_bit) != 0) {
            const auto child_slot = slot(branch->node_map_, selected_bit);
            auto result = factory_update_node(
                branch->children_[child_slot],
                key,
                hash,
                shift + bits_per_level,
                equal,
                values_equal,
                add_factory,
                update_factory);
            if (result.root.get() == branch->children_[child_slot].get()) {
                result.root = current;
                return result;
            }

            auto children = branch->children_;
            children[child_slot] = std::move(result.root);
            result.root = std::make_shared<bitmap_indexed_node>(
                branch->data_map_, branch->node_map_, branch->data_, std::move(children));
            return result;
        }

        auto selected = T(std::invoke(add_factory, key));
        auto data = branch->data_;
        data.insert(
            data.begin()
                + static_cast<std::ptrdiff_t>(slot(branch->data_map_, selected_bit)),
            payload{hash, value_type(key, selected)});
        return {
            std::make_shared<bitmap_indexed_node>(
                branch->data_map_ | selected_bit,
                branch->node_map_,
                std::move(data),
                branch->children_),
            true,
            std::move(selected),
        };
    }

    template <class AddFactory, class UpdateFactory>
    static factory_update_result factory_update_hash_node(
        const hash_node_ptr& current,
        const Key& key,
        std::uint32_t hash,
        int shift,
        const KeyEqual& equal,
        const ValueEqual& values_equal,
        AddFactory& add_factory,
        UpdateFactory* update_factory) {
        if (current->hash_ != hash) {
            auto selected = T(std::invoke(add_factory, key));
            return {
                merge_hash_nodes(current, make_leaf(hash, key, selected), shift),
                true,
                std::move(selected),
            };
        }

        if (current->kind() == persistent_hamt_node_kind::leaf) {
            const auto leaf = std::static_pointer_cast<const leaf_node>(current);
            if (std::invoke(equal, leaf->entry_.first, key)) {
                auto selection = select_factory_value(
                    key, leaf->entry_.second, values_equal, update_factory);
                if (!selection.replacement) {
                    return {current, false, std::move(selection.selected)};
                }

                return {
                    make_leaf(hash, leaf->entry_.first, *selection.replacement),
                    false,
                    std::move(selection.selected),
                };
            }

            auto selected = T(std::invoke(add_factory, key));
            return {
                collision_node::create(leaf, make_leaf(hash, key, selected)),
                true,
                std::move(selected),
            };
        }

        const auto collision = std::static_pointer_cast<const collision_node>(current);
        for (auto index_value = std::size_t{0};
             index_value < collision->entries_.size();
             ++index_value) {
            const auto& existing = collision->entries_[index_value];
            if (!std::invoke(equal, existing.first, key)) {
                continue;
            }

            auto selection = select_factory_value(
                key, existing.second, values_equal, update_factory);
            if (!selection.replacement) {
                return {current, false, std::move(selection.selected)};
            }

            auto entries = collision->entries_;
            entries[index_value] = value_type(existing.first, *selection.replacement);
            return {
                std::make_shared<collision_node>(hash, std::move(entries)),
                false,
                std::move(selection.selected),
            };
        }

        auto selected = T(std::invoke(add_factory, key));
        auto entries = collision->entries_;
        entries.emplace_back(key, selected);
        return {
            std::make_shared<collision_node>(hash, std::move(entries)),
            true,
            std::move(selected),
        };
    }

    struct entry_run_view {
        std::uint32_t hash;
        const value_type* single;
        const std::vector<value_type>* entries;

        /// Number of elements in the collection.
        [[nodiscard]] size_type count() const noexcept {
            return single != nullptr ? 1 : entries->size();
        }

        /// The element at the given position.
        [[nodiscard]] const value_type& at(size_type index_value) const noexcept {
            assert(index_value < count());
            return single != nullptr ? *single : (*entries)[index_value];
        }
    };

    struct diff_operand {
        const node* node_value;
        const payload* inline_entry;
    };

    static bool nodes_equal(
        const node* left,
        const node* right,
        const KeyEqual& equal,
        const ValueEqual& values_equal) {
        if (left == right) {
            return true;
        }
        if (left == nullptr || right == nullptr || left->kind() != right->kind()) {
            return false;
        }
        if (left->kind() == persistent_hamt_node_kind::leaf) {
            const auto* l = static_cast<const leaf_node*>(left);
            const auto* r = static_cast<const leaf_node*>(right);
            return l->hash_ == r->hash_
                && std::invoke(equal, l->entry_.first, r->entry_.first)
                && values_equal_by_policy(l->entry_.second, r->entry_.second, values_equal);
        }
        if (left->kind() == persistent_hamt_node_kind::collision) {
            const auto* l = static_cast<const collision_node*>(left);
            const auto* r = static_cast<const collision_node*>(right);
            return l->hash_ == r->hash_
                && l->entries_.size() == r->entries_.size()
                && std::ranges::all_of(l->entries_, [&](const value_type& left_entry) {
                    return std::ranges::any_of(r->entries_, [&](const value_type& right_entry) {
                        return std::invoke(equal, left_entry.first, right_entry.first)
                            && values_equal_by_policy(
                                left_entry.second, right_entry.second, values_equal);
                    });
                });
        }

        const auto* l = static_cast<const bitmap_indexed_node*>(left);
        const auto* r = static_cast<const bitmap_indexed_node*>(right);
        if (l->data_map_ != r->data_map_
            || l->node_map_ != r->node_map_
            || l->data_.size() != r->data_.size()
            || l->children_.size() != r->children_.size()) {
            return false;
        }
        for (auto index_value = size_type{0}; index_value != l->data_.size(); ++index_value) {
            const auto& left_entry = l->data_[index_value];
            const auto& right_entry = r->data_[index_value];
            if (left_entry.hash != right_entry.hash
                || !std::invoke(equal, left_entry.entry.first, right_entry.entry.first)
                || !values_equal_by_policy(
                    left_entry.entry.second, right_entry.entry.second, values_equal)) {
                return false;
            }
        }
        for (auto index_value = size_type{0}; index_value != l->children_.size(); ++index_value) {
            if (!nodes_equal(
                    l->children_[index_value].get(),
                    r->children_[index_value].get(),
                    equal,
                    values_equal)) {
                return false;
            }
        }
        return true;
    }

    static bool diff_operand_is_empty(diff_operand operand) noexcept {
        return operand.node_value == nullptr && operand.inline_entry == nullptr;
    }

    static bool diff_operand_is_run(diff_operand operand) noexcept {
        return operand.inline_entry != nullptr
            || (operand.node_value != nullptr
                && operand.node_value->kind() != persistent_hamt_node_kind::bitmap_indexed);
    }

    static entry_run_view entry_run_from_operand(diff_operand operand) noexcept {
        if (operand.inline_entry != nullptr) {
            return entry_run_view{
                operand.inline_entry->hash,
                std::addressof(operand.inline_entry->entry),
                nullptr};
        }
        assert(operand.node_value != nullptr);
        if (operand.node_value->kind() == persistent_hamt_node_kind::leaf) {
            const auto* leaf = static_cast<const leaf_node*>(operand.node_value);
            return entry_run_view{leaf->hash_, std::addressof(leaf->entry_), nullptr};
        }
        assert(operand.node_value->kind() == persistent_hamt_node_kind::collision);
        const auto* collision = static_cast<const collision_node*>(operand.node_value);
        return entry_run_view{collision->hash_, nullptr, std::addressof(collision->entries_)};
    }

    static diff_operand diff_operand_logical_slot(
        diff_operand operand,
        int slot_index,
        int shift) noexcept {
        if (diff_operand_is_empty(operand)) {
            return operand;
        }
        if (diff_operand_is_run(operand)) {
            assert(shift < 32);
            return index(entry_run_from_operand(operand).hash, shift) == slot_index
                ? operand
                : diff_operand{nullptr, nullptr};
        }

        const auto* branch = static_cast<const bitmap_indexed_node*>(operand.node_value);
        const auto selected_bit = bit(slot_index);
        if ((branch->data_map_ & selected_bit) != 0) {
            return diff_operand{
                nullptr,
                std::addressof(branch->data_[slot(branch->data_map_, selected_bit)])};
        }
        if ((branch->node_map_ & selected_bit) != 0) {
            return diff_operand{
                branch->children_[slot(branch->node_map_, selected_bit)].get(),
                nullptr};
        }
        return diff_operand{nullptr, nullptr};
    }

    static void append_run_differences(
        entry_run_view run,
        bool added,
        std::vector<map_difference<Key, T>>& result) {
        for (auto index_value = size_type{0}; index_value != run.count(); ++index_value) {
            const auto& entry = run.at(index_value);
            result.push_back(added
                ? map_difference<Key, T>{
                    map_difference_kind::added, entry.first, std::nullopt, entry.second}
                : map_difference<Key, T>{
                    map_difference_kind::removed, entry.first, entry.second, std::nullopt});
        }
    }

    static void append_diff_operand(
        diff_operand operand,
        bool added,
        std::vector<map_difference<Key, T>>& result) {
        if (diff_operand_is_empty(operand)) {
            return;
        }
        if (diff_operand_is_run(operand)) {
            append_run_differences(entry_run_from_operand(operand), added, result);
            return;
        }
        const auto* branch = static_cast<const bitmap_indexed_node*>(operand.node_value);
        for (auto slot_index = 0; slot_index <= branch_mask; ++slot_index) {
            const auto selected_bit = bit(slot_index);
            if ((branch->data_map_ & selected_bit) != 0) {
                append_diff_operand(
                    diff_operand{
                        nullptr,
                        std::addressof(branch->data_[slot(branch->data_map_, selected_bit)])},
                    added,
                    result);
            } else if ((branch->node_map_ & selected_bit) != 0) {
                append_diff_operand(
                    diff_operand{
                        branch->children_[slot(branch->node_map_, selected_bit)].get(),
                        nullptr},
                    added,
                    result);
            }
        }
    }

    static size_type find_entry(
        entry_run_view run,
        const Key& key,
        const KeyEqual& equal) {
        for (auto index_value = size_type{0}; index_value != run.count(); ++index_value) {
            if (std::invoke(equal, run.at(index_value).first, key)) {
                return index_value;
            }
        }
        return std::numeric_limits<size_type>::max();
    }

    static void diff_entry_runs(
        entry_run_view left,
        entry_run_view right,
        const KeyEqual& equal,
        const ValueEqual& values_equal,
        std::vector<map_difference<Key, T>>& result) {
        if (left.hash != right.hash) {
            append_run_differences(left, false, result);
            append_run_differences(right, true, result);
            return;
        }
        for (auto left_index = size_type{0}; left_index != left.count(); ++left_index) {
            const auto& before = left.at(left_index);
            const auto right_index = find_entry(right, before.first, equal);
            if (right_index == std::numeric_limits<size_type>::max()) {
                result.push_back({
                    map_difference_kind::removed,
                    before.first,
                    before.second,
                    std::nullopt});
                continue;
            }
            const auto& after = right.at(right_index);
            if (!values_equal_by_policy(before.second, after.second, values_equal)) {
                result.push_back({
                    map_difference_kind::changed,
                    before.first,
                    before.second,
                    after.second});
            }
        }
        for (auto right_index = size_type{0}; right_index != right.count(); ++right_index) {
            const auto& after = right.at(right_index);
            if (find_entry(left, after.first, equal) == std::numeric_limits<size_type>::max()) {
                result.push_back({
                    map_difference_kind::added,
                    after.first,
                    std::nullopt,
                    after.second});
            }
        }
    }

    static void diff_operands(
        diff_operand left,
        diff_operand right,
        int shift,
        const KeyEqual& equal,
        const ValueEqual& values_equal,
        std::vector<map_difference<Key, T>>& result) {
        if (left.node_value != nullptr
            && left.node_value == right.node_value
            && left.inline_entry == nullptr
            && right.inline_entry == nullptr) {
            return;
        }
        if (left.inline_entry != nullptr
            && left.inline_entry == right.inline_entry
            && left.node_value == nullptr
            && right.node_value == nullptr) {
            return;
        }
        if (diff_operand_is_empty(left)) {
            append_diff_operand(right, true, result);
            return;
        }
        if (diff_operand_is_empty(right)) {
            append_diff_operand(left, false, result);
            return;
        }
        if (diff_operand_is_run(left) && diff_operand_is_run(right)) {
            diff_entry_runs(
                entry_run_from_operand(left),
                entry_run_from_operand(right),
                equal,
                values_equal,
                result);
            return;
        }

        assert(shift < 32);
        for (auto slot_index = 0; slot_index <= branch_mask; ++slot_index) {
            diff_operands(
                diff_operand_logical_slot(left, slot_index, shift),
                diff_operand_logical_slot(right, slot_index, shift),
                shift + bits_per_level,
                equal,
                values_equal,
                result);
        }
    }

    static bool debug_hash_has_prefix(
        std::uint32_t hash, std::uint32_t prefix, std::uint32_t prefix_mask) noexcept {
        return (hash & prefix_mask) == prefix;
    }

    static std::uint32_t debug_next_prefix_mask(int shift) noexcept {
        return shift >= 27 ? ~std::uint32_t{0} : bit(shift + bits_per_level) - 1u;
    }

    static bool debug_validate_node(
        const node* candidate,
        int shift,
        std::uint32_t prefix,
        std::uint32_t prefix_mask,
        size_type& entries) noexcept {
        if (candidate == nullptr) {
            entries = 0;
            return true;
        }
        switch (candidate->kind()) {
        case persistent_hamt_node_kind::leaf: {
            const auto* leaf = static_cast<const leaf_node*>(candidate);
            entries = 1;
            return debug_hash_has_prefix(leaf->hash_, prefix, prefix_mask);
        }
        case persistent_hamt_node_kind::collision: {
            const auto* collision = static_cast<const collision_node*>(candidate);
            entries = collision->entries_.size();
            return entries >= 2
                && debug_hash_has_prefix(collision->hash_, prefix, prefix_mask);
        }
        case persistent_hamt_node_kind::bitmap_indexed: {
            const auto* branch = static_cast<const bitmap_indexed_node*>(candidate);
            if (shift > 30
                || (branch->data_map_ & branch->node_map_) != 0
                || static_cast<size_type>(std::popcount(branch->data_map_)) != branch->data_.size()
                || static_cast<size_type>(std::popcount(branch->node_map_)) != branch->children_.size()
                || branch->data_.size() + branch->children_.size() == 0
                || (branch->data_.size() + branch->children_.size() < 2
                    && !(branch->data_.empty() && branch->children_.size() == 1
                        && branch->children_[0]->kind() == persistent_hamt_node_kind::bitmap_indexed))) {
                return false;
            }
            const auto next_mask = debug_next_prefix_mask(shift);
            entries = branch->data_.size();
            auto data_index = size_type{0};
            auto child_index = size_type{0};
            for (auto slot = 0; slot <= branch_mask; ++slot) {
                const auto slot_bit = bit(slot);
                if (shift == 30 && slot > 3
                    && ((branch->data_map_ | branch->node_map_) & slot_bit) != 0) {
                    return false;
                }
                const auto slot_prefix = prefix | (static_cast<std::uint32_t>(slot) << shift);
                if ((branch->data_map_ & slot_bit) != 0) {
                    if (data_index >= branch->data_.size()
                        || !debug_hash_has_prefix(
                            branch->data_[data_index].hash, slot_prefix, next_mask)) {
                        return false;
                    }
                    ++data_index;
                }
                if ((branch->node_map_ & slot_bit) != 0) {
                    auto child_entries = size_type{0};
                    if (child_index >= branch->children_.size()
                        || branch->children_[child_index]->kind() == persistent_hamt_node_kind::leaf
                        || !debug_validate_node(
                            branch->children_[child_index].get(), shift + bits_per_level,
                            slot_prefix, next_mask, child_entries)) {
                        return false;
                    }
                    entries += child_entries;
                    ++child_index;
                }
            }
            return data_index == branch->data_.size()
                && child_index == branch->children_.size()
                && entries == branch->count_;
        }
        case persistent_hamt_node_kind::empty:
            break;
        }
        return false;
    }

    static bool debug_nodes_topology_equal(
        const node* left, const node* right, const KeyEqual& equal) {
        if (left == nullptr || right == nullptr) {
            return left == right;
        }
        if (left->kind() != right->kind()) {
            return false;
        }
        switch (left->kind()) {
        case persistent_hamt_node_kind::leaf:
            return static_cast<const leaf_node*>(left)->hash_ == static_cast<const leaf_node*>(right)->hash_;
        case persistent_hamt_node_kind::collision: {
            const auto* l = static_cast<const collision_node*>(left);
            const auto* r = static_cast<const collision_node*>(right);
            return l->hash_ == r->hash_
                && l->entries_.size() == r->entries_.size()
                && std::ranges::all_of(l->entries_, [&](const value_type& left_entry) {
                    return std::ranges::any_of(r->entries_, [&](const value_type& right_entry) {
                        return std::invoke(equal, left_entry.first, right_entry.first);
                    });
                });
        }
        case persistent_hamt_node_kind::bitmap_indexed: {
            const auto* l = static_cast<const bitmap_indexed_node*>(left);
            const auto* r = static_cast<const bitmap_indexed_node*>(right);
            return l->data_map_ == r->data_map_
                && l->node_map_ == r->node_map_
                && l->data_.size() == r->data_.size()
                && std::equal(l->data_.begin(), l->data_.end(), r->data_.begin(),
                    [](const payload& a, const payload& b) { return a.hash == b.hash; })
                && l->children_.size() == r->children_.size()
                && std::equal(l->children_.begin(), l->children_.end(), r->children_.begin(),
                    [&](const node_ptr& a, const node_ptr& b) {
                        return debug_nodes_topology_equal(a.get(), b.get(), equal);
                    });
        }
        case persistent_hamt_node_kind::empty:
            break;
        }
        return false;
    }

    static std::optional<T> select_bulk_replacement(
        const T& stored,
        const T& add_value,
        const ValueEqual& values_equal,
        const bulk_update_function* update,
        std::optional<T>* selected) {
        if (update == nullptr) {
            if (values_equal_by_policy(stored, add_value, values_equal)) {
                if (selected != nullptr) {
                    selected->emplace(stored);
                }
                return std::nullopt;
            }
            if (selected != nullptr) {
                selected->emplace(add_value);
            }
            return T(add_value);
        }

        auto candidate = std::invoke(*update, stored, add_value);
        if (values_equal_by_policy(stored, candidate, values_equal)) {
            if (selected != nullptr) {
                selected->emplace(stored);
            }
            return std::nullopt;
        }

        if (selected != nullptr) {
            selected->emplace(candidate);
        }
        return candidate;
    }

    static void select_bulk_add_value(
        const T& add_value,
        std::optional<T>* selected) {
        if (selected != nullptr) {
            selected->emplace(add_value);
        }
    }

    // Unpublished bulk-builder nodes. They are uniquely owned by exactly one
    // builder, are mutated in place, and never escape: `freeze` copies a
    // subtree into detached persistent nodes.
    struct mutable_node {
        /// An empty node.
        virtual ~mutable_node() = default;

        // A null result means this node was updated in place; a non-null
        // result is a replacement subtree that the caller publishes only
        // after lookup/factory selection has succeeded. Callback and comparer
        // failures therefore retain the builder's prior owning pointer.
        [[nodiscard]] virtual mutable_node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            const bulk_update_function* update,
            std::optional<T>* selected,
            bool& added) = 0;

        /// Closes the session and produces the node holding its accumulated edits.
        [[nodiscard]] virtual node_ptr freeze() const = 0;
    };

    struct mutable_hash_node : mutable_node {
        /// Constructs the mutable hash node from the given parts.
        explicit mutable_hash_node(std::uint32_t hash)
            : hash_(hash) {
        }

        std::uint32_t hash_;
    };

    struct mutable_leaf_node final : mutable_hash_node {
        /// Constructs the mutable leaf node from the given parts.
        mutable_leaf_node(std::uint32_t hash, Key key, T value)
            : mutable_hash_node(hash),
              entry_(std::move(key), std::move(value)) {
        }

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] mutable_node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            const bulk_update_function* update,
            std::optional<T>* selected,
            bool& added) override {
            if (this->hash_ == hash && std::invoke(equal, entry_.first, key)) {
                added = false;
                if (auto replacement = select_bulk_replacement(
                        entry_.second,
                        value,
                        values_equal,
                        update,
                        selected)) {
                    entry_.second = std::move(*replacement);
                }

                return nullptr;
            }

            select_bulk_add_value(value, selected);
            added = true;
            return merge_mutable_hash_nodes(
                std::make_unique<mutable_leaf_node>(
                    this->hash_, entry_.first, entry_.second),
                std::make_unique<mutable_leaf_node>(hash, key, value),
                shift);
        }

        /// Closes the session and produces the node holding its accumulated edits.
        [[nodiscard]] node_ptr freeze() const override {
            return make_leaf(this->hash_, entry_.first, entry_.second);
        }

        value_type entry_;
    };

    struct mutable_collision_node final : mutable_hash_node {
        /// Constructs the mutable collision node from the given parts.
        mutable_collision_node(std::uint32_t hash, std::vector<value_type> entries)
            : mutable_hash_node(hash),
              entries_(std::move(entries)) {
        }

        // Same precondition as collision_node::create: an equal-hash merge
        // only ever combines two leaves whose keys differ under the map's
        // comparer, so the entries can be adopted without a duplicate scan.
        [[nodiscard]] static std::unique_ptr<mutable_collision_node> create(
            mutable_hash_node_ptr left,
            mutable_leaf_node_ptr right) {
            assert(dynamic_cast<mutable_leaf_node*>(left.get()) != nullptr
                && "Equal-hash mutable merges must combine two leaves.");
            auto* const leaf = static_cast<mutable_leaf_node*>(left.get());
            std::vector<value_type> entries;
            entries.reserve(2);
            entries.push_back(std::move(leaf->entry_));
            entries.push_back(std::move(right->entry_));
            return std::make_unique<mutable_collision_node>(leaf->hash_, std::move(entries));
        }

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] mutable_node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            const bulk_update_function* update,
            std::optional<T>* selected,
            bool& added) override {
            if (this->hash_ != hash) {
                select_bulk_add_value(value, selected);
                added = true;
                return merge_mutable_hash_nodes(
                    std::make_unique<mutable_collision_node>(this->hash_, entries_),
                    std::make_unique<mutable_leaf_node>(hash, key, value),
                    shift);
            }

            for (auto& collision_entry : entries_) {
                if (!std::invoke(equal, collision_entry.first, key)) {
                    continue;
                }

                added = false;
                if (auto replacement = select_bulk_replacement(
                        collision_entry.second,
                        value,
                        values_equal,
                        update,
                        selected)) {
                    collision_entry.second = std::move(*replacement);
                }

                return nullptr;
            }

            select_bulk_add_value(value, selected);
            entries_.emplace_back(key, value);
            added = true;
            return nullptr;
        }

        /// Closes the session and produces the node holding its accumulated edits.
        [[nodiscard]] node_ptr freeze() const override {
            return std::make_shared<collision_node>(this->hash_, entries_);
        }

        std::vector<value_type> entries_;
    };

    struct mutable_bitmap_indexed_node final : mutable_node {
        /// An empty node.
        mutable_bitmap_indexed_node(
            std::uint32_t data_map,
            std::uint32_t node_map,
            std::vector<payload> data,
            std::vector<mutable_node_ptr> children)
            : data_map_(data_map),
              node_map_(node_map),
              data_(std::move(data)),
              children_(std::move(children)) {
        }

        /// A node with the key bound to the value, adding or replacing as needed.
        [[nodiscard]] mutable_node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            const bulk_update_function* update,
            std::optional<T>* selected,
            bool& added) override {
            const auto selected_bit = bit(index(hash, shift));
            if ((data_map_ & selected_bit) != 0) {
                const auto data_slot = slot(data_map_, selected_bit);
                auto& existing = data_[data_slot];
                if (existing.hash == hash && std::invoke(equal, existing.entry.first, key)) {
                    added = false;
                    if (auto replacement = select_bulk_replacement(
                            existing.entry.second,
                            value,
                            values_equal,
                            update,
                            selected)) {
                        existing.entry.second = std::move(*replacement);
                    }
                    return nullptr;
                }
                select_bulk_add_value(value, selected);
                auto child = merge_mutable_hash_nodes(
                    std::make_unique<mutable_leaf_node>(
                        existing.hash, existing.entry.first, existing.entry.second),
                    std::make_unique<mutable_leaf_node>(hash, key, value),
                    shift + bits_per_level);
                data_.erase(data_.begin() + static_cast<std::ptrdiff_t>(data_slot));
                children_.insert(
                    children_.begin() + static_cast<std::ptrdiff_t>(slot(node_map_, selected_bit)),
                    std::move(child));
                data_map_ &= ~selected_bit;
                node_map_ |= selected_bit;
                added = true;
                return nullptr;
            }

            if ((node_map_ & selected_bit) == 0) {
                select_bulk_add_value(value, selected);
                data_.insert(
                    data_.begin() + static_cast<std::ptrdiff_t>(slot(data_map_, selected_bit)),
                    payload{hash, value_type(key, value)});
                data_map_ |= selected_bit;
                added = true;
                return nullptr;
            }

            const auto selected_slot = slot(node_map_, selected_bit);
            if (auto replacement = children_[selected_slot]->set(
                key,
                value,
                hash,
                shift + bits_per_level,
                equal,
                values_equal,
                update,
                selected,
                added)) {
                children_[selected_slot] = std::move(replacement);
            }
            return nullptr;
        }

        /// Closes the session and produces the node holding its accumulated edits.
        [[nodiscard]] node_ptr freeze() const override {
            std::vector<node_ptr> children;
            children.reserve(children_.size());
            for (const auto& child : children_) {
                children.push_back(child->freeze());
            }

            return std::make_shared<bitmap_indexed_node>(
                data_map_, node_map_, data_, std::move(children));
        }

        std::uint32_t data_map_;
        std::uint32_t node_map_;
        std::vector<payload> data_;
        std::vector<mutable_node_ptr> children_;
    };

    static mutable_node_ptr merge_mutable_hash_nodes(
        mutable_hash_node_ptr left,
        mutable_leaf_node_ptr right,
        int shift) {
        if (left->hash_ == right->hash_) {
            return mutable_collision_node::create(std::move(left), std::move(right));
        }

        if (shift >= 32) {
            throw std::logic_error("Different 32-bit hashes cannot share every HAMT level.");
        }

        const auto left_index = index(left->hash_, shift);
        const auto right_index = index(right->hash_, shift);
        const auto left_bit = bit(left_index);
        const auto right_bit = bit(right_index);

        if (left_index == right_index) {
            std::vector<mutable_node_ptr> children;
            children.push_back(merge_mutable_hash_nodes(std::move(left), std::move(right), shift + bits_per_level));
            return std::make_unique<mutable_bitmap_indexed_node>(
                0, left_bit, std::vector<payload>{}, std::move(children));
        }

        std::vector<payload> data;
        std::vector<mutable_node_ptr> children;
        if (dynamic_cast<mutable_leaf_node*>(left.get()) != nullptr) {
            const auto* leaf = static_cast<mutable_leaf_node*>(left.get());
            if (left_index < right_index) {
                data.push_back(payload{leaf->hash_, leaf->entry_});
                data.push_back(payload{right->hash_, right->entry_});
            } else {
                data.push_back(payload{right->hash_, right->entry_});
                data.push_back(payload{leaf->hash_, leaf->entry_});
            }
            return std::make_unique<mutable_bitmap_indexed_node>(
                left_bit | right_bit, 0, std::move(data), std::move(children));
        }

        data.push_back(payload{right->hash_, right->entry_});
        children.push_back(std::move(left));
        return std::make_unique<mutable_bitmap_indexed_node>(
            right_bit, left_bit, std::move(data), std::move(children));
    }

    node_ptr root_;
    size_type count_ = 0;
    DURABLE7_HAMT_NO_UNIQUE_ADDRESS Hash hash_{};
    DURABLE7_HAMT_NO_UNIQUE_ADDRESS KeyEqual key_equal_{};
    DURABLE7_HAMT_NO_UNIQUE_ADDRESS ValueEqual value_equal_{};
    std::shared_ptr<const policy_identity> policy_identity_ =
        std::make_shared<const policy_identity>();
};

// A deliberately thin, one-way editing session over a persistent map value.
// Point edits call the ordinary persistent operations and therefore path-copy
// changed trie regions; this type does not expose or claim an owner-token
// mutable-node fast path. Adoption and publication move/copy only the map's
// root, count, policy objects, and policy-identity token and never walk the
// trie. Policy-object copy/move costs remain caller-defined.
template <class Key, class T, class Hash, class KeyEqual, class ValueEqual>
class persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>::transient final {
private:
    using map_type = persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>;

    enum class lifecycle_state {
        active,
        consumed,
        moved_from,
        move_failed,
        destroyed,
    };

    struct iterator_control final {
        lifecycle_state state = lifecycle_state::active;
        typename map_type::size_type version = 0;
    };

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = typename map_type::value_type;
    using size_type = typename map_type::size_type;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using value_equal = ValueEqual;

    /// Opens a mutable session seeded from this map. The map itself is unaffected; the session's
    /// edits are visible only through it.
    transient(const transient&) = delete;
    transient& operator=(const transient&) = delete;

    /// Opens a mutable session seeded from this map. The map itself is unaffected; the session's
    /// edits are visible only through it.
    transient(transient&& other) noexcept(std::is_nothrow_move_constructible_v<map_type>) try
        : map_(std::move(other.map_)),
          control_(std::move(other.control_)),
          state_(other.state_) {
        other.state_ = lifecycle_state::moved_from;
    } catch (...) {
        other.mark_move_failed();
        rethrow_move_failure<std::is_nothrow_move_constructible_v<map_type>>();
    }

    transient& operator=(transient&& other) noexcept(
        std::is_nothrow_move_assignable_v<map_type>) {
        if (this != &other) {
            // Invalidate the overwritten session before a potentially throwing
            // policy move can partially replace its persistent map. If the map
            // assignment throws, neither partially moved session remains
            // observable and every iterator fails deterministically.
            mark_move_failed();
            try {
                map_ = std::move(other.map_);
            } catch (...) {
                other.mark_move_failed();
                rethrow_move_failure<std::is_nothrow_move_assignable_v<map_type>>();
            }
            control_ = std::move(other.control_);
            state_ = other.state_;
            other.state_ = lifecycle_state::moved_from;
        }

        return *this;
    }

    /// Opens a mutable session seeded from this map. The map itself is unaffected; the session's
    /// edits are visible only through it.
    ~transient() {
        mark_destroyed();
    }

    /// Number of entries in the map.
    [[nodiscard]] size_type count() const {
        return active_map().count();
    }

    /// Whether the map holds no entries.
    [[nodiscard]] bool is_empty() const {
        return active_map().is_empty();
    }

    /// The retained hashing policy. Keys the policy treats as equivalent must hash identically.
    [[nodiscard]] const Hash& hash_function() const {
        return active_map().hash_function();
    }

    /// The retained key equivalence policy.
    [[nodiscard]] const KeyEqual& key_eq() const {
        return active_map().key_eq();
    }

    /// The retained value equivalence policy.
    [[nodiscard]] const ValueEqual& value_eq() const {
        return active_map().value_eq();
    }

    /// Whether the key is present.
    [[nodiscard]] bool contains_key(const Key& key) const {
        return active_map().contains_key(key);
    }

    // The returned pointers remain valid only until the session is changed,
    // consumed, moved, or destroyed, unless another retained map/iterator
    // independently keeps the containing immutable node alive.
    [[nodiscard]] const T* try_get(const Key& key) const {
        return active_map().try_get(key);
    }

    /// Reads the stored key representative, or nothing when absent.
    [[nodiscard]] const Key* try_get_key(const Key& equal_key) const {
        return active_map().try_get_key(equal_key);
    }

    /// The value stored for the key. Raises when the key is absent.
    [[nodiscard]] const T& at(const Key& key) const {
        return active_map().at(key);
    }

    /// A map with the key bound to the value, adding or replacing as needed.
    void set_item(const Key& key, const T& value) {
        commit(active_map().set_item(key, value));
    }

    /// A map containing the given entry; returns the receiver when already present.
    void add(const Key& key, const T& value) {
        commit(active_map().add(key, value));
    }

    /// Adds the entry unless an equivalent one is present, reporting which happened.
    [[nodiscard]] bool try_add(const Key& key, const T& value) {
        auto [candidate, added] = active_map().try_add(key, value);
        if (added) {
            commit(std::move(candidate));
        }
        return added;
    }

    /// A map without that entry; returns the receiver when absent.
    [[nodiscard]] bool remove(const Key& key) {
        auto& current = active_map();
        auto candidate = current.remove(key);
        if (candidate.shares_root_with(current)) {
            return false;
        }

        commit(std::move(candidate));
        return true;
    }

    /// An empty map retaining the same policies; returns the receiver when already empty.
    void clear() {
        commit(active_map().clear());
    }

    /// A forward const iterator over one map version's entries. It keeps that version alive, so it
    /// stays valid across later edits.
    class const_iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = typename map_type::value_type;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type&;
        using pointer = const value_type*;

        /// An unpositioned cursor, holding no version.
        const_iterator() = default;

        reference operator*() const {
            validate();
            return *inner_;
        }

        pointer operator->() const {
            validate();
            return inner_.operator->();
        }

        const_iterator& operator++() {
            validate();
            ++inner_;
            return *this;
        }

        const_iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        friend bool operator==(const const_iterator& iterator, std::default_sentinel_t sentinel) {
            iterator.validate();
            return iterator.inner_ == sentinel;
        }

        friend bool operator==(std::default_sentinel_t sentinel, const const_iterator& iterator) {
            return iterator == sentinel;
        }

        friend bool operator!=(const const_iterator& iterator, std::default_sentinel_t sentinel) {
            return !(iterator == sentinel);
        }

        friend bool operator!=(std::default_sentinel_t sentinel, const const_iterator& iterator) {
            return !(iterator == sentinel);
        }

    private:
        friend class transient;

        const_iterator(
            std::shared_ptr<const iterator_control> control,
            size_type version,
            typename map_type::const_iterator inner)
            : control_(std::move(control)),
              version_(version),
              inner_(std::move(inner)) {
        }

        void validate() const {
            if (!control_) {
                return;
            }

            if (control_->state == lifecycle_state::consumed) {
                throw std::logic_error(
                    "The persistent_hash_map transient has already been consumed by persist().");
            }
            if (control_->state == lifecycle_state::destroyed) {
                throw std::logic_error(
                    "The persistent_hash_map transient that created this iterator no longer exists.");
            }
            if (control_->state == lifecycle_state::move_failed) {
                throw std::logic_error(
                    "The persistent_hash_map transient was invalidated by a failed move.");
            }
            if (control_->version != version_) {
                throw std::logic_error(
                    "The persistent_hash_map transient was changed after this iterator was created.");
            }
        }

        std::shared_ptr<const iterator_control> control_;
        size_type version_ = 0;
        typename map_type::const_iterator inner_;
    };

    /// An iterator over the entries, in the map's own order.
    [[nodiscard]] const_iterator begin() const {
        const auto& map = active_map();
        return const_iterator(control_, control_->version, map.begin());
    }

    /// The iterator one past the last element.
    [[nodiscard]] std::default_sentinel_t end() const {
        ensure_active();
        return {};
    }

    /// A const iterator over the entries.
    [[nodiscard]] const_iterator cbegin() const {
        return begin();
    }

    /// The const iterator one past the last element.
    [[nodiscard]] std::default_sentinel_t cend() const {
        return end();
    }

    /// Copies the entries out into a vector, in the map's own order.
    [[nodiscard]] std::vector<value_type> to_vector() const {
        ensure_active();
        std::vector<value_type> items;
        items.reserve(map_.count());
        for (const auto& item : *this) {
            items.push_back(item);
        }
        return items;
    }

    /// The keys, in the collection's own order.
    [[nodiscard]] std::vector<Key> keys() const {
        ensure_active();
        std::vector<Key> result;
        result.reserve(map_.count());
        for (const auto& [key, value] : *this) {
            (void)value;
            result.push_back(key);
        }
        return result;
    }

    /// The values.
    [[nodiscard]] std::vector<T> values() const {
        ensure_active();
        std::vector<T> result;
        result.reserve(map_.count());
        for (const auto& [key, value] : *this) {
            (void)key;
            result.push_back(value);
        }
        return result;
    }

    /// Checks that the structure is in its canonical shape, which must depend only on contents and
    /// not on edit history.
    [[nodiscard]] bool debug_validate_canonical() const {
        return active_map().debug_validate_canonical();
    }

    // Publication is deliberately rvalue-only. A successful call moves the
    // current persistent value out in constant trie work and consumes this
    // session; every later observation throws std::logic_error.
    [[nodiscard]] map_type persist() && {
        ensure_active();
        auto result = std::move(map_);
        state_ = lifecycle_state::consumed;
        control_->state = lifecycle_state::consumed;
        return result;
    }

    /// Closes the session and produces the map holding its accumulated edits.
    map_type persist() & = delete;

private:
    friend map_type;

    explicit transient(map_type map)
        : map_(std::move(map)),
          control_(std::make_shared<iterator_control>()) {
    }

    void ensure_active() const {
        if (state_ == lifecycle_state::consumed) {
            throw std::logic_error(
                "The persistent_hash_map transient has already been consumed by persist().");
        }
        if (state_ == lifecycle_state::moved_from) {
            throw std::logic_error(
                "The persistent_hash_map transient has been moved from.");
        }
        if (state_ == lifecycle_state::move_failed) {
            throw std::logic_error(
                "The persistent_hash_map transient was invalidated by a failed move.");
        }
        if (!control_ || control_->state != lifecycle_state::active) {
            throw std::logic_error(
                "The persistent_hash_map transient is not active.");
        }
    }

    [[nodiscard]] map_type& active_map() {
        ensure_active();
        return map_;
    }

    [[nodiscard]] const map_type& active_map() const {
        ensure_active();
        return map_;
    }

    void commit(map_type candidate) {
        ensure_active();
        if (candidate.shares_root_with(map_)) {
            return;
        }
        if (control_->version == (std::numeric_limits<size_type>::max)()) {
            throw std::overflow_error(
                "The persistent_hash_map transient version counter overflowed.");
        }

        // Candidate construction performs every potentially throwing hash,
        // equality, allocation, and policy-copy operation. The candidate was
        // derived exclusively from map_, so its policies and identity are the
        // same; commit only the immutable root/count pair and keep the exact
        // policy objects already owned by the session. Both assignments are
        // non-throwing, as is the generation increment checked above.
        map_.root_ = std::move(candidate.root_);
        map_.count_ = candidate.count_;
        ++control_->version;
    }

    void mark_destroyed() noexcept {
        if (state_ == lifecycle_state::active && control_) {
            control_->state = lifecycle_state::destroyed;
        }
    }

    void mark_move_failed() noexcept {
        state_ = lifecycle_state::move_failed;
        if (control_) {
            control_->state = lifecycle_state::move_failed;
        }
    }

    template <bool IsNothrow>
    [[noreturn]] static void rethrow_move_failure() noexcept(IsNothrow) {
        if constexpr (IsNothrow) {
            // The catch is unreachable when the map move is correctly marked
            // noexcept. Keep that instantiation free of an explicit rethrow so
            // strict MSVC builds do not diagnose a throwing noexcept function.
            std::terminate();
        } else {
            throw;
        }
    }

    map_type map_;
    std::shared_ptr<iterator_control> control_;
    lifecycle_state state_ = lifecycle_state::active;
};

template <class Key, class T, class Hash, class KeyEqual, class ValueEqual>
[[nodiscard]] auto persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>::create_transient(
    Hash hash,
    KeyEqual equal,
    ValueEqual values_equal) -> transient {
    return transient(create(std::move(hash), std::move(equal), std::move(values_equal)));
}

template <class Key, class T, class Hash, class KeyEqual, class ValueEqual>
[[nodiscard]] auto persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>::to_transient() const &
    -> transient {
    return transient(*this);
}

template <class Key, class T, class Hash, class KeyEqual, class ValueEqual>
[[nodiscard]] auto persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>::to_transient() &&
    -> transient {
    return transient(std::move(*this));
}

} // namespace durable7::hamt

#undef DURABLE7_HAMT_NO_UNIQUE_ADDRESS
