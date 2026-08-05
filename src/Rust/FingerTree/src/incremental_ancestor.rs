//! The append-only incremental level-ancestor arena shared by the ancestry-backed collections.
//!
//! An *incremental level-ancestor arena* is a monotone forest that grows one leaf at a time and
//! answers "which ancestor of this node sits at absolute depth `d`?". Nodes are named by stable
//! integer handles, never move, and are never recycled, so a collection can retain a handle
//! forever and keep reading the same immutable answer. Adding a leaf below an *old* handle simply
//! starts a new branch, which is what makes the arena a persistence substrate: every version of a
//! consumer collection is one path interval in the shared forest.
//!
//! [`IncrementalAncestorArena`] is the service contract, and [`MyersAncestorArena`] is the shipped
//! implementation. It stores one parent link plus one coalesced jump link per immutable node, so a
//! leaf addition does O(1) link work and an ancestor query follows O(log M) parent/jump links in
//! the worst case after M published nodes. Nodes live in blocks of sizes 1, 3, 5, ..., whose square
//! boundaries give O(1) addressing and O(sqrt(M)) unused slots. Block and directory allocation
//! makes an addition O(1) *amortized* rather than worst case. Total arena storage is O(M), and
//! every published value stays reachable until the arena itself is dropped.
//!
//! An Alstrup-Holm incremental level-ancestor backend would supply O(1) worst-case addition *and*
//! O(1) worst-case queries with linear arena space; no such backend is included here. Nothing in
//! this module upgrades the Myers amortized/logarithmic bounds to worst-case ones.
//!
//! All [`MyersAncestorArena`] reads and writes are serialized by one private lock. Published nodes
//! are immutable, so retained handles stay semantically stable, but this implementation makes no
//! lock-free progress guarantee.

use std::fmt;
use std::sync::{Arc, Mutex, MutexGuard};

/// The handle of the distinguished unlabeled node that every arena publishes at construction.
const BOTTOM: usize = 0;

/// An append-only level-ancestor service over a monotone forest of immutable nodes.
///
/// Node handles are stable integers owned by one arena. The distinguished [`bottom`] node has
/// depth -1 and carries no value; every labeled node has a depth of zero or more. A leaf may be
/// added below *any* previously published node, not only below the most recently added one, so old
/// consumer versions can grow into independent branches.
///
/// Implementations must guarantee that:
///
/// * a successful [`add_leaf`] publishes a fresh, never-recycled handle whose immutable parent is
///   the supplied handle and whose depth is exactly one greater;
/// * [`depth`], [`parent`], [`value`], and ancestor answers for a published node never change; and
/// * a failed addition publishes no partially initialized node.
///
/// [`depth`], [`parent`], and [`value`] must take O(1) time: the parameterized bounds documented by
/// the consuming collections assume those primitive reads are constant-time. Implementations
/// determine the bounds those collections inherit. Let `U(M)` be leaf-add time and `Q(M)` be
/// level-ancestor query time after M published nodes. An implementation with `U(M) = Q(M) = O(1)`
/// worst case gives the consumers their optimal bound; neither binary lifting nor the shipped
/// [`MyersAncestorArena`] satisfies that stronger backend bound.
///
/// # Panics
///
/// Passing a handle this arena never published, asking the bottom node for a parent or a value, or
/// requesting a depth outside `-1..=depth(node)` is a caller contract violation and panics, exactly
/// as out-of-bounds slice indexing does. Integer handles are arena-relative capabilities: range
/// validation cannot identify an in-range integer copied from a *different* arena, so callers must
/// preserve arena ownership themselves. A handle is meaningful only to the arena that issued it.
///
/// [`bottom`]: IncrementalAncestorArena::bottom
/// [`add_leaf`]: IncrementalAncestorArena::add_leaf
/// [`depth`]: IncrementalAncestorArena::depth
/// [`parent`]: IncrementalAncestorArena::parent
/// [`value`]: IncrementalAncestorArena::value
pub trait IncrementalAncestorArena<T>: Send + Sync {
    /// Returns the distinguished unlabeled node at depth -1.
    fn bottom(&self) -> usize;

    /// Returns the number of labeled nodes published by this arena.
    fn published_node_count(&self) -> usize;

    /// Adds a labeled leaf below a previously published node and returns its stable handle.
    ///
    /// `parent` is the bottom node or a labeled node owned by this arena.
    fn add_leaf(&self, parent: usize, value: T) -> usize;

    /// Returns a node's immutable absolute depth; the bottom node has depth -1.
    fn depth(&self, node: usize) -> isize;

