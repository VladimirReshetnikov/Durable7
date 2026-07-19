#pragma once

#include <Tools/DataStructures/Hamt/merkle_encoding.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

// Implementation namespace: unstable and deliberately excluded from the supported API.
namespace merkle_persistence_detail {
template <class K, class V>
struct access;
} // namespace merkle_persistence_detail

template <class K, class V>
class merkle_search_tree;

template <class K, class V>
class merkle_search_tree_cursor;

template <class K, class V>
struct merkle_search_tree_cursor_search_result;

/// One retained key/value representative and its canonical encodings.
template <class K, class V>
class merkle_search_tree_entry final {
private:
    struct state final {
        std::shared_ptr<const K> key;
        std::shared_ptr<const V> value;
        std::shared_ptr<const merkle_bytes> key_bytes;
        std::shared_ptr<const merkle_bytes> value_bytes;
        std::uint8_t level;
    };

public:
    merkle_search_tree_entry() = delete;

    [[nodiscard]] const K& key() const noexcept { return *state_->key; }
    [[nodiscard]] const V& value() const noexcept { return *state_->value; }
    [[nodiscard]] std::shared_ptr<const K> key_handle() const noexcept { return state_->key; }
    [[nodiscard]] std::shared_ptr<const V> value_handle() const noexcept { return state_->value; }
    [[nodiscard]] const merkle_bytes& key_bytes() const noexcept { return *state_->key_bytes; }
    [[nodiscard]] const merkle_bytes& value_bytes() const noexcept { return *state_->value_bytes; }
    [[nodiscard]] std::uint8_t level() const noexcept { return state_->level; }

    [[nodiscard]] bool shares_identity_with(const merkle_search_tree_entry& other) const noexcept
    {
        return state_ == other.state_;
    }

private:
    template <class, class>
    friend class merkle_search_tree;
    friend struct merkle_persistence_detail::access<K, V>;

    merkle_search_tree_entry(
        std::shared_ptr<const K> key,
        std::shared_ptr<const V> value,
        std::shared_ptr<const merkle_bytes> key_bytes,
        std::shared_ptr<const merkle_bytes> value_bytes,
        const std::uint8_t level)
        : state_(std::make_shared<const state>(state{
              std::move(key),
              std::move(value),
              std::move(key_bytes),
              std::move(value_bytes),
              level}))
    {
    }

    [[nodiscard]] static merkle_search_tree_entry from_encoded(
        K key,
        V value,
        merkle_bytes key_bytes,
        merkle_bytes value_bytes,
        const std::uint8_t level)
    {
        return merkle_search_tree_entry{
            std::make_shared<const K>(std::move(key)),
            std::make_shared<const V>(std::move(value)),
            std::make_shared<const merkle_bytes>(std::move(key_bytes)),
            std::make_shared<const merkle_bytes>(std::move(value_bytes)),
            level};
    }

    [[nodiscard]] merkle_search_tree_entry replacing_value(
        V value,
        merkle_bytes value_bytes) const
    {
        return merkle_search_tree_entry{
            state_->key,
            std::make_shared<const V>(std::move(value)),
            state_->key_bytes,
            std::make_shared<const merkle_bytes>(std::move(value_bytes)),
            state_->level};
    }

    std::shared_ptr<const state> state_;
};

/// A key-level difference between two Merkle maps.
enum class merkle_map_difference_kind {
    added,
    removed,
    changed,
};

/// One semantic key-level map difference with shared representatives.
template <class K, class V>
struct merkle_map_difference final {
    merkle_map_difference_kind kind;
    std::shared_ptr<const K> key;
    std::shared_ptr<const V> old_value;
    std::shared_ptr<const V> new_value;

    friend bool operator==(
        const merkle_map_difference& left,
        const merkle_map_difference& right)
        requires std::equality_comparable<K> && std::equality_comparable<V>
    {
        const auto equal_optional_handle = [](const auto& first, const auto& second) {
            return first == nullptr || second == nullptr
                ? first == nullptr && second == nullptr
                : *first == *second;
        };
        return left.kind == right.kind
            && equal_optional_handle(left.key, right.key)
            && equal_optional_handle(left.old_value, right.old_value)
            && equal_optional_handle(left.new_value, right.new_value);
    }
};

/// One block-level shape record.
template <class K>
struct merkle_shape_entry final {
    std::uint8_t level;
    std::shared_ptr<const K> key;
    std::size_t entries_in_block;
    std::size_t subtree_count;

    friend bool operator==(const merkle_shape_entry& left, const merkle_shape_entry& right)
        requires std::equality_comparable<K>
    {
        return left.level == right.level
            && (left.key == nullptr || right.key == nullptr
                    ? left.key == nullptr && right.key == nullptr
                    : *left.key == *right.key)
            && left.entries_in_block == right.entries_in_block
            && left.subtree_count == right.subtree_count;
    }
};

/// One exact content-addressed canonical block.
struct merkle_block_record final {
    merkle_digest digest;
    std::shared_ptr<const merkle_bytes> bytes;

    friend bool operator==(const merkle_block_record& left, const merkle_block_record& right)
    {
        return left.digest == right.digest
            && (left.bytes == nullptr || right.bytes == nullptr
                    ? left.bytes == nullptr && right.bytes == nullptr
                    : *left.bytes == *right.bytes);
    }
};

/// Validated B=16 wide-tree representation statistics.
struct merkle_search_tree_statistics final {
    std::size_t count = 0;
    std::size_t block_count = 0;
    std::size_t height = 0;
    std::size_t minimum_entries_per_block = 0;
    std::size_t maximum_entries_per_block = 0;
    std::size_t minimum_block_bytes = 0;
    std::size_t maximum_block_bytes = 0;

    friend bool operator==(const merkle_search_tree_statistics&, const merkle_search_tree_statistics&) = default;
};

class merkle_policy_mismatch final : public std::invalid_argument {
public:
    merkle_policy_mismatch()
        : std::invalid_argument(
              "Merkle trees must use the same algorithm, policy, and codec domain")
    {
    }
};

class merkle_range_error final : public std::invalid_argument {
public:
    merkle_range_error()
        : std::invalid_argument("the minimum key must not follow the maximum key")
    {
    }
};

class merkle_tree_invariant_error final : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

/// An immutable ordered content-addressed map using canonical B=16 wide blocks.
template <class K, class V>
class merkle_search_tree final {
private:
    friend struct merkle_persistence_detail::access<K, V>;
    friend class merkle_search_tree_cursor<K, V>;

