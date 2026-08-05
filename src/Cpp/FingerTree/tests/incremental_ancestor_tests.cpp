/// Tests for the Myers incremental level-ancestor arena that backs the ancestry collections.
///
/// The consuming collections already cover what the arena *answers*: every ancestry-backed value
/// walks this seam, and a wrong ancestor surfaces immediately as a wrong element. What they cannot
/// cover is what the arena *costs*. Their profiles count queries, not the hops inside a query, so
/// deleting the coalesced jump link from the query loop leaves every collection test green while
/// turning an O(log M) query into an O(depth) parent walk -- the skip machinery, the whole reason
/// this backend exists instead of a parent array, could be dead and the port would still ship. The
/// hop-bound cases below are the assertions that bite in that case.
///
/// Two further contracts live only here. The odd-block directory ("blocks of sizes 1, 3, 5, ...",
/// O(sqrt(M)) unused slots) is observable only through `block_count` and `allocated_slot_count`,
/// and its boundaries are decided by `integer_square_root`, which this workspace rewrote into a
/// division form rather than C#'s widened-multiply form; perfect squares are exactly where such a
/// rewrite goes wrong, so the layout is pinned at every published count across sixty-four block
/// boundaries. And the arena's `@throws` clauses are unreachable from the collections, which
/// validate at their own boundary and never present an invalid handle, so the error contracts and
/// the failure-atomicity guarantee are exercised directly against the arena.

#include <durable7/finger_tree/incremental_ancestor.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

using int_arena = myers_incremental_ancestor_arena<int>;

/// The least `k` with `2^k >= value`; zero for zero and one.
[[nodiscard]] constexpr std::size_t ceiling_log2(const std::size_t value) noexcept
{
    auto power = std::size_t{1};
    auto result = std::size_t{0};
    while (power < value) {
        power <<= 1;
        ++result;
    }
    return result;
}

/// The greatest `root` with `root * root <= value`, computed by an independent method.
///
/// This is a deliberately naive oracle. The arena seeds its root from `std::sqrt` and corrects with
/// integer division, so an oracle written the same way would share the rewrite's blind spots; this
/// one only ever multiplies.
[[nodiscard]] constexpr std::size_t reference_integer_square_root(const std::size_t value) noexcept
{
    auto root = std::size_t{0};
    while ((root + 1) * (root + 1) <= value) {
        ++root;
    }
    return root;
}

/// One ancestor answer together with the hops the query actually needed.
struct measured_ancestor final {
    std::size_t handle = 0;
    std::size_t hops = 0;
};

/// Runs one ancestor query and reports its answer and its measured hop count.
///
/// The hop count is read as the delta of `total_ancestor_hop_count`, which the arena maintains
/// independently of `last_ancestor_hop_count`, so the equality checked here pins the latter to the
/// work the single query performed rather than to a running sum.
[[nodiscard]] measured_ancestor ancestor_with_measured_hops(
    const int_arena& arena,
    const std::size_t node_handle,
    const std::ptrdiff_t depth)
{
    const auto before = arena.statistics();
    const auto handle = arena.ancestor_at_depth(node_handle, depth);
    const auto after = arena.statistics();

    const auto hops =
        static_cast<std::size_t>(after.total_ancestor_hop_count - before.total_ancestor_hop_count);
    FT_REQUIRE_EQUAL(after.ancestor_query_count, before.ancestor_query_count + 1);
    FT_REQUIRE_EQUAL(after.last_ancestor_hop_count, hops);
    FT_REQUIRE(after.maximum_ancestor_hop_count >= hops);
    return measured_ancestor{handle, hops};
}