    /// Returns a labeled node's parent, which is the bottom node when the node has depth zero.
    fn parent(&self, node: usize) -> usize;

    /// Returns the unique ancestor of `node` at absolute `depth`, which ranges from -1 through the
    /// node's own depth.
    fn ancestor_at_depth(&self, node: usize, depth: isize) -> usize;

    /// Returns the immutable value retained for a labeled node.
    ///
    /// The value is returned as a retained [`Arc`] so consumers can read it without a `T: Clone`
    /// bound and without copying the payload.
    fn value(&self, node: usize) -> Arc<T>;
}

/// Deterministic representation and traversal counters for a [`MyersAncestorArena`].
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MyersAncestorStatistics {
    /// The number of labeled nodes added to the arena.
    pub published_node_count: usize,
    /// The number of allocated odd-sized blocks, including the block holding the bottom node.
    pub block_count: usize,
    /// The total slots reserved by the odd-sized blocks.
    pub allocated_slot_count: u64,
    /// The number of successful leaf additions.
    pub add_leaf_count: u64,
    /// The number of completed ancestor queries, saturated at [`u64::MAX`].
    pub ancestor_query_count: u64,
    /// The parent/jump hops made by the most recent ancestor query.
    pub last_ancestor_hop_count: usize,
    /// The greatest parent/jump hop count observed by any ancestor query.
    pub maximum_ancestor_hop_count: usize,
    /// The total parent/jump hops across all ancestor queries, saturated at [`u64::MAX`].
    pub total_ancestor_hop_count: u64,
}

/// The immutable navigation links of one published node.
#[derive(Clone, Copy)]
struct Links {
    parent: usize,
    depth: isize,
    skip: usize,
    skip_distance: usize,
}

/// One published node: its retained value and its immutable links.
struct Node<T> {
    value: Option<Arc<T>>,
    links: Links,
}

/// An append-only store whose blocks have sizes 1, 3, 5, ..., so block `b` covers handles
/// `b * b` through `(b + 1) * (b + 1) - 1` and addressing costs one integer square root.
struct OddBlockStore<I> {
    blocks: Vec<Vec<I>>,
    count: usize,
    allocated_slot_count: u64,
}

impl<I> OddBlockStore<I> {
    fn new() -> Self {
        Self {
            blocks: Vec::new(),
            count: 0,
            allocated_slot_count: 0,
        }
    }

    fn count(&self) -> usize {
        self.count
    }

    fn block_count(&self) -> usize {
        self.blocks.len()
    }

    fn allocated_slot_count(&self) -> u64 {
        self.allocated_slot_count
    }

    fn get(&self, index: usize) -> &I {
        assert!(
            index < self.count,
            "incremental ancestor arena handle {index} was never published by this arena \
             (it holds {} nodes)",
            self.count
        );
        let block = index.isqrt();
        &self.blocks[block][index - block * block]
    }

    fn append(&mut self, item: I) -> usize {
        assert!(
            self.count != usize::MAX,
            "incremental ancestor arena node-count overflow"
        );
        let index = self.count;
        let block = index.isqrt();
        if block == self.blocks.len() {
            let block_length = 2 * block + 1;
            self.blocks.push(Vec::with_capacity(block_length));
            self.allocated_slot_count = self
                .allocated_slot_count
                .saturating_add(block_length as u64);
        }

        debug_assert_eq!(self.blocks[block].len(), index - block * block);
        self.blocks[block].push(item);
        self.count = index + 1;
        index
    }
}

/// The lock-protected arena state.
struct ArenaState<T> {
    nodes: OddBlockStore<Node<T>>,
    add_leaf_count: u64,
    ancestor_query_count: u64,
    total_ancestor_hop_count: u64,
    last_ancestor_hop_count: usize,
    maximum_ancestor_hop_count: usize,
}

/// An incremental ancestor arena using Myers's two-link applicative-stack scheme.
///
/// Each immutable node keeps one parent link and one coalesced jump link. Adding a leaf performs
/// O(1) link work; because block and directory allocation is amortized, an addition is O(1)
/// amortized rather than O(1) worst case. An ancestor query follows O(log M) parent/jump links in
/// the worst case after M published nodes. Nodes live in blocks of sizes 1, 3, 5, ..., whose square
/// boundaries give O(1) addressing and O(sqrt(M)) unused slots. Total arena storage is O(M), and
/// every published value remains reachable until the arena itself is dropped.
///
/// Every read and every write is serialized by one private [`Mutex`]. Published nodes are
/// immutable, so retained handles remain semantically stable, but this implementation makes no
/// lock-free progress guarantee: concurrent queries contend on the same lock. A panic raised for an
/// invalid handle happens before any state change, so a poisoned lock is recovered rather than
/// propagated and the arena is left exactly as it was.
///
/// # Panics
///
/// See [`IncrementalAncestorArena`] for the handle and depth contracts. Depth, node-count, and jump
/// arithmetic overflow also panic, matching how this crate reports capacity overflow elsewhere.
pub struct MyersAncestorArena<T> {
    state: Mutex<ArenaState<T>>,
}