    using entry_type_internal = merkle_search_tree_entry<K, V>;

    struct node;
    using node_pointer = std::shared_ptr<const node>;

    struct node final {
        std::uint8_t level;
        std::vector<entry_type_internal> entries;
        std::vector<node_pointer> children;
        std::size_t count;
        std::size_t height;
        std::size_t block_count;
        std::shared_ptr<const K> minimum_key;
        std::shared_ptr<const K> maximum_key;
        std::shared_ptr<const merkle_bytes> block_bytes;
        merkle_digest digest;
    };

public:
    using key_type = K;
    using mapped_type = V;
    using entry_type = merkle_search_tree_entry<K, V>;
    using policy_type = merkle_search_tree_policy<K, V>;
    using difference_type = merkle_map_difference<K, V>;
    using size_type = std::size_t;
    using cursor_type = merkle_search_tree_cursor<K, V>;
    using cursor_search_result = merkle_search_tree_cursor_search_result<K, V>;

    class const_iterator final {
    private:
        struct frame final {
            const node* current;
            size_type next_entry;

            friend bool operator==(const frame&, const frame&) = default;
        };

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = entry_type;
        using difference_type = std::ptrdiff_t;
        using pointer = const entry_type*;
        using reference = const entry_type&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const { return *current_; }
        [[nodiscard]] pointer operator->() const noexcept { return current_; }

        const_iterator& operator++()
        {
            advance();
            return *this;
        }

        const_iterator operator++(int)
        {
            auto result = *this;
            ++*this;
            return result;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.current_ == nullptr || right.current_ == nullptr) {
                return left.current_ == right.current_;
            }
            return left.current_ == right.current_ && left.stack_ == right.stack_;
        }

    private:
        friend class merkle_search_tree;

        explicit const_iterator(const node_pointer& root)
        {
            push_left(root.get());
            advance();
        }

        void push_left(const node* current)
        {
            while (current != nullptr) {
                stack_.push_back(frame{current, 0});
                current = current->children.front().get();
            }
        }

        void advance()
        {
            current_ = nullptr;
            while (!stack_.empty()) {
                auto& top = stack_.back();
                if (top.next_entry == top.current->entries.size()) {
                    stack_.pop_back();
                    continue;
                }
                const auto index = top.next_entry++;
                current_ = &top.current->entries[index];
                push_left(top.current->children[index + 1].get());
                return;
            }
        }

