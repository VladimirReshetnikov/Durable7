/// The append-only incremental level-ancestor seam shared by the ancestry-backed collections.
///
/// An incremental level-ancestor arena is a monotone forest that grows one leaf at a time and
/// answers "which ancestor of this node sits at absolute depth `d`?". Node handles are stable
/// indices owned by one arena. The distinguished bottom node has depth `-1` and no value; every
/// labelled node has a depth of zero or more. A leaf may be added below any previously published
/// node, not only below the most recently added one, so old consumer versions grow into independent
/// branches.
///
/// Where the managed ports express the backend as a runtime interface, this workspace expresses it
/// the way it expresses every other policy: as a compile-time seam. `incremental_ancestor_arena` is
/// a concept, the consuming collections take the arena as a template parameter defaulted to
/// `myers_incremental_ancestor_arena<T>`, and a different backend is a different type rather than a
/// virtual dispatch. The observable per-collection contract is unchanged.

#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace durable7::finger_tree {

/// One snapshot of a Myers incremental-ancestor arena's representation and traversal counters.
struct myers_incremental_ancestor_statistics final {
    /// The number of labelled nodes added to the arena.
    std::size_t published_node_count = 0;
    /// The number of allocated odd-sized blocks, including the bottom block.
    std::size_t block_count = 0;
    /// The total slots reserved across the odd-sized blocks.
    std::size_t allocated_slot_count = 0;
    /// The number of successful leaf additions.
    std::uint64_t add_leaf_count = 0;
    /// The number of completed ancestor queries, saturated at `UINT64_MAX`.
    std::uint64_t ancestor_query_count = 0;
    /// The parent/jump hops made by the most recent query.
    std::size_t last_ancestor_hop_count = 0;
    /// The greatest parent/jump hop count observed.
    std::size_t maximum_ancestor_hop_count = 0;
    /// The total hops across all queries, saturated at `UINT64_MAX`.
    std::uint64_t total_ancestor_hop_count = 0;

    friend bool operator==(
        const myers_incremental_ancestor_statistics&,
        const myers_incremental_ancestor_statistics&) = default;
};

/// The requirements an incremental level-ancestor backend must satisfy.
///
/// An implementation must guarantee that a successful `add_leaf` publishes a fresh, never-recycled
/// handle whose immutable parent is the supplied handle and whose depth is exactly one greater; that
/// `depth_of`, `parent_of`, `value_at`, and ancestor answers for a published node never change; and
/// that a failed addition publishes no partially initialized node.
///
/// `depth_of`, `parent_of`, and `value_at` must be O(1): the parameterized bounds documented by the
/// consuming collections assume those primitive reads are constant-time. Let `U` be leaf-add time
/// and `Q` be level-ancestor query time after `M` published nodes. A backend with `U = Q = O(1)`
/// worst case gives the consumers their optimal bound; neither logarithmic binary lifting nor the
/// shipped `myers_incremental_ancestor_arena` satisfies that stronger bound.
///
/// Handles are arena-relative capabilities. Presenting a handle obtained from a different arena
/// violates the precondition even when the same integer is valid locally, because range validation
/// cannot identify an in-range integer copied from elsewhere.
template <typename Arena, typename T>
concept incremental_ancestor_arena = requires(Arena& arena, const Arena& constant, T value) {
    { constant.bottom() } noexcept -> std::same_as<std::size_t>;
    { constant.published_node_count() } -> std::same_as<std::size_t>;
    { arena.add_leaf(std::size_t{}, std::move(value)) } -> std::same_as<std::size_t>;
    { constant.depth_of(std::size_t{}) } -> std::same_as<std::ptrdiff_t>;
    { constant.parent_of(std::size_t{}) } -> std::same_as<std::size_t>;
    { constant.ancestor_at_depth(std::size_t{}, std::ptrdiff_t{}) } -> std::same_as<std::size_t>;
    { constant.value_at(std::size_t{}) } -> std::same_as<const T&>;
};

/// An incremental ancestor arena using the two-link applicative-stack scheme of Myers.
///
/// Each immutable node keeps one parent link and one coalesced jump link. Adding a leaf performs
/// O(1) link work. Nodes are retained in blocks of sizes 1, 3, 5, ..., whose square boundaries give
/// O(1) addressing and O(sqrt(M)) unused slots after `M` published nodes; block allocation makes
/// insertion O(1) amortized rather than worst case: a single addition at a square boundary pays Θ(√M) to allocate the next odd block,
/// the accepted contract of the shared odd-block representation (deamortizing it was considered
/// and declined; Haskell's arena-free port exceeds this profile at O(1) worst case). An ancestor
/// query follows O(log M) parent/jump
/// links in the worst case. Total storage is O(M), and every published value stays reachable until
/// the arena is destroyed.
///
/// Operations are serialized by one private mutex, so an arena shared between collection values is
/// safe for concurrent use. Published nodes are immutable, so retained collection values remain
/// semantically stable; this implementation makes no lock-free progress guarantee.
template <typename T>
class myers_incremental_ancestor_arena final {
public:
    /// The element type stored in each labelled node.
    using value_type = T;

