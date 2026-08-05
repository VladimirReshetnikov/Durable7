/// A fully branching persistent insertion-only connectivity forest with a first-connection query.
///
/// Vertices are the fixed integer universe `0 .. vertex_count - 1`. Each successful `link` performs
/// union by size *without* path compression and labels the one new root-parent edge with the child
/// `ancestral_connection_version`. A redundant (already connected) link still creates a child
/// history version, but shares the complete connectivity index.
///
/// The parent cells live sparsely in a `persistent_hash_map`. An absent cell denotes a singleton
/// root, so `create` is O(1) rather than O(vertex_count). Union by size limits every parent path to
/// O(log n) cells. `first_connected` compares the two current parent paths and takes the latest
/// edge-version strictly below their forest lowest common ancestor, so it answers "at which ancestor
/// version did these two vertices first become connected?" without ever searching the version
/// history; its cost is independent of history depth.
///
/// # Cost of the CHAMP path
///
/// Throughout this header w denotes the cost of one parent-cell lookup or insertion in the backing
/// `persistent_hash_map`. That substrate truncates the hash policy's result to 32 bits, consumes
/// five hash bits per trie level for at most seven levels, and parks keys sharing a full 32-bit hash
/// in a collision node whose bucket is scanned linearly. A collision bucket can therefore hold two
/// distinct keys, and under a colliding hash w would be bounded only *in expectation*.
///
/// This forest removes that caveat by pinning `ancestral_connection_vertex_hash`, which is a
/// bijection of the 32-bit word (see that type). Distinct vertices therefore never share a 32-bit
/// hash, no collision node ever holds two distinct vertices, and every trie path is at most seven
/// levels. w is O(1) *unconditionally*, not merely in expectation, so each O(w log n) bound stated
/// below is worst-case, as is the O(log n) union-by-size path factor. That factor is worst-case
/// logarithmic in component size rather than the inverse-Ackermann amortized bound of an ephemeral
/// linear-history union-find, which path compression would buy at the cost of persistence.
///
/// Instances and version tokens are immutable snapshots safe for concurrent readers. Deletion,
/// confluent merging, retroactive updates, and path compression are deliberately outside this type's
/// contract.

#pragma once

#include <durable7/hamt/persistent_hash_map.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace durable7::hamt {

