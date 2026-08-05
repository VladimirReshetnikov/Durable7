//! A persistent queue whose every version is one contiguous interval of an append-only tree path.
//!
//! [`AncestralSliceQueue`] represents a sequence *indirectly*: instead of owning nodes, a value is
//! the constant-sized pair `(tail, low_depth)` naming an interval of the unique root-to-`tail` path
//! inside a shared [`IncrementalAncestorArena`], plus a redundant count cache. The visible elements
//! are the labels of the ancestors of `tail` at absolute depths `low_depth, low_depth + 1, ...,
//! depth(tail)`, so:
//!
//! ```text
//! len = depth(tail) - low_depth + 1
//! ```
//!
//! Appending adds one child below `tail`; removing or slicing moves only the two interval
//! boundaries. Because the arena is append-only and its published nodes are immutable, every
//! retained handle keeps denoting exactly the same sequence forever, and adding below an *old*
//! handle simply starts a sibling branch. That is what makes the structure fully persistent for its
//! restricted algebra: any historical version can be extended, not just the newest one. It is not
//! confluently persistent, because two unrelated versions cannot be merged.
//!
//! # The anchored-empty rule
//!
//! An empty value is not "nothing". It retains the node immediately *before* its window as an
//! anchor, so `low_depth == depth(tail) + 1` holds whenever [`is_empty`] does. Appending to any
//! empty slice therefore yields exactly the new value and never resurrects the discarded prefix.
//! Empty values reached through different histories can consequently have distinct provenance while
//! denoting the same empty sequence; there is no canonical empty and no process-global empty arena.
//!
//! # Bounds
//!
//! The bounds are *parameterized by the backend*. Let `M` be the number of labeled nodes ever
//! published by the arena, `U(M)` the cost of adding a leaf, and `Q(M)` the cost of one
//! level-ancestor query. Then [`push_back`] costs `U(M)`; [`first`], [`get`], [`take`],
//! [`get_range`], and a nontrivial [`split_at`] cost `Q(M)`; and [`len`], [`is_empty`], [`last`],
//! [`remove_first`], [`remove_last`], [`pop_back`], and [`skip`] are O(1). [`pop_front`] costs
//! `Q(M)` because it also returns the front element. Every operation allocates O(1) new words
//! besides the single arena node that [`push_back`] publishes. Enumeration walks parent links into
//! one temporary array, taking Θ(len) time and Θ(len) transient element slots.
//!
//! Instantiating the backend with Alstrup-Holm incremental level ancestry would give
//! `U(M) = Q(M) = O(1)` *worst case* in linear space, making every scalar operation above O(1)
//! worst case. **That backend is not implemented here.** The statement is a reduction to a
//! published result, not a property of anything in this crate. The shipped [`MyersAncestorArena`]
//! instead has O(1) *amortized* leaf addition and O(log M) worst-case ancestor queries, so the
//! shipped bounds are: [`push_back`] O(1) amortized; [`first`], [`get`], [`take`], [`pop_front`], a
//! [`get_range`] whose result tail moves, and a nontrivial [`split_at`] O(log M) worst case; and
//! [`len`], [`is_empty`], [`last`], [`remove_first`], [`remove_last`], [`pop_back`], [`skip`], and
//! a suffix range that keeps the old tail O(1) worst case. Boundary-specialized calls such as
//! `take(len())`, `skip(0)`, `get_range(s, len() - s)`, or a split at `len()` reuse an existing or
//! derived tail and make no ancestor query at all. No amortized bound above may be read as a
//! worst-case bound.
//!
//! # Deliberate limits
//!
//! The algebra intentionally excludes prepending, point replacement, arbitrary middle edits, and
//! concatenation of unrelated histories — omitting them is precisely why the indexed and slicing
//! bounds are possible, and no dominance claim here counts an operation the queue does not support.
//! Arena space is charged to *all* successful historical appends, not to the visible length of one
//! handle: dropping a prefix or releasing a handle reclaims nothing, and every published payload
//! stays reachable until the arena itself is dropped. Two handles may enumerate equal values while
//! having different tails, anchors, or arenas, so this type promises no O(1) sequence equality,
//! hashing, or canonical empty identity and implements none of them.
//!
//! Snapshots clone in O(1) and are `Send + Sync` whenever the arena is, so any number of readers
//! may share one version. The shipped arena serializes its reads and writes on one private lock;
//! that is a semantic thread-safety property, not a parallel-progress guarantee.
//!
//! [`is_empty`]: AncestralSliceQueue::is_empty
//! [`len`]: AncestralSliceQueue::len
//! [`first`]: AncestralSliceQueue::first
//! [`last`]: AncestralSliceQueue::last
//! [`get`]: AncestralSliceQueue::get
//! [`push_back`]: AncestralSliceQueue::push_back
//! [`remove_first`]: AncestralSliceQueue::remove_first
//! [`remove_last`]: AncestralSliceQueue::remove_last
//! [`pop_front`]: AncestralSliceQueue::pop_front
//! [`pop_back`]: AncestralSliceQueue::pop_back
//! [`take`]: AncestralSliceQueue::take
//! [`skip`]: AncestralSliceQueue::skip
//! [`get_range`]: AncestralSliceQueue::get_range
//! [`split_at`]: AncestralSliceQueue::split_at

use crate::incremental_ancestor::{IncrementalAncestorArena, MyersAncestorArena};
use std::error::Error;
use std::fmt;
use std::iter::FusedIterator;
use std::sync::Arc;

/// The failure returned when an arena cannot anchor an empty ancestral slice queue.
///
/// The anchored-empty rule requires the arena's bottom node to sit at depth -1, so that a queue
/// anchored there denotes the empty sequence beginning at depth zero.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AncestralSliceQueueArenaError;