    /// Constructs an arena containing only its unlabelled bottom node.
    myers_incremental_ancestor_arena()
    {
        append(node{std::nullopt, 0, -1, 0, 0});
    }

    myers_incremental_ancestor_arena(const myers_incremental_ancestor_arena&) = delete;
    myers_incremental_ancestor_arena& operator=(const myers_incremental_ancestor_arena&) = delete;
    myers_incremental_ancestor_arena(myers_incremental_ancestor_arena&&) = delete;
    myers_incremental_ancestor_arena& operator=(myers_incremental_ancestor_arena&&) = delete;
    ~myers_incremental_ancestor_arena() = default;

    /// The distinguished unlabelled node at depth `-1`. This read is O(1).
    [[nodiscard]] static constexpr std::size_t bottom() noexcept
    {
        return 0;
    }

    /// The number of labelled nodes published by this arena. This read is O(1).
    [[nodiscard]] std::size_t published_node_count() const
    {
        const auto guard = std::lock_guard{gate_};
        return count_ - 1;
    }

    /// Adds a labelled leaf below a previously published node and returns its stable handle.
    ///
    /// This addition is O(1) amortized over the block allocations; a block-boundary call pays
    /// Θ(√M) for the new odd block.
    ///
    /// @throws std::out_of_range when `parent` was never published by this arena.
    /// @throws std::overflow_error when the depth, node count, or a coalesced jump distance cannot
    ///     grow further.
    [[nodiscard]] std::size_t add_leaf(const std::size_t parent, T value)
    {
        const auto guard = std::lock_guard{gate_};
        const node& parent_node = checked_node(parent);
        if (parent_node.depth == std::numeric_limits<std::ptrdiff_t>::max()) {
            throw std::overflow_error("the ancestry depth cannot grow further");
        }

        std::size_t skip = 0;
        std::size_t skip_distance = 0;
        if (parent == bottom() || parent_node.skip == bottom()) {
            skip = parent;
            skip_distance = 1;
        } else {
            const node& skipped = node_at(parent_node.skip);
            if (skipped.skip_distance <= parent_node.skip_distance) {
                if (parent_node.skip_distance
                    > std::numeric_limits<std::size_t>::max() - skipped.skip_distance - 1) {
                    throw std::overflow_error("the coalesced jump distance cannot grow further");
                }
                skip = skipped.skip;
                skip_distance = 1 + parent_node.skip_distance + skipped.skip_distance;
            } else {
                skip = parent;
                skip_distance = 1;
            }
        }

        const std::size_t handle = append(
            node{std::optional<T>{std::move(value)}, parent, parent_node.depth + 1, skip, skip_distance});
        ++add_leaf_count_;
        return handle;
    }

    /// A node's immutable absolute depth; the bottom node has depth `-1`. This read is O(1).
    ///
    /// @throws std::out_of_range when `node_handle` was never published by this arena.
    [[nodiscard]] std::ptrdiff_t depth_of(const std::size_t node_handle) const
    {
        const auto guard = std::lock_guard{gate_};
        return checked_node(node_handle).depth;
    }

    /// A labelled node's parent, or the bottom node when the node has depth zero. This read is O(1).
    ///
    /// @throws std::out_of_range when `node_handle` is the bottom node, which has no parent, or was
    ///     never published by this arena.
    [[nodiscard]] std::size_t parent_of(const std::size_t node_handle) const
    {
        const auto guard = std::lock_guard{gate_};
        if (node_handle == bottom()) {
            throw std::out_of_range("the bottom node has no parent");
        }
        return checked_node(node_handle).parent;
    }

    /// The unique ancestor of a node at an absolute depth.
    ///
    /// This query follows O(log M) parent/jump links in the worst case after `M` published nodes.
    ///
    /// @throws std::out_of_range when `node_handle` was never published by this arena, or `depth` is
    ///     outside `-1` through the node's own depth.
    [[nodiscard]] std::size_t ancestor_at_depth(
        const std::size_t node_handle,
        const std::ptrdiff_t depth) const
    {
        const auto guard = std::lock_guard{gate_};
        const node* current = &checked_node(node_handle);
        if (depth < -1 || depth > current->depth) {
            throw std::out_of_range("the depth is outside the node's ancestry");
        }

        std::size_t handle = node_handle;
        std::size_t hops = 0;
        while (current->depth > depth) {
            const auto remaining = static_cast<std::size_t>(current->depth - depth);
            handle = (current->skip_distance > 0 && current->skip_distance <= remaining)
                ? current->skip
                : current->parent;
            current = &node_at(handle);
            ++hops;
        }

        if (ancestor_query_count_ != std::numeric_limits<std::uint64_t>::max()) {
            ++ancestor_query_count_;
        }
        last_ancestor_hop_count_ = hops;
        total_ancestor_hop_count_ = saturating_add(total_ancestor_hop_count_, hops);
        if (hops > maximum_ancestor_hop_count_) {
            maximum_ancestor_hop_count_ = hops;
        }
        return handle;
    }

    /// The immutable value associated with a labelled node. This read is O(1).
    ///
    /// The reference remains valid for the arena's lifetime, because published nodes are never
    /// moved or rewritten.
    ///
    /// @throws std::out_of_range when `node_handle` is the bottom node, which has no value, or was
    ///     never published by this arena.
    [[nodiscard]] const T& value_at(const std::size_t node_handle) const
    {
        const auto guard = std::lock_guard{gate_};
        if (node_handle == bottom()) {
            throw std::out_of_range("the bottom node has no value");
        }
        const node& stored = checked_node(node_handle);
        if (!stored.value.has_value()) {
            throw std::out_of_range("the node carries no value");
        }
        return *stored.value;
    }

    /// Deterministic representation and traversal counters, read in O(1).
    [[nodiscard]] myers_incremental_ancestor_statistics statistics() const
    {
        const auto guard = std::lock_guard{gate_};
        return myers_incremental_ancestor_statistics{
            count_ - 1,
            blocks_.size(),
            allocated_slot_count_,
            add_leaf_count_,
            ancestor_query_count_,
            last_ancestor_hop_count_,
            maximum_ancestor_hop_count_,
            total_ancestor_hop_count_};
    }

private:
    struct node final {
        std::optional<T> value;
        std::size_t parent = 0;
        std::ptrdiff_t depth = -1;
        std::size_t skip = 0;
        std::size_t skip_distance = 0;
    };

    [[nodiscard]] static std::uint64_t saturating_add(
        const std::uint64_t value,
        const std::size_t increment) noexcept
    {
        const auto widened = static_cast<std::uint64_t>(increment);
        return value > std::numeric_limits<std::uint64_t>::max() - widened
            ? std::numeric_limits<std::uint64_t>::max()
            : value + widened;
    }

    [[nodiscard]] static std::size_t integer_square_root(const std::size_t value) noexcept
    {
        auto root = static_cast<std::size_t>(std::sqrt(static_cast<double>(value)));
        while (root + 1 <= value / (root + 1)) {
            ++root;
        }
        while (root > 0 && root > value / root) {
            --root;
        }
        return root;
    }

    [[nodiscard]] const node& node_at(const std::size_t index) const noexcept
    {
        const std::size_t block = integer_square_root(index);
        return blocks_[block][index - block * block];
    }

    [[nodiscard]] const node& checked_node(const std::size_t index) const
    {
        if (index >= count_) {
            throw std::out_of_range("the node was never published by this arena");
        }
        return node_at(index);
    }

    std::size_t append(node published)
    {
        if (count_ == std::numeric_limits<std::size_t>::max()) {
            throw std::overflow_error("the arena cannot hold another node");
        }

        const std::size_t index = count_;
        const std::size_t block = integer_square_root(index);
        if (block == blocks_.size()) {
            // Reserve before publishing. Emplacing an empty block and reserving it afterwards would
            // leave an unreserved block behind if the reservation threw, permanently desynchronizing
            // `allocated_slot_count_` from `block_count` and breaking the seam's promise that a
            // failed addition publishes nothing. `std::vector<node>` is nothrow-move-constructible,
            // so the push below either takes ownership or leaves `blocks_` untouched.
            const std::size_t block_length = 2 * block + 1;
            std::vector<node> reserved;
            reserved.reserve(block_length);
            blocks_.push_back(std::move(reserved));
            allocated_slot_count_ += block_length;
        }

        blocks_[block].push_back(std::move(published));
        count_ = index + 1;
        return index;
    }

    mutable std::mutex gate_;
    std::vector<std::vector<node>> blocks_;
    std::size_t count_ = 0;
    std::size_t allocated_slot_count_ = 0;
    std::uint64_t add_leaf_count_ = 0;
    mutable std::uint64_t ancestor_query_count_ = 0;
    mutable std::size_t last_ancestor_hop_count_ = 0;
    mutable std::size_t maximum_ancestor_hop_count_ = 0;
    mutable std::uint64_t total_ancestor_hop_count_ = 0;
};

} // namespace durable7::finger_tree