        std::vector<frame> stack_;
        const entry_type* current_ = nullptr;
    };

    /// Creates an empty tree retaining an explicit deterministic policy.
    explicit merkle_search_tree(policy_type policy)
        : policy_(std::move(policy))
    {
    }

    [[nodiscard]] static merkle_search_tree create(policy_type policy)
    {
        return merkle_search_tree{std::move(policy)};
    }

    /// Creates a canonical tree with stable first-equivalent-key and last-value semantics.
    [[nodiscard]] static merkle_search_tree create_range(
        std::vector<std::pair<K, V>> entries,
        policy_type policy)
    {
        auto tree = merkle_search_tree{std::move(policy)};
        std::stable_sort(
            entries.begin(),
            entries.end(),
            [&tree](const auto& left, const auto& right) {
                return tree.policy_.compare(left.first, right.first) < 0;
            });

        auto canonical_entries = std::vector<entry_type>{};
        canonical_entries.reserve(entries.size());
        for (auto start = size_type{0}; start != entries.size();) {
            auto end = start + 1;
            while (end != entries.size()
                   && tree.policy_.compare(entries[start].first, entries[end].first) == 0) {
                ++end;
            }
            canonical_entries.push_back(tree.create_entry(
                std::move(entries[start].first),
                std::move(entries[end - 1].second)));
            start = end;
        }
        tree.root_ = tree.build_canonical(canonical_entries);
        return tree;
    }

    [[nodiscard]] const policy_type& policy() const noexcept { return policy_; }
    [[nodiscard]] size_type size() const noexcept { return root_ == nullptr ? 0 : root_->count; }
    [[nodiscard]] size_type count() const noexcept { return size(); }
    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type height() const noexcept { return root_ == nullptr ? 0 : root_->height; }
    [[nodiscard]] size_type block_count() const noexcept
    {
        return root_ == nullptr ? 0 : root_->block_count;
    }
    [[nodiscard]] merkle_digest root_hash() const noexcept
    {
        return root_ == nullptr ? policy_.empty_digest() : root_->digest;
    }

    /// Creates an immutable ordered gap cursor at `position`.
    [[nodiscard]] cursor_type get_cursor(size_type position = 0) const;

    /// Creates an immutable ordered gap cursor after the final entry.
    [[nodiscard]] cursor_type get_cursor_at_end() const;

    /// Creates the lower-bound cursor for `key`.
    [[nodiscard]] cursor_type get_cursor_lower_bound(const K& key) const;

    /// Creates the upper-bound cursor for `key`.
    [[nodiscard]] cursor_type get_cursor_upper_bound(const K& key) const;

    /// Creates a usable lower-bound cursor and reports whether its next entry is exact.
    [[nodiscard]] cursor_search_result get_cursor_at_key(const K& key) const;

    [[nodiscard]] bool shares_root_with(const merkle_search_tree& other) const noexcept
    {
        return root_ == other.root_;
    }

    [[nodiscard]] bool shares_policy_with(const merkle_search_tree& other) const noexcept
    {
        return policy_.shares_identity_with(other.policy_);
    }

    [[nodiscard]] size_type shared_block_count(const merkle_search_tree& other) const
    {
        auto mine = std::unordered_set<const node*>{};
        collect_node_identities(root_, mine);
        auto visited = std::unordered_set<const node*>{};
        auto pending = std::vector<node_pointer>{};
        if (other.root_ != nullptr) {
            pending.push_back(other.root_);
        }
        auto result = size_type{0};
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(current.get()).second) {
                continue;
            }
            if (mine.contains(current.get())) {
                ++result;
            }
            for (const auto& child : current->children) {
                if (child != nullptr) {
                    pending.push_back(child);
                }
            }
        }
        return result;
    }

    [[nodiscard]] const entry_type* get_entry(const K& key) const
    {
        auto current = root_.get();
        while (current != nullptr) {
            const auto [position, found] = find_position(current->entries, key);
            if (found) {
                return &current->entries[position];
            }
            current = current->children[position].get();
        }
        return nullptr;
    }

    [[nodiscard]] const V* try_get(const K& key) const
    {
        const auto* entry = get_entry(key);
        return entry == nullptr ? nullptr : &entry->value();
    }

    [[nodiscard]] const K* try_get_key(const K& key) const
    {
        const auto* entry = get_entry(key);
        return entry == nullptr ? nullptr : &entry->key();
    }

    [[nodiscard]] bool contains_key(const K& key) const { return get_entry(key) != nullptr; }

    [[nodiscard]] const V& at(const K& key) const
    {
        const auto* value = try_get(key);
        if (value == nullptr) {
            throw std::out_of_range("key was not present in the Merkle search tree");
        }
        return *value;
    }

    /// Adds or replaces an entry while copying only the affected block paths.
    [[nodiscard]] merkle_search_tree set_item(K key, V value) const
    {
        if (const auto* existing = get_entry(key); existing != nullptr) {
            auto value_bytes = policy_.value_codec().encode(value);
            if (value_bytes == existing->value_bytes()) {
                return *this;
            }
            auto replacement = existing->replacing_value(
                std::move(value),
                std::move(value_bytes));
            return merkle_search_tree{
                update_value(root_, key, std::move(replacement)),
                policy_};
        }

        auto entry = create_entry(std::move(key), std::move(value));
        return merkle_search_tree{insert(root_, std::move(entry)), policy_};
    }

    /// Removes an equivalent key and contracts empty block shells.
    [[nodiscard]] merkle_search_tree remove(const K& key) const
    {
        auto [root, removed] = remove_node(root_, key);
        return removed ? merkle_search_tree{std::move(root), policy_} : *this;
    }

    [[nodiscard]] merkle_search_tree clear() const
    {
        return empty() ? *this : merkle_search_tree{policy_};
    }

    [[nodiscard]] bool content_equals(const merkle_search_tree& other) const noexcept
    {
        return policy_.domain_digest() == other.policy_.domain_digest()
            && root_hash() == other.root_hash();
    }

    template <class ValuesEqual = std::equal_to<V>>
    [[nodiscard]] bool map_equals(
        const merkle_search_tree& other,
        ValuesEqual values_equal = {}) const
    {
        if (shares_root_with(other) && policy_.is_compatible_with(other.policy_)) {
            return true;
        }
        if (size() != other.size() || !policy_.is_compatible_with(other.policy_)) {
            return false;
        }
        if (root_hash() == other.root_hash()) {
            return nodes_equal(root_.get(), other.root_.get(), values_equal);
        }
        auto left = begin();
        auto right = other.begin();
        for (; left != end(); ++left, ++right) {
            if (right == other.end()
                || policy_.compare(left->key(), right->key()) != 0
                || !std::invoke(values_equal, left->value(), right->value())) {
                return false;
            }
        }
        return right == other.end();
    }

    template <class ValuesEqual = std::equal_to<V>>
    [[nodiscard]] std::vector<difference_type> diff(
        const merkle_search_tree& other,
        ValuesEqual values_equal = {}) const
    {
        ensure_compatible(other);
        auto result = std::vector<difference_type>{};
        diff_nodes(root_.get(), other.root_.get(), values_equal, result);
        return result;
    }

    /// Returns an inclusive range in semantic key order.
    [[nodiscard]] std::vector<entry_type> enumerate_range(
        const K& minimum_key,
        const K& maximum_key) const
    {
        if (policy_.compare(minimum_key, maximum_key) > 0) {
            throw merkle_range_error{};
        }
        auto result = std::vector<entry_type>{};
        enumerate_range_node(root_.get(), minimum_key, maximum_key, result);
        return result;
    }

    [[nodiscard]] std::vector<merkle_shape_entry<K>> shape() const
    {
        auto result = std::vector<merkle_shape_entry<K>>{};
        result.reserve(size());
        for (const auto& current : nodes_preorder()) {
            for (const auto& entry : current->entries) {
                result.push_back(merkle_shape_entry<K>{
                    current->level,
                    entry.key_handle(),
                    current->entries.size(),
                    current->count});
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<merkle_block_record> blocks_preorder() const
    {
        auto result = std::vector<merkle_block_record>{};
        result.reserve(block_count());
        for (const auto& current : nodes_preorder()) {
            result.push_back(merkle_block_record{current->digest, current->block_bytes});
        }
        return result;
    }

    /// Re-encodes every entry and block, then validates ordering, levels, metadata, and hashes.
    [[nodiscard]] merkle_search_tree_statistics validate_structure() const
    {
        if (root_ == nullptr) {
            return {};
        }
        auto accumulator = validation_accumulator{};
        validate_node(*root_, accumulator);
        if (accumulator.entry_count != size() || accumulator.block_count != block_count()) {
            throw merkle_tree_invariant_error(
                "Merkle root metadata disagrees with validated contents");
        }
        return accumulator.statistics(height());
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{root_}; }
    [[nodiscard]] const_iterator end() const noexcept { return {}; }

private:
    struct validation_accumulator final {
        size_type entry_count = 0;
        size_type block_count = 0;
        size_type minimum_entries = 0;
        size_type maximum_entries = 0;
        size_type minimum_block_bytes = 0;
        size_type maximum_block_bytes = 0;

        void add(const node& current)
        {
            entry_count = checked_add(entry_count, current.entries.size());
            ++block_count;
            if (block_count == 1) {
                minimum_entries = current.entries.size();
                minimum_block_bytes = current.block_bytes->size();
            } else {
                minimum_entries = (std::min)(minimum_entries, current.entries.size());
                minimum_block_bytes = (std::min)(minimum_block_bytes, current.block_bytes->size());
            }
            maximum_entries = (std::max)(maximum_entries, current.entries.size());
            maximum_block_bytes = (std::max)(maximum_block_bytes, current.block_bytes->size());
        }

        [[nodiscard]] merkle_search_tree_statistics statistics(const size_type tree_height) const
        {
            return merkle_search_tree_statistics{
                entry_count,
                block_count,
                tree_height,
                minimum_entries,
                maximum_entries,
                minimum_block_bytes,
                maximum_block_bytes};
        }
    };

    merkle_search_tree(node_pointer root, policy_type policy)
        : root_(std::move(root)), policy_(std::move(policy))
    {
    }

    [[nodiscard]] static size_type checked_add(const size_type left, const size_type right)
    {
        if (right > (std::numeric_limits<size_type>::max)() - left) {
            throw std::length_error("Merkle search tree size exceeds native limits");
        }
        return left + right;
    }

    [[nodiscard]] static size_type checked_multiply(const size_type left, const size_type right)
    {
        if (left != 0 && right > (std::numeric_limits<size_type>::max)() / left) {
            throw std::length_error("Merkle search tree size exceeds native limits");
        }
        return left * right;
    }

    [[nodiscard]] entry_type create_entry(K key, V value) const
    {
        auto key_bytes = policy_.key_codec().encode(key);
        auto value_bytes = policy_.value_codec().encode(value);
        const auto level = policy_type::level(policy_.hash_key_bytes(key_bytes));
        return entry_type::from_encoded(
            std::move(key),
            std::move(value),
            std::move(key_bytes),
            std::move(value_bytes),
            level);
    }

    [[nodiscard]] std::pair<size_type, bool> find_position(
        const std::vector<entry_type>& entries,
        const K& key) const
    {
        auto low = size_type{0};
        auto high = entries.size();
        while (low < high) {
            const auto middle = low + (high - low) / 2;
            if (policy_.compare(entries[middle].key(), key) < 0) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        return {low, low != entries.size() && policy_.compare(entries[low].key(), key) == 0};
    }

    /// Returns the entry at an in-order rank, or nullptr when the rank is at or past the end.
    ///
    /// A rank that is in range but that the cached subtree counts cannot locate is a
    /// structural-integrity failure of the tree, not an absent entry, so it is reported by
    /// throwing rather than by returning nullptr. Degrading a broken count invariant into
    /// "there is no next entry" would let an authenticated collection silently answer
    /// navigation queries from an inconsistent index; this matches the reference C#
    /// implementation, which throws on the same condition.
    [[nodiscard]] const entry_type* entry_at_rank_for_cursor(size_type rank) const
    {
        if (rank >= size()) {
            return nullptr;
        }
        auto* current = root_.get();
        while (current != nullptr) {
            auto descended = false;
            for (auto index = size_type{0}; index != current->entries.size(); ++index) {
                const auto child_count = current->children[index] == nullptr
                    ? size_type{0}
                    : current->children[index]->count;
                if (rank < child_count) {
                    current = current->children[index].get();
                    descended = true;
                    break;
                }
                rank -= child_count;
                if (rank == 0) {
                    return &current->entries[index];
                }
                --rank;
            }
            if (!descended) {
                current = current->children.back().get();
            }
        }
        throw std::logic_error("Merkle subtree counts do not contain the requested rank");
    }

    [[nodiscard]] std::pair<size_type, bool> lower_bound_rank_for_cursor(const K& key) const
    {
        auto rank = size_type{0};
        auto* current = root_.get();
        while (current != nullptr) {
            const auto [position, found] = find_position(current->entries, key);
            for (auto index = size_type{0}; index != position; ++index) {
                rank = checked_add(
                    rank,
                    checked_add(
                        current->children[index] == nullptr
                            ? size_type{0}
                            : current->children[index]->count,
                        size_type{1}));
            }
            if (found) {
                return {
                    checked_add(
                        rank,
                        current->children[position] == nullptr
                            ? size_type{0}
                            : current->children[position]->count),
                    true};
            }
            current = current->children[position].get();
        }
        return {rank, false};
    }

    [[nodiscard]] merkle_search_tree set_value_at_key(const K& key, V value) const
    {
        const auto* existing = get_entry(key);
        if (existing == nullptr) {
            throw merkle_tree_invariant_error("cursor replacement key is absent");
        }
        auto value_bytes = policy_.value_codec().encode(value);
        if (value_bytes == existing->value_bytes()) {
            return *this;
        }
        auto replacement = existing->replacing_value(std::move(value), std::move(value_bytes));
        return merkle_search_tree{
            update_value(root_, key, std::move(replacement)),
            policy_};
    }

    [[nodiscard]] node_pointer build_canonical(const std::span<const entry_type> entries) const
    {
        if (entries.empty()) {
            return nullptr;
        }
        auto maximum_level = std::uint8_t{0};
        for (const auto& entry : entries) {
            maximum_level = (std::max)(maximum_level, entry.level());
        }

        auto block_entries = std::vector<entry_type>{};
        auto children = std::vector<node_pointer>{};
        auto segment_start = size_type{0};
        for (auto index = size_type{0}; index != entries.size(); ++index) {
            if (entries[index].level() != maximum_level) {
                continue;
            }
            children.push_back(build_canonical(entries.subspan(segment_start, index - segment_start)));
            block_entries.push_back(entries[index]);
            segment_start = index + 1;
        }
        children.push_back(build_canonical(entries.subspan(segment_start)));
        return new_node(maximum_level, std::move(block_entries), std::move(children));
    }

    [[nodiscard]] node_pointer update_value(
        const node_pointer& current,
        const K& key,
        entry_type replacement) const
    {
        const auto [position, found] = find_position(current->entries, key);
        if (found) {
            auto entries = current->entries;
            entries[position] = std::move(replacement);
            return new_node(current->level, std::move(entries), current->children);
        }
        auto children = current->children;
        children[position] = update_value(children[position], key, std::move(replacement));
        return new_node(current->level, current->entries, std::move(children));
    }

    [[nodiscard]] node_pointer insert(node_pointer current, entry_type entry) const
    {
        const auto entry_level = entry.level();
        if (current == nullptr) {
            return new_node(entry_level, {std::move(entry)}, {nullptr, nullptr});
        }
        if (entry_level > current->level) {
            auto [left, right] = split(current, entry.key());
            return new_node(
                entry_level,
                {std::move(entry)},
                {std::move(left), std::move(right)});
        }

        const auto [position, found] = find_position(current->entries, entry.key());
        if (found) {
            throw merkle_tree_invariant_error("attempted to insert an equivalent existing key");
        }
        if (entry_level < current->level) {
            auto children = current->children;
            children[position] = insert(children[position], std::move(entry));
            return new_node(current->level, current->entries, std::move(children));
        }

        auto [left, right] = split(current->children[position], entry.key());
        auto entries = current->entries;
        entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(position), std::move(entry));
        auto children = current->children;
        children[position] = std::move(left);
        children.insert(
            children.begin() + static_cast<std::ptrdiff_t>(position + 1),
            std::move(right));
        return new_node(current->level, std::move(entries), std::move(children));
    }

    [[nodiscard]] std::pair<node_pointer, node_pointer> split(
        const node_pointer& current,
        const K& key) const
    {
        if (current == nullptr) {
            return {nullptr, nullptr};
        }
        const auto [position, found] = find_position(current->entries, key);
        if (found) {
            throw merkle_tree_invariant_error("attempted to split at an existing key");
        }
        auto [child_left, child_right] = split(current->children[position], key);

        auto left_entries = std::vector<entry_type>{
            current->entries.begin(),
            current->entries.begin() + static_cast<std::ptrdiff_t>(position)};
        auto left_children = std::vector<node_pointer>{
            current->children.begin(),
            current->children.begin() + static_cast<std::ptrdiff_t>(position + 1)};
        left_children[position] = std::move(child_left);

        auto right_entries = std::vector<entry_type>{
            current->entries.begin() + static_cast<std::ptrdiff_t>(position),
            current->entries.end()};
        auto right_children = std::vector<node_pointer>{
            current->children.begin() + static_cast<std::ptrdiff_t>(position),
            current->children.end()};
        right_children.front() = std::move(child_right);
        return {
            create_or_collapse(current->level, std::move(left_entries), std::move(left_children)),
            create_or_collapse(current->level, std::move(right_entries), std::move(right_children))};
    }

    [[nodiscard]] std::pair<node_pointer, bool> remove_node(
        const node_pointer& current,
        const K& key) const
    {
        if (current == nullptr) {
            return {nullptr, false};
        }
        const auto [position, found] = find_position(current->entries, key);
        if (!found) {
            auto [child, removed] = remove_node(current->children[position], key);
            if (!removed) {
                return {current, false};
            }
            auto children = current->children;
            children[position] = std::move(child);
            return {
                new_node(current->level, current->entries, std::move(children)),
                true};
        }

        auto entries = current->entries;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(position));
        auto children = current->children;
        children[position] = join(children[position], children[position + 1]);
        children.erase(children.begin() + static_cast<std::ptrdiff_t>(position + 1));
        return {
            create_or_collapse(current->level, std::move(entries), std::move(children)),
            true};
    }

    [[nodiscard]] node_pointer join(const node_pointer& left, const node_pointer& right) const
    {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }
        if (left->level > right->level) {
            auto children = left->children;
            children.back() = join(children.back(), right);
            return new_node(left->level, left->entries, std::move(children));
        }
        if (left->level < right->level) {
            auto children = right->children;
            children.front() = join(left, children.front());
            return new_node(right->level, right->entries, std::move(children));
        }

        auto entries = left->entries;
        entries.insert(entries.end(), right->entries.begin(), right->entries.end());
        auto children = std::vector<node_pointer>{
            left->children.begin(),
            left->children.begin() + static_cast<std::ptrdiff_t>(left->entries.size())};
        children.push_back(join(left->children.back(), right->children.front()));
        children.insert(children.end(), right->children.begin() + 1, right->children.end());
        return new_node(left->level, std::move(entries), std::move(children));
    }

    [[nodiscard]] node_pointer create_or_collapse(
        const std::uint8_t level,
        std::vector<entry_type> entries,
        std::vector<node_pointer> children) const
    {
        if (children.size() != entries.size() + 1) {
            throw merkle_tree_invariant_error("invalid entry/child arity while collapsing an MST block");
        }
        return entries.empty()
            ? std::move(children.front())
            : new_node(level, std::move(entries), std::move(children));
    }

    [[nodiscard]] node_pointer new_node(
        const std::uint8_t level,
        std::vector<entry_type> entries,
        std::vector<node_pointer> children) const
    {
        if (level > 64 || entries.empty() || children.size() != entries.size() + 1) {
            throw merkle_tree_invariant_error(
                "an MST block must contain a valid level, entries, and one extra child interval");
        }
        auto count_value = entries.size();
        auto height_value = size_type{1};
        auto block_count_value = size_type{1};
        for (const auto& child : children) {
            if (child == nullptr) {
                continue;
            }
            count_value = checked_add(count_value, child->count);
            height_value = (std::max)(height_value, checked_add(1, child->height));
            block_count_value = checked_add(block_count_value, child->block_count);
        }
        auto block_bytes = encode_block(level, count_value, entries, children);
        const auto digest = merkle_digest::hash(block_bytes);
        auto minimum_key = children.front() == nullptr
            ? entries.front().key_handle()
            : children.front()->minimum_key;
        auto maximum_key = children.back() == nullptr
            ? entries.back().key_handle()
            : children.back()->maximum_key;
        return std::make_shared<const node>(node{
            level,
            std::move(entries),
            std::move(children),
            count_value,
            height_value,
            block_count_value,
            std::move(minimum_key),
            std::move(maximum_key),
            std::make_shared<const merkle_bytes>(std::move(block_bytes)),
            digest});
    }

    [[nodiscard]] merkle_bytes encode_block(
        const std::uint8_t level,
        const size_type subtree_count,
        const std::vector<entry_type>& entries,
        const std::vector<node_pointer>& children) const
    {
        constexpr auto digest_length = merkle_digest::byte_length;
        constexpr auto header_length = size_type{4 + 1 + digest_length + 1 + 4 + 4};
        if (subtree_count > static_cast<size_type>((std::numeric_limits<std::int32_t>::max)())
            || entries.size() > static_cast<size_type>((std::numeric_limits<std::int32_t>::max)())) {
            throw std::length_error("Merkle search tree count exceeds the signed 32-bit wire limit");
        }
        auto length = checked_add(
            header_length,
            checked_multiply(children.size(), digest_length));
        for (const auto& entry : entries) {
            merkle_detail::require_framed_length(entry.key_bytes().size());
            merkle_detail::require_framed_length(entry.value_bytes().size());
            length = checked_add(length, 8);
            length = checked_add(length, entry.key_bytes().size());
            length = checked_add(length, entry.value_bytes().size());
        }

        auto result = merkle_bytes{};
        result.reserve(length);
        const auto magic = merkle_detail::bytes_of("MST2");
        result.insert(result.end(), magic.begin(), magic.end());
        result.push_back(std::byte{1});
        const auto domain = policy_.domain_digest();
        result.insert(result.end(), domain.bytes().begin(), domain.bytes().end());
        result.push_back(merkle_detail::as_byte(level));
        merkle_detail::append_i32_be(result, static_cast<std::int32_t>(subtree_count));
        merkle_detail::append_i32_be(result, static_cast<std::int32_t>(entries.size()));
        for (const auto& entry : entries) {
            merkle_detail::append_i32_be(
                result,
                static_cast<std::int32_t>(entry.key_bytes().size()));
            result.insert(result.end(), entry.key_bytes().begin(), entry.key_bytes().end());
            merkle_detail::append_i32_be(
                result,
                static_cast<std::int32_t>(entry.value_bytes().size()));
            result.insert(result.end(), entry.value_bytes().begin(), entry.value_bytes().end());
        }
        for (const auto& child : children) {
            const auto digest = child == nullptr ? policy_.empty_digest() : child->digest;
            result.insert(result.end(), digest.bytes().begin(), digest.bytes().end());
        }
        if (result.size() != length) {
            throw merkle_tree_invariant_error("MST block encoder produced an unexpected length");
        }
        return result;
    }

    template <class ValuesEqual>
    [[nodiscard]] bool nodes_equal(
        const node* left,
        const node* right,
        ValuesEqual& values_equal) const
    {
        if (left == right) {
            return true;
        }
        if (left == nullptr || right == nullptr
            || left->level != right->level
            || left->entries.size() != right->entries.size()) {
            return false;
        }
        for (auto index = size_type{0}; index != left->entries.size(); ++index) {
            const auto& left_entry = left->entries[index];
            const auto& right_entry = right->entries[index];
            if (!left_entry.shares_identity_with(right_entry)
                && (policy_.compare(left_entry.key(), right_entry.key()) != 0
                    || !std::invoke(
                        values_equal,
                        left_entry.value(),
                        right_entry.value()))) {
                return false;
            }
        }
        for (auto index = size_type{0}; index != left->children.size(); ++index) {
            if (!nodes_equal(left->children[index].get(), right->children[index].get(), values_equal)) {
                return false;
            }
        }
        return true;
    }

    template <class ValuesEqual>
    void diff_nodes(
        const node* left,
        const node* right,
        ValuesEqual& values_equal,
        std::vector<difference_type>& result) const
    {
        if (left == right
            || (left == nullptr ? policy_.empty_digest() : left->digest)
                == (right == nullptr ? policy_.empty_digest() : right->digest)) {
            return;
        }
        if (left == nullptr) {
            append_subtree_differences(right, merkle_map_difference_kind::added, result);
            return;
        }
        if (right == nullptr) {
            append_subtree_differences(left, merkle_map_difference_kind::removed, result);
            return;
        }
        if (same_separators(*left, *right)) {
            for (auto index = size_type{0}; index != left->entries.size(); ++index) {
                diff_nodes(
                    left->children[index].get(),
                    right->children[index].get(),
                    values_equal,
                    result);
                if (!std::invoke(
                        values_equal,
                        left->entries[index].value(),
                        right->entries[index].value())) {
                    result.push_back(difference_type{
                        merkle_map_difference_kind::changed,
                        left->entries[index].key_handle(),
                        left->entries[index].value_handle(),
                        right->entries[index].value_handle()});
                }
            }
            diff_nodes(
                left->children.back().get(),
                right->children.back().get(),
                values_equal,
                result);
            return;
        }
        merge_diff(*left, *right, values_equal, result);
    }

    [[nodiscard]] bool same_separators(const node& left, const node& right) const
    {
        if (left.level != right.level || left.entries.size() != right.entries.size()) {
            return false;
        }
        for (auto index = size_type{0}; index != left.entries.size(); ++index) {
            if (policy_.compare(left.entries[index].key(), right.entries[index].key()) != 0) {
                return false;
            }
        }
        return true;
    }

    void append_subtree_differences(
        const node* root,
        const merkle_map_difference_kind kind,
        std::vector<difference_type>& result) const
    {
        const auto entries = enumerate_node(root);
        for (const auto* entry : entries) {
            result.push_back(kind == merkle_map_difference_kind::added
                ? difference_type{
                      kind,
                      entry->key_handle(),
                      nullptr,
                      entry->value_handle()}
                : difference_type{
                      kind,
                      entry->key_handle(),
                      entry->value_handle(),
                      nullptr});
        }
    }

    template <class ValuesEqual>
    void merge_diff(
        const node& left_root,
        const node& right_root,
        ValuesEqual& values_equal,
        std::vector<difference_type>& result) const
    {
        const auto left = enumerate_node(&left_root);
        const auto right = enumerate_node(&right_root);
        auto left_index = size_type{0};
        auto right_index = size_type{0};
        while (left_index != left.size() || right_index != right.size()) {
            const auto comparison = left_index == left.size()
                ? 1
                : right_index == right.size()
                    ? -1
                    : policy_.compare(left[left_index]->key(), right[right_index]->key());
            if (comparison < 0) {
                const auto* entry = left[left_index++];
                result.push_back(difference_type{
                    merkle_map_difference_kind::removed,
                    entry->key_handle(),
                    entry->value_handle(),
                    nullptr});
            } else if (comparison > 0) {
                const auto* entry = right[right_index++];
                result.push_back(difference_type{
                    merkle_map_difference_kind::added,
                    entry->key_handle(),
                    nullptr,
                    entry->value_handle()});
            } else {
                const auto* old_entry = left[left_index++];
                const auto* new_entry = right[right_index++];
                if (!std::invoke(values_equal, old_entry->value(), new_entry->value())) {
                    result.push_back(difference_type{
                        merkle_map_difference_kind::changed,
                        old_entry->key_handle(),
                        old_entry->value_handle(),
                        new_entry->value_handle()});
                }
            }
        }
    }

    void ensure_compatible(const merkle_search_tree& other) const
    {
        if (!policy_.is_compatible_with(other.policy_)) {
            throw merkle_policy_mismatch{};
        }
    }

    void enumerate_range_node(
        const node* current,
        const K& minimum_key,
        const K& maximum_key,
        std::vector<entry_type>& result) const
    {
        if (current == nullptr) {
            return;
        }
        for (auto index = size_type{0}; index != current->entries.size(); ++index) {
            const auto low = policy_.compare(current->entries[index].key(), minimum_key);
            const auto high = policy_.compare(current->entries[index].key(), maximum_key);
            if (low > 0) {
                enumerate_range_node(
                    current->children[index].get(),
                    minimum_key,
                    maximum_key,
                    result);
            }
            if (low >= 0 && high <= 0) {
                result.push_back(current->entries[index]);
            }
            if (high > 0) {
                return;
            }
        }
        if (policy_.compare(current->entries.back().key(), maximum_key) < 0) {
            enumerate_range_node(
                current->children.back().get(),
                minimum_key,
                maximum_key,
                result);
        }
    }

    void validate_node(const node& current, validation_accumulator& accumulator) const
    {
        if (current.level > 64 || current.entries.empty()
            || current.children.size() != current.entries.size() + 1) {
            throw merkle_tree_invariant_error("an MST block has invalid level or entry/child arity");
        }

        auto count_value = current.entries.size();
        auto height_value = size_type{1};
        auto block_count_value = size_type{1};
        for (auto index = size_type{0}; index != current.entries.size(); ++index) {
            const auto& entry = current.entries[index];
            const auto key_bytes = policy_.key_codec().encode(entry.key());
            const auto value_bytes = policy_.value_codec().encode(entry.value());
            if (key_bytes != entry.key_bytes() || value_bytes != entry.value_bytes()) {
                throw merkle_tree_invariant_error(
                    "an MST entry disagrees with its canonical key or value encoding");
            }
            const auto expected_level = policy_type::level(policy_.hash_key_bytes(key_bytes));
            if (entry.level() != expected_level || entry.level() != current.level) {
                throw merkle_tree_invariant_error("an MST entry is stored at the wrong hash level");
            }
            if (index != 0
                && policy_.compare(current.entries[index - 1].key(), entry.key()) >= 0) {
                throw merkle_tree_invariant_error("MST block entries are not strictly ordered");
            }
        }

        for (auto index = size_type{0}; index != current.children.size(); ++index) {
            const auto& child = current.children[index];
            if (child == nullptr) {
                continue;
            }
            if (child->level >= current.level) {
                throw merkle_tree_invariant_error("an MST child does not occupy a lower level");
            }
            if (index != 0
                && policy_.compare(*child->minimum_key, current.entries[index - 1].key()) <= 0) {
                throw merkle_tree_invariant_error("an MST child crosses its lower key separator");
            }
            if (index != current.entries.size()
                && policy_.compare(*child->maximum_key, current.entries[index].key()) >= 0) {
                throw merkle_tree_invariant_error("an MST child crosses its upper key separator");
            }
            validate_node(*child, accumulator);
            count_value = checked_add(count_value, child->count);
            height_value = (std::max)(height_value, checked_add(1, child->height));
            block_count_value = checked_add(block_count_value, child->block_count);
        }

        const auto& expected_minimum = current.children.front() == nullptr
            ? current.entries.front().key()
            : *current.children.front()->minimum_key;
        const auto& expected_maximum = current.children.back() == nullptr
            ? current.entries.back().key()
            : *current.children.back()->maximum_key;
        if (count_value != current.count
            || height_value != current.height
            || block_count_value != current.block_count
            || policy_.compare(expected_minimum, *current.minimum_key) != 0
            || policy_.compare(expected_maximum, *current.maximum_key) != 0) {
            throw merkle_tree_invariant_error("an MST block has invalid cached metadata");
        }
        const auto block_bytes = encode_block(
            current.level,
            current.count,
            current.entries,
            current.children);
        if (block_bytes != *current.block_bytes
            || merkle_digest::hash(*current.block_bytes) != current.digest) {
            throw merkle_tree_invariant_error(
                "an MST block has invalid canonical bytes or content digest");
        }
        accumulator.add(current);
    }

    [[nodiscard]] std::vector<node_pointer> nodes_preorder() const
    {
        auto result = std::vector<node_pointer>{};
        result.reserve(block_count());
        auto pending = std::vector<node_pointer>{};
        if (root_ != nullptr) {
            pending.push_back(root_);
        }
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            for (auto iterator = current->children.rbegin(); iterator != current->children.rend(); ++iterator) {
                if (*iterator != nullptr) {
                    pending.push_back(*iterator);
                }
            }
            result.push_back(std::move(current));
        }
        return result;
    }

    [[nodiscard]] static std::vector<const entry_type*> enumerate_node(const node* root)
    {
        struct frame final {
            const node* current;
            size_type next_entry;
        };
        auto result = std::vector<const entry_type*>{};
        if (root == nullptr) {
            return result;
        }
        result.reserve(root->count);
        auto pending = std::vector<frame>{};
        auto push_left = [&pending](const node* current) {
            while (current != nullptr) {
                pending.push_back(frame{current, 0});
                current = current->children.front().get();
            }
        };
        push_left(root);
        while (!pending.empty()) {
            auto& top = pending.back();
            if (top.next_entry == top.current->entries.size()) {
                pending.pop_back();
                continue;
            }
            const auto index = top.next_entry++;
            result.push_back(&top.current->entries[index]);
            push_left(top.current->children[index + 1].get());
        }
        return result;
    }

    static void collect_node_identities(
        const node_pointer& root,
        std::unordered_set<const node*>& result)
    {
        auto pending = std::vector<node_pointer>{};
        if (root != nullptr) {
            pending.push_back(root);
        }
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            if (!result.insert(current.get()).second) {
                continue;
            }
            for (const auto& child : current->children) {
                if (child != nullptr) {
                    pending.push_back(child);
                }
            }
        }
    }

    node_pointer root_;
    policy_type policy_;
};