impl fmt::Display for AncestralSliceQueueArenaError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .write_str("an ancestral slice queue arena must report depth -1 for its bottom node")
    }
}

impl Error for AncestralSliceQueueArenaError {}

/// One removed endpoint value and the persistent queue that remains without it.
///
/// The value is an owned shared handle, so the result stays usable independently of both the source
/// queue and the remainder without requiring `T: Clone`.
#[derive(Debug)]
pub struct AncestralSliceQueuePop<T: 'static> {
    /// The removed endpoint value.
    pub value: Arc<T>,
    /// The persistent queue remaining after that endpoint is removed.
    pub rest: AncestralSliceQueue<T>,
}

impl<T> Clone for AncestralSliceQueuePop<T> {
    /// Clones both constant-sized handles in O(1). Written by hand rather than derived, because a
    /// derived `Clone` would impose `T: Clone` that the `Arc`-backed fields do not need.
    fn clone(&self) -> Self {
        Self {
            value: Arc::clone(&self.value),
            rest: self.rest.clone(),
        }
    }
}

/// The two appendable ancestry intervals produced by [`AncestralSliceQueue::split_at`].
#[derive(Debug)]
pub struct AncestralSliceQueueSplit<T: 'static> {
    /// The prefix holding the first `index` visible values.
    pub left: AncestralSliceQueue<T>,
    /// The suffix holding the remaining visible values.
    pub right: AncestralSliceQueue<T>,
}

impl<T> Clone for AncestralSliceQueueSplit<T> {
    /// Clones both constant-sized handles in O(1). Written by hand rather than derived, because a
    /// derived `Clone` would impose `T: Clone` that the `Arc`-backed fields do not need.
    fn clone(&self) -> Self {
        Self {
            left: self.left.clone(),
            right: self.right.clone(),
        }
    }
}

/// An immutable, fully persistent queue slice denoting one interval of an append ancestry path.
///
/// A value retains its arena, a tail node, the absolute depth of its first visible element, and a
/// redundant count cache. Empty values retain the node immediately before their window as an
/// anchor, so appending to any empty slice yields exactly the new value without exposing a
/// discarded prefix. All handles are immutable; adding below an old handle creates a sibling branch
/// in the shared monotone arena.
///
/// See the [module documentation](self) for the parameterized and shipped complexity tables and for
/// the operations this algebra deliberately omits.
pub struct AncestralSliceQueue<T: 'static> {
    arena: Arc<dyn IncrementalAncestorArena<T>>,
    tail: usize,
    low_depth: isize,
    count: usize,
}

impl<T> Clone for AncestralSliceQueue<T> {
    /// Clones the constant-sized handle in O(1); the arena is shared, never copied.
    fn clone(&self) -> Self {
        Self {
            arena: Arc::clone(&self.arena),
            tail: self.tail,
            low_depth: self.low_depth,
            count: self.count,
        }
    }
}

impl<T: Send + Sync + 'static> AncestralSliceQueue<T> {
    /// Creates an empty queue backed by a fresh, independently droppable Myers jump-link arena.
    ///
    /// The shipped arena gives O(1)-amortized appends and O(log M) ancestor queries after M
    /// historical appends. There is deliberately no shared global empty value: each call owns its
    /// own arena, so unrelated workloads never share retained history or write contention.
    #[must_use]
    pub fn new() -> Self {
        Self::with_arena(Arc::new(MyersAncestorArena::new()))
            .expect("the shipped Myers arena publishes its bottom node at depth -1")
    }
}

