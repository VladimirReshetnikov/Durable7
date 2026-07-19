#pragma once

#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

template<class Key, class T>
class basic_patricia_map_cursor;

template<class Key, class T>
struct basic_patricia_map_cursor_search_result;

template<class Key>
class basic_patricia_set_cursor;

template<class Key>
class basic_patricia_set;

template<class Key, class T>
class basic_patricia_map {
    static_assert(std::is_signed_v<Key> && std::is_integral_v<Key>);
    using path_type = std::make_unsigned_t<Key>;
    struct node;
    struct leaf_node;
    struct branch_node;
    using node_ptr = std::shared_ptr<const node>;

    struct node {
        explicit node(std::size_t size) noexcept : size(size) {}
        virtual ~node() = default;
        virtual bool is_leaf() const noexcept = 0;
        std::size_t size;
    };
    struct leaf_node final : node {
        path_type path; Key key; T value;
        leaf_node(path_type path, Key key, T value) : node(1), path(path), key(key), value(std::move(value)) {}
        bool is_leaf() const noexcept override { return true; }
    };
    struct branch_node final : node {
        path_type prefix; path_type mask; node_ptr left; node_ptr right;
        branch_node(path_type prefix, path_type mask, node_ptr left, node_ptr right)
            : node(left->size + right->size), prefix(prefix), mask(mask), left(std::move(left)), right(std::move(right)) {}
        bool is_leaf() const noexcept override { return false; }
    };

public:
    using key_type = Key;
    using mapped_type = T;
    using value_type = std::pair<Key, T>;
    using cursor_type = basic_patricia_map_cursor<Key, T>;
    using cursor_search_result = basic_patricia_map_cursor_search_result<Key, T>;

    basic_patricia_map() = default;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool shares_root_with(const basic_patricia_map& other) const noexcept { return root_.get() == other.root_.get(); }

    [[nodiscard]] const T* try_get(Key key) const noexcept {
        const auto path = encode(key);
        auto current = root_.get();
        while (current != nullptr) {
            if (current->is_leaf()) {
                const auto* leaf = static_cast<const leaf_node*>(current);
                return leaf->path == path ? std::addressof(leaf->value) : nullptr;
            }
            const auto* branch = static_cast<const branch_node*>(current);
            if (prefix_of(path, branch->mask) != branch->prefix) return nullptr;
            current = (path & branch->mask) == 0 ? branch->left.get() : branch->right.get();
        }
        return nullptr;
    }
    [[nodiscard]] bool contains_key(Key key) const noexcept { return try_get(key) != nullptr; }

    /// Creates an immutable ordered gap cursor at `position`.
    [[nodiscard]] cursor_type get_cursor(std::size_t position = 0) const;
    /// Creates an immutable ordered gap cursor after the final entry.
    [[nodiscard]] cursor_type get_cursor_at_end() const;
    /// Creates the lower-bound cursor for `key`.
    [[nodiscard]] cursor_type get_cursor_lower_bound(Key key) const;
    /// Creates the upper-bound cursor for `key`.
    [[nodiscard]] cursor_type get_cursor_upper_bound(Key key) const;
    /// Creates a usable lower-bound cursor and reports whether its next entry is exact.
    [[nodiscard]] cursor_search_result get_cursor_at_key(Key key) const;

    [[nodiscard]] basic_patricia_map set_item(Key key, const T& value) const {
        bool added = false;
        bool changed = false;
        auto root = insert(root_, encode(key), key, value, false, added, changed);
        return changed ? basic_patricia_map(std::move(root), count_ + (added ? 1 : 0)) : *this;
    }

    [[nodiscard]] basic_patricia_map remove(Key key) const {
        bool changed = false;
        auto root = remove_node(root_, encode(key), changed);
        return changed ? basic_patricia_map(std::move(root), count_ - 1) : *this;
    }

    [[nodiscard]] basic_patricia_map union_with(const basic_patricia_map& other) const {
        auto root = union_nodes(root_, other.root_);
        if (root.get() == root_.get()) return *this;
        const auto count = root ? root->size : 0;
        return basic_patricia_map(std::move(root), count);
    }
    template<class Combine>
    [[nodiscard]] basic_patricia_map union_with(const basic_patricia_map& other, Combine combine) const {
        auto root = union_nodes_with(root_, other.root_, combine);
        if (root.get() == root_.get()) return *this;
        const auto count = root ? root->size : 0;
        return basic_patricia_map(std::move(root), count);
    }

    [[nodiscard]] basic_patricia_map intersect_with(const basic_patricia_map& other) const {
        auto root = intersect_nodes(root_, other.root_);
        if (root.get() == root_.get()) return *this;
        const auto count = root ? root->size : 0;
        return basic_patricia_map(std::move(root), count);
    }
    template<class Combine>
    [[nodiscard]] basic_patricia_map intersect_with(const basic_patricia_map& other, Combine combine) const {
        auto root = intersect_nodes_with(root_, other.root_, combine);
        if (root.get() == root_.get()) return *this;
        const auto count = root ? root->size : 0;
        return basic_patricia_map(std::move(root), count);
    }

    [[nodiscard]] basic_patricia_map except_with(const basic_patricia_map& other) const {
        auto root = except_nodes(root_, other.root_);
        if (root.get() == root_.get()) return *this;
        const auto count = root ? root->size : 0;
        return basic_patricia_map(std::move(root), count);
    }

    [[nodiscard]] std::vector<value_type> to_vector() const {
        std::vector<value_type> result; result.reserve(count_); append(root_, result); return result;
    }

private:
    friend class basic_patricia_map_cursor<Key, T>;
    friend class basic_patricia_set_cursor<Key>;
    friend class basic_patricia_set<Key>;

    basic_patricia_map(node_ptr root, std::size_t count) : root_(std::move(root)), count_(count) {}
    static constexpr path_type encode(Key key) noexcept {
        constexpr auto sign = path_type{1} << (sizeof(Key) * 8 - 1);
        return static_cast<path_type>(key) ^ sign;
    }
    static constexpr path_type prefix_of(path_type path, path_type mask) noexcept {
        return path & ~static_cast<path_type>((mask << 1) - 1);
    }
    static node_ptr make_leaf(path_type path, Key key, const T& value) { return std::make_shared<leaf_node>(path, key, value); }
    static node_ptr make_branch(path_type prefix, path_type mask, node_ptr left, node_ptr right) {
        return std::make_shared<branch_node>(prefix, mask, std::move(left), std::move(right));
    }
    static node_ptr join(path_type lp, node_ptr left, path_type rp, node_ptr right) {
        const auto mask = std::bit_floor(static_cast<path_type>(lp ^ rp));
        const auto prefix = prefix_of(lp, mask);
        return (lp & mask) == 0
            ? make_branch(prefix, mask, std::move(left), std::move(right))
            : make_branch(prefix, mask, std::move(right), std::move(left));
    }

    static node_ptr insert(node_ptr current, path_type path, Key key, const T& value, bool prefer_existing, bool& added, bool& changed) {
        if (!current) { added = changed = true; return make_leaf(path, key, value); }
        if (current->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(current.get());
            if (leaf->path == path) {
                added = false;
                if (prefer_existing || same_value(leaf->value, value)) { changed = false; return current; }
                changed = true; return make_leaf(path, leaf->key, value);
            }
            added = changed = true; return join(leaf->path, current, path, make_leaf(path, key, value));
        }
        const auto* branch = static_cast<const branch_node*>(current.get());
        if (prefix_of(path, branch->mask) != branch->prefix) {
            added = changed = true; return join(branch->prefix, current, path, make_leaf(path, key, value));
        }
        node_ptr child;
        if ((path & branch->mask) == 0) {
            child = insert(branch->left, path, key, value, prefer_existing, added, changed);
            return changed ? make_branch(branch->prefix, branch->mask, std::move(child), branch->right) : current;
        }
        child = insert(branch->right, path, key, value, prefer_existing, added, changed);
        return changed ? make_branch(branch->prefix, branch->mask, branch->left, std::move(child)) : current;
    }

    static node_ptr remove_node(node_ptr current, path_type path, bool& changed) {
        if (!current) { changed = false; return {}; }
        if (current->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(current.get());
            changed = leaf->path == path; return changed ? node_ptr{} : current;
        }
        const auto* branch = static_cast<const branch_node*>(current.get());
        if (prefix_of(path, branch->mask) != branch->prefix) { changed = false; return current; }
        if ((path & branch->mask) == 0) {
            auto child = remove_node(branch->left, path, changed);
            if (!changed) return current;
            return child ? make_branch(branch->prefix, branch->mask, std::move(child), branch->right) : branch->right;
        }
        auto child = remove_node(branch->right, path, changed);
        if (!changed) return current;
        return child ? make_branch(branch->prefix, branch->mask, branch->left, std::move(child)) : branch->left;
    }

    static node_ptr rebuild_branch(
        const node_ptr& original, path_type prefix, path_type mask, node_ptr left, node_ptr right) {
        if (original && !original->is_leaf()) {
            const auto* branch = static_cast<const branch_node*>(original.get());
            if (branch->left.get() == left.get() && branch->right.get() == right.get()) return original;
        }
        return make_branch(prefix, mask, std::move(left), std::move(right));
    }

    static node_ptr collapse_reusing(
        const node_ptr& original, path_type prefix, path_type mask, node_ptr left, node_ptr right) {
        if (!left) return right;
        if (!right) return left;
        return rebuild_branch(original, prefix, mask, std::move(left), std::move(right));
    }

    template<class Combine>
    static node_ptr union_nodes_with(const node_ptr& left, const node_ptr& right, Combine& combine) {
        if (!left) return right;
        if (!right) return left;
        if (left->is_leaf() && right->is_leaf()) {
            const auto* l = static_cast<const leaf_node*>(left.get());
            const auto* r = static_cast<const leaf_node*>(right.get());
            if (l->path != r->path) return join(l->path, left, r->path, right);
            T value = combine(l->key, l->value, r->value);
            if (same_value(value, l->value)) return left;
            if (same_value(value, r->value)) return right;
            return make_leaf(l->path, l->key, value);
        }
        if (left->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(left.get());
            const auto existing = find(right, leaf->path);
            T value = existing
                ? combine(leaf->key, leaf->value, static_cast<const leaf_node*>(existing.get())->value)
                : leaf->value;
            bool added{}, changed{};
            return insert(right, leaf->path, leaf->key, value, false, added, changed);
        }
        if (right->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(right.get());
            const auto existing = find(left, leaf->path);
            T value = existing
                ? combine(leaf->key, static_cast<const leaf_node*>(existing.get())->value, leaf->value)
                : leaf->value;
            bool added{}, changed{};
            return insert(left, leaf->path, leaf->key, value, false, added, changed);
        }
        const auto* l = static_cast<const branch_node*>(left.get());
        const auto* r = static_cast<const branch_node*>(right.get());
        if (l->mask == r->mask && l->prefix == r->prefix) {
            return rebuild_branch(left, l->prefix, l->mask,
                union_nodes_with(l->left, r->left, combine),
                union_nodes_with(l->right, r->right, combine));
        }
        if (l->mask > r->mask && prefix_of(r->prefix, l->mask) == l->prefix) {
            return (r->prefix & l->mask) == 0
                ? rebuild_branch(left, l->prefix, l->mask,
                    union_nodes_with(l->left, right, combine), l->right)
                : rebuild_branch(left, l->prefix, l->mask,
                    l->left, union_nodes_with(l->right, right, combine));
        }
        if (r->mask > l->mask && prefix_of(l->prefix, r->mask) == r->prefix) {
            return (l->prefix & r->mask) == 0
                ? rebuild_branch(right, r->prefix, r->mask,
                    union_nodes_with(left, r->left, combine), r->right)
                : rebuild_branch(right, r->prefix, r->mask,
                    r->left, union_nodes_with(left, r->right, combine));
        }
        return join(l->prefix, left, r->prefix, right);
    }

    static node_ptr union_nodes(const node_ptr& left, const node_ptr& right) {
        if (!left) return right;
        if (!right || left.get() == right.get()) return left;
        if (left->is_leaf() && right->is_leaf()) {
            const auto* l = static_cast<const leaf_node*>(left.get());
            const auto* r = static_cast<const leaf_node*>(right.get());
            if (l->path != r->path) return join(l->path, left, r->path, right);
            return same_value(l->value, r->value) ? left : right;
        }
        if (left->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(left.get()); bool a{}, c{};
            return insert(right, leaf->path, leaf->key, leaf->value, true, a, c);
        }
        if (right->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(right.get()); bool a{}, c{};
            return insert(left, leaf->path, leaf->key, leaf->value, false, a, c);
        }
        const auto* l = static_cast<const branch_node*>(left.get());
        const auto* r = static_cast<const branch_node*>(right.get());
        if (l->mask == r->mask && l->prefix == r->prefix)
            return rebuild_branch(left, l->prefix, l->mask,
                union_nodes(l->left, r->left), union_nodes(l->right, r->right));
        if (l->mask > r->mask && prefix_of(r->prefix, l->mask) == l->prefix)
            return (r->prefix & l->mask) == 0
                ? rebuild_branch(left, l->prefix, l->mask, union_nodes(l->left, right), l->right)
                : rebuild_branch(left, l->prefix, l->mask, l->left, union_nodes(l->right, right));
        if (r->mask > l->mask && prefix_of(l->prefix, r->mask) == r->prefix)
            return (l->prefix & r->mask) == 0
                ? rebuild_branch(right, r->prefix, r->mask, union_nodes(left, r->left), r->right)
                : rebuild_branch(right, r->prefix, r->mask, r->left, union_nodes(left, r->right));
        return join(l->prefix, left, r->prefix, right);
    }

    static node_ptr find(const node_ptr& node, path_type path) {
        auto current = node;
        while (current && !current->is_leaf()) {
            const auto* b = static_cast<const branch_node*>(current.get());
            if (prefix_of(path, b->mask) != b->prefix) return {};
            current = (path & b->mask) == 0 ? b->left : b->right;
        }
        return current && static_cast<const leaf_node*>(current.get())->path == path ? current : node_ptr{};
    }
    template<class Combine>
    static node_ptr intersect_nodes_with(const node_ptr& left, const node_ptr& right, Combine& combine) {
        if (!left || !right) return {};
        if (left->is_leaf() || right->is_leaf()) {
            const auto left_leaf = left->is_leaf()
                ? left
                : find(left, static_cast<const leaf_node*>(right.get())->path);
            const auto right_leaf = right->is_leaf()
                ? right
                : find(right, static_cast<const leaf_node*>(left.get())->path);
            if (!left_leaf || !right_leaf) return {};
            const auto* l = static_cast<const leaf_node*>(left_leaf.get());
            const auto* r = static_cast<const leaf_node*>(right_leaf.get());
            if (l->path != r->path) return {};
            T value = combine(l->key, l->value, r->value);
            if (same_value(value, l->value)) return left_leaf;
            if (same_value(value, r->value)) return right_leaf;
            return make_leaf(l->path, l->key, value);
        }
        const auto* l = static_cast<const branch_node*>(left.get());
        const auto* r = static_cast<const branch_node*>(right.get());
        if (l->mask == r->mask && l->prefix == r->prefix) {
            return collapse_reusing(left, l->prefix, l->mask,
                intersect_nodes_with(l->left, r->left, combine),
                intersect_nodes_with(l->right, r->right, combine));
        }
        if (l->mask > r->mask && prefix_of(r->prefix, l->mask) == l->prefix) {
            return (r->prefix & l->mask) == 0
                ? intersect_nodes_with(l->left, right, combine)
                : intersect_nodes_with(l->right, right, combine);
        }
        if (r->mask > l->mask && prefix_of(l->prefix, r->mask) == r->prefix) {
            return (l->prefix & r->mask) == 0
                ? intersect_nodes_with(left, r->left, combine)
                : intersect_nodes_with(left, r->right, combine);
        }
        return {};
    }
    static node_ptr intersect_nodes(const node_ptr& left, const node_ptr& right) {
        if (!left || !right) return {};
        if (left.get() == right.get()) return left;
        if (left->is_leaf()) return find(right, static_cast<const leaf_node*>(left.get())->path) ? left : node_ptr{};
        if (right->is_leaf()) return find(left, static_cast<const leaf_node*>(right.get())->path);
        const auto* l = static_cast<const branch_node*>(left.get()); const auto* r = static_cast<const branch_node*>(right.get());
        if (l->mask == r->mask && l->prefix == r->prefix)
            return collapse_reusing(left, l->prefix, l->mask,
                intersect_nodes(l->left, r->left), intersect_nodes(l->right, r->right));
        if (l->mask > r->mask && prefix_of(r->prefix, l->mask) == l->prefix) return (r->prefix & l->mask) == 0 ? intersect_nodes(l->left, right) : intersect_nodes(l->right, right);
        if (r->mask > l->mask && prefix_of(l->prefix, r->mask) == r->prefix) return (l->prefix & r->mask) == 0 ? intersect_nodes(left, r->left) : intersect_nodes(left, r->right);
        return {};
    }
    static node_ptr except_nodes(const node_ptr& left, const node_ptr& right) {
        if (!left || !right) return left;
        if (left.get() == right.get()) return {};
        if (left->is_leaf()) return find(right, static_cast<const leaf_node*>(left.get())->path) ? node_ptr{} : left;
        if (right->is_leaf()) { bool changed{}; return remove_node(left, static_cast<const leaf_node*>(right.get())->path, changed); }
        const auto* l = static_cast<const branch_node*>(left.get()); const auto* r = static_cast<const branch_node*>(right.get());
        if (l->mask == r->mask && l->prefix == r->prefix)
            return collapse_reusing(left, l->prefix, l->mask,
                except_nodes(l->left, r->left), except_nodes(l->right, r->right));
        if (l->mask > r->mask && prefix_of(r->prefix, l->mask) == l->prefix)
            return (r->prefix & l->mask) == 0
                ? collapse_reusing(left, l->prefix, l->mask, except_nodes(l->left, right), l->right)
                : collapse_reusing(left, l->prefix, l->mask, l->left, except_nodes(l->right, right));
        if (r->mask > l->mask && prefix_of(l->prefix, r->mask) == r->prefix) return (l->prefix & r->mask) == 0 ? except_nodes(left, r->left) : except_nodes(left, r->right);
        return left;
    }

    [[nodiscard]] const leaf_node* entry_at_node(std::size_t index) const noexcept {
        if (index >= count_) return nullptr;
        auto current = root_.get();
        while (!current->is_leaf()) {
            const auto* branch = static_cast<const branch_node*>(current);
            if (index < branch->left->size) {
                current = branch->left.get();
            } else {
                index -= branch->left->size;
                current = branch->right.get();
            }
        }
        return static_cast<const leaf_node*>(current);
    }

    [[nodiscard]] std::pair<std::size_t, bool> lower_bound_rank(Key key) const noexcept {
        const auto path = encode(key);
        std::size_t rank = 0;
        auto current = root_.get();
        while (current != nullptr && !current->is_leaf()) {
            const auto* branch = static_cast<const branch_node*>(current);
            if (prefix_of(path, branch->mask) != branch->prefix) {
                return path < branch->prefix
                    ? std::pair<std::size_t, bool>{rank, false}
                    : std::pair<std::size_t, bool>{rank + branch->size, false};
            }
            if ((path & branch->mask) == 0) {
                current = branch->left.get();
            } else {
                rank += branch->left->size;
                current = branch->right.get();
            }
        }
        if (current == nullptr) return {rank, false};
        const auto* leaf = static_cast<const leaf_node*>(current);
        if (leaf->path == path) return {rank, true};
        return {rank + (leaf->path < path ? 1u : 0u), false};
    }

    static void append(const node_ptr& node, std::vector<value_type>& output) {
        if (!node) return;
        if (node->is_leaf()) {
            const auto* leaf = static_cast<const leaf_node*>(node.get());
            output.emplace_back(leaf->key, leaf->value);
            return;
        }
        const auto* branch = static_cast<const branch_node*>(node.get());
        append(branch->left, output);
        append(branch->right, output);
    }

    static bool same_value(const T& left, const T& right) {
        return std::addressof(left) == std::addressof(right) || left == right;
    }

    node_ptr root_;
    std::size_t count_ = 0;
};

/// Immutable root-plus-rank gap cursor over a signed Patricia map.
///
/// The cursor owns its exact immutable map version. Rank movement is O(1), while peeking,
/// ordered seek, and edits are O(key width). Borrowed entry views are available only from
/// lvalue cursors so their references cannot outlive a temporary cursor's retained root.
template<class Key, class T>
class basic_patricia_map_cursor final {
public:
    struct entry_view final {
        const Key& key;
        const T& value;
    };

    basic_patricia_map_cursor() = delete;

    [[nodiscard]] std::size_t count() const noexcept { return map_.size(); }
    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == count(); }

    [[nodiscard]] std::optional<entry_view> peek_previous() const & noexcept {
        if (position_ == 0) return std::nullopt;
        return view_at(position_ - 1);
    }
    [[nodiscard]] std::optional<entry_view> peek_previous() const && = delete;

    [[nodiscard]] std::optional<entry_view> peek_next() const & noexcept {
        return view_at(position_);
    }
    [[nodiscard]] std::optional<entry_view> peek_next() const && = delete;

    [[nodiscard]] basic_patricia_map_cursor move_previous() const {
        if (is_at_start()) throw std::logic_error("Patricia map cursor is already at the start");
        return basic_patricia_map_cursor{map_, position_ - 1};
    }

    [[nodiscard]] basic_patricia_map_cursor move_next() const {
        if (is_at_end()) throw std::logic_error("Patricia map cursor is already at the end");
        return basic_patricia_map_cursor{map_, position_ + 1};
    }

    [[nodiscard]] basic_patricia_map_cursor seek(std::size_t position) const {
        if (position > count()) throw std::out_of_range("Patricia map cursor position is outside the map bounds");
        return position == position_ ? *this : basic_patricia_map_cursor{map_, position};
    }

    /// Strictly inserts a missing key at the current lower-bound gap.
    [[nodiscard]] basic_patricia_map_cursor insert(Key key, const T& value) const {
        const auto [expected, found] = map_.lower_bound_rank(key);
        if (found) throw std::invalid_argument("Patricia map cursor cannot insert a duplicate key");
        ensure_current_gap(expected);
        return basic_patricia_map_cursor{map_.set_item(key, value), position_ + 1};
    }

    /// Updates an exact next entry or inserts at a missing lower-bound gap. Named for the map
    /// operation it delegates to, matching the other C++ cursors that expose set_item.
    [[nodiscard]] basic_patricia_map_cursor set_item(Key key, const T& value) const {
        const auto [expected, found] = map_.lower_bound_rank(key);
        ensure_current_gap(expected);
        auto map = map_.set_item(key, value);
        if (map.shares_root_with(map_)) return *this;
        return basic_patricia_map_cursor{std::move(map), position_ + (found ? 0u : 1u)};
    }

    /// Replaces the next entry's value while retaining its key and this gap.
    [[nodiscard]] basic_patricia_map_cursor set_next_value(const T& value) const {
        const auto next = peek_next();
        if (!next) throw std::logic_error("Patricia map cursor has no next entry");
        auto map = map_.set_item(next->key, value);
        return map.shares_root_with(map_)
            ? *this
            : basic_patricia_map_cursor{std::move(map), position_};
    }

    [[nodiscard]] basic_patricia_map_cursor delete_previous() const {
        const auto previous = peek_previous();
        if (!previous) throw std::logic_error("Patricia map cursor has no previous entry");
        return basic_patricia_map_cursor{map_.remove(previous->key), position_ - 1};
    }

    [[nodiscard]] basic_patricia_map_cursor delete_next() const {
        const auto next = peek_next();
        if (!next) throw std::logic_error("Patricia map cursor has no next entry");
        return basic_patricia_map_cursor{map_.remove(next->key), position_};
    }

    [[nodiscard]] basic_patricia_map<Key, T> snapshot() const { return map_; }

private:
    friend class basic_patricia_map<Key, T>;

    basic_patricia_map_cursor(basic_patricia_map<Key, T> map, std::size_t position)
        : map_(std::move(map)), position_(position) {}

    [[nodiscard]] std::optional<entry_view> view_at(std::size_t index) const noexcept {
        const auto* leaf = map_.entry_at_node(index);
        if (leaf == nullptr) return std::nullopt;
        return entry_view{leaf->key, leaf->value};
    }

    void ensure_current_gap(std::size_t expected) const {
        if (expected != position_) {
            throw std::invalid_argument("Patricia key does not belong at the current cursor gap");
        }
    }

    basic_patricia_map<Key, T> map_;
    std::size_t position_;
};

template<class Key, class T>
struct basic_patricia_map_cursor_search_result final {
    basic_patricia_map_cursor<Key, T> cursor;
    bool found;
};

template<class Key, class T>
[[nodiscard]] auto basic_patricia_map<Key, T>::get_cursor(std::size_t position) const -> cursor_type {
    if (position > count_) throw std::out_of_range("Patricia map cursor position is outside the map bounds");
    return cursor_type{*this, position};
}

template<class Key, class T>
[[nodiscard]] auto basic_patricia_map<Key, T>::get_cursor_at_end() const -> cursor_type {
    return cursor_type{*this, count_};
}

template<class Key, class T>
[[nodiscard]] auto basic_patricia_map<Key, T>::get_cursor_lower_bound(Key key) const -> cursor_type {
    return cursor_type{*this, lower_bound_rank(key).first};
}

template<class Key, class T>
[[nodiscard]] auto basic_patricia_map<Key, T>::get_cursor_upper_bound(Key key) const -> cursor_type {
    const auto [position, found] = lower_bound_rank(key);
    return cursor_type{*this, position + (found ? 1u : 0u)};
}

template<class Key, class T>
[[nodiscard]] auto basic_patricia_map<Key, T>::get_cursor_at_key(Key key) const -> cursor_search_result {
    const auto [position, found] = lower_bound_rank(key);
    return cursor_search_result{cursor_type{*this, position}, found};
}

template<class T> using persistent_int_map = basic_patricia_map<std::int32_t, T>;
template<class T> using persistent_long_map = basic_patricia_map<std::int64_t, T>;
template<class T> using persistent_int_map_cursor = basic_patricia_map_cursor<std::int32_t, T>;
template<class T> using persistent_long_map_cursor = basic_patricia_map_cursor<std::int64_t, T>;

struct patricia_unit { friend constexpr bool operator==(patricia_unit, patricia_unit) noexcept = default; };
template<class Key>
class basic_patricia_set {
    using map_type = basic_patricia_map<Key, patricia_unit>;
public:
    using cursor_type = basic_patricia_set_cursor<Key>;

    basic_patricia_set() = default;
    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }
    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }
    /// Reports whether both snapshots name the very same root, which is how an add or remove
    /// that was an exact no-op is distinguished from one that rebuilt an equal set.
    [[nodiscard]] bool shares_root_with(const basic_patricia_set& other) const noexcept { return map_.shares_root_with(other.map_); }
    [[nodiscard]] bool contains(Key value) const noexcept { return map_.contains_key(value); }
    [[nodiscard]] cursor_type get_cursor(std::size_t position = 0) const;
    [[nodiscard]] cursor_type get_cursor_at_end() const;
    [[nodiscard]] cursor_type get_cursor_lower_bound(Key value) const;
    [[nodiscard]] cursor_type get_cursor_upper_bound(Key value) const;
    [[nodiscard]] std::pair<cursor_type, bool> get_cursor_at_item(Key value) const;
    [[nodiscard]] basic_patricia_set add(Key value) const { return basic_patricia_set(map_.set_item(value, {})); }
    [[nodiscard]] basic_patricia_set remove(Key value) const { return basic_patricia_set(map_.remove(value)); }
    [[nodiscard]] basic_patricia_set union_with(const basic_patricia_set& other) const { return basic_patricia_set(map_.union_with(other.map_)); }
    [[nodiscard]] basic_patricia_set intersect_with(const basic_patricia_set& other) const { return basic_patricia_set(map_.intersect_with(other.map_)); }
    [[nodiscard]] basic_patricia_set except_with(const basic_patricia_set& other) const { return basic_patricia_set(map_.except_with(other.map_)); }
    [[nodiscard]] std::vector<Key> to_vector() const { std::vector<Key> result; for (const auto& entry : map_.to_vector()) result.push_back(entry.first); return result; }
private:
    friend class basic_patricia_set_cursor<Key>;
    explicit basic_patricia_set(map_type map) : map_(std::move(map)) {}
    map_type map_;
};

/// Immutable root-plus-rank gap cursor over a signed Patricia set.
template<class Key>
class basic_patricia_set_cursor final {
public:
    basic_patricia_set_cursor() = delete;

    [[nodiscard]] std::size_t count() const noexcept { return set_.size(); }
    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == count(); }

    [[nodiscard]] const Key* peek_previous() const & noexcept {
        if (position_ == 0) return nullptr;
        const auto* leaf = set_.map_.entry_at_node(position_ - 1);
        return leaf == nullptr ? nullptr : std::addressof(leaf->key);
    }
    [[nodiscard]] const Key* peek_previous() const && = delete;

    [[nodiscard]] const Key* peek_next() const & noexcept {
        const auto* leaf = set_.map_.entry_at_node(position_);
        return leaf == nullptr ? nullptr : std::addressof(leaf->key);
    }
    [[nodiscard]] const Key* peek_next() const && = delete;

    [[nodiscard]] basic_patricia_set_cursor move_previous() const {
        if (is_at_start()) throw std::logic_error("Patricia set cursor is already at the start");
        return basic_patricia_set_cursor{set_, position_ - 1};
    }

    [[nodiscard]] basic_patricia_set_cursor move_next() const {
        if (is_at_end()) throw std::logic_error("Patricia set cursor is already at the end");
        return basic_patricia_set_cursor{set_, position_ + 1};
    }

    [[nodiscard]] basic_patricia_set_cursor seek(std::size_t position) const {
        if (position > count()) throw std::out_of_range("Patricia set cursor position is outside the set bounds");
        return position == position_ ? *this : basic_patricia_set_cursor{set_, position};
    }

    /// Adds at the current lower-bound gap; an exact duplicate is a no-op.
    [[nodiscard]] basic_patricia_set_cursor insert(Key value) const {
        const auto [expected, found] = set_.map_.lower_bound_rank(value);
        ensure_current_gap(expected);
        return found
            ? *this
            : basic_patricia_set_cursor{set_.add(value), position_ + 1};
    }

    [[nodiscard]] basic_patricia_set_cursor delete_previous() const {
        const auto* previous = peek_previous();
        if (previous == nullptr) throw std::logic_error("Patricia set cursor has no previous item");
        return basic_patricia_set_cursor{set_.remove(*previous), position_ - 1};
    }

    [[nodiscard]] basic_patricia_set_cursor delete_next() const {
        const auto* next = peek_next();
        if (next == nullptr) throw std::logic_error("Patricia set cursor has no next item");
        return basic_patricia_set_cursor{set_.remove(*next), position_};
    }

    [[nodiscard]] basic_patricia_set<Key> snapshot() const { return set_; }

private:
    friend class basic_patricia_set<Key>;

    basic_patricia_set_cursor(basic_patricia_set<Key> set, std::size_t position)
        : set_(std::move(set)), position_(position) {}

    void ensure_current_gap(std::size_t expected) const {
        if (expected != position_) {
            throw std::invalid_argument("Patricia value does not belong at the current cursor gap");
        }
    }

    basic_patricia_set<Key> set_;
    std::size_t position_;
};

template<class Key>
[[nodiscard]] auto basic_patricia_set<Key>::get_cursor(std::size_t position) const -> cursor_type {
    if (position > size()) throw std::out_of_range("Patricia set cursor position is outside the set bounds");
    return cursor_type{*this, position};
}

template<class Key>
[[nodiscard]] auto basic_patricia_set<Key>::get_cursor_at_end() const -> cursor_type {
    return cursor_type{*this, size()};
}

template<class Key>
[[nodiscard]] auto basic_patricia_set<Key>::get_cursor_lower_bound(Key value) const -> cursor_type {
    return cursor_type{*this, map_.lower_bound_rank(value).first};
}

template<class Key>
[[nodiscard]] auto basic_patricia_set<Key>::get_cursor_upper_bound(Key value) const -> cursor_type {
    const auto [position, found] = map_.lower_bound_rank(value);
    return cursor_type{*this, position + (found ? 1u : 0u)};
}

template<class Key>
[[nodiscard]] auto basic_patricia_set<Key>::get_cursor_at_item(Key value) const -> std::pair<cursor_type, bool> {
    const auto [position, found] = map_.lower_bound_rank(value);
    return {cursor_type{*this, position}, found};
}

using persistent_int_set = basic_patricia_set<std::int32_t>;
using persistent_long_set = basic_patricia_set<std::int64_t>;
using persistent_int_set_cursor = basic_patricia_set_cursor<std::int32_t>;
using persistent_long_set_cursor = basic_patricia_set_cursor<std::int64_t>;

} // namespace tools::data_structures::hamt