impl<T> MyersAncestorArena<T> {
    /// Creates an empty arena containing only its unlabeled bottom node.
    #[must_use]
    pub fn new() -> Self {
        let mut nodes = OddBlockStore::new();
        let bottom = nodes.append(Node {
            value: None,
            links: Links {
                parent: BOTTOM,
                depth: -1,
                skip: BOTTOM,
                skip_distance: 0,
            },
        });
        debug_assert_eq!(bottom, BOTTOM);
        Self {
            state: Mutex::new(ArenaState {
                nodes,
                add_leaf_count: 0,
                ancestor_query_count: 0,
                total_ancestor_hop_count: 0,
                last_ancestor_hop_count: 0,
                maximum_ancestor_hop_count: 0,
            }),
        }
    }

    /// Returns the distinguished unlabeled node at depth -1.
    #[must_use]
    pub fn bottom(&self) -> usize {
        BOTTOM
    }

    /// Returns the number of labeled nodes published by this arena.
    #[must_use]
    pub fn published_node_count(&self) -> usize {
        self.lock().nodes.count() - 1
    }

    /// Adds a labeled leaf below a previously published node and returns its stable handle.
    ///
    /// # Panics
    ///
    /// Panics when `parent` was never published by this arena, or when the ancestry depth, the
    /// node count, or a coalesced jump distance would overflow.
    pub fn add_leaf(&self, parent: usize, value: T) -> usize {
        let mut state = self.lock();
        let parent_links = state.nodes.get(parent).links;
        let depth = parent_links
            .depth
            .checked_add(1)
            .expect("incremental ancestor arena ancestry-depth overflow");

        let (skip, skip_distance) = if parent == BOTTOM || parent_links.skip == BOTTOM {
            (parent, 1)
        } else {
            let skipped = state.nodes.get(parent_links.skip).links;
            if skipped.skip_distance <= parent_links.skip_distance {
                let distance = parent_links
                    .skip_distance
                    .checked_add(skipped.skip_distance)
                    .and_then(|sum| sum.checked_add(1))
                    .expect("incremental ancestor arena jump-distance overflow");
                (skipped.skip, distance)
            } else {
                (parent, 1)
            }
        };

        let handle = state.nodes.append(Node {
            value: Some(Arc::new(value)),
            links: Links {
                parent,
                depth,
                skip,
                skip_distance,
            },
        });
        state.add_leaf_count += 1;
        handle
    }

    /// Returns a node's immutable absolute depth; the bottom node has depth -1.
    ///
    /// # Panics
    ///
    /// Panics when `node` was never published by this arena.
    #[must_use]
    pub fn depth(&self, node: usize) -> isize {
        self.lock().nodes.get(node).links.depth
    }

    /// Returns a labeled node's parent, which is the bottom node when the node has depth zero.
    ///
    /// # Panics
    ///
    /// Panics when `node` is the bottom node, which has no parent, or was never published by this
    /// arena.
    #[must_use]
    pub fn parent(&self, node: usize) -> usize {
        assert!(
            node != BOTTOM,
            "the incremental ancestor arena bottom node has no parent"
        );
        self.lock().nodes.get(node).links.parent
    }

    /// Returns the unique ancestor of `node` at absolute `depth`.
    ///
    /// # Panics
    ///
    /// Panics when `node` was never published by this arena, or when `depth` lies outside
    /// `-1..=self.depth(node)`.
    #[must_use]
    pub fn ancestor_at_depth(&self, node: usize, depth: isize) -> usize {
        let mut state = self.lock();
        let mut current = state.nodes.get(node).links;
        assert!(
            depth >= -1 && depth <= current.depth,
            "ancestor depth {depth} is outside -1..={} for incremental ancestor arena handle {node}",
            current.depth
        );

        let mut node = node;
        let mut hops = 0usize;
        while current.depth > depth {
            let remaining = current.depth.saturating_sub(depth);
            let skip_distance = isize::try_from(current.skip_distance).unwrap_or(isize::MAX);
            node = if current.skip_distance > 0 && skip_distance <= remaining {
                current.skip
            } else {
                current.parent
            };
            current = state.nodes.get(node).links;
            hops += 1;
        }

        state.ancestor_query_count = state.ancestor_query_count.saturating_add(1);
        state.last_ancestor_hop_count = hops;
        state.total_ancestor_hop_count = state.total_ancestor_hop_count.saturating_add(hops as u64);
        if hops > state.maximum_ancestor_hop_count {
            state.maximum_ancestor_hop_count = hops;
        }
        node
    }