/// Immutable tree-snapshot-plus-rank gap cursor in policy-comparer order.
template <class K, class V>
class merkle_search_tree_cursor final {
public:
    using tree_type = merkle_search_tree<K, V>;
    using entry_type = typename tree_type::entry_type;
    using size_type = typename tree_type::size_type;

    /// A cursor names a real gap in a real tree, so it has no valid empty state and no default
    /// constructor. There is consequently no "invalid default" whose members must all throw.
    merkle_search_tree_cursor() = delete;

    merkle_search_tree_cursor(const merkle_search_tree_cursor&) = default;
    merkle_search_tree_cursor& operator=(const merkle_search_tree_cursor&) = default;

    /// Cursor values are immutable version handles, so moving deliberately copies the retained
    /// tree instead of stealing it. A moved-from cursor therefore remains a fully valid cursor
    /// on the same version and the same gap: every member stays callable and observably
    /// unchanged. This matches the C++ sequence and rope cursors and keeps the class free of
    /// the split state a defaulted move would produce, where the retained tree is emptied while
    /// the position is copied, leaving count() and position() disagreeing and the policy null.
    merkle_search_tree_cursor(merkle_search_tree_cursor&& other) noexcept(
        std::is_nothrow_copy_constructible_v<tree_type>)
        : tree_(other.tree_), position_(other.position_)
    {
    }