/// Checks two statistics snapshots field by field and then as whole values.
///
/// The field comparisons name the counter that moved; the whole-value comparison is the contract,
/// so a counter added to the snapshot later is covered without editing this helper.
void require_same_statistics(
    const myers_incremental_ancestor_statistics& actual,
    const myers_incremental_ancestor_statistics& expected)
{
    FT_REQUIRE_EQUAL(actual.published_node_count, expected.published_node_count);
    FT_REQUIRE_EQUAL(actual.block_count, expected.block_count);
    FT_REQUIRE_EQUAL(actual.allocated_slot_count, expected.allocated_slot_count);
    FT_REQUIRE_EQUAL(actual.add_leaf_count, expected.add_leaf_count);
    FT_REQUIRE_EQUAL(actual.ancestor_query_count, expected.ancestor_query_count);
    FT_REQUIRE_EQUAL(actual.last_ancestor_hop_count, expected.last_ancestor_hop_count);
    FT_REQUIRE_EQUAL(actual.maximum_ancestor_hop_count, expected.maximum_ancestor_hop_count);
    FT_REQUIRE_EQUAL(actual.total_ancestor_hop_count, expected.total_ancestor_hop_count);
    FT_REQUIRE(actual == expected);
}

} // namespace

void add_incremental_ancestor_tests(suite& tests)
{
    static_assert(incremental_ancestor_arena<int_arena, int>);
    static_assert(ceiling_log2(0) == 0);
    static_assert(ceiling_log2(1) == 0);
    static_assert(ceiling_log2(2) == 1);
    static_assert(ceiling_log2(32'769) == 16);
    static_assert(reference_integer_square_root(0) == 0);
    static_assert(reference_integer_square_root(4'096) == 64);
    static_assert(reference_integer_square_root(4'095) == 63);

    tests.add("Myers arena deep chain queries use conservatively logarithmic hops", [] {
        constexpr auto node_count = std::size_t{32'768};

        int_arena arena;
        auto nodes_by_depth = std::vector<std::size_t>{};
        nodes_by_depth.reserve(node_count + 1);
        nodes_by_depth.push_back(int_arena::bottom());

        auto tail = int_arena::bottom();
        for (auto value = std::size_t{0}; value != node_count; ++value) {
            tail = arena.add_leaf(tail, static_cast<int>(value));
            nodes_by_depth.push_back(tail);
        }

        // Building the chain performs no query, so the traversal counters must all still be zero.
        const auto after_adds = arena.statistics();
        FT_REQUIRE_EQUAL(after_adds.published_node_count, node_count);
        FT_REQUIRE_EQUAL(after_adds.add_leaf_count, static_cast<std::uint64_t>(node_count));
        FT_REQUIRE_EQUAL(after_adds.ancestor_query_count, std::uint64_t{0});
        FT_REQUIRE_EQUAL(after_adds.last_ancestor_hop_count, std::size_t{0});
        FT_REQUIRE_EQUAL(after_adds.maximum_ancestor_hop_count, std::size_t{0});
        FT_REQUIRE_EQUAL(after_adds.total_ancestor_hop_count, std::uint64_t{0});
        FT_REQUIRE_EQUAL(arena.depth_of(tail), static_cast<std::ptrdiff_t>(node_count) - 1);

        // Every ancestor of the deepest node, from its own depth down to the bottom node.
        for (auto target_depth = static_cast<std::ptrdiff_t>(node_count) - 1; target_depth >= -1;
             --target_depth) {
            FT_REQUIRE_EQUAL(
                arena.ancestor_at_depth(tail, target_depth),
                nodes_by_depth[static_cast<std::size_t>(target_depth + 1)]);
        }

        // The bound is shaped, not tuned. `ceiling_log2(M + 1)` is the deepest depth expressed in
        // bits -- the number of hops an ideal halving walk would need -- and the factor of four is
        // headroom for the constant in Myers' coalescing schedule, whose jump links do not close a
        // full remaining half on every hop. Stating it this way makes the assertion discriminating
        // in the one direction that matters. A coalescing-free arena, one whose query loop always
        // follows `parent`, answers every equality above exactly as correctly, but needs `M` hops
        // to reach the bottom of this chain: 32,768 against a bound of 4 * 16 = 64. Deleting the
        // jump branch therefore fails here and nowhere else in the suite. In the other direction
        // the bound is loose enough not to pin the exact schedule: the shipped implementation peaks
        // at 29 hops here, well under half the permitted 64, so it would survive a coalescing
        // change that doubled its constant.
        const auto after_queries = arena.statistics();
        const auto conservative_logarithmic_bound = 4 * ceiling_log2(node_count + 1);
        FT_REQUIRE_EQUAL(
            after_queries.ancestor_query_count,
            static_cast<std::uint64_t>(node_count) + 1);
        FT_REQUIRE(after_queries.maximum_ancestor_hop_count >= 1);
        FT_REQUIRE(after_queries.maximum_ancestor_hop_count <= conservative_logarithmic_bound);
        FT_REQUIRE(after_queries.total_ancestor_hop_count >= 1);
        FT_REQUIRE(
            after_queries.total_ancestor_hop_count
            <= static_cast<std::uint64_t>(node_count + 1)
                * static_cast<std::uint64_t>(conservative_logarithmic_bound));

        // The counters describe queries only; a later addition leaves them alone.
        const auto extra = arena.add_leaf(tail, -1);
        const auto after_extra_add = arena.statistics();
        FT_REQUIRE_EQUAL(arena.depth_of(extra), static_cast<std::ptrdiff_t>(node_count));
        FT_REQUIRE_EQUAL(after_extra_add.ancestor_query_count, after_queries.ancestor_query_count);
        FT_REQUIRE_EQUAL(
            after_extra_add.maximum_ancestor_hop_count,
            after_queries.maximum_ancestor_hop_count);
        FT_REQUIRE_EQUAL(
            after_extra_add.total_ancestor_hop_count,
            after_queries.total_ancestor_hop_count);
    });

    tests.add("Myers arena last hop count reports only the most recent query", [] {
        constexpr auto chain_length = std::size_t{1'024};

        int_arena arena;
        auto tail = int_arena::bottom();
        for (auto value = std::size_t{0}; value != chain_length; ++value) {
            tail = arena.add_leaf(tail, static_cast<int>(value));
        }

        const auto tail_depth = arena.depth_of(tail);
        FT_REQUIRE_EQUAL(tail_depth, static_cast<std::ptrdiff_t>(chain_length) - 1);
        FT_REQUIRE_EQUAL(arena.statistics().last_ancestor_hop_count, std::size_t{0});

        // A query for the node itself terminates before the first hop.
        const auto self = ancestor_with_measured_hops(arena, tail, tail_depth);
        FT_REQUIRE_EQUAL(self.handle, tail);
        FT_REQUIRE_EQUAL(self.hops, std::size_t{0});

        // A query for the immediate parent needs exactly one hop: with one level remaining, a
        // coalesced jump of distance greater than one overshoots and is refused, and a jump of
        // distance exactly one is the parent link itself.
        const auto parent = ancestor_with_measured_hops(arena, tail, tail_depth - 1);
        FT_REQUIRE_EQUAL(parent.handle, arena.parent_of(tail));
        FT_REQUIRE_EQUAL(parent.hops, std::size_t{1});
        FT_REQUIRE_EQUAL(arena.statistics().last_ancestor_hop_count, std::size_t{1});

        // A full walk to the bottom node costs more than one hop, and the counter reports that
        // query's own cost rather than a running sum of the queries before it.
        const auto bottom = ancestor_with_measured_hops(arena, tail, -1);
        FT_REQUIRE_EQUAL(bottom.handle, int_arena::bottom());
        FT_REQUIRE(bottom.hops > 1);
        FT_REQUIRE(bottom.hops <= 4 * ceiling_log2(chain_length + 1));

        // Repeating the zero-hop query drives the counter back down, so it is replaced by each
        // query rather than accumulated across queries.
        const auto repeated = ancestor_with_measured_hops(arena, tail, tail_depth);
        FT_REQUIRE_EQUAL(repeated.handle, tail);
        FT_REQUIRE_EQUAL(repeated.hops, std::size_t{0});

        const auto statistics = arena.statistics();
        FT_REQUIRE_EQUAL(statistics.last_ancestor_hop_count, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.maximum_ancestor_hop_count, bottom.hops);
        FT_REQUIRE_EQUAL(
            statistics.total_ancestor_hop_count,
            static_cast<std::uint64_t>(self.hops + parent.hops + bottom.hops + repeated.hops));
        FT_REQUIRE_EQUAL(statistics.ancestor_query_count, std::uint64_t{4});
    });

    tests.add("Myers arena branched queries use conservatively logarithmic hops", [] {
        constexpr auto chain_length = std::size_t{64};
        constexpr auto branch_count = std::size_t{1'984};
        constexpr auto total_count = chain_length + branch_count;

        // An irregular branching arena: most additions extend the current frontier, the rest hang a
        // fresh branch off an arbitrary older node, so jump links coalesce across sibling branches
        // instead of along one straight chain. A skip scheme that happened to be logarithmic only
        // on a single path would fail here rather than in the deep-chain case.
        int_arena arena;
        deterministic_rng random{0x5EED1234ULL};

        auto nodes = std::vector<std::size_t>{};
        auto parent_position = std::vector<std::size_t>{};
        auto depths = std::vector<std::ptrdiff_t>{};
        nodes.reserve(total_count + 1);
        parent_position.reserve(total_count + 1);
        depths.reserve(total_count + 1);
        nodes.push_back(int_arena::bottom());
        parent_position.push_back(0);
        depths.push_back(-1);

        auto frontier_position = std::size_t{0};
        auto branch_point_count = std::size_t{0};
        for (auto value = std::size_t{0}; value != total_count; ++value) {
            auto chosen = frontier_position;
            if (value >= chain_length && random.next_index(8) == 0) {
                ++branch_point_count;
                chosen = random.next_index(nodes.size());
            }

            nodes.push_back(arena.add_leaf(nodes[chosen], static_cast<int>(value)));
            parent_position.push_back(chosen);
            depths.push_back(depths[chosen] + 1);
            frontier_position = nodes.size() - 1;
        }

        FT_REQUIRE_EQUAL(arena.published_node_count(), total_count);
        // The generated arena must actually branch rather than degenerate into one chain.
        FT_REQUIRE(branch_point_count > branch_count / 16);

        auto maximum_depth = std::ptrdiff_t{-1};
        for (auto position = std::size_t{1}; position != nodes.size(); ++position) {
            const auto node_handle = nodes[position];
            const auto node_depth = depths[position];
            FT_REQUIRE_EQUAL(arena.depth_of(node_handle), node_depth);
            FT_REQUIRE_EQUAL(arena.parent_of(node_handle), nodes[parent_position[position]]);
            FT_REQUIRE_EQUAL(arena.value_at(node_handle), static_cast<int>(position) - 1);
            if (node_depth > maximum_depth) {
                maximum_depth = node_depth;
            }

            // A conservative envelope logarithmic in this node's own depth, padded exactly as in
            // the deep-chain case so the test pins the asymptotic shape rather than the schedule.
            // The worst query here reaches roughly 57 per cent of its envelope, and that figure
            // barely moves across replay seeds, so the padding is margin rather than slack; a
            // parent-only walk needs `node_depth + 1` hops and blows straight past it.
            const auto hop_envelope =
                4 * ceiling_log2(static_cast<std::size_t>(node_depth) + 2);

            // Naive oracle: walk parent links one level at a time.
            auto expected_position = position;
            for (auto target_depth = node_depth; target_depth >= -1; --target_depth) {
                const auto measured =
                    ancestor_with_measured_hops(arena, node_handle, target_depth);
                FT_REQUIRE_EQUAL(measured.handle, nodes[expected_position]);
                FT_REQUIRE(measured.hops <= hop_envelope);
                if (target_depth >= 0) {
                    expected_position = parent_position[expected_position];
                }
            }
        }

        // The branched arena must stay deep enough for jump links to matter at all.
        FT_REQUIRE(maximum_depth >= static_cast<std::ptrdiff_t>(chain_length));
        FT_REQUIRE(
            arena.statistics().maximum_ancestor_hop_count
            <= 4 * ceiling_log2(static_cast<std::size_t>(maximum_depth) + 2));
    });

    tests.add("Myers arena odd block statistics match the exact square layout", [] {
        constexpr auto maximum_published_count = std::size_t{4'096};

        int_arena arena;
        auto handles = std::vector<std::size_t>{};
        handles.reserve(maximum_published_count);

        auto parent = int_arena::bottom();
        for (auto published_count = std::size_t{0}; published_count <= maximum_published_count;
             ++published_count) {
            const auto statistics = arena.statistics();

            // With `published_count` labelled nodes the arena holds indices `0 .. published_count`,
            // so the last block is `isqrt(published_count)` and the directory holds one more block
            // than that. The blocks have lengths 1, 3, 5, ..., whose partial sums are the squares,
            // so the reserved slots are exactly `block_count` squared and the wasted tail is
            // `block_count^2 - (published_count + 1) < 2 * sqrt(published_count) + 1`.
            const auto expected_block_count =
                reference_integer_square_root(published_count) + 1;
            FT_REQUIRE_EQUAL(arena.published_node_count(), published_count);
            FT_REQUIRE_EQUAL(statistics.published_node_count, published_count);
            FT_REQUIRE_EQUAL(statistics.block_count, expected_block_count);
            FT_REQUIRE_EQUAL(
                statistics.allocated_slot_count,
                expected_block_count * expected_block_count);
            FT_REQUIRE(statistics.allocated_slot_count >= published_count + 1);
            FT_REQUIRE(
                statistics.allocated_slot_count - (published_count + 1)
                < 2 * expected_block_count);
            FT_REQUIRE_EQUAL(statistics.add_leaf_count, static_cast<std::uint64_t>(published_count));

            // Additions never consult the ancestry, so no query counter may have moved.
            FT_REQUIRE_EQUAL(statistics.ancestor_query_count, std::uint64_t{0});
            FT_REQUIRE_EQUAL(statistics.last_ancestor_hop_count, std::size_t{0});
            FT_REQUIRE_EQUAL(statistics.maximum_ancestor_hop_count, std::size_t{0});
            FT_REQUIRE_EQUAL(statistics.total_ancestor_hop_count, std::uint64_t{0});

            if (published_count != maximum_published_count) {
                parent = arena.add_leaf(parent, static_cast<int>(published_count));
                handles.push_back(parent);
            }
        }

        // The block directory is also the addressing scheme: `node_at` splits a handle into a block
        // and an offset with the same square root. Reading every published node back therefore
        // pins the rewritten root at each of the sixty-four boundaries from the other side, where a
        // root that was one too large or one too small at a perfect square would misaddress.
        FT_REQUIRE_EQUAL(handles.size(), maximum_published_count);
        for (auto index = std::size_t{0}; index != handles.size(); ++index) {
            FT_REQUIRE_EQUAL(arena.value_at(handles[index]), static_cast<int>(index));
            FT_REQUIRE_EQUAL(arena.depth_of(handles[index]), static_cast<std::ptrdiff_t>(index));
        }

        // A fresh arena starts with the bottom block alone: one block, one slot, nothing published.
        const int_arena empty;
        const auto empty_statistics = empty.statistics();
        FT_REQUIRE_EQUAL(empty_statistics.published_node_count, std::size_t{0});
        FT_REQUIRE_EQUAL(empty_statistics.block_count, std::size_t{1});
        FT_REQUIRE_EQUAL(empty_statistics.allocated_slot_count, std::size_t{1});
        FT_REQUIRE_EQUAL(empty_statistics.add_leaf_count, std::uint64_t{0});
    });

    tests.add("Myers arena rejects unpublished handles and depths outside a node's ancestry", [] {
        // Deliberately not `constexpr`: every use below sits inside the lambda the throw-assertion
        // macro builds, and a constant would be folded there rather than captured, which MSVC then
        // reports as an unreferenced local.
        const auto unpublished = std::size_t{1};
        const auto far_out_of_range = (std::numeric_limits<std::size_t>::max)();

        int_arena arena;
        const auto bottom = int_arena::bottom();

        // A fresh arena has published nothing, so every handle other than the bottom node is
        // outside the arena, whether it is barely past the end or nowhere near it.
        FT_REQUIRE_THROWS(std::out_of_range, arena.depth_of(unpublished));
        FT_REQUIRE_THROWS(std::out_of_range, arena.parent_of(unpublished));
        FT_REQUIRE_THROWS(std::out_of_range, arena.value_at(unpublished));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(unpublished, -1));
        FT_REQUIRE_THROWS(std::out_of_range, arena.add_leaf(unpublished, 0));
        FT_REQUIRE_THROWS(std::out_of_range, arena.depth_of(far_out_of_range));
        FT_REQUIRE_THROWS(std::out_of_range, arena.parent_of(far_out_of_range));
        FT_REQUIRE_THROWS(std::out_of_range, arena.value_at(far_out_of_range));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(far_out_of_range, -1));
        FT_REQUIRE_THROWS(std::out_of_range, arena.add_leaf(far_out_of_range, 0));

        // The bottom node is published but unlabelled: it has a depth and is its own ancestor at
        // depth `-1`, yet it has neither a parent nor a value.
        FT_REQUIRE_EQUAL(arena.depth_of(bottom), std::ptrdiff_t{-1});
        FT_REQUIRE_EQUAL(arena.ancestor_at_depth(bottom, -1), bottom);
        FT_REQUIRE_THROWS(std::out_of_range, arena.parent_of(bottom));
        FT_REQUIRE_THROWS(std::out_of_range, arena.value_at(bottom));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(bottom, -2));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(bottom, 0));

        auto chain = std::vector<std::size_t>{bottom};
        auto tail = bottom;
        for (auto value = std::size_t{0}; value != 8; ++value) {
            tail = arena.add_leaf(tail, static_cast<int>(value));
            chain.push_back(tail);
        }

        // Both ends of the legal window answer; one step past either end is a range error.
        FT_REQUIRE_EQUAL(arena.depth_of(tail), std::ptrdiff_t{7});
        FT_REQUIRE_EQUAL(arena.ancestor_at_depth(tail, 7), tail);
        FT_REQUIRE_EQUAL(arena.ancestor_at_depth(tail, -1), bottom);
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(tail, 8));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(tail, -2));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            arena.ancestor_at_depth(tail, (std::numeric_limits<std::ptrdiff_t>::max)()));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            arena.ancestor_at_depth(tail, (std::numeric_limits<std::ptrdiff_t>::min)()));

        // The window is the queried node's own ancestry, not the arena's: a depth that a deeper
        // node reaches is still out of range for a shallower one.
        for (auto depth = std::ptrdiff_t{0}; depth != 8; ++depth) {
            const auto node_handle = chain[static_cast<std::size_t>(depth) + 1];
            FT_REQUIRE_EQUAL(arena.ancestor_at_depth(node_handle, depth), node_handle);
            FT_REQUIRE_EQUAL(arena.ancestor_at_depth(node_handle, -1), bottom);
            FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(node_handle, depth + 1));
            FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(node_handle, -2));
        }

        // A handle exactly one past the published range is still unpublished.
        FT_REQUIRE_THROWS(std::out_of_range, arena.depth_of(arena.published_node_count() + 1));
        FT_REQUIRE_THROWS(std::out_of_range, arena.parent_of(arena.published_node_count() + 1));
        FT_REQUIRE_THROWS(std::out_of_range, arena.value_at(arena.published_node_count() + 1));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            arena.ancestor_at_depth(arena.published_node_count() + 1, -1));
        FT_REQUIRE_THROWS(std::out_of_range, arena.add_leaf(arena.published_node_count() + 1, 0));
    });

    tests.add("Myers arena rejected calls publish no node and leave every counter untouched", [] {
        int_arena arena;
        auto tail = int_arena::bottom();
        for (auto value = std::size_t{0}; value != 8; ++value) {
            tail = arena.add_leaf(tail, static_cast<int>(value));
        }

        // Spend a query first, so every traversal counter is non-zero and a reset would show.
        FT_REQUIRE_EQUAL(arena.ancestor_at_depth(tail, -1), int_arena::bottom());

        const auto before = arena.statistics();
        FT_REQUIRE_EQUAL(before.published_node_count, std::size_t{8});
        FT_REQUIRE_EQUAL(before.block_count, std::size_t{3});
        FT_REQUIRE_EQUAL(before.allocated_slot_count, std::size_t{9});
        FT_REQUIRE(before.total_ancestor_hop_count > 0);
        FT_REQUIRE(before.maximum_ancestor_hop_count > 0);

        // The next successful addition takes index 9, the first slot of a block the directory does
        // not yet hold, so a rejected addition that leaked any work would move `block_count` and
        // `allocated_slot_count` as well as the node counters.
        const auto expected_handle = before.published_node_count + 1;

        FT_REQUIRE_THROWS(std::out_of_range, arena.add_leaf(expected_handle, 0));
        FT_REQUIRE_THROWS(std::out_of_range, arena.add_leaf(std::size_t{1'000}, 0));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            arena.add_leaf((std::numeric_limits<std::size_t>::max)(), 0));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(tail, -2));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(tail, 8));
        FT_REQUIRE_THROWS(std::out_of_range, arena.ancestor_at_depth(expected_handle, 0));

        // Every counter, including the block directory and the traversal counters a rejected query
        // must not touch, is identical to the snapshot taken before the rejected calls.
        require_same_statistics(arena.statistics(), before);
        FT_REQUIRE_EQUAL(arena.published_node_count(), before.published_node_count);

        // No partially initialized node was published: the handle the rejected additions named is
        // still unknown to the arena through every accessor.
        FT_REQUIRE_THROWS(std::out_of_range, arena.depth_of(expected_handle));
        FT_REQUIRE_THROWS(std::out_of_range, arena.parent_of(expected_handle));
        FT_REQUIRE_THROWS(std::out_of_range, arena.value_at(expected_handle));

        // The retained nodes still answer exactly as they did before the rejected calls.
        FT_REQUIRE_EQUAL(arena.depth_of(tail), std::ptrdiff_t{7});
        FT_REQUIRE_EQUAL(arena.value_at(tail), 7);

        // The arena stays fully usable, and the next successful addition receives the very handle
        // the rejected ones would have consumed had they leaked a slot.
        const auto handle = arena.add_leaf(tail, 8);
        FT_REQUIRE_EQUAL(handle, expected_handle);
        FT_REQUIRE_EQUAL(arena.depth_of(handle), std::ptrdiff_t{8});
        FT_REQUIRE_EQUAL(arena.parent_of(handle), tail);
        FT_REQUIRE_EQUAL(arena.value_at(handle), 8);
        FT_REQUIRE_EQUAL(arena.ancestor_at_depth(handle, -1), int_arena::bottom());

        const auto grown = arena.statistics();
        FT_REQUIRE_EQUAL(grown.published_node_count, std::size_t{9});
        FT_REQUIRE_EQUAL(grown.add_leaf_count, before.add_leaf_count + 1);
        FT_REQUIRE_EQUAL(grown.block_count, std::size_t{4});
        FT_REQUIRE_EQUAL(grown.allocated_slot_count, std::size_t{16});
    });
}
