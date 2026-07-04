#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#if defined(__clang__) && defined(_MSC_VER)
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

    struct node;
    struct hash_node;
    struct leaf_node;
    struct collision_node;
    struct bitmap_indexed_node;

    using node_ptr = std::shared_ptr<const node>;
    using hash_node_ptr = std::shared_ptr<const hash_node>;
    using leaf_node_ptr = std::shared_ptr<const leaf_node>;

    struct entry {
        Key key;
        T value;

        entry(Key entry_key, T entry_value)
            : key(std::move(entry_key)),
              value(std::move(entry_value)) {
        }
    };

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<Key, T>;
    using size_type = std::size_t;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using value_equal = ValueEqual;

    persistent_hash_map() = default;

    static persistent_hash_map empty() {
        return {};
    }

    static persistent_hash_map create(
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        return persistent_hash_map(nullptr, 0, std::move(hash), std::move(equal), std::move(values_equal));
    }

    static persistent_hash_map create_range(
        std::initializer_list<value_type> items,
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        auto map = create(std::move(hash), std::move(equal), std::move(values_equal));
        for (const auto& item : items) {
            map = map.set_item(item.first, item.second);
        }

        return map;
    }

    template <class Range>
    static persistent_hash_map create_range(
        const Range& items,
        Hash hash = {},
        KeyEqual equal = {},
        ValueEqual values_equal = {}) {
        auto map = create(std::move(hash), std::move(equal), std::move(values_equal));
        for (const auto& item : items) {
            map = map.set_item(item.first, item.second);
        }

        return map;
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

    [[nodiscard]] const T* try_get(const Key& key) const {
        const Key* actual_key = nullptr;
        const T* value = nullptr;
        return try_get_entry(key, actual_key, value) ? value : nullptr;
    }

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
            const std::vector<node_ptr>* children = nullptr;
            std::size_t index = 0;
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
            return std::addressof(*current_);
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

        explicit const_iterator(const node* root)
            : next_(root),
              at_end_(false) {
            at_end_ = !move_next();
        }

        bool move_next() {
            if (move_next_collision_entry()) {
                return true;
            }

            const node* current_node = next_;
            next_ = nullptr;

            while (true) {
                if (current_node == nullptr) {
                    if (depth_ == 0) {
                        current_.reset();
                        return false;
                    }

                    auto& top = frames_[depth_ - 1];
                    if (top.index == top.children->size()) {
                        frames_[--depth_] = {};
                        continue;
                    }

                    current_node = (*top.children)[top.index++].get();
                }

                if (const auto* leaf = dynamic_cast<const leaf_node*>(current_node)) {
                    current_.emplace(leaf->key_, leaf->value_);
                    return true;
                }

                if (const auto* collision = dynamic_cast<const collision_node*>(current_node)) {
                    collision_entries_ = std::addressof(collision->entries_);
                    collision_index_ = 0;
                    return move_next_collision_entry();
                }

                const auto* branch = static_cast<const bitmap_indexed_node*>(current_node);
                frames_[depth_++] = frame{std::addressof(branch->children_), 0};
                current_node = nullptr;
            }
        }

        bool move_next_collision_entry() {
            if (collision_entries_ == nullptr) {
                return false;
            }

            if (collision_index_ < collision_entries_->size()) {
                const auto& collision_entry = (*collision_entries_)[collision_index_++];
                current_.emplace(collision_entry.key, collision_entry.value);
                return true;
            }

            collision_entries_ = nullptr;
            collision_index_ = 0;
            return false;
        }

        const node* next_ = nullptr;
        std::array<frame, max_depth> frames_{};
        std::size_t depth_ = 0;
        const std::vector<entry>* collision_entries_ = nullptr;
        std::size_t collision_index_ = 0;
        std::optional<value_type> current_;
        bool at_end_ = true;
    };

    [[nodiscard]] const_iterator begin() const {
        return const_iterator(root_.get());
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

    [[nodiscard]] const void* debug_root_identity() const noexcept {
        return root_.get();
    }

    [[nodiscard]] persistent_hamt_node_kind debug_root_kind() const noexcept {
        return kind_of(root_.get());
    }

    [[nodiscard]] std::vector<const void*> debug_root_child_identities() const {
        std::vector<const void*> children;
        const auto* branch = dynamic_cast<const bitmap_indexed_node*>(root_.get());
        if (branch == nullptr) {
            return children;
        }

        children.reserve(branch->children_.size());
        for (const auto& child : branch->children_) {
            children.push_back(child.get());
        }

        return children;
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
        return std::popcount(bitmap & (bit - 1u));
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
        if (candidate == nullptr) {
            return persistent_hamt_node_kind::empty;
        }

        if (dynamic_cast<const leaf_node*>(candidate) != nullptr) {
            return persistent_hamt_node_kind::leaf;
        }

        if (dynamic_cast<const collision_node*>(candidate) != nullptr) {
            return persistent_hamt_node_kind::collision;
        }

        return persistent_hamt_node_kind::bitmap_indexed;
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
        while (const auto* branch = dynamic_cast<const bitmap_indexed_node*>(current_node)) {
            const auto selected_bit = bit(index(hash, shift));
            if ((branch->bitmap_ & selected_bit) == 0) {
                return false;
            }

            current_node = branch->children_[slot(branch->bitmap_, selected_bit)].get();
            shift += bits_per_level;
        }

        if (const auto* leaf = dynamic_cast<const leaf_node*>(current_node)) {
            if (leaf->hash_ == hash && std::invoke(key_equal_, leaf->key_, key)) {
                actual_key = std::addressof(leaf->key_);
                value = std::addressof(leaf->value_);
                return true;
            }

            return false;
        }

        const auto* collision = static_cast<const collision_node*>(current_node);
        if (collision->hash_ != hash) {
            return false;
        }

        for (const auto& collision_entry : collision->entries_) {
            if (std::invoke(key_equal_, collision_entry.key, key)) {
                actual_key = std::addressof(collision_entry.key);
                value = std::addressof(collision_entry.value);
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
            return std::make_shared<bitmap_indexed_node>(left_bit, std::move(children));
        }

        std::vector<node_ptr> children;
        children.reserve(2);
        if (left_index < right_index) {
            children.push_back(std::move(left));
            children.push_back(std::move(right));
        } else {
            children.push_back(std::move(right));
            children.push_back(std::move(left));
        }

        return std::make_shared<bitmap_indexed_node>(left_bit | right_bit, std::move(children));
    }

    struct node : std::enable_shared_from_this<node> {
        virtual ~node() = default;

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
              key_(std::move(key)),
              value_(std::move(value)) {
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
            if (this->hash_ == hash && std::invoke(equal, key_, key)) {
                added = false;
                if (!overwrite || std::invoke(values_equal, value_, value)) {
                    return this->shared_from_this();
                }

                return make_leaf(this->hash_, key_, value);
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
            if (this->hash_ == hash && std::invoke(equal, key_, key)) {
                removed = true;
                value = value_;
                return nullptr;
            }

            removed = false;
            return this->shared_from_this();
        }

        Key key_;
        T value_;
    };

    struct collision_node final : hash_node {
        collision_node(std::uint32_t hash, std::vector<entry> entries)
            : hash_node(hash),
              entries_(std::move(entries)) {
        }

        static std::shared_ptr<const collision_node> create(hash_node_ptr left, leaf_node_ptr right) {
            std::vector<entry> entries;
            if (const auto collision = std::dynamic_pointer_cast<const collision_node>(left)) {
                entries = collision->entries_;
                entries.emplace_back(right->key_, right->value_);
            } else {
                const auto leaf = std::static_pointer_cast<const leaf_node>(left);
                entries.emplace_back(leaf->key_, leaf->value_);
                entries.emplace_back(right->key_, right->value_);
            }

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
                if (!std::invoke(equal, entries_[i].key, key)) {
                    continue;
                }

                added = false;
                if (!overwrite || std::invoke(values_equal, entries_[i].value, value)) {
                    return this->shared_from_this();
                }

                auto replaced = entries_;
                replaced[i] = entry(entries_[i].key, value);
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
                if (!std::invoke(equal, entries_[i].key, key)) {
                    continue;
                }

                removed = true;
                value = entries_[i].value;

                if (entries_.size() == 2) {
                    const auto& remaining = entries_[1 - i];
                    return make_leaf(this->hash_, remaining.key, remaining.value);
                }

                auto entries = entries_;
                entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(i));
                return std::make_shared<collision_node>(this->hash_, std::move(entries));
            }

            removed = false;
            return this->shared_from_this();
        }

        std::vector<entry> entries_;
    };

    struct bitmap_indexed_node final : node {
        bitmap_indexed_node(std::uint32_t bitmap, std::vector<node_ptr> children)
            : bitmap_(bitmap),
              children_(std::move(children)) {
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
            const auto selected_slot = slot(bitmap_, selected_bit);

            if ((bitmap_ & selected_bit) == 0) {
                auto children = children_;
                children.insert(
                    children.begin() + static_cast<std::ptrdiff_t>(selected_slot),
                    make_leaf(hash, key, value));
                added = true;
                return std::make_shared<bitmap_indexed_node>(bitmap_ | selected_bit, std::move(children));
            }

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
            return std::make_shared<bitmap_indexed_node>(bitmap_, std::move(replaced));
        }

        [[nodiscard]] node_ptr remove(
            const Key& key,
            std::uint32_t hash,
            int shift,
            const KeyEqual& equal,
            bool& removed,
            std::optional<T>& value) const override {
            const auto selected_bit = bit(index(hash, shift));
            if ((bitmap_ & selected_bit) == 0) {
                removed = false;
                return this->shared_from_this();
            }

            const auto selected_slot = slot(bitmap_, selected_bit);
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
                if (children_.size() == 1) {
                    return nullptr;
                }

                auto children = children_;
                children.erase(children.begin() + static_cast<std::ptrdiff_t>(selected_slot));
                return rebuild(bitmap_ ^ selected_bit, std::move(children));
            }

            auto replaced = children_;
            replaced[selected_slot] = std::move(new_child);
            return rebuild(bitmap_, std::move(replaced));
        }

        static node_ptr rebuild(std::uint32_t bitmap, std::vector<node_ptr> children) {
            if (children.size() == 1 && dynamic_cast<const hash_node*>(children[0].get()) != nullptr) {
                return std::move(children[0]);
            }

            return std::make_shared<bitmap_indexed_node>(bitmap, std::move(children));
        }

        std::uint32_t bitmap_;
        std::vector<node_ptr> children_;
    };

    node_ptr root_;
    size_type count_ = 0;
    TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS Hash hash_{};
    TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS KeyEqual key_equal_{};
    TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS ValueEqual value_equal_{};
};

} // namespace tools::data_structures::hamt

#undef TOOLS_DATA_STRUCTURES_HAMT_NO_UNIQUE_ADDRESS