impl<T: Send + Sync + 'static> Default for AncestralSliceQueue<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> AncestralSliceQueue<T> {
    /// Creates an empty queue owned by an explicit incremental-ancestor arena.
    ///
    /// This is the public extension seam: supplying a backend with different `U(M)`/`Q(M)` costs
    /// changes every parameterized bound documented here. The arena retains and navigates every
    /// appended node.
    ///
    /// # Errors
    ///
    /// Returns [`AncestralSliceQueueArenaError`] when the arena does not report depth -1 for its
    /// bottom node.
    pub fn with_arena(
        arena: Arc<dyn IncrementalAncestorArena<T>>,
    ) -> Result<Self, AncestralSliceQueueArenaError> {
        let bottom = arena.bottom();
        if arena.depth(bottom) != -1 {
            return Err(AncestralSliceQueueArenaError);
        }
        Ok(Self {
            arena,
            tail: bottom,
            low_depth: 0,
            count: 0,
        })
    }

    /// Returns the number of visible values.
    ///
    /// O(1) from the cached count; the arena is not consulted.
    #[must_use]
    pub fn len(&self) -> usize {
        self.count
    }

    /// Returns whether this handle denotes an empty ancestry interval. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Returns the first visible value, or `None` when empty.
    ///
    /// Costs one ancestor query `Q(M)`: the front is an ancestor selected jointly by the tail and
    /// the low boundary, so it is not an O(1) endpoint read under a logarithmic backend. A
    /// singleton reuses its retained tail and makes no query.
    #[must_use]
    pub fn first(&self) -> Option<Arc<T>> {
        if self.count == 0 {
            return None;
        }
        let node = if self.count == 1 {
            self.tail
        } else {
            self.arena.ancestor_at_depth(self.tail, self.low_depth)
        };
        Some(self.arena.value(node))
    }

    /// Returns the last visible value, or `None` when empty.
    ///
    /// O(1): the tail is retained directly by the handle.
    #[must_use]
    pub fn last(&self) -> Option<Arc<T>> {
        (self.count != 0).then(|| self.arena.value(self.tail))
    }

    /// Returns the visible value at a zero-based index, or `None` when the index is out of range.
    ///
    /// Costs one ancestor query `Q(M)`; the last index reuses the retained tail and makes no query.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<Arc<T>> {
        if index >= self.count {
            return None;
        }
        let node = if index == self.count - 1 {
            self.tail
        } else {
            self.arena
                .ancestor_at_depth(self.tail, offset_depth(self.low_depth, index))
        };
        Some(self.arena.value(node))
    }

    /// Returns a queue with one value appended below this handle's tail or empty anchor.
    ///
    /// Costs `U(M)` and publishes exactly one arena node. The source handle and every other
    /// retained version are unaffected; appending below an old handle starts a sibling branch.
    ///
    /// # Panics
    ///
    /// Panics when the visible count would exceed [`usize::MAX`], or when the arena rejects the
    /// addition because the ancestry depth or its node count would overflow. A rejected addition
    /// publishes nothing and leaves the source handle valid.
    #[must_use]
    pub fn push_back(&self, value: T) -> Self {
        let count = self
            .count
            .checked_add(1)
            .expect("ancestral slice queue count overflow");
        let tail = self.arena.add_leaf(self.tail, value);
        Self {
            arena: Arc::clone(&self.arena),
            tail,
            low_depth: self.low_depth,
            count,
        }
    }

    /// Returns the queue without its first visible value, or `None` when empty. O(1).
    ///
    /// Only the low boundary moves, so no ancestor query is made. Removing the one visible value of
    /// a singleton this way leaves the old tail as the empty result's anchor.
    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        (self.count != 0).then(|| Self {
            arena: Arc::clone(&self.arena),
            tail: self.tail,
            low_depth: offset_depth(self.low_depth, 1),
            count: self.count - 1,
        })
    }

    /// Returns the queue without its last visible value, or `None` when empty. O(1).
    ///
    /// The new tail is the old tail's parent, which the arena reads in constant time. Emptying a
    /// singleton this way anchors the result immediately before the window.
    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        (self.count != 0).then(|| Self {
            arena: Arc::clone(&self.arena),
            tail: self.arena.parent(self.tail),
            low_depth: self.low_depth,
            count: self.count - 1,
        })
    }

    /// Removes and returns the first visible value together with the remainder, or `None` when
    /// empty.
    ///
    /// Costs `Q(M)` because it also returns the front value. Nothing is published on the empty
    /// path, and the source handle stays valid either way.
    #[must_use]
    pub fn pop_front(&self) -> Option<AncestralSliceQueuePop<T>> {
        let value = self.first()?;
        Some(AncestralSliceQueuePop {
            value,
            rest: self.remove_first().expect("a non-empty queue has a front"),
        })
    }

    /// Removes and returns the last visible value together with the remainder, or `None` when
    /// empty. O(1).
    #[must_use]
    pub fn pop_back(&self) -> Option<AncestralSliceQueuePop<T>> {
        let value = self.last()?;
        Some(AncestralSliceQueuePop {
            value,
            rest: self.remove_last().expect("a non-empty queue has a back"),
        })
    }

    /// Returns the first `count` visible values, or `None` when `count` exceeds the length.
    ///
    /// Costs `Q(M)`; `take(len())` returns the source handle unchanged and makes no query. The
    /// result is a real ancestry slice and remains appendable, and `take(0)` is anchored
    /// immediately before the window.
    #[must_use]
    pub fn take(&self, count: usize) -> Option<Self> {
        if count > self.count {
            return None;
        }
        if count == self.count {
            return Some(self.clone());
        }
        self.get_range(0, count)
    }

    /// Returns the queue after omitting its first `count` visible values, or `None` when `count`
    /// exceeds the length. O(1).
    ///
    /// Only the low boundary moves. `skip(len())` keeps the old tail as the empty result's anchor.
    /// This is the reference contract's `Drop`; the name `drop` would collide with the [`Drop`]
    /// trait.
    #[must_use]
    pub fn skip(&self, count: usize) -> Option<Self> {
        if count > self.count {
            return None;
        }
        if count == 0 {
            return Some(self.clone());
        }
        Some(Self {
            arena: Arc::clone(&self.arena),
            tail: self.tail,
            low_depth: offset_depth(self.low_depth, count),
            count: self.count - count,
        })
    }

    /// Returns one contiguous, appendable ancestry slice, or `None` for an invalid range.
    ///
    /// Costs `Q(M)` in general. A range whose result ends at the current tail — the whole range and
    /// every suffix — reuses that tail and makes no query. A zero-length range selects the ancestor
    /// immediately before its start as an anchor, which is the arena's bottom node when the start
    /// is the very front of a queue grown from an empty one.
    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = index.checked_add(count)?;
        if end > self.count {
            return None;
        }
        if index == 0 && count == self.count {
            return Some(self.clone());
        }

        let low_depth = offset_depth(self.low_depth, index);
        let target_depth = window_end_depth(low_depth, count);
        let current_tail_depth = window_end_depth(self.low_depth, self.count);
        let tail = if target_depth == current_tail_depth {
            self.tail
        } else {
            self.arena.ancestor_at_depth(self.tail, target_depth)
        };
        Some(Self {
            arena: Arc::clone(&self.arena),
            tail,
            low_depth,
            count,
        })
    }

    /// Splits this queue at a zero-based boundary, or returns `None` when the boundary exceeds the
    /// length.
    ///
    /// Costs `Q(M)`: the prefix performs the one ancestor-seeking query and the suffix only moves
    /// the low boundary. Both results are real ancestry slices and can independently start new
    /// branches.
    ///
    /// Only a split at `len()` makes no query: the prefix is the source handle itself and the
    /// suffix moves the low boundary past the tail. A split at `0` on a non-empty queue still costs
    /// `Q(M)`, and cannot be specialized away — the anchored-empty rule requires the empty prefix to
    /// retain the node at depth `low_depth - 1`, and only an ancestor query can name that node from
    /// the retained tail.
    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<AncestralSliceQueueSplit<T>> {
        if index > self.count {
            return None;
        }
        Some(AncestralSliceQueueSplit {
            left: self.take(index).expect("a validated boundary is takeable"),
            right: self.skip(index).expect("a validated boundary is skippable"),
        })
    }

    /// Collects the visible values in front-to-back order as retained shared handles.
    ///
    /// Follows parent links from the tail into one temporary array, taking Θ(len) time and Θ(len)
    /// transient element slots rather than performing `len` ancestor queries. Concurrent appends
    /// cannot enter the captured path.
    #[must_use]
    pub fn to_vec(&self) -> Vec<Arc<T>> {
        let mut values = Vec::with_capacity(self.count);
        if self.count == 0 {
            return values;
        }

        let mut node = self.tail;
        for remaining in (0..self.count).rev() {
            values.push(self.arena.value(node));
            if remaining != 0 {
                node = self.arena.parent(node);
            }
        }
        values.reverse();
        values
    }

    /// Returns a stable front-to-back iterator over this immutable ancestry interval.
    ///
    /// The iterator captures the whole path up front, so it observes exactly the handle it was
    /// obtained from and never sees a later append. See [`to_vec`](Self::to_vec) for the cost.
    #[must_use]
    pub fn iter(&self) -> AncestralSliceQueueIter<T> {
        AncestralSliceQueueIter {
            inner: self.to_vec().into_iter(),
        }
    }
}