    merkle_search_tree_cursor& operator=(merkle_search_tree_cursor&& other) noexcept(
        std::is_nothrow_copy_assignable_v<tree_type>)
    {
        if (this != &other) {
            tree_ = other.tree_;
            position_ = other.position_;
        }
        return *this;
    }

    [[nodiscard]] size_type count() const noexcept { return tree_.size(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == count(); }

    /// Returns the retained entry immediately before the gap, or nullptr at the start.
    /// Throws if the retained tree's cached subtree counts cannot locate an in-range rank.
    [[nodiscard]] const entry_type* peek_previous() const &
    {
        return position_ == 0 ? nullptr : tree_.entry_at_rank_for_cursor(position_ - 1);
    }
    [[nodiscard]] const entry_type* peek_previous() const && = delete;

    /// Returns the retained entry immediately after the gap, or nullptr at the end.
    /// Throws if the retained tree's cached subtree counts cannot locate an in-range rank.
    [[nodiscard]] const entry_type* peek_next() const &
    {
        return tree_.entry_at_rank_for_cursor(position_);
    }
    [[nodiscard]] const entry_type* peek_next() const && = delete;

    [[nodiscard]] merkle_search_tree_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("Merkle cursor is already at the start");
        }
        return merkle_search_tree_cursor{tree_, position_ - 1};
    }

    [[nodiscard]] merkle_search_tree_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("Merkle cursor is already at the end");
        }
        return merkle_search_tree_cursor{tree_, position_ + 1};
    }

    [[nodiscard]] merkle_search_tree_cursor seek(const size_type position) const
    {
        if (position > count()) {
            throw std::out_of_range("Merkle cursor position is outside the tree bounds");
        }
        return position == position_ ? *this : merkle_search_tree_cursor{tree_, position};
    }

    /// Strictly inserts a missing key at its lower-bound gap.
    [[nodiscard]] merkle_search_tree_cursor insert(K key, V value) const
    {
        const auto [expected, found] = tree_.lower_bound_rank_for_cursor(key);
        if (found) {
            throw std::invalid_argument("Merkle cursor cannot insert a duplicate key");
        }
        ensure_current_gap(expected);
        return merkle_search_tree_cursor{
            tree_.set_item(std::move(key), std::move(value)),
            position_ + 1};
    }

    /// Updates an exact next entry or inserts at a missing lower-bound gap. Named for the
    /// tree operation it delegates to, matching the other C++ cursors that expose set_item.
    [[nodiscard]] merkle_search_tree_cursor set_item(K key, V value) const
    {
        const auto [expected, found] = tree_.lower_bound_rank_for_cursor(key);
        ensure_current_gap(expected);
        auto tree = tree_.set_item(std::move(key), std::move(value));
        if (tree.shares_root_with(tree_)) {
            return *this;
        }
        return merkle_search_tree_cursor{
            std::move(tree),
            position_ + (found ? size_type{0} : size_type{1})};
    }

    /// Replaces the next value while retaining its key representative and this gap.
    [[nodiscard]] merkle_search_tree_cursor set_next_value(V value) const
    {
        const auto* next = peek_next();
        if (next == nullptr) {
            throw std::logic_error("Merkle cursor has no next entry");
        }
        auto tree = tree_.set_value_at_key(next->key(), std::move(value));
        return tree.shares_root_with(tree_)
            ? *this
            : merkle_search_tree_cursor{std::move(tree), position_};
    }

    [[nodiscard]] merkle_search_tree_cursor delete_previous() const
    {
        const auto* previous = peek_previous();
        if (previous == nullptr) {
            throw std::logic_error("Merkle cursor has no previous entry");
        }
        return merkle_search_tree_cursor{tree_.remove(previous->key()), position_ - 1};
    }

    [[nodiscard]] merkle_search_tree_cursor delete_next() const
    {
        const auto* next = peek_next();
        if (next == nullptr) {
            throw std::logic_error("Merkle cursor has no next entry");
        }
        return merkle_search_tree_cursor{tree_.remove(next->key()), position_};
    }

    [[nodiscard]] tree_type snapshot() const { return tree_; }