    /// Returns the immutable value retained for a labeled node.
    ///
    /// # Panics
    ///
    /// Panics when `node` is the bottom node, which has no value, or was never published by this
    /// arena.
    #[must_use]
    pub fn value(&self, node: usize) -> Arc<T> {
        assert!(
            node != BOTTOM,
            "the incremental ancestor arena bottom node has no value"
        );
        let state = self.lock();
        let value = state
            .nodes
            .get(node)
            .value
            .as_ref()
            .expect("a labeled incremental ancestor node retains a value");
        Arc::clone(value)
    }

    /// Returns deterministic representation and traversal counters.
    #[must_use]
    pub fn statistics(&self) -> MyersAncestorStatistics {
        let state = self.lock();
        MyersAncestorStatistics {
            published_node_count: state.nodes.count() - 1,
            block_count: state.nodes.block_count(),
            allocated_slot_count: state.nodes.allocated_slot_count(),
            add_leaf_count: state.add_leaf_count,
            ancestor_query_count: state.ancestor_query_count,
            last_ancestor_hop_count: state.last_ancestor_hop_count,
            maximum_ancestor_hop_count: state.maximum_ancestor_hop_count,
            total_ancestor_hop_count: state.total_ancestor_hop_count,
        }
    }

    /// Acquires the single arena lock, recovering from a poisoned lock.
    ///
    /// Every panic raised inside the critical section happens before any state change, so the
    /// protected state is always consistent and poisoning carries no additional information.
    fn lock(&self) -> MutexGuard<'_, ArenaState<T>> {
        self.state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }
}

impl<T> Default for MyersAncestorArena<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> fmt::Debug for MyersAncestorArena<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let statistics = self.statistics();
        formatter
            .debug_struct("MyersAncestorArena")
            .field("published_node_count", &statistics.published_node_count)
            .field("block_count", &statistics.block_count)
            .finish_non_exhaustive()
    }
}

impl<T: Send + Sync> IncrementalAncestorArena<T> for MyersAncestorArena<T> {
    fn bottom(&self) -> usize {
        MyersAncestorArena::bottom(self)
    }

    fn published_node_count(&self) -> usize {
        MyersAncestorArena::published_node_count(self)
    }

    fn add_leaf(&self, parent: usize, value: T) -> usize {
        MyersAncestorArena::add_leaf(self, parent, value)
    }

    fn depth(&self, node: usize) -> isize {
        MyersAncestorArena::depth(self, node)
    }

    fn parent(&self, node: usize) -> usize {
        MyersAncestorArena::parent(self, node)
    }

    fn ancestor_at_depth(&self, node: usize, depth: isize) -> usize {
        MyersAncestorArena::ancestor_at_depth(self, node, depth)
    }

    fn value(&self, node: usize) -> Arc<T> {
        MyersAncestorArena::value(self, node)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::panic::{AssertUnwindSafe, catch_unwind};
    use std::thread;

    /// A deterministic xorshift generator; the tests must not depend on an external crate.
    struct Xorshift(u64);

    impl Xorshift {
        fn new(seed: u64) -> Self {
            Self(seed | 1)
        }

        fn next_below(&mut self, bound: usize) -> usize {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            (self.0 % bound as u64) as usize
        }
    }

    fn ceiling_log2(value: usize) -> usize {
        let mut power = 1usize;
        let mut result = 0usize;
        while power < value {
            power <<= 1;
            result += 1;
        }
        result
    }

    /// Runs one ancestor query and returns its answer together with the hops it actually needed.
    ///
    /// The hop count is read as the delta of `total_ancestor_hop_count`, which is maintained
    /// independently of `last_ancestor_hop_count`, so the equality asserted here pins the latter to
    /// the work the single query performed.
    fn ancestor_with_measured_hops<T>(
        arena: &MyersAncestorArena<T>,
        node: usize,
        depth: isize,
    ) -> (usize, usize) {
        let before = arena.statistics();
        let ancestor = arena.ancestor_at_depth(node, depth);
        let after = arena.statistics();

        let hops = (after.total_ancestor_hop_count - before.total_ancestor_hop_count) as usize;
        assert_eq!(after.ancestor_query_count, before.ancestor_query_count + 1);
        assert_eq!(
            after.last_ancestor_hop_count, hops,
            "last hop count {} did not match the {hops} hops the query performed",
            after.last_ancestor_hop_count
        );
        assert!(after.maximum_ancestor_hop_count >= hops);
        (ancestor, hops)
    }

    #[test]
    fn a_new_arena_holds_only_its_unlabeled_bottom_node() {
        let arena = MyersAncestorArena::<i32>::new();
        assert_eq!(arena.bottom(), 0);
        assert_eq!(arena.published_node_count(), 0);
        assert_eq!(arena.depth(arena.bottom()), -1);
        assert_eq!(arena.ancestor_at_depth(arena.bottom(), -1), arena.bottom());

        let statistics = arena.statistics();
        assert_eq!(statistics.published_node_count, 0);
        assert_eq!(statistics.block_count, 1);
        assert_eq!(statistics.allocated_slot_count, 1);
        assert_eq!(statistics.add_leaf_count, 0);
        assert_eq!(statistics.ancestor_query_count, 1);
        assert_eq!(statistics.total_ancestor_hop_count, 0);
        assert_eq!(statistics.maximum_ancestor_hop_count, 0);
    }

    #[test]
    fn a_singleton_leaf_hangs_directly_below_bottom() {
        let arena = MyersAncestorArena::new();
        let leaf = arena.add_leaf(arena.bottom(), "only");

        assert_eq!(arena.published_node_count(), 1);
        assert_eq!(arena.depth(leaf), 0);
        assert_eq!(arena.parent(leaf), arena.bottom());
        assert_eq!(*arena.value(leaf), "only");
        assert_eq!(arena.ancestor_at_depth(leaf, 0), leaf);
        assert_eq!(arena.ancestor_at_depth(leaf, -1), arena.bottom());
    }

    #[test]
    fn branched_ancestor_queries_match_a_naive_parent_walk() {
        const CHAIN_LENGTH: usize = 512;
        const BRANCH_COUNT: usize = 512;

        let arena = MyersAncestorArena::new();
        let mut random = Xorshift::new(0xA11CE);
        let mut nodes = vec![arena.bottom()];
        let mut model: HashMap<usize, (usize, isize, i32)> = HashMap::new();
        model.insert(arena.bottom(), (arena.bottom(), -1, 0));

        let mut parent = arena.bottom();
        for value in 0..CHAIN_LENGTH as i32 {
            let node = arena.add_leaf(parent, value);
            let depth = model[&parent].1 + 1;
            nodes.push(node);
            model.insert(node, (parent, depth, value));
            parent = node;
        }

        for offset in 0..BRANCH_COUNT {
            let parent = nodes[random.next_below(nodes.len())];
            let value = (CHAIN_LENGTH + offset) as i32;
            let node = arena.add_leaf(parent, value);
            let depth = model[&parent].1 + 1;
            nodes.push(node);
            model.insert(node, (parent, depth, value));
        }

        assert_eq!(arena.depth(arena.bottom()), -1);
        assert_eq!(arena.published_node_count(), CHAIN_LENGTH + BRANCH_COUNT);

        let mut expected_query_count = 0u64;
        for &node in nodes.iter().skip(1) {
            let (expected_parent, expected_depth, expected_value) = model[&node];
            assert_eq!(arena.depth(node), expected_depth);
            assert_eq!(arena.parent(node), expected_parent);
            assert_eq!(*arena.value(node), expected_value);

            // Naive oracle: walk parent links one level at a time.
            let mut expected_ancestor = node;
            let mut target_depth = expected_depth;
            while target_depth >= -1 {
                assert_eq!(
                    arena.ancestor_at_depth(node, target_depth),
                    expected_ancestor
                );
                expected_query_count += 1;
                if target_depth >= 0 {
                    expected_ancestor = model[&expected_ancestor].0;
                }
                target_depth -= 1;
            }
        }

        assert_eq!(arena.ancestor_at_depth(arena.bottom(), -1), arena.bottom());
        expected_query_count += 1;
        assert_eq!(
            arena.statistics().ancestor_query_count,
            expected_query_count
        );
    }

    #[test]
    fn odd_block_statistics_match_the_exact_square_layout() {
        const MAXIMUM_PUBLISHED_COUNT: usize = 4_096;

        let arena = MyersAncestorArena::new();
        let mut parent = arena.bottom();
        for published_count in 0..=MAXIMUM_PUBLISHED_COUNT {
            let statistics = arena.statistics();
            let expected_block_count = published_count.isqrt() + 1;

            assert_eq!(arena.published_node_count(), published_count);
            assert_eq!(statistics.published_node_count, published_count);
            assert_eq!(statistics.block_count, expected_block_count);
            assert_eq!(
                statistics.allocated_slot_count,
                (expected_block_count * expected_block_count) as u64
            );
            assert_eq!(statistics.add_leaf_count, published_count as u64);
            assert_eq!(statistics.ancestor_query_count, 0);
            assert_eq!(statistics.total_ancestor_hop_count, 0);

            if published_count != MAXIMUM_PUBLISHED_COUNT {
                parent = arena.add_leaf(parent, published_count as i32);
            }
        }
    }

    #[test]
    fn deep_chain_queries_use_conservatively_logarithmic_hops() {
        const NODE_COUNT: usize = 32_768;

        let arena = MyersAncestorArena::new();
        let mut nodes_by_depth = vec![arena.bottom()];
        let mut tail = arena.bottom();
        for value in 0..NODE_COUNT as i32 {
            tail = arena.add_leaf(tail, value);
            nodes_by_depth.push(tail);
        }

        let after_adds = arena.statistics();
        assert_eq!(after_adds.add_leaf_count, NODE_COUNT as u64);
        assert_eq!(after_adds.ancestor_query_count, 0);
        assert_eq!(after_adds.maximum_ancestor_hop_count, 0);
        assert_eq!(after_adds.total_ancestor_hop_count, 0);

        for target_depth in (-1..NODE_COUNT as isize).rev() {
            assert_eq!(
                arena.ancestor_at_depth(tail, target_depth),
                nodes_by_depth[(target_depth + 1) as usize]
            );
        }

        let after_queries = arena.statistics();
        let conservative_logarithmic_bound = 4 * ceiling_log2(NODE_COUNT + 1);
        assert_eq!(after_queries.ancestor_query_count, NODE_COUNT as u64 + 1);
        assert!(after_queries.maximum_ancestor_hop_count >= 1);
        assert!(
            after_queries.maximum_ancestor_hop_count <= conservative_logarithmic_bound,
            "maximum hop count {} exceeded the conservative logarithmic bound {}",
            after_queries.maximum_ancestor_hop_count,
            conservative_logarithmic_bound
        );
        assert!(after_queries.total_ancestor_hop_count >= 1);
        assert!(
            after_queries.total_ancestor_hop_count
                <= (NODE_COUNT as u64 + 1) * conservative_logarithmic_bound as u64
        );
    }

    #[test]
    fn the_last_hop_count_reports_only_the_most_recent_query() {
        const CHAIN_LENGTH: usize = 1_024;

        let arena = MyersAncestorArena::new();
        let mut tail = arena.bottom();
        for value in 0..CHAIN_LENGTH as i32 {
            tail = arena.add_leaf(tail, value);
        }
        let tail_depth = arena.depth(tail);
        assert_eq!(arena.statistics().last_ancestor_hop_count, 0);

        // A query for the node itself terminates before the first hop.
        let (self_ancestor, self_hops) = ancestor_with_measured_hops(&arena, tail, tail_depth);
        assert_eq!(self_ancestor, tail);
        assert_eq!(self_hops, 0);

        // A query for the immediate parent needs exactly one hop: the coalesced jump link of a
        // node at depth d covers more than one level, so only the parent link can be taken.
        let (parent_ancestor, parent_hops) =
            ancestor_with_measured_hops(&arena, tail, tail_depth - 1);
        assert_eq!(parent_ancestor, arena.parent(tail));
        assert_eq!(parent_hops, 1);
        assert_eq!(arena.statistics().last_ancestor_hop_count, 1);

        // A full walk to the bottom node costs more than one hop, and the counter reports that
        // query's own cost rather than a running sum of the queries before it.
        let (bottom_ancestor, bottom_hops) = ancestor_with_measured_hops(&arena, tail, -1);
        assert_eq!(bottom_ancestor, arena.bottom());
        assert!(bottom_hops > 1);

        // Repeating the zero-hop query drives the counter back down, so it is replaced by each
        // query rather than accumulated across queries.
        let (repeated_ancestor, repeated_hops) =
            ancestor_with_measured_hops(&arena, tail, tail_depth);
        assert_eq!(repeated_ancestor, tail);
        assert_eq!(repeated_hops, 0);

        let statistics = arena.statistics();
        assert_eq!(statistics.last_ancestor_hop_count, 0);
        assert_eq!(statistics.maximum_ancestor_hop_count, bottom_hops);
        assert_eq!(
            statistics.total_ancestor_hop_count,
            (self_hops + parent_hops + bottom_hops + repeated_hops) as u64
        );
        assert_eq!(statistics.ancestor_query_count, 4);
    }

    #[test]
    fn branched_queries_use_conservatively_logarithmic_hops() {
        const CHAIN_LENGTH: usize = 64;
        const BRANCH_COUNT: usize = 1_984;

        // An irregular branching arena: most additions extend the current frontier, the rest hang
        // a fresh branch off an arbitrary older node, so jump links coalesce over sibling branches
        // instead of over one straight chain.
        let arena = MyersAncestorArena::new();
        let mut random = Xorshift::new(0x5EED_1234);
        let mut nodes = vec![arena.bottom()];
        let mut parent_of: HashMap<usize, usize> = HashMap::new();
        let mut depth_of: HashMap<usize, isize> = HashMap::new();
        parent_of.insert(arena.bottom(), arena.bottom());
        depth_of.insert(arena.bottom(), -1);

        let mut frontier = arena.bottom();
        let mut branch_point_count = 0usize;
        for value in 0..(CHAIN_LENGTH + BRANCH_COUNT) as i32 {
            let parent = if (value as usize) < CHAIN_LENGTH || random.next_below(8) != 0 {
                frontier
            } else {
                branch_point_count += 1;
                nodes[random.next_below(nodes.len())]
            };
            let node = arena.add_leaf(parent, value);
            parent_of.insert(node, parent);
            depth_of.insert(node, depth_of[&parent] + 1);
            nodes.push(node);
            frontier = node;
        }

        assert_eq!(
            arena.published_node_count(),
            CHAIN_LENGTH + BRANCH_COUNT,
            "the arena must hold every added node"
        );
        assert!(
            branch_point_count > BRANCH_COUNT / 16,
            "the generated arena must actually branch, not degenerate into one chain"
        );

        let mut maximum_depth = -1isize;
        for &node in nodes.iter().skip(1) {
            let node_depth = depth_of[&node];
            assert_eq!(arena.depth(node), node_depth);
            maximum_depth = maximum_depth.max(node_depth);

            // Conservative envelope: logarithmic in this node's own depth, generously padded so
            // the test pins the asymptotic shape rather than the exact coalescing schedule.
            let hop_envelope = 4 * ceiling_log2(node_depth as usize + 2);

            // Naive oracle: walk parent links one level at a time.
            let mut expected_ancestor = node;
            let mut target_depth = node_depth;
            while target_depth >= -1 {
                let (ancestor, hops) = ancestor_with_measured_hops(&arena, node, target_depth);
                assert_eq!(ancestor, expected_ancestor);
                assert!(
                    hops <= hop_envelope,
                    "query for depth {target_depth} of a node at depth {node_depth} took {hops} \
                     hops, exceeding the conservative logarithmic envelope {hop_envelope}"
                );
                if target_depth >= 0 {
                    expected_ancestor = parent_of[&expected_ancestor];
                }
                target_depth -= 1;
            }
        }

        assert!(
            maximum_depth >= CHAIN_LENGTH as isize,
            "the branched arena must stay deep enough to exercise jump links"
        );
        assert!(
            arena.statistics().maximum_ancestor_hop_count
                <= 4 * ceiling_log2(maximum_depth as usize + 2)
        );
    }

    #[test]
    fn published_nodes_stay_immutable_when_old_handles_grow_new_branches() {
        let arena = MyersAncestorArena::new();
        let mut chain = vec![arena.bottom()];
        let mut tail = arena.bottom();
        for value in 0..64 {
            tail = arena.add_leaf(tail, value);
            chain.push(tail);
        }

        let fork = chain[10];
        let mut branch_tail = fork;
        for value in 100..140 {
            branch_tail = arena.add_leaf(branch_tail, value);
        }

        // The retained original chain answers exactly as it did before the branch existed.
        for (index, &node) in chain.iter().enumerate().skip(1) {
            assert_eq!(arena.depth(node), index as isize - 1);
            assert_eq!(arena.parent(node), chain[index - 1]);
            assert_eq!(*arena.value(node), index as i32 - 1);
            for target_depth in -1..=(index as isize - 1) {
                assert_eq!(
                    arena.ancestor_at_depth(node, target_depth),
                    chain[(target_depth + 1) as usize]
                );
            }
        }

        // The branch shares its prefix with the chain and diverges after the fork.
        assert_eq!(arena.ancestor_at_depth(branch_tail, 9), fork);
        assert_eq!(arena.ancestor_at_depth(branch_tail, -1), arena.bottom());
        assert_ne!(arena.ancestor_at_depth(branch_tail, 10), chain[12]);
        assert_eq!(arena.published_node_count(), 104);
    }

    #[test]
    fn value_reads_return_the_one_retained_arc() {
        let arena = MyersAncestorArena::new();
        let node = arena.add_leaf(arena.bottom(), vec![1, 2, 3]);
        let first = arena.value(node);
        let second = arena.value(node);
        assert!(Arc::ptr_eq(&first, &second));
        assert_eq!(*first, vec![1, 2, 3]);
    }

    #[test]
    fn rejected_additions_and_queries_leave_the_arena_unchanged() {
        let arena = MyersAncestorArena::new();
        let mut tail = arena.bottom();
        for value in 0..8 {
            tail = arena.add_leaf(tail, value);
        }
        let _ = arena.ancestor_at_depth(tail, 3);

        let before = arena.statistics();
        assert!(catch_unwind(AssertUnwindSafe(|| arena.add_leaf(usize::MAX, 0))).is_err());
        assert!(catch_unwind(AssertUnwindSafe(|| arena.add_leaf(9_999, 0))).is_err());
        assert!(catch_unwind(AssertUnwindSafe(|| arena.ancestor_at_depth(tail, -2))).is_err());
        assert!(catch_unwind(AssertUnwindSafe(|| arena.ancestor_at_depth(tail, 8))).is_err());
        assert_eq!(arena.statistics(), before);

        // The arena is still fully usable after the rejected calls.
        let extra = arena.add_leaf(tail, 8);
        assert_eq!(arena.depth(extra), 8);
        assert_eq!(arena.published_node_count(), 9);
    }

    #[test]
    #[should_panic(expected = "was never published by this arena")]
    fn depth_rejects_an_unpublished_handle() {
        let arena = MyersAncestorArena::<i32>::new();
        let _ = arena.depth(7);
    }

    #[test]
    #[should_panic(expected = "bottom node has no parent")]
    fn parent_rejects_the_bottom_node() {
        let arena = MyersAncestorArena::<i32>::new();
        let _ = arena.parent(arena.bottom());
    }

    #[test]
    #[should_panic(expected = "bottom node has no value")]
    fn value_rejects_the_bottom_node() {
        let arena = MyersAncestorArena::<i32>::new();
        let _ = arena.value(arena.bottom());
    }

    #[test]
    #[should_panic(expected = "is outside -1..=")]
    fn ancestor_at_depth_rejects_a_depth_below_bottom() {
        let arena = MyersAncestorArena::<i32>::new();
        let _ = arena.ancestor_at_depth(arena.bottom(), -2);
    }

    #[test]
    #[should_panic(expected = "is outside -1..=")]
    fn ancestor_at_depth_rejects_a_depth_below_the_node() {
        let arena = MyersAncestorArena::new();
        let node = arena.add_leaf(arena.bottom(), 1);
        let _ = arena.ancestor_at_depth(node, 1);
    }

    #[test]
    fn the_arena_is_usable_through_a_shared_trait_object() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<MyersAncestorArena<i32>>();

        let arena: Arc<dyn IncrementalAncestorArena<i32>> = Arc::new(MyersAncestorArena::new());
        let first = arena.add_leaf(arena.bottom(), 10);
        let second = arena.add_leaf(first, 20);
        assert_eq!(arena.published_node_count(), 2);
        assert_eq!(arena.depth(second), 1);
        assert_eq!(arena.parent(second), first);
        assert_eq!(*arena.value(second), 20);
        assert_eq!(arena.ancestor_at_depth(second, 0), first);
    }

    #[test]
    fn concurrent_readers_share_one_arena() {
        let arena = Arc::new(MyersAncestorArena::new());
        let mut nodes_by_depth = vec![arena.bottom()];
        let mut tail = arena.bottom();
        for value in 0..512 {
            tail = arena.add_leaf(tail, value);
            nodes_by_depth.push(tail);
        }

        let mut handles = Vec::new();
        for _ in 0..8 {
            let arena = Arc::clone(&arena);
            let nodes_by_depth = nodes_by_depth.clone();
            handles.push(thread::spawn(move || {
                for _ in 0..32 {
                    for target_depth in (-1..512isize).step_by(37) {
                        assert_eq!(
                            arena.ancestor_at_depth(tail, target_depth),
                            nodes_by_depth[(target_depth + 1) as usize]
                        );
                    }
                    assert_eq!(*arena.value(tail), 511);
                    assert_eq!(arena.depth(tail), 511);
                }
            }));
        }
        for handle in handles {
            handle.join().expect("reader thread failed");
        }

        assert_eq!(arena.published_node_count(), 512);
    }
}