impl<T: Send + Sync + 'static> FromIterator<T> for AncestralSliceQueue<T> {
    /// Builds a Myers-backed queue by appending the values in iteration order.
    ///
    /// Publishes one arena node per value, for Θ(k) amortized total time on the shipped backend.
    fn from_iter<I: IntoIterator<Item = T>>(values: I) -> Self {
        let mut result = Self::new();
        for value in values {
            result = result.push_back(value);
        }
        result
    }
}

impl<T> IntoIterator for &AncestralSliceQueue<T> {
    type Item = Arc<T>;
    type IntoIter = AncestralSliceQueueIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T: fmt::Debug> fmt::Debug for AncestralSliceQueue<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

/// A front-to-back iterator over one immutable ancestral slice.
///
/// Values are yielded as retained [`Arc`] handles, so iteration imposes no `T: Clone` bound.
pub struct AncestralSliceQueueIter<T> {
    inner: std::vec::IntoIter<Arc<T>>,
}

impl<T> Iterator for AncestralSliceQueueIter<T> {
    type Item = Arc<T>;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> ExactSizeIterator for AncestralSliceQueueIter<T> {}

impl<T> FusedIterator for AncestralSliceQueueIter<T> {}

impl<T> fmt::Debug for AncestralSliceQueueIter<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("AncestralSliceQueueIter")
            .field("remaining", &self.inner.len())
            .finish()
    }
}

/// Returns `base + offset` as an absolute ancestry depth.
///
/// # Panics
///
/// Panics on depth overflow, matching how this crate reports capacity overflow elsewhere.
fn offset_depth(base: isize, offset: usize) -> isize {
    isize::try_from(offset)
        .ok()
        .and_then(|offset| base.checked_add(offset))
        .expect("ancestral slice queue ancestry-depth overflow")
}