/// The vertex hashing policy this forest pins into its parent-cell map.
///
/// The function is the MurmurHash3 32-bit finalizer applied to the vertex's two's-complement bit
/// pattern. Each of its five steps is a bijection of the 32-bit word: `h ^= h >> k` with `k > 0`
/// leaves the top `k` bits fixed and is undone by the same shape of expression, and multiplication
/// by an odd constant is invertible modulo 2^32 (both constants below are odd). Their composition is
/// therefore a bijection, so the map's truncation of this hash to 32 bits stays injective over the
/// whole `std::int32_t` vertex universe.
///
/// Injectivity is what makes the CHAMP factor w a worst-case rather than an expected constant, so
/// this policy is part of the type's complexity contract rather than a tuning detail. Avalanche is
/// the secondary benefit: it spreads clustered vertex numbering across the low-order trie levels the
/// map consumes first.
struct ancestral_connection_vertex_hash final {
    /// Hashes a vertex. O(1), and never collides with a different vertex.
    [[nodiscard]] std::size_t operator()(std::int32_t vertex) const noexcept {
        auto hash = static_cast<std::uint32_t>(vertex);
        hash = static_cast<std::uint32_t>(hash ^ (hash >> 16));
        hash = static_cast<std::uint32_t>(hash * 0x85eb'ca6bu);
        hash = static_cast<std::uint32_t>(hash ^ (hash >> 13));
        hash = static_cast<std::uint32_t>(hash * 0xc2b2'ae35u);
        hash = static_cast<std::uint32_t>(hash ^ (hash >> 16));
        return static_cast<std::size_t>(hash);
    }
};

/// Statistics returned by a complete connection-forest representation audit.
struct ancestral_connection_forest_statistics final {
    /// The fixed vertex-universe size.
    std::int32_t vertex_count = 0;
    /// The number of connected components, recounted from the parent forest.
    std::int32_t component_count = 0;
    /// The number of explicitly stored parent cells; absent cells are singleton roots.
    std::size_t stored_cell_count = 0;
    /// The longest parent path found, in edges. Bounded by `floor(log2(vertex_count))`.
    std::size_t maximum_parent_path_length = 0;
    /// The number of link events from the history root to the validated version.
    std::uint64_t history_depth = 0;

    [[nodiscard]] friend bool operator==(
        const ancestral_connection_forest_statistics&,
        const ancestral_connection_forest_statistics&) = default;
};

class persistent_ancestral_connection_forest;

/// Identifies one immutable version in a `persistent_ancestral_connection_forest` history tree.
///
/// A token is an ordinary copyable value that shares one immutable history node; copying is O(1) and
/// yields the same identity. Identity is *pointer* identity of that shared node, not structural
/// equality: `operator==` and the `std::hash` specialization both compare the node address, so two
/// versions at equal depths in sibling branches are never equal. Structural identity would be wrong
/// here, because a token records only its depth, its parent, and its history root — exactly the
/// fields on which sibling branches agree — so structural equality would silently merge branches
/// that `first_connected` must keep apart. This is the C++ equivalent of the reference identity the
/// managed contract relies on.
///
/// The parent chain contains history identities, not mutable connectivity state.
class ancestral_connection_version final {
private:
    struct node;
    using node_pointer = std::shared_ptr<node>;

    struct node final {
        node_pointer parent;
        /// The history root, or null when this node *is* the root. Storing the root as a nullable
        /// pointer rather than as a self-reference keeps `root()` O(1) without a `shared_ptr` cycle.
        node_pointer root;
        std::uint64_t depth;

        node(node_pointer parent_value, node_pointer root_value, std::uint64_t depth_value) noexcept
            : parent(std::move(parent_value)),
              root(std::move(root_value)),
              depth(depth_value) {
        }

        node(const node&) = delete;
        node& operator=(const node&) = delete;

        /// Releases the parent chain iteratively.
        ///
        /// A history is a linked list that can be hundreds of thousands of links deep, so letting
        /// the members' `shared_ptr` destructors recurse would overflow the stack. Each node is
        /// unlinked before it is released, so its own destructor finds nothing left to follow and
        /// the whole release is O(1) per node. A node another owner still holds ends the walk;
        /// that owner releases the remaining suffix the same way later.
        ~node() {
            root.reset();
            auto current = std::move(parent);
            while (current && current.use_count() == 1) {
                auto next = std::move(current->parent);
                current.reset();
                current = std::move(next);
            }
        }
    };

public:
    ancestral_connection_version(const ancestral_connection_version&) = default;
    ancestral_connection_version(ancestral_connection_version&&) noexcept = default;
    ancestral_connection_version& operator=(const ancestral_connection_version&) = default;
    ancestral_connection_version& operator=(ancestral_connection_version&&) noexcept = default;
    ~ancestral_connection_version() = default;

    /// This version's parent, or nothing at the history root. O(1).
    [[nodiscard]] std::optional<ancestral_connection_version> parent() const {
        if (!node_->parent) {
            return std::nullopt;
        }

        return ancestral_connection_version(node_->parent);
    }

    /// The number of link events from the history root. O(1).
    [[nodiscard]] std::uint64_t depth() const noexcept {
        return node_->depth;
    }

    /// The root identity of this version tree. O(1).
    [[nodiscard]] ancestral_connection_version root() const {
        return node_->root ? ancestral_connection_version(node_->root) : *this;
    }

    /// Whether two handles denote the very same version. O(1).
    [[nodiscard]] bool is_same_version(const ancestral_connection_version& other) const noexcept {
        return node_.get() == other.node_.get();
    }

    /// The address standing for this version's identity. For hashing and diagnostics only; it is
    /// stable while any handle to the version is alive.
    [[nodiscard]] const void* identity() const noexcept {
        return node_.get();
    }

    /// Whether two handles denote the very same version. O(1).
    [[nodiscard]] friend bool operator==(
        const ancestral_connection_version& left,
        const ancestral_connection_version& right) noexcept {
        return left.is_same_version(right);
    }

private:
    friend class persistent_ancestral_connection_forest;

    explicit ancestral_connection_version(node_pointer version_node) noexcept
        : node_(std::move(version_node)) {
    }

    [[nodiscard]] static ancestral_connection_version create_root() {
        return ancestral_connection_version(
            std::make_shared<node>(nullptr, nullptr, std::uint64_t{0}));
    }

    [[nodiscard]] static ancestral_connection_version create_child(
        const ancestral_connection_version& parent_version) {
        if (parent_version.node_->depth == (std::numeric_limits<std::uint64_t>::max)()) {
            throw std::overflow_error("The connection-forest history depth would overflow.");
        }

        auto root_node = parent_version.node_->root
            ? parent_version.node_->root
            : parent_version.node_;
        return ancestral_connection_version(std::make_shared<node>(
            parent_version.node_,
            std::move(root_node),
            parent_version.node_->depth + 1));
    }

    node_pointer node_;
};

/// A fully branching persistent insertion-only connectivity forest that reports the first ancestor
/// version in which two vertices became connected.
///
/// An ordinary copyable value: a copy is O(1) because it shares the same immutable cell trie and the
/// same history node, and a copy is indistinguishable from its source.
class persistent_ancestral_connection_forest final {
private:
    /// One sparse parent record.
    ///
    /// `size` is meaningful only for a root (`parent` equal to the vertex itself), and `joined_at`
    /// is present only on a non-root edge. An absent cell canonically means "singleton root of size
    /// one", which is what keeps `create` O(1) over a universe of any size.
    struct cell final {
        std::int32_t parent = 0;
        std::int32_t size = 0;
        std::optional<ancestral_connection_version> joined_at;

        [[nodiscard]] static cell make_root(std::int32_t vertex, std::int32_t size_value) {
            return cell{vertex, size_value, std::nullopt};
        }

        [[nodiscard]] static cell make_child(
            std::int32_t parent_vertex,
            ancestral_connection_version joined_at_version) {
            return cell{parent_vertex, 0, std::move(joined_at_version)};
        }

        [[nodiscard]] friend bool operator==(const cell& left, const cell& right) {
            return left.parent == right.parent
                && left.size == right.size
                && left.joined_at == right.joined_at;
        }
    };

    struct path_step final {
        std::int32_t vertex;
        /// Aims into the shared cell trie this forest retains, so it stays valid for the lifetime
        /// of the receiver.
        const ancestral_connection_version* joined_at;
    };

    using cell_map = persistent_hash_map<std::int32_t, cell, ancestral_connection_vertex_hash>;

public:
    using vertex_type = std::int32_t;
    using version_type = ancestral_connection_version;
    using statistics_type = ancestral_connection_forest_statistics;
    using size_type = std::size_t;

    persistent_ancestral_connection_forest(const persistent_ancestral_connection_forest&) = default;
    persistent_ancestral_connection_forest(persistent_ancestral_connection_forest&&) noexcept
        = default;
    persistent_ancestral_connection_forest& operator=(
        const persistent_ancestral_connection_forest&) = default;
    persistent_ancestral_connection_forest& operator=(
        persistent_ancestral_connection_forest&&) noexcept = default;
    ~persistent_ancestral_connection_forest() = default;

    /// An edgeless root version over vertices `0 .. vertex_count - 1`. O(1).
    ///
    /// Every vertex starts as its own component. Because a singleton is represented by the *absence*
    /// of a cell, construction costs the same for a universe of two vertices and for one of
    /// `INT32_MAX`. There is deliberately no default constructor: every forest owns a distinct
    /// history root identity, and a defaulted one would suggest that two of them share a history.
    ///
    /// Raises `std::invalid_argument` when `vertex_count` is negative.
    [[nodiscard]] static persistent_ancestral_connection_forest create(std::int32_t vertex_count) {
        if (vertex_count < 0) {
            throw std::invalid_argument("Vertex count cannot be negative.");
        }

        return persistent_ancestral_connection_forest(
            vertex_count,
            vertex_count,
            cell_map{},
            ancestral_connection_version::create_root());
    }

    /// The fixed number of vertices in this forest. O(1).
    [[nodiscard]] std::int32_t vertex_count() const noexcept {
        return vertex_count_;
    }

    /// The number of connected components in this version. O(1).
    [[nodiscard]] std::int32_t component_count() const noexcept {
        return component_count_;
    }

    /// The identity token for this history version. O(1).
    [[nodiscard]] const ancestral_connection_version& version() const noexcept {
        return version_;
    }

    /// The current union-find representative of a vertex. O(w log n) worst-case.
    ///
    /// The representative is selected by deterministic union-by-size history and is an
    /// implementation artifact: sibling versions need not agree on it.
    ///
    /// Raises `std::out_of_range` when the vertex lies outside the fixed universe.
    [[nodiscard]] std::int32_t find(std::int32_t vertex) const {
        check_vertex(vertex);
        return find_root(vertex);
    }

    /// Whether two vertices are connected, that is, whether they have the same current root.
    /// O(w log n) worst-case.
    ///
    /// Raises `std::out_of_range` when either vertex lies outside the fixed universe.
    [[nodiscard]] bool connected(std::int32_t left, std::int32_t right) const {
        check_vertices(left, right);
        return find_root(left) == find_root(right);
    }

    /// The number of vertices in a vertex's connected component. O(w log n) worst-case.
    ///
    /// An isolated vertex is still a singleton root and reports one. The answer is the union-by-size
    /// count cached at the component's current root, so the walk reads the structure without
    /// compressing it; this forest deliberately has no path compression.
    ///
    /// Raises `std::out_of_range` when the vertex lies outside the fixed universe.
    [[nodiscard]] std::int32_t component_size(std::int32_t vertex) const {
        check_vertex(vertex);
        return size_of(find_root(vertex));
    }

    /// A child history version containing an undirected connection between two vertices.
    ///
    /// The returned version is always distinct. When the endpoints were already connected — which
    /// includes linking a vertex to itself — its connectivity cells are shared unchanged, which
    /// `shares_connectivity_root_with` can confirm; otherwise exactly one union-by-size root edge is
    /// added and labelled with the child version. On a size tie the first endpoint's root stays the
    /// representative, making the result deterministic without changing the logarithmic-height
    /// proof.
    ///
    /// O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1)
    /// space when redundant. The receiver is left untouched, including on failure.
    ///
    /// Raises `std::out_of_range` when either endpoint lies outside the fixed universe, before
    /// anything is published. Raises `std::overflow_error` when the history depth would exceed
    /// `UINT64_MAX`, or when the combined component size would exceed `INT32_MAX`.
    [[nodiscard]] persistent_ancestral_connection_forest link(
        std::int32_t left,
        std::int32_t right) const {
        check_vertices(left, right);
        auto left_root = find_root(left);
        auto right_root = find_root(right);
        auto child_version = ancestral_connection_version::create_child(version_);

        if (left_root == right_root) {
            return persistent_ancestral_connection_forest(
                vertex_count_,
                component_count_,
                cells_,
                std::move(child_version));
        }

        auto left_size = size_of(left_root);
        auto right_size = size_of(right_root);
        if (left_size < right_size) {
            std::swap(left_root, right_root);
            std::swap(left_size, right_size);
        }

        auto cells = cells_
            .set_item(left_root, cell::make_root(left_root, checked_sum(left_size, right_size)))
            .set_item(right_root, cell::make_child(left_root, child_version));
        return persistent_ancestral_connection_forest(
            vertex_count_,
            component_count_ - 1,
            std::move(cells),
            std::move(child_version));
    }

    /// The earliest version on this version's root-to-current history in which two vertices were
    /// connected, or nothing when they are disconnected in this version.
    ///
    /// A vertex is connected to itself at the history root. The answer is computed from the two
    /// current parent paths as the latest union-edge version strictly below their forest lowest
    /// common ancestor; the version history is never searched. The result is never a version on
    /// another branch, and a later merge of the pair's component into a bigger one never replaces
    /// the pair's earlier witness. O(w log n) worst-case time and O(log n) temporary path space,
    /// independent of history depth.
    ///
    /// The two failure modes stay distinguishable: an out-of-universe vertex raises
    /// `std::out_of_range`, while a merely disconnected pair returns an empty `std::optional`.
    [[nodiscard]] std::optional<ancestral_connection_version> first_connected(
        std::int32_t left,
        std::int32_t right) const {
        check_vertices(left, right);
        if (left == right) {
            return version_.root();
        }

        const auto left_path = build_path(left);
        const auto right_path = build_path(right);
        if (left_path.back().vertex != right_path.back().vertex) {
            return std::nullopt;
        }

        // Delete the common rootward suffix. The remaining steps are precisely the two paths
        // strictly below the forest LCA, and every one of those steps owns a successful-union tag.
        auto left_length = left_path.size();
        auto right_length = right_path.size();
        while (left_length > 0
               && right_length > 0
               && left_path[left_length - 1].vertex == right_path[right_length - 1].vertex) {
            --left_length;
            --right_length;
        }

        const ancestral_connection_version* latest = nullptr;
        for (std::size_t index = 0; index < left_length; ++index) {
            latest = later(latest, left_path[index].joined_at);
        }
        for (std::size_t index = 0; index < right_length; ++index) {
            latest = later(latest, right_path[index].joined_at);
        }

        // Distinct connected vertices have at least one edge strictly below their LCA.
        if (latest == nullptr) {
            throw std::logic_error("A connected pair has no union-edge witness.");
        }

        return *latest;
    }

    /// Whether two versions retain the exact same sparse connectivity index, so neither can observe
    /// a union recorded in the other. A representation test, not an equality test. O(1).
    [[nodiscard]] bool shares_connectivity_root_with(
        const persistent_ancestral_connection_forest& other) const noexcept {
        return cells_.shares_root_with(other.cells_);
    }

    /// Audits sparse-cell canonicality, current root sizes, the resulting union-by-size height
    /// ceiling, union-tag ancestry and order, and the cached component count.
    ///
    /// O(H + m w log n), where H is the history depth and m is the explicit-cell count. A defensive
    /// audit; ordinary operations maintain these invariants. Raises `std::logic_error` naming the
    /// first violated invariant.
    [[nodiscard]] ancestral_connection_forest_statistics validate_structure() const {
        if (vertex_count_ < 0 || component_count_ < 0 || component_count_ > vertex_count_) {
            throw std::logic_error("Connection-forest counts are invalid.");
        }

        std::unordered_set<const void*> ancestors;
        const auto history_root = version_.root();
        auto expected_depth = version_.depth();
        auto reached_root_depth = false;
        for (auto current = std::optional<ancestral_connection_version>(version_);
             current.has_value();
             current = current->parent()) {
            if (reached_root_depth
                || current->depth() != expected_depth
                || !current->root().is_same_version(history_root)) {
                throw std::logic_error("The history-token parent chain is inconsistent.");
            }

            ancestors.insert(current->identity());
            if (expected_depth == 0) {
                reached_root_depth = true;
            } else {
                --expected_depth;
            }
        }
        if (!reached_root_depth
            || ancestors.find(history_root.identity()) == ancestors.end()
            || history_root.parent().has_value()
            || history_root.depth() != 0) {
            throw std::logic_error("The history root is inconsistent.");
        }

        for (const auto& entry : cells_) {
            const auto vertex = entry.first;
            const auto& current_cell = entry.second;
            if (!is_vertex(vertex) || !is_vertex(current_cell.parent)) {
                throw std::logic_error(
                    "A sparse parent cell addresses a vertex outside the universe.");
            }

            if (current_cell.parent == vertex) {
                if (current_cell.size <= 1 || current_cell.joined_at.has_value()) {
                    throw std::logic_error(
                        "A stored root cell is not a canonical non-singleton root.");
                }
            } else {
                const auto tagged = current_cell.joined_at.has_value()
                    && ancestors.find(current_cell.joined_at->identity()) != ancestors.end();
                if (current_cell.size != 0
                    || !tagged
                    || !cells_.contains_key(current_cell.parent)) {
                    throw std::logic_error(
                        "A non-root cell has an invalid size or union-version tag.");
                }
            }
        }

        std::unordered_map<std::int32_t, std::int32_t> component_sizes;
        std::size_t maximum_parent_path_length = 0;
        const auto maximum_height = vertex_count_ <= 1
            ? std::size_t{0}
            : static_cast<std::size_t>(
                std::bit_width(static_cast<std::uint32_t>(vertex_count_)) - 1);
        for (const auto& entry : cells_) {
            auto current = entry.first;
            std::size_t height = 0;
            std::uint64_t previous_depth = 0;
            auto has_previous_depth = false;
            while (const auto* walked = cells_.try_get(current)) {
                if (walked->parent == current) {
                    break;
                }
                if (!walked->joined_at.has_value()
                    || (has_previous_depth && walked->joined_at->depth() <= previous_depth)
                    || ancestors.find(walked->joined_at->identity()) == ancestors.end()) {
                    throw std::logic_error(
                        "Union-edge versions do not strictly increase toward the root.");
                }

                previous_depth = walked->joined_at->depth();
                has_previous_depth = true;
                current = walked->parent;
                if (++height > maximum_height) {
                    throw std::logic_error(
                        "A parent path exceeds the union-by-size height bound.");
                }
            }

            maximum_parent_path_length = (std::max)(maximum_parent_path_length, height);
            ++component_sizes[current];
        }

        const auto stored_cell_count = cells_.count();
        const auto absent_singletons = static_cast<std::int64_t>(vertex_count_)
            - static_cast<std::int64_t>(stored_cell_count);
        if (absent_singletons < 0
            || absent_singletons + static_cast<std::int64_t>(component_sizes.size())
                != static_cast<std::int64_t>(component_count_)) {
            throw std::logic_error("The cached component count disagrees with the parent forest.");
        }
        for (const auto& entry : component_sizes) {
            if (size_of(entry.first) != entry.second) {
                throw std::logic_error("A root's cached union size is incorrect.");
            }
        }

        return ancestral_connection_forest_statistics{
            vertex_count_,
            component_count_,
            stored_cell_count,
            maximum_parent_path_length,
            version_.depth()};
    }

private:
    persistent_ancestral_connection_forest(
        std::int32_t vertex_count_value,
        std::int32_t component_count_value,
        cell_map cells,
        ancestral_connection_version version_value) noexcept
        : vertex_count_(vertex_count_value),
          component_count_(component_count_value),
          cells_(std::move(cells)),
          version_(std::move(version_value)) {
    }

    /// Walks parent pointers to the component root. Iterative rather than recursive: a parent path
    /// is short, but the same discipline as the version-chain walk keeps stack use O(1).
    [[nodiscard]] std::int32_t find_root(std::int32_t vertex) const {
        auto current = vertex;
        while (true) {
            const auto parent = parent_of(current);
            if (parent == current) {
                return current;
            }
            current = parent;
        }
    }

    [[nodiscard]] std::vector<path_step> build_path(std::int32_t vertex) const {
        std::vector<path_step> path;
        auto current = vertex;
        while (true) {
            const auto* current_cell = cells_.try_get(current);
            const auto parent = current_cell == nullptr ? current : current_cell->parent;
            path.push_back(path_step{
                current,
                current_cell != nullptr && current_cell->joined_at.has_value()
                    ? std::addressof(*current_cell->joined_at)
                    : nullptr});
            if (parent == current) {
                return path;
            }
            current = parent;
        }
    }

    /// Reads the parent of a vertex; an absent cell is canonically its own singleton root.
    [[nodiscard]] std::int32_t parent_of(std::int32_t vertex) const {
        const auto* current_cell = cells_.try_get(vertex);
        return current_cell == nullptr ? vertex : current_cell->parent;
    }

    /// Reads the component size cached at a root; an absent cell is a singleton.
    [[nodiscard]] std::int32_t size_of(std::int32_t vertex) const {
        const auto* current_cell = cells_.try_get(vertex);
        return current_cell == nullptr ? 1 : current_cell->size;
    }

    /// The deeper of two candidate witnesses, keeping the left one when the depths are equal.
    ///
    /// The tie is unreachable through the public API — each `link` labels at most one edge, so no
    /// two distinct edges of one lineage share a depth — but the rule is fixed so the reduction
    /// stays a well-defined fold rather than an order-dependent one.
    [[nodiscard]] static const ancestral_connection_version* later(
        const ancestral_connection_version* left,
        const ancestral_connection_version* right) noexcept {
        if (right == nullptr) {
            return left;
        }
        if (left == nullptr) {
            return right;
        }
        return right->depth() > left->depth() ? right : left;
    }

    [[nodiscard]] static std::int32_t checked_sum(std::int32_t left, std::int32_t right) {
        if (right > (std::numeric_limits<std::int32_t>::max)() - left) {
            throw std::overflow_error("The combined component size would overflow.");
        }
        return left + right;
    }

    [[nodiscard]] bool is_vertex(std::int32_t vertex) const noexcept {
        return vertex >= 0 && vertex < vertex_count_;
    }

    void check_vertex(std::int32_t vertex) const {
        if (!is_vertex(vertex)) {
            throw std::out_of_range("Vertex must lie within the forest's fixed universe.");
        }
    }

    void check_vertices(std::int32_t left, std::int32_t right) const {
        check_vertex(left);
        check_vertex(right);
    }

    std::int32_t vertex_count_;
    std::int32_t component_count_;
    cell_map cells_;
    ancestral_connection_version version_;
};

} // namespace durable7::hamt

/// Hashes a version by its identity, matching the pointer-identity `operator==`.
template <>
struct std::hash<durable7::hamt::ancestral_connection_version> {
    [[nodiscard]] std::size_t operator()(
        const durable7::hamt::ancestral_connection_version& version) const noexcept {
        return std::hash<const void*>{}(version.identity());
    }
};