private:
    friend class merkle_search_tree<K, V>;

    merkle_search_tree_cursor(tree_type tree, const size_type position)
        : tree_(std::move(tree)), position_(position)
    {
    }

    void ensure_current_gap(const size_type expected) const
    {
        if (expected != position_) {
            throw std::invalid_argument("Merkle key does not belong at the current cursor gap");
        }
    }

    tree_type tree_;
    size_type position_;
};

template <class K, class V>
struct merkle_search_tree_cursor_search_result final {
    merkle_search_tree_cursor<K, V> cursor;
    bool found;
};

template <class K, class V>
[[nodiscard]] auto merkle_search_tree<K, V>::get_cursor(const size_type position) const
    -> cursor_type
{
    if (position > size()) {
        throw std::out_of_range("Merkle cursor position is outside the tree bounds");
    }
    return cursor_type{*this, position};
}

template <class K, class V>
[[nodiscard]] auto merkle_search_tree<K, V>::get_cursor_at_end() const -> cursor_type
{
    return cursor_type{*this, size()};
}

template <class K, class V>
[[nodiscard]] auto merkle_search_tree<K, V>::get_cursor_lower_bound(const K& key) const
    -> cursor_type
{
    return cursor_type{*this, lower_bound_rank_for_cursor(key).first};
}

template <class K, class V>
[[nodiscard]] auto merkle_search_tree<K, V>::get_cursor_upper_bound(const K& key) const
    -> cursor_type
{
    const auto [position, found] = lower_bound_rank_for_cursor(key);
    return cursor_type{*this, position + (found ? size_type{1} : size_type{0})};
}

template <class K, class V>
[[nodiscard]] auto merkle_search_tree<K, V>::get_cursor_at_key(const K& key) const
    -> cursor_search_result
{
    const auto [position, found] = lower_bound_rank_for_cursor(key);
    return cursor_search_result{cursor_type{*this, position}, found};
}

} // namespace tools::data_structures::hamt