/// Returns the absolute depth of the last element of a `count`-long window starting at `low_depth`.
///
/// An empty window yields `low_depth - 1`, which is exactly the anchor depth the anchored-empty
/// rule requires.
///
/// # Panics
///
/// Panics on depth overflow, matching how this crate reports capacity overflow elsewhere.
fn window_end_depth(low_depth: isize, count: usize) -> isize {
    offset_depth(low_depth, count)
        .checked_sub(1)
        .expect("ancestral slice queue ancestry-depth overflow")
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::incremental_ancestor::MyersAncestorStatistics;
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

    /// An element type that deliberately does not implement `Clone`.
    #[derive(Debug, PartialEq)]
    struct Unclonable(i32);

    /// An arena whose bottom node is not at depth -1, which the queue must refuse.
    struct FlatBottomArena;

    impl<T: Send + Sync> IncrementalAncestorArena<T> for FlatBottomArena {
        fn bottom(&self) -> usize {
            0
        }

        fn published_node_count(&self) -> usize {
            0
        }

        fn add_leaf(&self, _parent: usize, _value: T) -> usize {
            unreachable!("the queue must reject this arena before using it")
        }

        fn depth(&self, _node: usize) -> isize {
            0
        }

        fn parent(&self, _node: usize) -> usize {
            unreachable!("the queue must reject this arena before using it")
        }

        fn ancestor_at_depth(&self, _node: usize, _depth: isize) -> usize {
            unreachable!("the queue must reject this arena before using it")
        }

        fn value(&self, _node: usize) -> Arc<T> {
            unreachable!("the queue must reject this arena before using it")
        }
    }

    fn queue_of<I>(values: I) -> AncestralSliceQueue<i32>
    where
        I: IntoIterator<Item = i32>,
    {
        values.into_iter().collect()
    }

    fn values_of<T: Clone>(queue: &AncestralSliceQueue<T>) -> Vec<T> {
        queue
            .to_vec()
            .iter()
            .map(|value| (**value).clone())
            .collect()
    }

    /// Checks every read the reference `AssertState` helper checks against one expected model.
    fn assert_state<T>(queue: &AncestralSliceQueue<T>, expected: &[T])
    where
        T: Clone + fmt::Debug + PartialEq + Send + Sync + 'static,
    {
        assert_eq!(queue.len(), expected.len());
        assert_eq!(queue.is_empty(), expected.is_empty());
        assert_eq!(values_of(queue), expected.to_vec());
        assert_eq!(
            queue
                .iter()
                .map(|value| (*value).clone())
                .collect::<Vec<_>>(),
            expected.to_vec()
        );

        for (index, item) in expected.iter().enumerate() {
            assert_eq!(&*queue.get(index).expect("index inside the queue"), item);
        }
        assert!(queue.get(expected.len()).is_none());

        if expected.is_empty() {
            assert!(queue.first().is_none());
            assert!(queue.last().is_none());
            assert!(queue.remove_first().is_none());
            assert!(queue.remove_last().is_none());
            assert!(queue.pop_front().is_none());
            assert!(queue.pop_back().is_none());
        } else {
            assert_eq!(&*queue.first().expect("non-empty front"), &expected[0]);
            assert_eq!(
                &*queue.last().expect("non-empty back"),
                &expected[expected.len() - 1]
            );
        }
    }

    fn assert_arena_delta(
        arena: &MyersAncestorArena<i32>,
        expected_adds: u64,
        expected_queries: u64,
        action: impl FnOnce(),
    ) {
        let before: MyersAncestorStatistics = arena.statistics();
        action();
        let after = arena.statistics();

        assert_eq!(after.add_leaf_count, before.add_leaf_count + expected_adds);
        assert_eq!(
            after.published_node_count,
            before.published_node_count + expected_adds as usize
        );
        assert_eq!(
            after.ancestor_query_count,
            before.ancestor_query_count + expected_queries
        );
    }

    #[test]
    fn empty_singleton_and_stored_option_values_use_queue_endpoint_contracts() {
        let empty = AncestralSliceQueue::<Option<String>>::new();

        assert_state(&empty, &[]);
        assert!(empty.get(0).is_none());
        assert!(empty.pop_front().is_none());
        assert!(empty.pop_back().is_none());

        // A stored `None` is a present value; absence is reported by the outer `Option`.
        let singleton = empty.push_back(None);
        assert_state(&singleton, &[None]);
        assert_eq!(*singleton.first().expect("a present stored None"), None);
        assert_eq!(*singleton.last().expect("a present stored None"), None);

        let pair = singleton.push_back(Some("tail".to_owned()));
        assert_state(&pair, &[None, Some("tail".to_owned())]);
        assert_state(&singleton, &[None]);

        let popped_front = pair.pop_front().expect("a non-empty queue pops");
        assert_eq!(*popped_front.value, None);
        assert_state(&popped_front.rest, &[Some("tail".to_owned())]);

        let popped_back = pair.pop_back().expect("a non-empty queue pops");
        assert_eq!(*popped_back.value, Some("tail".to_owned()));
        assert_state(&popped_back.rest, &[None]);
        assert_state(&pair, &[None, Some("tail".to_owned())]);
    }

    #[test]
    fn every_range_matches_the_model_and_empty_anchors_remain_appendable() {
        const LENGTH: usize = 65;
        let values = (0..LENGTH as i32).collect::<Vec<_>>();
        let source = queue_of(values.iter().copied());

        for start in 0..=LENGTH {
            for count in 0..=(LENGTH - start) {
                let slice = source.get_range(start, count).expect("valid range");
                assert_state(&slice, &values[start..start + count]);

                if count == 0 {
                    // The anchored-empty rule: appending to any empty slice yields exactly the new
                    // values and never resurrects the discarded prefix.
                    let marker = start as i32;
                    let continued = slice.push_back(-marker).push_back(10_000 + marker);
                    assert_state(&continued, &[-marker, 10_000 + marker]);
                    assert_state(&slice, &[]);
                }
            }
        }

        assert_state(
            &source.get_range(0, source.len()).expect("whole range"),
            &values,
        );
        assert_state(&source.take(source.len()).expect("whole prefix"), &values);
        assert_state(&source.skip(0).expect("empty skip"), &values);
        assert_state(&source, &values);
    }

    #[test]
    fn take_skip_and_split_at_partition_the_source_at_every_boundary() {
        let values = (0..257i32).collect::<Vec<_>>();
        let source = queue_of(values.iter().copied());

        for position in 0..=values.len() {
            let prefix = source.take(position).expect("valid prefix");
            let suffix = source.skip(position).expect("valid suffix");
            assert_state(&prefix, &values[..position]);
            assert_state(&suffix, &values[position..]);

            let split = source.split_at(position).expect("valid split");
            assert_state(&split.left, &values[..position]);
            assert_state(&split.right, &values[position..]);

            // Both results remain real ancestry slices and start independent branches.
            let marker = -(position as i32) - 1;
            let mut expected_prefix = values[..position].to_vec();
            expected_prefix.push(marker);
            assert_state(&prefix.push_back(marker), &expected_prefix);

            let mut expected_suffix = values[position..].to_vec();
            expected_suffix.push(marker);
            assert_state(&suffix.push_back(marker), &expected_suffix);
        }

        assert_state(&source, &values);
    }

    #[test]
    fn square_and_odd_block_boundaries_support_index_range_and_branching() {
        const MAXIMUM_ROOT: usize = 128;
        let length = MAXIMUM_ROOT * MAXIMUM_ROOT + 1;
        let values = (0..length as i32).collect::<Vec<_>>();
        let source = queue_of(values.iter().copied());

        for root in 0..=MAXIMUM_ROOT {
            let square = root * root;
            assert_eq!(*source.get(square).expect("square index"), square as i32);

            let start = square.saturating_sub(1);
            let count = 3.min(source.len() - start);
            assert_state(
                &source.get_range(start, count).expect("seam range"),
                &values[start..start + count],
            );

            let marker = -(square as i32) - 1;
            let empty_at_square = source.get_range(square, 0).expect("empty range");
            assert_state(&empty_at_square.push_back(marker), &[marker]);

            if square > 0 {
                // The queue length equals the tail handle here, so the versions of length
                // square - 1 and square end immediately before and at the odd-block seam.
                let before_seam = source.take(square - 1).expect("prefix before the seam");
                let mut expected = values[..square - 1].to_vec();
                expected.push(marker - 1);
                assert_state(&before_seam.push_back(marker - 1), &expected);

                let at_seam = source.take(square).expect("prefix at the seam");
                let mut expected = values[..square].to_vec();
                expected.push(marker - 2);
                assert_state(&at_seam.push_back(marker - 2), &expected);
            }

            if root == MAXIMUM_ROOT {
                continue;
            }

            let next_square = (root + 1) * (root + 1);
            assert_eq!(next_square - square, 2 * root + 1);
            assert_state(
                &source
                    .get_range(square, next_square - square)
                    .expect("whole odd block"),
                &values[square..next_square],
            );
        }

        assert_state(&source, &values);
    }

    #[test]
    fn iteration_is_repeatable_and_does_not_observe_later_branches() {
        let values = (0..100i32).collect::<Vec<_>>();
        let source = queue_of(values.iter().copied());
        let slice = source.get_range(20, 40).expect("interior slice");
        let captured = slice.iter();

        let branch = slice.push_back(-1).push_back(-2);

        assert_eq!(values_of(&slice), values[20..60].to_vec());
        assert_eq!(values_of(&slice), values[20..60].to_vec());
        let mut expected_branch = values[20..60].to_vec();
        expected_branch.extend([-1, -2]);
        assert_eq!(values_of(&branch), expected_branch);

        // The iterator captured before the branch existed still yields the original path.
        let captured_values = captured.map(|value| *value).collect::<Vec<_>>();
        assert_eq!(captured_values, values[20..60].to_vec());
        assert_state(&source, &values);
    }

    #[test]
    fn queue_operations_use_the_parameterized_backend_complexity_boundary() {
        let arena = Arc::new(MyersAncestorArena::<i32>::new());
        let mut queue = AncestralSliceQueue::with_arena(
            Arc::clone(&arena) as Arc<dyn IncrementalAncestorArena<i32>>
        )
        .expect("the Myers arena anchors at depth -1");
        for value in 0..16 {
            queue = queue.push_back(value);
        }

        assert_arena_delta(&arena, 0, 0, || {
            assert_eq!(queue.len(), 16);
            assert!(!queue.is_empty());
            assert_eq!(*queue.last().expect("non-empty back"), 15);
            assert_eq!(*queue.get(15).expect("tail index"), 15);
        });
        assert_arena_delta(&arena, 0, 1, || {
            assert_eq!(*queue.first().expect("non-empty front"), 0);
        });
        assert_arena_delta(&arena, 0, 1, || {
            assert_eq!(*queue.get(7).expect("interior index"), 7);
        });
        assert_arena_delta(&arena, 0, 0, || {
            assert_eq!(queue.remove_first().expect("non-empty").len(), 15);
        });
        assert_arena_delta(&arena, 0, 0, || {
            assert_eq!(queue.remove_last().expect("non-empty").len(), 15);
        });
        assert_arena_delta(&arena, 0, 0, || {
            assert_eq!(queue.skip(7).expect("valid skip").len(), 9);
        });
        assert_arena_delta(&arena, 0, 1, || {
            assert_eq!(queue.take(7).expect("valid prefix").len(), 7);
        });
        assert_arena_delta(&arena, 0, 1, || {
            assert_eq!(queue.get_range(3, 5).expect("interior range").len(), 5);
        });
        assert_arena_delta(&arena, 0, 0, || {
            // A suffix range keeps the old tail, so it makes no ancestor query.
            assert_eq!(queue.get_range(3, 13).expect("suffix range").len(), 13);
        });
        assert_arena_delta(&arena, 0, 1, || {
            let split = queue.split_at(7).expect("valid split");
            assert_eq!(split.left.len(), 7);
            assert_eq!(split.right.len(), 9);
        });
        assert_arena_delta(&arena, 0, 1, || {
            // A split at 0 is not query-free: the anchored-empty rule makes the empty prefix retain
            // the node at low_depth - 1, which only an ancestor query can name from the tail.
            let split = queue.split_at(0).expect("valid split");
            assert_eq!(split.left.len(), 0);
            assert_eq!(split.right.len(), 16);
            assert_eq!(*split.right.last().expect("non-empty back"), 15);
        });
        assert_arena_delta(&arena, 0, 0, || {
            // A split at len() reuses the source tail for the prefix and only moves the low
            // boundary for the empty suffix, so it makes no query at all.
            let split = queue.split_at(queue.len()).expect("valid split");
            assert_eq!(split.left.len(), 16);
            assert_eq!(split.right.len(), 0);
        });
        assert_arena_delta(&arena, 0, 1, || {
            let popped = queue.pop_front().expect("non-empty");
            assert_eq!(*popped.value, 0);
            assert_eq!(popped.rest.len(), 15);
        });
        assert_arena_delta(&arena, 0, 0, || {
            let popped = queue.pop_back().expect("non-empty");
            assert_eq!(*popped.value, 15);
            assert_eq!(popped.rest.len(), 15);
        });
        assert_arena_delta(&arena, 1, 0, || {
            let appended = queue.push_back(16);
            assert_eq!(appended.len(), 17);
            assert_eq!(*appended.last().expect("non-empty back"), 16);
        });
    }

    #[test]
    fn factories_create_independent_queues_and_reject_an_unanchored_arena() {
        let arena = Arc::new(MyersAncestorArena::<i32>::new());
        let explicit = AncestralSliceQueue::with_arena(
            Arc::clone(&arena) as Arc<dyn IncrementalAncestorArena<i32>>
        )
        .expect("the Myers arena anchors at depth -1")
        .push_back(10)
        .push_back(20);
        assert_state(&explicit, &[10, 20]);
        assert_eq!(arena.published_node_count(), 2);

        let range = queue_of([1, 2, 3]);
        assert_state(&range, &[1, 2, 3]);
        assert_state(&range.push_back(4), &[1, 2, 3, 4]);
        assert_state(&range, &[1, 2, 3]);

        // Each convenience factory owns a private arena, so unrelated queues share no history.
        let first = AncestralSliceQueue::<i32>::new().push_back(1);
        let second = AncestralSliceQueue::<i32>::default().push_back(2);
        assert_state(&first, &[1]);
        assert_state(&second, &[2]);

        let rejected = AncestralSliceQueue::<i32>::with_arena(Arc::new(FlatBottomArena));
        assert_eq!(rejected.err(), Some(AncestralSliceQueueArenaError));
        assert_eq!(
            AncestralSliceQueueArenaError.to_string(),
            "an ancestral slice queue arena must report depth -1 for its bottom node"
        );
    }

    #[test]
    fn endpoint_removal_histories_retain_every_version() {
        let values = (0..512i32).collect::<Vec<_>>();
        let source = queue_of(values.iter().copied());
        let mut front_versions = vec![source.clone()];
        let mut back_versions = vec![source.clone()];

        for _ in 0..values.len() {
            let next_front = front_versions
                .last()
                .expect("seeded")
                .remove_first()
                .expect("non-empty");
            front_versions.push(next_front);

            let next_back = back_versions
                .last()
                .expect("seeded")
                .remove_last()
                .expect("non-empty");
            back_versions.push(next_back);
        }

        for removed in 0..=values.len() {
            assert_state(&front_versions[removed], &values[removed..]);
            assert_state(&back_versions[removed], &values[..values.len() - removed]);
        }

        // Both fully drained versions keep different anchors but behave as empty queues.
        assert_state(&front_versions[values.len()].push_back(-1), &[-1]);
        assert_state(&back_versions[values.len()].push_back(-2), &[-2]);
        assert_state(&source, &values);
    }

    #[test]
    fn retained_branches_from_whole_and_sliced_versions_stay_independent() {
        let values = (0..200i32).collect::<Vec<_>>();
        let source = queue_of(values.iter().copied());
        let middle = source.get_range(50, 100).expect("interior slice");

        let branches = [
            middle.push_back(-1),
            middle.push_back(-2).push_back(-3),
            middle.remove_first().expect("non-empty").push_back(-4),
            middle.remove_last().expect("non-empty").push_back(-5),
            middle.take(40).expect("valid prefix").push_back(-6),
            middle.skip(60).expect("valid suffix").push_back(-7),
            middle.get_range(30, 0).expect("empty range").push_back(-8),
        ];

        let expect = |range: std::ops::Range<usize>, suffix: &[i32]| {
            let mut expected = values[range].to_vec();
            expected.extend_from_slice(suffix);
            expected
        };

        assert_state(&branches[0], &expect(50..150, &[-1]));
        assert_state(&branches[1], &expect(50..150, &[-2, -3]));
        assert_state(&branches[2], &expect(51..150, &[-4]));
        assert_state(&branches[3], &expect(50..149, &[-5]));
        assert_state(&branches[4], &expect(50..90, &[-6]));
        assert_state(&branches[5], &expect(110..150, &[-7]));
        assert_state(&branches[6], &[-8]);
        assert_state(&middle, &values[50..150]);
        assert_state(&source, &values);
    }

    #[test]
    fn randomized_branching_history_matches_vector_models() {
        const STEPS: usize = 2_000;
        let mut random = Xorshift::new(0x51_1CE);
        let mut histories: Vec<(AncestralSliceQueue<i32>, Vec<i32>)> =
            vec![(AncestralSliceQueue::new(), Vec::new())];

        for step in 0..STEPS {
            let selected = random.next_below(histories.len());
            let (source, model) = histories[selected].clone();
            let step_value = step as i32;
            let length = model.len();

            let (result, expected) = match random.next_below(9) {
                1 if length != 0 => (
                    source.remove_first().expect("non-empty"),
                    model[1..].to_vec(),
                ),
                2 if length != 0 => (
                    source.remove_last().expect("non-empty"),
                    model[..length - 1].to_vec(),
                ),
                3 => {
                    let count = random.next_below(length + 1);
                    (
                        source.take(count).expect("valid prefix"),
                        model[..count].to_vec(),
                    )
                }
                4 => {
                    let count = random.next_below(length + 1);
                    (
                        source.skip(count).expect("valid suffix"),
                        model[count..].to_vec(),
                    )
                }
                5 => {
                    let start = random.next_below(length + 1);
                    let count = random.next_below(length - start + 1);
                    (
                        source.get_range(start, count).expect("valid range"),
                        model[start..start + count].to_vec(),
                    )
                }
                6 => {
                    let position = random.next_below(length + 1);
                    let split = source.split_at(position).expect("valid split");
                    if random.next_below(2) == 0 {
                        (split.left, model[..position].to_vec())
                    } else {
                        (split.right, model[position..].to_vec())
                    }
                }
                7 if length != 0 => {
                    let popped = source.pop_front().expect("non-empty");
                    assert_eq!(*popped.value, model[0]);
                    (popped.rest, model[1..].to_vec())
                }
                8 if length != 0 => {
                    let popped = source.pop_back().expect("non-empty");
                    assert_eq!(*popped.value, model[length - 1]);
                    (popped.rest, model[..length - 1].to_vec())
                }
                _ => {
                    let mut expected = model.clone();
                    expected.push(step_value);
                    (source.push_back(step_value), expected)
                }
            };

            assert_state(&source, &model);
            assert_state(&result, &expected);
            histories.push((result, expected));

            if step % 211 == 0 {
                for _ in 0..16.min(histories.len()) {
                    let sample = random.next_below(histories.len());
                    let (queue, model) = &histories[sample];
                    assert_state(queue, model);
                }
            }
        }

        for (queue, model) in histories.iter().step_by(97) {
            assert_state(queue, model);
        }
    }

    #[test]
    fn concurrent_branch_appends_and_reads_publish_complete_independent_versions() {
        const THREAD_COUNT: usize = 8;
        const BRANCHES_PER_THREAD: usize = 16;

        let expected = (0..512i32).collect::<Vec<_>>();
        let source = queue_of(expected.iter().copied());

        let mut handles = Vec::new();
        for thread_index in 0..THREAD_COUNT {
            let source = source.clone();
            let expected = expected.clone();
            handles.push(thread::spawn(move || {
                let mut branches = Vec::with_capacity(BRANCHES_PER_THREAD);
                for offset in 0..BRANCHES_PER_THREAD {
                    let marker = -((thread_index * BRANCHES_PER_THREAD + offset) as i32) - 1;
                    let branch = source.push_back(marker);
                    assert_eq!(branch.len(), expected.len() + 1);
                    assert_eq!(*branch.last().expect("non-empty back"), marker);

                    // Read level ancestors while sibling nodes are being published elsewhere.
                    let mut index = offset % 31;
                    while index < expected.len() {
                        assert_eq!(*source.get(index).expect("index inside"), expected[index]);
                        index += 127;
                    }
                    assert_eq!(
                        values_of(&source.get_range(100, 40).expect("valid range")),
                        expected[100..140].to_vec()
                    );
                    branches.push((marker, branch));
                }
                branches
            }));
        }

        let mut published = Vec::new();
        for handle in handles {
            published.extend(handle.join().expect("writer thread failed"));
        }

        assert_eq!(published.len(), THREAD_COUNT * BRANCHES_PER_THREAD);
        for (marker, branch) in &published {
            let mut model = expected.clone();
            model.push(*marker);
            assert_state(branch, &model);
        }
        assert_state(&source, &expected);
    }

    #[test]
    fn out_of_range_positions_are_rejected_without_changing_the_source() {
        let source = queue_of([10, 20, 30]);

        assert!(source.get(3).is_none());
        assert!(source.get(usize::MAX).is_none());
        assert!(source.get_range(4, 0).is_none());
        assert!(source.get_range(2, 2).is_none());
        assert!(source.get_range(2, usize::MAX).is_none());
        assert!(source.take(4).is_none());
        assert!(source.skip(4).is_none());
        assert!(source.split_at(4).is_none());

        assert_state(&source, &[10, 20, 30]);
    }

    #[test]
    fn boundary_ranges_of_an_empty_queue_stay_anchored() {
        let empty = AncestralSliceQueue::<i32>::new();

        assert_state(&empty.take(0).expect("empty prefix"), &[]);
        assert_state(&empty.skip(0).expect("empty suffix"), &[]);
        assert_state(&empty.get_range(0, 0).expect("empty range"), &[]);

        let split = empty.split_at(0).expect("empty split");
        assert_state(&split.left, &[]);
        assert_state(&split.right, &[]);
        assert_state(&split.left.push_back(1), &[1]);
        assert_state(&split.right.push_back(2), &[2]);

        // A singleton emptied from either end appends to exactly one visible value.
        let singleton = empty.push_back(7);
        assert_state(
            &singleton.remove_first().expect("non-empty").push_back(8),
            &[8],
        );
        assert_state(
            &singleton.remove_last().expect("non-empty").push_back(9),
            &[9],
        );
        assert_state(&singleton, &[7]);
    }

    #[test]
    fn snapshots_are_send_and_sync_when_their_elements_are() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<AncestralSliceQueue<i32>>();
        assert_send_sync::<AncestralSliceQueuePop<i32>>();
        assert_send_sync::<AncestralSliceQueueSplit<i32>>();
    }

    #[test]
    fn pop_and_split_results_clone_for_a_non_clonable_element_type() {
        let queue = AncestralSliceQueue::new()
            .push_back(Unclonable(1))
            .push_back(Unclonable(2));

        let popped = queue.pop_front().expect("non-empty");
        let popped_copy = popped.clone();
        assert_eq!(*popped_copy.value, Unclonable(1));
        assert_eq!(popped_copy.rest.len(), 1);
        assert_eq!(
            *popped_copy.rest.first().expect("non-empty front"),
            Unclonable(2)
        );
        assert_eq!(*popped.value, Unclonable(1));

        let split = queue.split_at(1).expect("valid split");
        let split_copy = split.clone();
        assert_eq!(
            *split_copy.left.first().expect("non-empty front"),
            Unclonable(1)
        );
        assert_eq!(
            *split_copy.right.first().expect("non-empty front"),
            Unclonable(2)
        );
        assert_eq!(split.left.len(), 1);
        assert_eq!(split.right.len(), 1);
    }

    #[test]
    fn debug_renders_the_visible_interval() {
        let source = queue_of([1, 2, 3, 4, 5]);
        let slice = source.get_range(1, 3).expect("interior slice");
        assert_eq!(format!("{slice:?}"), "[2, 3, 4]");
        assert_eq!(format!("{:?}", AncestralSliceQueue::<i32>::new()), "[]");
    }
}
