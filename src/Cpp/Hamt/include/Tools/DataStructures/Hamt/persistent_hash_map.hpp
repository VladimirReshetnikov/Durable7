#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
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
#define TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace tools::data_structures::hamt {

enum class persistent_hamt_node_kind {
    empty,
    leaf,
    collision,
    bitmap_indexed,
};

enum class map_difference_kind {
    added,
    removed,
    changed,
};

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

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<Key, T>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using value_equal = ValueEqual;

private:
    struct payload {
        std::uint32_t hash;
        value_type entry;
    };

public:

    persistent_hash_map() = default;

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
          value_equal_(std::move(other.value_equal_)) {
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
        }

        return *this;
    }

    ~persistent_hash_map() = default;

    static persistent_hash_map empty() {
        return {};
    }

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
        explicit bulk_builder(Hash hash = {}, KeyEqual equal = {}, ValueEqual values_equal = {})
            : hash_(std::move(hash)),
              key_equal_(std::move(equal)),
              value_equal_(std::move(values_equal)) {
        }

        [[nodiscard]] size_type count() const noexcept {
            return count_;
        }

        void set_item(const Key& key, const T& value) {
            const auto hash = static_cast<std::uint32_t>(std::invoke(hash_, key));
            if (!root_) {
                root_ = std::make_unique<mutable_leaf_node>(hash, key, value);
                count_ = 1;
                return;
            }

            bool added = false;
            auto* const root = root_.get();
            root_ = root->set(std::move(root_), key, value, hash, 0, key_equal_, value_equal_, added);
            if (added) {
                ++count_;
            }
        }

        [[nodiscard]] persistent_hash_map to_immutable() const {
            if (!root_) {
                return persistent_hash_map(nullptr, 0, hash_, key_equal_, value_equal_);
            }

            return persistent_hash_map(root_->freeze(), count_, hash_, key_equal_, value_equal_);
        }

    private:
        mutable_node_ptr root_;
        size_type count_ = 0;
        TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS Hash hash_{};
        TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS KeyEqual key_equal_{};
        TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS ValueEqual value_equal_{};
    };

    static bulk_builder create_bulk_builder(
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        return bulk_builder(std::move(hash), std::move(equal), std::move(values_equal));
    }

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

    [[nodiscard]] size_type count() const noexcept {
        return count_;
    }

    [[nodiscard]] bool is_empty() const noexcept {
        return count_ == 0;
    }

    [[nodiscard]] const Hash& hash_function() const noexcept {
        return hash_;
    }

    [[nodiscard]] const KeyEqual& key_eq() const noexcept {
        return key_equal_;
    }

    [[nodiscard]] const ValueEqual& value_eq() const noexcept {
        return value_equal_;
    }

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

    [[nodiscard]] const T& at(const Key& key) const {
        if (const auto* value = try_get(key)) {
            return *value;
        }

        throw std::out_of_range("The key was not present in the persistent_hash_map.");
    }

    [[nodiscard]] persistent_hash_map set_item(const Key& key, const T& value) const {
        const auto hash = get_hash(key);
        if (!root_) {
            return persistent_hash_map(
                make_leaf(hash, key, value),
                1,
                hash_,
                key_equal_,
                value_equal_);
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
            value_equal_);
    }

    [[nodiscard]] persistent_hash_map add(const Key& key, const T& value) const {
        auto [result, added] = try_add(key, value);
        if (!added) {
            throw std::invalid_argument("An equivalent key is already present in the persistent_hash_map.");
        }

        return result;
    }

    [[nodiscard]] std::pair<persistent_hash_map, bool> try_add(const Key& key, const T& value) const {
        const auto hash = get_hash(key);
        if (!root_) {
            return {
                persistent_hash_map(
                    make_leaf(hash, key, value),
                    1,
                    hash_,
                    key_equal_,
                    value_equal_),
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
                value_equal_),
            true,
        };
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
            from_root(std::move(new_root), count_ - 1, hash_, key_equal_, value_equal_),
            true,
            std::move(value),
        };
    }

    [[nodiscard]] persistent_hash_map clear() const {
        if (count_ == 0) {
            return *this;
        }

        return persistent_hash_map(nullptr, 0, hash_, key_equal_, value_equal_);
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

    [[nodiscard]] bool map_equals(const persistent_hash_map& other) const {
        if (root_.get() == other.root_.get()) {
            return true;
        }
        if (count_ != other.count_) {
            return false;
        }
        for (const auto& [key, value] : *this) {
            const auto* actual_key = static_cast<const Key*>(nullptr);
            const auto* other_value = static_cast<const T*>(nullptr);
            if (!other.try_get_entry(key, actual_key, other_value)
                || !std::invoke(value_equal_, value, *other_value)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::vector<map_difference<Key, T>> diff(const persistent_hash_map& other) const {
        if (root_.get() == other.root_.get()) {
            return {};
        }
        std::vector<map_difference<Key, T>> result;
        for (const auto& [key, value] : *this) {
            const auto* actual_key = static_cast<const Key*>(nullptr);
            const auto* other_value = static_cast<const T*>(nullptr);
            if (!other.try_get_entry(key, actual_key, other_value)) {
                result.push_back({map_difference_kind::removed, key, value, std::nullopt});
            } else if (!std::invoke(value_equal_, value, *other_value)) {
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
        return debug_validate_node(root_.get(), entries) && entries == count();
    }

    [[nodiscard]] bool debug_topology_equal(const persistent_hash_map& other) const noexcept {
        return debug_nodes_topology_equal(root_.get(), other.root_.get());
    }

private:
    persistent_hash_map(
        node_ptr root,
        size_type count,
        Hash hash,
        KeyEqual equal,
        ValueEqual values_equal)
        : root_(std::move(root)),
          count_(count),
          hash_(std::move(hash)),
          key_equal_(std::move(equal)),
          value_equal_(std::move(values_equal)) {
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

    static leaf_node_ptr make_leaf(std::uint32_t hash, const Key& key, const T& value) {
        return std::make_shared<leaf_node>(hash, key, value);
    }

    static persistent_hash_map from_root(
        node_ptr root,
        size_type count,
        Hash hash,
        KeyEqual equal,
        ValueEqual values_equal) {
        if (count == 0) {
            return persistent_hash_map(nullptr, 0, std::move(hash), std::move(equal), std::move(values_equal));
        }

        return persistent_hash_map(std::move(root), count, std::move(hash), std::move(equal), std::move(values_equal));
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
        virtual ~node() = default;

        [[nodiscard]] virtual persistent_hamt_node_kind kind() const noexcept = 0;

        [[nodiscard]] virtual node_ptr set(
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool overwrite,
            bool& added) const = 0;

        [[nodiscard]] virtual node_ptr remove(
            const Key& key,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            bool& removed,
            std::optional<T>& value) const = 0;
    };

    struct hash_node : node {
        explicit hash_node(std::uint32_t hash)
            : hash_(hash) {
        }

        [[nodiscard]] hash_node_ptr shared_hash_from_this() const {
            return std::static_pointer_cast<const hash_node>(this->shared_from_this());
        }

        std::uint32_t hash_;
    };

    struct leaf_node final : hash_node {
        leaf_node(std::uint32_t hash, Key key, T value)
            : hash_node(hash),
              entry_(std::move(key), std::move(value)) {
        }

        [[nodiscard]] persistent_hamt_node_kind kind() const noexcept override {
            return persistent_hamt_node_kind::leaf;
        }

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
                if (!overwrite || std::invoke(values_equal, entry_.second, value)) {
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
        collision_node(std::uint32_t hash, std::vector<value_type> entries)
            : hash_node(hash),
              entries_(std::move(entries)) {
        }

        [[nodiscard]] persistent_hamt_node_kind kind() const noexcept override {
            return persistent_hamt_node_kind::collision;
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
                if (!overwrite || std::invoke(values_equal, entries_[i].second, value)) {
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
        bitmap_indexed_node(
            std::uint32_t data_map,
            std::uint32_t node_map,
            std::vector<payload> data,
            std::vector<node_ptr> children)
            : data_map_(data_map),
              node_map_(node_map),
              data_(std::move(data)),
              children_(std::move(children)) {
        }

        [[nodiscard]] persistent_hamt_node_kind kind() const noexcept override {
            return persistent_hamt_node_kind::bitmap_indexed;
        }

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
                    if (!overwrite || std::invoke(values_equal, existing.entry.second, value)) {
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
    };

    static bool debug_validate_node(const node* candidate, size_type& entries) noexcept {
        if (candidate == nullptr) {
            entries = 0;
            return true;
        }
        switch (candidate->kind()) {
        case persistent_hamt_node_kind::leaf:
            entries = 1;
            return true;
        case persistent_hamt_node_kind::collision: {
            const auto* collision = static_cast<const collision_node*>(candidate);
            entries = collision->entries_.size();
            return entries >= 2;
        }
        case persistent_hamt_node_kind::bitmap_indexed: {
            const auto* branch = static_cast<const bitmap_indexed_node*>(candidate);
            if ((branch->data_map_ & branch->node_map_) != 0
                || std::popcount(branch->data_map_) != branch->data_.size()
                || std::popcount(branch->node_map_) != branch->children_.size()
                || branch->data_.size() + branch->children_.size() == 0
                || (branch->data_.size() + branch->children_.size() < 2
                    && !(branch->data_.empty() && branch->children_.size() == 1
                        && branch->children_[0]->kind() == persistent_hamt_node_kind::bitmap_indexed))) {
                return false;
            }
            entries = branch->data_.size();
            for (const auto& child : branch->children_) {
                auto child_entries = size_type{0};
                if (child->kind() == persistent_hamt_node_kind::leaf
                    || !debug_validate_node(child.get(), child_entries)) {
                    return false;
                }
                entries += child_entries;
            }
            return true;
        }
        case persistent_hamt_node_kind::empty:
            break;
        }
        return false;
    }

    static bool debug_nodes_topology_equal(const node* left, const node* right) noexcept {
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
            return l->hash_ == r->hash_ && l->entries_.size() == r->entries_.size();
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
                    [](const node_ptr& a, const node_ptr& b) {
                        return debug_nodes_topology_equal(a.get(), b.get());
                    });
        }
        case persistent_hamt_node_kind::empty:
            break;
        }
        return false;
    }

    // Unpublished bulk-builder nodes. They are uniquely owned by exactly one
    // builder, are mutated in place, and never escape: `freeze` copies a
    // subtree into detached persistent nodes.
    struct mutable_node {
        virtual ~mutable_node() = default;

        // `self` must own `this`. The call either mutates in place and returns
        // `self`, or consumes `self` into a replacement subtree.
        [[nodiscard]] virtual mutable_node_ptr set(
            mutable_node_ptr self,
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool& added) = 0;

        [[nodiscard]] virtual node_ptr freeze() const = 0;
    };

    struct mutable_hash_node : mutable_node {
        explicit mutable_hash_node(std::uint32_t hash)
            : hash_(hash) {
        }

        std::uint32_t hash_;
    };

    struct mutable_leaf_node final : mutable_hash_node {
        mutable_leaf_node(std::uint32_t hash, Key key, T value)
            : mutable_hash_node(hash),
              entry_(std::move(key), std::move(value)) {
        }

        [[nodiscard]] mutable_node_ptr set(
            mutable_node_ptr self,
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool& added) override {
            if (this->hash_ == hash && std::invoke(equal, entry_.first, key)) {
                added = false;
                if (!std::invoke(values_equal, entry_.second, value)) {
                    entry_.second = value;
                }

                return self;
            }

            added = true;
            return merge_mutable_hash_nodes(
                mutable_hash_node_ptr(static_cast<mutable_hash_node*>(self.release())),
                std::make_unique<mutable_leaf_node>(hash, key, value),
                shift);
        }

        [[nodiscard]] node_ptr freeze() const override {
            return make_leaf(this->hash_, entry_.first, entry_.second);
        }

        value_type entry_;
    };

    struct mutable_collision_node final : mutable_hash_node {
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

        [[nodiscard]] mutable_node_ptr set(
            mutable_node_ptr self,
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool& added) override {
            if (this->hash_ != hash) {
                added = true;
                return merge_mutable_hash_nodes(
                    mutable_hash_node_ptr(static_cast<mutable_hash_node*>(self.release())),
                    std::make_unique<mutable_leaf_node>(hash, key, value),
                    shift);
            }

            for (auto& collision_entry : entries_) {
                if (!std::invoke(equal, collision_entry.first, key)) {
                    continue;
                }

                added = false;
                if (!std::invoke(values_equal, collision_entry.second, value)) {
                    collision_entry.second = value;
                }

                return self;
            }

            entries_.emplace_back(key, value);
            added = true;
            return self;
        }

        [[nodiscard]] node_ptr freeze() const override {
            return std::make_shared<collision_node>(this->hash_, entries_);
        }

        std::vector<value_type> entries_;
    };

    struct mutable_bitmap_indexed_node final : mutable_node {
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

        [[nodiscard]] mutable_node_ptr set(
            mutable_node_ptr self,
            const Key& key,
            const T& value,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            const ValueEqual& values_equal,
            bool& added) override {
            const auto selected_bit = bit(index(hash, shift));
            if ((data_map_ & selected_bit) != 0) {
                const auto data_slot = slot(data_map_, selected_bit);
                auto& existing = data_[data_slot];
                if (existing.hash == hash && std::invoke(equal, existing.entry.first, key)) {
                    added = false;
                    if (!std::invoke(values_equal, existing.entry.second, value)) {
                        existing.entry.second = value;
                    }
                    return self;
                }
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
                return self;
            }

            if ((node_map_ & selected_bit) == 0) {
                data_.insert(
                    data_.begin() + static_cast<std::ptrdiff_t>(slot(data_map_, selected_bit)),
                    payload{hash, value_type(key, value)});
                data_map_ |= selected_bit;
                added = true;
                return self;
            }

            const auto selected_slot = slot(node_map_, selected_bit);
            auto* const child = children_[selected_slot].get();
            children_[selected_slot] = child->set(
                std::move(children_[selected_slot]),
                key,
                value,
                hash,
                shift + bits_per_level,
                equal,
                values_equal,
                added);
            return self;
        }

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
    TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS Hash hash_{};
    TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS KeyEqual key_equal_{};
    TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS ValueEqual value_equal_{};
};

} // namespace tools::data_structures::hamt

#undef TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS
