#pragma once

#include <bit>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

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
                if (prefer_existing || leaf->value == value) { changed = false; return current; }
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
            if (value == l->value) return left;
            if (value == r->value) return right;
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
            return l->value == r->value ? left : right;
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
            if (value == l->value) return left_leaf;
            if (value == r->value) return right_leaf;
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

    node_ptr root_;
    std::size_t count_ = 0;
};

template<class T> using persistent_int_map = basic_patricia_map<std::int32_t, T>;
template<class T> using persistent_long_map = basic_patricia_map<std::int64_t, T>;

struct patricia_unit { friend constexpr bool operator==(patricia_unit, patricia_unit) noexcept = default; };
template<class Key>
class basic_patricia_set {
    using map_type = basic_patricia_map<Key, patricia_unit>;
public:
    basic_patricia_set() = default;
    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }
    [[nodiscard]] bool contains(Key value) const noexcept { return map_.contains_key(value); }
    [[nodiscard]] basic_patricia_set add(Key value) const { return basic_patricia_set(map_.set_item(value, {})); }
    [[nodiscard]] basic_patricia_set remove(Key value) const { return basic_patricia_set(map_.remove(value)); }
    [[nodiscard]] basic_patricia_set union_with(const basic_patricia_set& other) const { return basic_patricia_set(map_.union_with(other.map_)); }
    [[nodiscard]] basic_patricia_set intersect_with(const basic_patricia_set& other) const { return basic_patricia_set(map_.intersect_with(other.map_)); }
    [[nodiscard]] basic_patricia_set except_with(const basic_patricia_set& other) const { return basic_patricia_set(map_.except_with(other.map_)); }
    [[nodiscard]] std::vector<Key> to_vector() const { std::vector<Key> result; for (const auto& entry : map_.to_vector()) result.push_back(entry.first); return result; }
private:
    explicit basic_patricia_set(map_type map) : map_(std::move(map)) {}
    map_type map_;
};
using persistent_int_set = basic_patricia_set<std::int32_t>;
using persistent_long_set = basic_patricia_set<std::int64_t>;

} // namespace tools::data_structures::hamt
