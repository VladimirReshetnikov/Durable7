//! A persistent deque represented as two oppositely oriented intervals of an ancestry arena.
//!
//! [`BilateralAncestralDeque`] is not a balanced sequence tree. A version is a constant-sized
//! handle over one append-only [`IncrementalAncestorArena`]: it stores a *left* interval
//! `(l_anchor, l_tail]` and a *right* interval `(r_anchor, r_tail]`, and its logical value is
//! `reverse(left)` followed by `right`. A front insertion appends a leaf below the left tail; a
//! back insertion appends one below the right tail. Reversal only exchanges the two intervals, so
//! it costs O(1) and needs no lazy flag or rebuild.
//!
//! The load-bearing property is *slice closure*: every contiguous slice intersects each interval at
//! most once, so a slice is again a bilateral handle rather than a rebuilt tree. Slicing and
//! splitting each cost at most **two** level-ancestor queries, indexing at most one, end removal
//! zero queries on its own nonempty arm and at most one when it crosses the centre, and the
//! structural audit at most four. Endpoint reads, [`len`], [`clear`], and [`reverse`] are O(1).
//!
//! Two layers of complexity claim must not be conflated:
//!
//! 1. **The reduction.** Let `U(M)` be arena leaf-add time and `Q(M)` be arena level-ancestor query
//!    time after `M` published nodes. End insertion costs `U(M)`; end removal, indexing, slicing,
//!    splitting, and [`validate_structure`] cost `O(1 + Q(M))` with the query ceilings above.
//!    Enumeration is `Theta(n)`.
//! 2. **The optimal backend.** An Alstrup-Holm incremental level-ancestor backend has
//!    `U(M) = Q(M) = O(1)` worst case with linear space, which makes every scalar operation of this
//!    restricted algebra O(1) worst case. No such backend is included here.
//!
//! The shipped [`MyersAncestorArena`] does **not** realize that theorem. Backed by it, end insertion
//! is O(1) *amortized*, endpoint reads, [`len`], [`reverse`], and [`clear`] are O(1) worst case, and
//! removal, indexing, slicing, splitting, and [`validate_structure`] are O(log M) worst case after
//! M historical insertions.
//! Nothing here upgrades an amortized or logarithmic bound to a worst-case constant one.
//!
//! The algebra is deliberately restricted: add or remove at either end, read either end or an
//! indexed value, reverse, take/skip/slice/split, and enumerate front to back. Arbitrary
//! concatenation of unrelated versions, point replacement, and middle editing are intentionally
//! absent, and no comparison here treats an omitted operation as fast. Arena space is charged to all
//! successful historical end insertions, not merely to the values visible from one handle: a handle
//! retains its arena, and the arena retains every node it ever published. Enumeration is
//! `Theta(count)` time and may retain `O(count)` temporary values, because the forward-oriented
//! right interval is reversed through a temporary buffer to keep enumeration query-free.
//!
//! There is no process-global empty value: an empty deque belongs to an arena, and retaining it
//! retains that arena. Handles are immutable and clone in O(1); all progress guarantees come from
//! the arena.
//!
//! [`len`]: BilateralAncestralDeque::len
//! [`clear`]: BilateralAncestralDeque::clear
//! [`reverse`]: BilateralAncestralDeque::reverse
//! [`validate_structure`]: BilateralAncestralDeque::validate_structure

use std::fmt;
use std::iter::FusedIterator;
use std::sync::Arc;

use crate::incremental_ancestor::{IncrementalAncestorArena, MyersAncestorArena};

/// A rejected arena supplied to [`BilateralAncestralDeque::with_arena`].
///
/// The interval arithmetic assumes the arena's distinguished bottom node sits at depth -1, so an
/// arena that reports any other bottom depth is rejected before a handle is published.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BilateralArenaError {
    /// The depth the supplied arena reported for its bottom node.
    pub bottom_depth: isize,
}

impl fmt::Display for BilateralArenaError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "the arena bottom node must have depth -1 but reports depth {}",
            self.bottom_depth
        )
    }
}

impl std::error::Error for BilateralArenaError {}

/// An invariant failure detected by [`BilateralAncestralDeque::validate_structure`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BilateralAncestralDequeInvariantError {
    message: &'static str,
}

impl fmt::Display for BilateralAncestralDequeInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl std::error::Error for BilateralAncestralDequeInvariantError {}

/// Representation statistics returned by [`BilateralAncestralDeque::validate_structure`].
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct BilateralAncestralDequeStatistics {
    /// The number of visible values, which equals `left_count + right_count`.
    pub count: usize,
    /// The number of values held by the reversed left interval.
    pub left_count: usize,
    /// The number of values held by the forward right interval.
    pub right_count: usize,
}

/// The two persistent deques produced by [`BilateralAncestralDeque::split_at`].
#[derive(Debug)]
pub struct BilateralAncestralDequeSplit<T: 'static> {
    /// The prefix holding the values before the boundary.
    pub left: BilateralAncestralDeque<T>,
    /// The suffix holding the values from the boundary onward.
    pub right: BilateralAncestralDeque<T>,
}

/// An endpoint value and the deque that remains after removing it.
///
/// The value is a retained [`Arc`] rather than an owned `T`: arena nodes are immutable and shared by
/// every version that can still see them, so removal needs no `T: Clone` bound.
#[derive(Debug)]
pub struct BilateralAncestralDequePop<T: 'static> {
    /// The removed endpoint value.
    pub value: Arc<T>,
    /// The deque without that value.
    pub rest: BilateralAncestralDeque<T>,
}

/// One oriented ancestry interval `(anchor, tail]`.
///
/// `base` is redundant but load-bearing: it caches the first physical node after `anchor`, which
/// keeps both public endpoint reads query-free even when the opposite arm is empty. For the left arm
/// the logical first value sits at `tail` and the logical last at `base`; for the right arm those
/// roles are exchanged.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Segment {
    anchor: usize,
    base: usize,
    tail: usize,
    count: usize,
}

impl Segment {
    /// Returns the empty interval anchored at one node.
    fn empty_at(node: usize) -> Self {
        Self {
            anchor: node,
            base: node,
            tail: node,
            count: 0,
        }
    }
}

/// An immutable deque held as two oppositely oriented intervals of an append-only ancestry arena.
///
/// Cloning a handle is O(1) and shares the arena; every produced version leaves its source and every
/// earlier version usable. A handle is `Send + Sync` because the arena it retains is.
///
/// See the [module documentation](self) for the representation, the query ceilings, and the
/// separation between the optimal-backend bounds and the shipped Myers bounds.
pub struct BilateralAncestralDeque<T: 'static> {
    arena: Arc<dyn IncrementalAncestorArena<T>>,
    left: Segment,
    right: Segment,
    count: usize,
}

impl<T: 'static> Clone for BilateralAncestralDeque<T> {
    fn clone(&self) -> Self {
        Self {
            arena: Arc::clone(&self.arena),
            left: self.left,
            right: self.right,
            count: self.count,
        }
    }
}

impl<T: Send + Sync + 'static> BilateralAncestralDeque<T> {
    /// Creates an empty deque backed by a fresh, independently droppable Myers arena.
    ///
    /// The arena is private to this deque family: every version derived from the result shares it,
    /// and it is dropped once the last of those versions is.
    #[must_use]
    pub fn new() -> Self {
        Self::with_arena(Arc::new(MyersAncestorArena::new()))
            .expect("a fresh Myers arena publishes its bottom node at depth -1")
    }
}

impl<T: Send + Sync + 'static> Default for BilateralAncestralDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: 'static> BilateralAncestralDeque<T> {
    /// Creates an empty deque owned by an explicit level-ancestor arena.
    ///
    /// The arena retains and navigates every inserted node and therefore determines the bounds this
    /// deque inherits. Sharing one arena between independent deque families is allowed; the arena is
    /// append-only, so their nodes simply interleave.
    ///
    /// # Errors
    ///
    /// Returns [`BilateralArenaError`] when the arena's bottom node is not at depth -1.
    pub fn with_arena(
        arena: Arc<dyn IncrementalAncestorArena<T>>,
    ) -> Result<Self, BilateralArenaError> {
        let bottom = arena.bottom();
        let bottom_depth = arena.depth(bottom);
        if bottom_depth != -1 {
            return Err(BilateralArenaError { bottom_depth });
        }

        let empty = Segment::empty_at(bottom);
        Ok(Self {
            arena,
            left: empty,
            right: empty,
            count: 0,
        })
    }

    /// Returns the number of visible values. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.count
    }

    /// Returns `true` when this handle denotes an empty deque. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Returns the arena this deque and every version derived from it publishes into.
    #[must_use]
    pub fn arena(&self) -> &Arc<dyn IncrementalAncestorArena<T>> {
        &self.arena
    }

    /// Returns the first visible value, or `None` when the deque is empty.
    ///
    /// Reads one cached handle and makes no level-ancestor query, so it is O(1).
    #[must_use]
    pub fn first(&self) -> Option<Arc<T>> {
        (!self.is_empty()).then(|| {
            let node = if self.left.count != 0 {
                self.left.tail
            } else {
                self.right.base
            };
            self.arena.value(node)
        })
    }

    /// Returns the last visible value, or `None` when the deque is empty.
    ///
    /// Reads one cached handle and makes no level-ancestor query, so it is O(1).
    #[must_use]
    pub fn last(&self) -> Option<Arc<T>> {
        (!self.is_empty()).then(|| {
            let node = if self.right.count != 0 {
                self.right.tail
            } else {
                self.left.base
            };
            self.arena.value(node)
        })
    }

    /// Returns the value at a zero-based visible index, or `None` when the index is out of range.
    ///
    /// Costs at most one level-ancestor query; the two logical endpoints of each interval are cached
    /// and answered with no query at all.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<Arc<T>> {
        if index >= self.count {
            return None;
        }

        let node = if index < self.left.count {
            if index == 0 {
                self.left.tail
            } else if index == self.left.count - 1 {
                self.left.base
            } else {
                let depth = sub_depth(self.arena.depth(self.left.tail), index);
                self.arena.ancestor_at_depth(self.left.tail, depth)
            }
        } else {
            let right_index = index - self.left.count;
            if right_index == 0 {
                self.right.base
            } else if right_index == self.right.count - 1 {
                self.right.tail
            } else {
                let anchor_depth = self.arena.depth(self.right.anchor);
                let depth = add_depth(add_depth(anchor_depth, right_index), 1);
                self.arena.ancestor_at_depth(self.right.tail, depth)
            }
        };

        Some(self.arena.value(node))
    }

    /// Returns a deque with one value added at the front. The receiver is unchanged.
    ///
    /// Publishes exactly one arena leaf and makes no level-ancestor query, so it costs one arena
    /// leaf addition: O(1) amortized with the shipped Myers arena.
    ///
    /// # Panics
    ///
    /// Panics when the visible count or the ancestry depth cannot grow further.
    #[must_use]
    pub fn push_front(&self, value: T) -> Self {
        let count = self.next_count();
        let left = self.add_to_tail(self.left, value);
        Self {
            arena: Arc::clone(&self.arena),
            left,
            right: self.right,
            count,
        }
    }

    /// Returns a deque with one value added at the back. The receiver is unchanged.
    ///
    /// Publishes exactly one arena leaf and makes no level-ancestor query, so it costs one arena
    /// leaf addition: O(1) amortized with the shipped Myers arena.
    ///
    /// # Panics
    ///
    /// Panics when the visible count or the ancestry depth cannot grow further.
    #[must_use]
    pub fn push_back(&self, value: T) -> Self {
        let count = self.next_count();
        let right = self.add_to_tail(self.right, value);
        Self {
            arena: Arc::clone(&self.arena),
            left: self.left,
            right,
            count,
        }
    }

    /// Returns the deque without its first value, or `None` when the deque is empty.
    ///
    /// Makes no level-ancestor query when the left arm is nonempty, and at most one when the removal
    /// crosses the centre into the right arm.
    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        if self.is_empty() {
            return None;
        }
        if self.count == 1 {
            return Some(self.empty_handle());
        }
        if self.left.count != 0 {
            let left = self.remove_tail(self.left);
            return Some(Self {
                arena: Arc::clone(&self.arena),
                left,
                right: self.right,
                count: self.count - 1,
            });
        }

        let right = self.remove_base(self.right);
        Some(Self {
            arena: Arc::clone(&self.arena),
            left: self.left,
            right,
            count: self.count - 1,
        })
    }

    /// Returns the deque without its last value, or `None` when the deque is empty.
    ///
    /// Makes no level-ancestor query when the right arm is nonempty, and at most one when the
    /// removal crosses the centre into the left arm.
    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        if self.is_empty() {
            return None;
        }
        if self.count == 1 {
            return Some(self.empty_handle());
        }
        if self.right.count != 0 {
            let right = self.remove_tail(self.right);
            return Some(Self {
                arena: Arc::clone(&self.arena),
                left: self.left,
                right,
                count: self.count - 1,
            });
        }

        let left = self.remove_base(self.left);
        Some(Self {
            arena: Arc::clone(&self.arena),
            left,
            right: self.right,
            count: self.count - 1,
        })
    }

    /// Removes the first value and returns it with the remaining deque, or `None` when the deque is
    /// empty. The receiver is unchanged.
    #[must_use]
    pub fn pop_front(&self) -> Option<BilateralAncestralDequePop<T>> {
        Some(BilateralAncestralDequePop {
            value: self.first()?,
            rest: self
                .remove_first()
                .expect("a nonempty deque has a first value to remove"),
        })
    }

    /// Removes the last value and returns it with the remaining deque, or `None` when the deque is
    /// empty. The receiver is unchanged.
    #[must_use]
    pub fn pop_back(&self) -> Option<BilateralAncestralDequePop<T>> {
        Some(BilateralAncestralDequePop {
            value: self.last()?,
            rest: self
                .remove_last()
                .expect("a nonempty deque has a last value to remove"),
        })
    }

    /// Returns the first `count` values, or `None` when `count` exceeds the length.
    ///
    /// A prefix is a slice, so it costs at most two level-ancestor queries.
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

    /// Returns the deque after omitting its first `count` values, or `None` when `count` exceeds the
    /// length.
    ///
    /// A suffix is a slice, so it costs at most two level-ancestor queries. The C# member is named
    /// `Drop`; `skip` avoids colliding with the [`Drop`] trait.
    #[must_use]
    pub fn skip(&self, count: usize) -> Option<Self> {
        if count > self.count {
            return None;
        }
        if count == 0 {
            return Some(self.clone());
        }
        self.get_range(count, self.count - count)
    }

    /// Returns one contiguous slice, or `None` when the index/count pair is outside this deque.
    ///
    /// Every contiguous slice intersects each interval at most once, so the result is another
    /// bilateral handle and the operation costs **at most two** level-ancestor queries regardless of
    /// how many values it selects. An empty slice costs none.
    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = index.checked_add(count)?;
        if end > self.count {
            return None;
        }
        if index == 0 && count == self.count {
            return Some(self.clone());
        }
        if count == 0 {
            return Some(self.empty_handle());
        }

        let left_start = index.min(self.left.count);
        let left_end = end.min(self.left.count);
        let right_start = index.saturating_sub(self.left.count);
        let right_end = end.saturating_sub(self.left.count);

        let left = self.slice_left(self.left, left_start, left_end - left_start);
        let right = self.slice_right(self.right, right_start, right_end - right_start);
        Some(Self {
            arena: Arc::clone(&self.arena),
            left,
            right,
            count,
        })
    }

    /// Splits this deque at a zero-based boundary, or returns `None` when the boundary exceeds the
    /// length.
    ///
    /// The two results together enumerate the original value. Although the split is expressed as a
    /// prefix plus a suffix, the pair costs at most two level-ancestor queries in total.
    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<BilateralAncestralDequeSplit<T>> {
        if index > self.count {
            return None;
        }
        Some(BilateralAncestralDequeSplit {
            left: self.take(index).expect("a validated boundary is a prefix"),
            right: self.skip(index).expect("a validated boundary is a suffix"),
        })
    }

    /// Returns the logical reversal by exchanging the two oriented intervals. O(1), no queries.
    #[must_use]
    pub fn reverse(&self) -> Self {
        if self.count <= 1 {
            return self.clone();
        }
        Self {
            arena: Arc::clone(&self.arena),
            left: self.right,
            right: self.left,
            count: self.count,
        }
    }

    /// Returns an empty, independently appendable handle in the same arena. O(1), no queries.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        self.empty_handle()
    }

    /// Copies the retained values into a standard vector, in logical order.
    ///
    /// `Theta(count)` time with no level-ancestor queries. Values are returned as retained [`Arc`]s
    /// rather than owned clones, so this needs no `T: Clone` bound.
    #[must_use]
    pub fn to_vec(&self) -> Vec<Arc<T>> {
        let mut values: Vec<Arc<T>> = Vec::with_capacity(self.count);
        let mut node = self.left.tail;
        for index in 0..self.left.count {
            values.push(self.arena.value(node));
            if index + 1 != self.left.count {
                node = self.arena.parent(node);
            }
        }

        let start = values.len();
        node = self.right.tail;
        for index in (0..self.right.count).rev() {
            values.push(self.arena.value(node));
            if index != 0 {
                node = self.arena.parent(node);
            }
        }
        values[start..].reverse();
        values
    }

    /// Iterates the retained values front to back.
    ///
    /// Enumeration takes `Theta(count)` time and makes no level-ancestor query. The reversed left
    /// interval streams through parent links, while the forward-oriented right interval is buffered
    /// when it is first reached, so the iterator can retain `O(count)` values in the worst case.
    #[must_use]
    pub fn iter(&self) -> BilateralAncestralDequeIter<T> {
        BilateralAncestralDequeIter {
            arena: Arc::clone(&self.arena),
            left_node: self.left.tail,
            left_remaining: self.left.count,
            right: self.right,
            right_buffer: None,
            remaining: self.count,
        }
    }

    /// Checks the cached interval endpoints and counts of this handle.
    ///
    /// The audit inspects a constant number of cached fields: it compares the cached counts against
    /// the interval endpoint depths and confirms each cached base and anchor lies on its tail's
    /// ancestry path. Confirming the ancestry path costs **two** level-ancestor queries per nonempty
    /// interval, so the audit costs `O(1 + Q(M))` with a ceiling of four queries — not O(1) work.
    /// With the shipped [`MyersAncestorArena`] that is O(log M) worst case after M historical
    /// insertions; only an O(1)-query backend makes the audit constant time. An empty handle spends
    /// no query.
    ///
    /// It ports the C# internal `ValidateInvariants` audit.
    ///
    /// # Errors
    ///
    /// Returns the first [`BilateralAncestralDequeInvariantError`] observed.
    pub fn validate_structure(
        &self,
    ) -> Result<BilateralAncestralDequeStatistics, BilateralAncestralDequeInvariantError> {
        let total = self
            .left
            .count
            .checked_add(self.right.count)
            .ok_or_else(|| invariant_error("the interval counts overflow the visible count"))?;
        if self.count != total {
            return Err(invariant_error(
                "the total count disagrees with the interval counts",
            ));
        }

        self.validate_segment(self.left)?;
        self.validate_segment(self.right)?;
        Ok(BilateralAncestralDequeStatistics {
            count: self.count,
            left_count: self.left.count,
            right_count: self.right.count,
        })
    }

    /// Returns the visible count this deque would have after one insertion.
    fn next_count(&self) -> usize {
        self.count
            .checked_add(1)
            .expect("bilateral ancestral deque count overflow")
    }

    /// Returns an empty handle anchored at the arena bottom node.
    fn empty_handle(&self) -> Self {
        let empty = Segment::empty_at(self.arena.bottom());
        Self {
            arena: Arc::clone(&self.arena),
            left: empty,
            right: empty,
            count: 0,
        }
    }

    /// Publishes one leaf below an interval's tail and extends the interval by one.
    fn add_to_tail(&self, segment: Segment, value: T) -> Segment {
        let tail = self.arena.add_leaf(segment.tail, value);
        let base = if segment.count == 0 {
            tail
        } else {
            segment.base
        };
        Segment {
            anchor: segment.anchor,
            base,
            tail,
            count: segment.count + 1,
        }
    }

    /// Drops an interval's newest physical label by moving its tail to the parent. No queries.
    fn remove_tail(&self, segment: Segment) -> Segment {
        debug_assert_ne!(segment.count, 0);
        let tail = self.arena.parent(segment.tail);
        if segment.count == 1 {
            return Segment::empty_at(tail);
        }
        Segment {
            anchor: segment.anchor,
            base: segment.base,
            tail,
            count: segment.count - 1,
        }
    }

    /// Drops an interval's oldest physical label by advancing its anchor to the cached base.
    ///
    /// Costs at most one level-ancestor query, which selects the next cached base.
    fn remove_base(&self, segment: Segment) -> Segment {
        debug_assert_ne!(segment.count, 0);
        if segment.count == 1 {
            return Segment::empty_at(segment.tail);
        }

        let anchor = segment.base;
        let base = if segment.count == 2 {
            segment.tail
        } else {
            let tail_depth = self.arena.depth(segment.tail);
            let depth = add_depth(sub_depth(tail_depth, segment.count), 2);
            self.arena.ancestor_at_depth(segment.tail, depth)
        };
        Segment {
            anchor,
            base,
            tail: segment.tail,
            count: segment.count - 1,
        }
    }

    /// Selects the sub-interval `[start, start + count)` of a reversed left arm.
    ///
    /// The reversed reading order means `start` counts down from the tail. At most two queries, and
    /// fewer whenever a cached endpoint already answers.
    fn slice_left(&self, segment: Segment, start: usize, count: usize) -> Segment {
        debug_assert!(count <= segment.count && start <= segment.count - count);
        if count == 0 {
            return Segment::empty_at(self.arena.bottom());
        }
        if start == 0 && count == segment.count {
            return segment;
        }

        let tail_depth = self.arena.depth(segment.tail);
        let base_depth = add_depth(sub_depth(tail_depth, segment.count), 1);
        let sliced_tail_depth = sub_depth(tail_depth, start);
        let sliced_base_depth = add_depth(sub_depth(sliced_tail_depth, count), 1);

        let tail = if sliced_tail_depth == tail_depth {
            segment.tail
        } else {
            self.arena.ancestor_at_depth(segment.tail, sliced_tail_depth)
        };
        let base = if sliced_base_depth == sliced_tail_depth {
            tail
        } else if sliced_base_depth == base_depth {
            segment.base
        } else {
            self.arena.ancestor_at_depth(segment.tail, sliced_base_depth)
        };

        let anchor = if base == segment.base {
            segment.anchor
        } else {
            self.arena.parent(base)
        };
        Segment {
            anchor,
            base,
            tail,
            count,
        }
    }

    /// Selects the sub-interval `[start, start + count)` of a forward right arm.
    ///
    /// At most two queries, and fewer whenever a cached endpoint already answers.
    fn slice_right(&self, segment: Segment, start: usize, count: usize) -> Segment {
        debug_assert!(count <= segment.count && start <= segment.count - count);
        if count == 0 {
            return Segment::empty_at(self.arena.bottom());
        }
        if start == 0 && count == segment.count {
            return segment;
        }

        let anchor_depth = self.arena.depth(segment.anchor);
        let sliced_base_depth = add_depth(add_depth(anchor_depth, start), 1);
        let sliced_tail_depth = sub_depth(add_depth(sliced_base_depth, count), 1);
        let end = start + count;

        let base = if start == 0 {
            segment.base
        } else {
            self.arena.ancestor_at_depth(segment.tail, sliced_base_depth)
        };
        let tail = if sliced_tail_depth == sliced_base_depth {
            base
        } else if end == segment.count {
            segment.tail
        } else {
            self.arena.ancestor_at_depth(segment.tail, sliced_tail_depth)
        };

        let anchor = if start == 0 {
            segment.anchor
        } else {
            self.arena.parent(base)
        };
        Segment {
            anchor,
            base,
            tail,
            count,
        }
    }

    /// Checks one interval's endpoint identities, cached base, and count/depth agreement.
    ///
    /// An empty interval is settled from cached fields alone. A nonempty one spends exactly two
    /// level-ancestor queries to place the anchor and the cached base on the tail's ancestry path.
    fn validate_segment(
        &self,
        segment: Segment,
    ) -> Result<(), BilateralAncestralDequeInvariantError> {
        if segment.count == 0 {
            if segment.anchor != segment.base || segment.base != segment.tail {
                return Err(invariant_error(
                    "an empty interval does not have one shared endpoint",
                ));
            }
            return Ok(());
        }

        let anchor_depth = self.arena.depth(segment.anchor);
        let tail_depth = self.arena.depth(segment.tail);
        if tail_depth.checked_sub(anchor_depth) != isize::try_from(segment.count).ok() {
            return Err(invariant_error(
                "an interval count disagrees with its endpoint depths",
            ));
        }
        if self.arena.parent(segment.base) != segment.anchor {
            return Err(invariant_error(
                "the cached base is not the first node after the anchor",
            ));
        }
        if self.arena.ancestor_at_depth(segment.tail, anchor_depth) != segment.anchor {
            return Err(invariant_error(
                "the interval anchor is not an ancestor of its tail",
            ));
        }
        if self.arena.ancestor_at_depth(segment.tail, anchor_depth + 1) != segment.base {
            return Err(invariant_error(
                "the cached base is not on the tail's ancestry path",
            ));
        }
        Ok(())
    }
}

impl<T: Send + Sync + 'static> FromIterator<T> for BilateralAncestralDeque<T> {
    /// Builds a Myers-backed deque from values in enumeration order, appending each at the back.
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        let mut result = Self::new();
        for value in iter {
            result = result.push_back(value);
        }
        result
    }
}

impl<T: 'static> IntoIterator for &BilateralAncestralDeque<T> {
    type Item = Arc<T>;
    type IntoIter = BilateralAncestralDequeIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T: fmt::Debug + 'static> fmt::Debug for BilateralAncestralDeque<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

/// A front-to-back iterator over one immutable bilateral handle.
///
/// The reversed left interval streams through parent links with constant iterator state. The
/// forward right interval is buffered when it is first reached, which keeps enumeration query-free
/// at the cost of retaining up to `O(count)` values.
pub struct BilateralAncestralDequeIter<T: 'static> {
    arena: Arc<dyn IncrementalAncestorArena<T>>,
    left_node: usize,
    left_remaining: usize,
    right: Segment,
    right_buffer: Option<std::vec::IntoIter<Arc<T>>>,
    remaining: usize,
}

impl<T: 'static> Iterator for BilateralAncestralDequeIter<T> {
    type Item = Arc<T>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.left_remaining != 0 {
            let value = self.arena.value(self.left_node);
            self.left_remaining -= 1;
            if self.left_remaining != 0 {
                self.left_node = self.arena.parent(self.left_node);
            }
            self.remaining -= 1;
            return Some(value);
        }

        let arena = &self.arena;
        let right = self.right;
        let buffer = self.right_buffer.get_or_insert_with(|| {
            let mut values: Vec<Arc<T>> = Vec::with_capacity(right.count);
            let mut node = right.tail;
            for index in (0..right.count).rev() {
                values.push(arena.value(node));
                if index != 0 {
                    node = arena.parent(node);
                }
            }
            values.reverse();
            values.into_iter()
        });
        let value = buffer.next()?;
        self.remaining -= 1;
        Some(value)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T: 'static> ExactSizeIterator for BilateralAncestralDequeIter<T> {}

impl<T: 'static> FusedIterator for BilateralAncestralDequeIter<T> {}

impl<T: 'static> fmt::Debug for BilateralAncestralDequeIter<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("BilateralAncestralDequeIter")
            .field("remaining", &self.remaining)
            .finish_non_exhaustive()
    }
}

/// Converts a visible count into a depth offset.
fn depth_offset(count: usize) -> isize {
    isize::try_from(count).expect("bilateral ancestral deque interval exceeds the depth domain")
}

/// Adds a visible offset to an absolute depth.
fn add_depth(depth: isize, offset: usize) -> isize {
    depth
        .checked_add(depth_offset(offset))
        .expect("bilateral ancestral deque ancestry-depth overflow")
}

/// Subtracts a visible offset from an absolute depth.
fn sub_depth(depth: isize, offset: usize) -> isize {
    depth
        .checked_sub(depth_offset(offset))
        .expect("bilateral ancestral deque ancestry-depth overflow")
}

/// Builds one invariant failure.
fn invariant_error(message: &'static str) -> BilateralAncestralDequeInvariantError {
    BilateralAncestralDequeInvariantError { message }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
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

    /// An arena whose bottom node reports the wrong depth, used to prove eager rejection.
    struct InvalidBottomArena {
        touched: AtomicUsize,
    }

    impl InvalidBottomArena {
        fn new() -> Self {
            Self {
                touched: AtomicUsize::new(0),
            }
        }
    }

    impl IncrementalAncestorArena<i32> for InvalidBottomArena {
        fn bottom(&self) -> usize {
            0
        }

        fn published_node_count(&self) -> usize {
            0
        }

        fn add_leaf(&self, _parent: usize, _value: i32) -> usize {
            self.touched.fetch_add(1, AtomicOrdering::SeqCst);
            unreachable!("a rejected arena is never asked to publish")
        }

        fn depth(&self, _node: usize) -> isize {
            0
        }

        fn parent(&self, _node: usize) -> usize {
            unreachable!("a rejected arena is never navigated")
        }

        fn ancestor_at_depth(&self, _node: usize, _depth: isize) -> usize {
            unreachable!("a rejected arena is never queried")
        }

        fn value(&self, _node: usize) -> Arc<i32> {
            unreachable!("a rejected arena holds no values")
        }
    }

    impl<T: 'static> BilateralAncestralDeque<T> {
        /// Test-only representation identity: the same arena and the same cached intervals.
        ///
        /// The C# tests assert reference identity (`Assert.Same`) for the operations that return
        /// their receiver. A Rust handle is a value, so the equivalent observation is that the
        /// produced handle has exactly the same representation.
        fn has_same_representation(&self, other: &Self) -> bool {
            Arc::ptr_eq(&self.arena, &other.arena)
                && self.left == other.left
                && self.right == other.right
                && self.count == other.count
        }
    }

    /// Reads a deque into a plain vector of copied values.
    fn values<T: Copy + 'static>(deque: &BilateralAncestralDeque<T>) -> Vec<T> {
        deque.to_vec().iter().map(|value| **value).collect()
    }

    /// Asserts the full observable contract of one handle against a sequence oracle.
    fn assert_deque<T>(expected: &[T], actual: &BilateralAncestralDeque<T>)
    where
        T: Copy + PartialEq + fmt::Debug + 'static,
    {
        assert_eq!(actual.len(), expected.len());
        assert_eq!(actual.is_empty(), expected.is_empty());
        assert_eq!(values(actual), expected);
        assert_eq!(
            actual.iter().map(|value| *value).collect::<Vec<_>>(),
            expected
        );
        assert_eq!(actual.iter().len(), expected.len());
        for (index, item) in expected.iter().enumerate() {
            assert_eq!(actual.get(index).as_deref(), Some(item));
        }
        assert!(actual.get(expected.len()).is_none());
        assert_eq!(actual.first().as_deref(), expected.first());
        assert_eq!(actual.last().as_deref(), expected.last());

        let statistics = actual.validate_structure().expect("valid representation");
        assert_eq!(statistics.count, expected.len());
        assert_eq!(
            statistics.left_count + statistics.right_count,
            expected.len()
        );
    }

    /// Creates a deque sharing an explicitly retained Myers arena.
    fn with_myers<T: Send + Sync + 'static>()
    -> (Arc<MyersAncestorArena<T>>, BilateralAncestralDeque<T>) {
        let arena = Arc::new(MyersAncestorArena::new());
        let deque = BilateralAncestralDeque::with_arena(
            Arc::clone(&arena) as Arc<dyn IncrementalAncestorArena<T>>
        )
        .expect("a fresh Myers arena is accepted");
        (arena, deque)
    }

    /// Runs an operation and asserts it made at most `maximum` level-ancestor queries.
    fn assert_queries_at_most<T, R>(
        arena: &MyersAncestorArena<T>,
        maximum: u64,
        operation: impl FnOnce() -> R,
    ) -> R {
        let before = arena.statistics().ancestor_query_count;
        let result = operation();
        let after = arena.statistics().ancestor_query_count;
        assert!(
            after - before <= maximum,
            "the operation made {} level-ancestor queries but at most {maximum} are allowed",
            after - before
        );
        result
    }

    #[test]
    fn an_empty_deque_has_stable_reads_failures_and_identity() {
        let empty = BilateralAncestralDeque::<i32>::new();

        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
        assert!(empty.to_vec().is_empty());
        assert_eq!(empty.iter().count(), 0);
        assert!(empty.first().is_none());
        assert!(empty.last().is_none());
        assert!(empty.get(0).is_none());
        assert!(empty.remove_first().is_none());
        assert!(empty.remove_last().is_none());
        assert!(empty.pop_front().is_none());
        assert!(empty.pop_back().is_none());

        for produced in [
            empty.clear(),
            empty.take(0).expect("zero is a valid prefix"),
            empty.skip(0).expect("zero is a valid suffix"),
            empty.get_range(0, 0).expect("the empty range is valid"),
            empty.reverse(),
        ] {
            assert_deque(&[], &produced);
            assert!(produced.has_same_representation(&empty));
        }

        let split = empty.split_at(0).expect("zero is a valid boundary");
        assert!(split.left.has_same_representation(&empty));
        assert!(split.right.has_same_representation(&empty));

        assert!(empty.take(1).is_none());
        assert!(empty.skip(1).is_none());
        assert!(empty.split_at(1).is_none());
        assert!(empty.get_range(0, 1).is_none());
        assert!(empty.get_range(1, 0).is_none());
    }

    #[test]
    fn absent_values_stay_distinct_from_a_stored_none() {
        // C# needs `[MaybeNullWhen]` only because `null` is a valid element; Rust expresses the
        // same contract with `Option<Arc<T>>`, so a stored `None` element stays visible.
        let deque = BilateralAncestralDeque::<Option<&str>>::new()
            .push_front(None)
            .push_back(Some("tail"));

        assert_eq!(deque.len(), 2);
        assert_eq!(deque.first().as_deref(), Some(&None));
        assert_eq!(deque.last().as_deref(), Some(&Some("tail")));
        assert_eq!(deque.get(0).as_deref(), Some(&None));
        assert!(deque.get(2).is_none());
        assert_eq!(
            deque
                .to_vec()
                .iter()
                .map(|value| **value)
                .collect::<Vec<_>>(),
            vec![None, Some("tail")]
        );

        let popped = deque.pop_front().expect("a nonempty deque pops");
        assert_eq!(*popped.value, None);
        assert_eq!(popped.rest.len(), 1);
        deque.validate_structure().expect("valid representation");
    }

    #[test]
    fn an_arena_with_a_misplaced_bottom_node_is_rejected_before_publication() {
        let arena = Arc::new(InvalidBottomArena::new());
        let error = BilateralAncestralDeque::with_arena(
            Arc::clone(&arena) as Arc<dyn IncrementalAncestorArena<i32>>
        )
        .expect_err("a bottom node at depth zero is rejected");

        assert_eq!(error, BilateralArenaError { bottom_depth: 0 });
        assert_eq!(
            error.to_string(),
            "the arena bottom node must have depth -1 but reports depth 0"
        );
        assert_eq!(arena.touched.load(AtomicOrdering::SeqCst), 0);
    }

    #[test]
    fn a_collected_range_appends_in_enumeration_order() {
        let deque: BilateralAncestralDeque<i32> = (1..=5).collect();
        assert_deque(&[1, 2, 3, 4, 5], &deque);

        let empty: BilateralAncestralDeque<i32> = std::iter::empty().collect();
        assert_deque(&[], &empty);

        let copied: BilateralAncestralDeque<i32> = deque.iter().map(|value| *value).collect();
        assert_deque(&[1, 2, 3, 4, 5], &copied);
        assert_deque(&[1, 2, 3, 4, 5], &deque);
    }

    #[test]
    fn endpoint_operations_retain_every_prior_branch() {
        let root = BilateralAncestralDeque::<i32>::new();
        let one = root.push_back(10);
        let two = one.push_back(20);
        let three = two.push_front(5);
        let four = three.push_front(1);
        let five = four.push_back(30);

        assert_deque(&[], &root);
        assert_deque(&[10], &one);
        assert_deque(&[10, 20], &two);
        assert_deque(&[5, 10, 20], &three);
        assert_deque(&[1, 5, 10, 20], &four);
        assert_deque(&[1, 5, 10, 20, 30], &five);

        assert_deque(&[5, 10, 20, 30], &five.remove_first().unwrap());
        assert_deque(&[1, 5, 10, 20], &five.remove_last().unwrap());
        assert_deque(&[1, 5, 10, 20, 30], &five);

        let right_only = root.push_back(1).push_back(2).push_back(3);
        let after_one = right_only.remove_first().unwrap();
        let after_two = after_one.remove_first().unwrap();
        assert_deque(&[2, 3], &after_one);
        assert_deque(&[3], &after_two);
        assert_deque(&[], &after_two.remove_first().unwrap());
        assert_deque(&[1, 2, 3], &right_only);

        let left_only = root.push_front(3).push_front(2).push_front(1);
        let without_last = left_only.remove_last().unwrap();
        let without_two = without_last.remove_last().unwrap();
        assert_deque(&[1, 2], &without_last);
        assert_deque(&[1], &without_two);
        assert_deque(&[], &without_two.remove_last().unwrap());

        let popped_front = five.pop_front().expect("a nonempty deque pops");
        assert_eq!(*popped_front.value, 1);
        assert_deque(&[5, 10, 20, 30], &popped_front.rest);
        let popped_back = five.pop_back().expect("a nonempty deque pops");
        assert_eq!(*popped_back.value, 30);
        assert_deque(&[1, 5, 10, 20], &popped_back.rest);

        // Two independent branches grow from one retained version.
        let left_branch = two.push_front(-1);
        let right_branch = two.push_back(99);
        assert_deque(&[-1, 10, 20], &left_branch);
        assert_deque(&[10, 20, 99], &right_branch);
        assert_deque(&[10, 20], &two);

        let cleared = five.clear();
        assert_deque(&[], &cleared);
        assert_deque(&[-7, 8], &cleared.push_front(-7).push_back(8));
        assert_deque(&[1, 5, 10, 20, 30], &five);
    }

    #[test]
    fn reverse_swaps_orientation_without_changing_retained_versions() {
        let deque = BilateralAncestralDeque::<i32>::new()
            .push_front(3)
            .push_front(2)
            .push_front(1)
            .push_back(4)
            .push_back(5)
            .push_back(6)
            .push_back(7);
        let reversed = deque.reverse();
        let restored = reversed.reverse();

        assert_deque(&[1, 2, 3, 4, 5, 6, 7], &deque);
        assert_deque(&[7, 6, 5, 4, 3, 2, 1], &reversed);
        assert_deque(&[1, 2, 3, 4, 5, 6, 7], &restored);
        assert_deque(
            &[0, 7, 6, 5, 4, 3, 2, 1, 8],
            &reversed.push_front(0).push_back(8),
        );
        assert_deque(
            &[6, 5, 4, 3, 2],
            &reversed.remove_first().unwrap().remove_last().unwrap(),
        );
        assert_deque(&[1, 2, 3, 4, 5, 6, 7], &deque);

        let singleton: BilateralAncestralDeque<i32> = std::iter::once(42).collect();
        assert!(singleton.reverse().has_same_representation(&singleton));
    }

    #[test]
    fn every_slice_and_split_boundary_crossing_both_arms_remains_growable() {
        let original = BilateralAncestralDeque::<i32>::new()
            .push_front(3)
            .push_front(2)
            .push_front(1)
            .push_back(4)
            .push_back(5)
            .push_back(6)
            .push_back(7);
        let expected: Vec<i32> = (1..=7).collect();

        for start in 0..=expected.len() {
            for count in 0..=expected.len() - start {
                let slice = original.get_range(start, count).expect("a valid range");
                let model = &expected[start..start + count];
                assert_deque(model, &slice);

                let mut extended_model = vec![-100 - start as i32];
                extended_model.extend_from_slice(model);
                extended_model.push(100 + count as i32);
                let extended = slice
                    .push_front(-100 - start as i32)
                    .push_back(100 + count as i32);
                assert_deque(&extended_model, &extended);
                assert_deque(model, &slice);

                let mut reversed_model = vec![-1];
                reversed_model.extend(model.iter().rev().copied());
                reversed_model.push(-2);
                let reversed = slice.reverse().push_front(-1).push_back(-2);
                assert_deque(&reversed_model, &reversed);
            }
        }

        for boundary in 0..=expected.len() {
            let split = original.split_at(boundary).expect("a valid boundary");
            assert_deque(&expected[..boundary], &split.left);
            assert_deque(&expected[boundary..], &split.right);

            let mut left_model = expected[..boundary].to_vec();
            left_model.push(88);
            assert_deque(&left_model, &split.left.push_back(88));

            let mut right_model = vec![77];
            right_model.extend_from_slice(&expected[boundary..]);
            assert_deque(&right_model, &split.right.push_front(77));
        }

        assert_deque(&expected, &original);
    }

    #[test]
    fn small_endpoint_construction_words_match_the_sequence_oracle() {
        for length in 0..=8usize {
            for word in 0..1u32 << length {
                let mut deque = BilateralAncestralDeque::<i32>::new();
                let mut model: Vec<i32> = Vec::new();
                for value in 0..length {
                    if word & (1 << value) == 0 {
                        deque = deque.push_front(value as i32);
                        model.insert(0, value as i32);
                    } else {
                        deque = deque.push_back(value as i32);
                        model.push(value as i32);
                    }
                }

                assert_deque(&model, &deque);
                for start in 0..=length {
                    for count in 0..=length - start {
                        let expected = &model[start..start + count];
                        let slice = deque.get_range(start, count).expect("a valid range");
                        assert_deque(expected, &slice);

                        let mut grown = vec![-1];
                        grown.extend_from_slice(expected);
                        grown.push(-2);
                        assert_deque(&grown, &slice.push_front(-1).push_back(-2));
                    }
                }

                for boundary in 0..=length {
                    let split = deque.split_at(boundary).expect("a valid boundary");
                    assert_deque(&model[..boundary], &split.left);
                    assert_deque(&model[boundary..], &split.right);
                    assert_deque(
                        &model[..boundary],
                        &deque.take(boundary).expect("a valid prefix"),
                    );
                    assert_deque(
                        &model[boundary..],
                        &deque.skip(boundary).expect("a valid suffix"),
                    );
                }
            }
        }
    }

    #[test]
    fn randomized_retained_branches_match_an_immutable_sequence_model() {
        for seed in [0u64, 1, 17, 91, 8_675_309] {
            let mut random = Xorshift::new(seed);
            let root = BilateralAncestralDeque::<i64>::new();
            let mut versions: Vec<(BilateralAncestralDeque<i64>, Vec<i64>)> =
                vec![(root.clone(), Vec::new())];

            for step in 0..500usize {
                let source_index = random.next_below(versions.len());
                let (source, source_model) = versions[source_index].clone();
                assert_deque(&source_model, &source);

                let value = seed as i64 + step as i64 * 31;
                let (result, model) = match random.next_below(11) {
                    0 => {
                        let mut model = vec![value];
                        model.extend_from_slice(&source_model);
                        (source.push_front(value), model)
                    }
                    1 => {
                        let mut model = source_model.clone();
                        model.push(value);
                        (source.push_back(value), model)
                    }
                    2 if !source_model.is_empty() => (
                        source.remove_first().expect("a nonempty deque"),
                        source_model[1..].to_vec(),
                    ),
                    3 if !source_model.is_empty() => (
                        source.remove_last().expect("a nonempty deque"),
                        source_model[..source_model.len() - 1].to_vec(),
                    ),
                    4 => {
                        let mut model = source_model.clone();
                        model.reverse();
                        (source.reverse(), model)
                    }
                    5 => {
                        let start = random.next_below(source_model.len() + 1);
                        let count = random.next_below(source_model.len() - start + 1);
                        (
                            source.get_range(start, count).expect("a valid range"),
                            source_model[start..start + count].to_vec(),
                        )
                    }
                    6 => {
                        let boundary = random.next_below(source_model.len() + 1);
                        let split = source.split_at(boundary).expect("a valid boundary");
                        if random.next_below(2) == 0 {
                            (split.left, source_model[..boundary].to_vec())
                        } else {
                            (split.right, source_model[boundary..].to_vec())
                        }
                    }
                    7 => {
                        let count = random.next_below(source_model.len() + 1);
                        (
                            source.take(count).expect("a valid prefix"),
                            source_model[..count].to_vec(),
                        )
                    }
                    8 => {
                        let count = random.next_below(source_model.len() + 1);
                        (
                            source.skip(count).expect("a valid suffix"),
                            source_model[count..].to_vec(),
                        )
                    }
                    9 => (source.clear(), Vec::new()),
                    _ => {
                        let mut model = vec![value];
                        model.extend_from_slice(&source_model);
                        model.reverse();
                        model.push(!value);
                        (source.push_front(value).reverse().push_back(!value), model)
                    }
                };

                assert_deque(&model, &result);
                assert_deque(&source_model, &source);
                versions.push((result, model));

                if step % 29 == 0 {
                    for _ in 0..versions.len().min(12) {
                        let index = random.next_below(versions.len());
                        let (retained, retained_model) = &versions[index];
                        assert_deque(retained_model, retained);
                    }
                }
            }

            assert_deque(&[], &root);
        }
    }

    #[test]
    fn public_operations_respect_the_level_ancestor_query_ceilings() {
        let (arena, root) = with_myers::<i32>();
        let deque = root
            .push_front(4)
            .push_front(3)
            .push_front(2)
            .push_front(1)
            .push_back(5)
            .push_back(6)
            .push_back(7)
            .push_back(8);

        assert_queries_at_most(&arena, 0, || deque.first());
        assert_queries_at_most(&arena, 0, || deque.last());
        assert_queries_at_most(&arena, 0, || deque.len());
        assert_queries_at_most(&arena, 0, || deque.reverse());
        assert_queries_at_most(&arena, 0, || deque.clear());
        assert_queries_at_most(&arena, 0, || deque.push_front(0));
        assert_queries_at_most(&arena, 0, || deque.push_back(9));
        assert_queries_at_most(&arena, 0, || deque.to_vec());
        assert_queries_at_most(&arena, 0, || deque.iter().collect::<Vec<_>>());

        for index in 0..deque.len() {
            let value = assert_queries_at_most(&arena, 1, || deque.get(index));
            assert_eq!(value.as_deref(), Some(&(index as i32 + 1)));
        }

        for start in 0..=deque.len() {
            for count in 0..=deque.len() - start {
                let slice =
                    assert_queries_at_most(&arena, 2, || deque.get_range(start, count).unwrap());
                let expected: Vec<i32> = (1..=8).skip(start).take(count).collect();
                assert_eq!(values(&slice), expected);
            }
        }

        for boundary in 0..=deque.len() {
            let split = assert_queries_at_most(&arena, 2, || deque.split_at(boundary).unwrap());
            assert_eq!(
                values(&split.left),
                (1..=8).take(boundary).collect::<Vec<i32>>()
            );
            assert_eq!(
                values(&split.right),
                (1..=8).skip(boundary).collect::<Vec<i32>>()
            );
        }

        // Removal on its own nonempty arm makes no query at all.
        let left_own = assert_queries_at_most(&arena, 0, || deque.remove_first().unwrap());
        let right_own = assert_queries_at_most(&arena, 0, || deque.remove_last().unwrap());
        assert_deque(&[2, 3, 4, 5, 6, 7, 8], &left_own);
        assert_deque(&[1, 2, 3, 4, 5, 6, 7], &right_own);

        // Removal that crosses the centre makes at most one.
        let mut right_only = root.push_back(10).push_back(11).push_back(12).push_back(13);
        while !right_only.is_empty() {
            right_only = assert_queries_at_most(&arena, 1, || right_only.remove_first().unwrap());
        }
        let mut left_only = root
            .push_front(13)
            .push_front(12)
            .push_front(11)
            .push_front(10);
        while !left_only.is_empty() {
            left_only = assert_queries_at_most(&arena, 1, || left_only.remove_last().unwrap());
        }
    }

    #[test]
    fn enumeration_and_endpoint_reads_publish_no_nodes() {
        let (arena, root) = with_myers::<i32>();
        let deque = root.push_front(2).push_front(1).push_back(3).push_back(4);

        let before = arena.statistics();
        let _ = deque.to_vec();
        let _ = deque.iter().collect::<Vec<_>>();
        let _ = deque.first();
        let _ = deque.last();
        let _ = deque.get(2);
        let _ = deque.reverse();
        let _ = deque.clear();
        let _ = deque.get_range(1, 2);
        let after = arena.statistics();

        assert_eq!(after.published_node_count, before.published_node_count);
        assert_eq!(after.add_leaf_count, before.add_leaf_count);
        assert_eq!(deque.arena().published_node_count(), 4);
    }

    #[test]
    fn rejected_boundaries_publish_no_nodes_and_leave_the_deque_usable() {
        let (arena, root) = with_myers::<i32>();
        let deque = root.push_back(1).push_back(2).push_back(3);
        let before = arena.statistics();

        assert!(deque.get(3).is_none());
        assert!(deque.get(usize::MAX).is_none());
        assert!(deque.take(4).is_none());
        assert!(deque.skip(4).is_none());
        assert!(deque.split_at(4).is_none());
        assert!(deque.get_range(4, 0).is_none());
        assert!(deque.get_range(2, 2).is_none());
        assert!(deque.get_range(usize::MAX, 1).is_none());
        assert!(deque.get_range(1, usize::MAX).is_none());

        let after = arena.statistics();
        assert_eq!(after.published_node_count, before.published_node_count);
        assert_eq!(after.add_leaf_count, before.add_leaf_count);
        assert_eq!(after.ancestor_query_count, before.ancestor_query_count);
        assert_deque(&[1, 2, 3], &deque);
    }

    #[test]
    fn validate_structure_reports_counts_and_rejects_a_corrupted_interval() {
        let deque = BilateralAncestralDeque::<i32>::new()
            .push_front(2)
            .push_front(1)
            .push_back(3);
        assert_eq!(
            deque.validate_structure().expect("valid representation"),
            BilateralAncestralDequeStatistics {
                count: 3,
                left_count: 2,
                right_count: 1,
            }
        );

        let mut wrong_total = deque.clone();
        wrong_total.count += 1;
        assert_eq!(
            wrong_total.validate_structure().unwrap_err().to_string(),
            "the total count disagrees with the interval counts"
        );

        let mut wrong_empty = BilateralAncestralDeque::<i32>::new();
        wrong_empty.left.tail += 1;
        assert_eq!(
            wrong_empty.validate_structure().unwrap_err().to_string(),
            "an empty interval does not have one shared endpoint"
        );

        let mut wrong_span = deque.clone();
        wrong_span.left.count -= 1;
        wrong_span.count -= 1;
        assert_eq!(
            wrong_span.validate_structure().unwrap_err().to_string(),
            "an interval count disagrees with its endpoint depths"
        );

        let mut wrong_base = deque.clone();
        wrong_base.left.base = wrong_base.left.tail;
        assert_eq!(
            wrong_base.validate_structure().unwrap_err().to_string(),
            "the cached base is not the first node after the anchor"
        );
    }

    #[test]
    fn validate_structure_spends_two_queries_per_nonempty_interval() {
        let (arena, root) = with_myers::<i32>();

        // An empty handle has no ancestry path to confirm.
        assert_queries_at_most(&arena, 0, || root.validate_structure().unwrap());

        // One nonempty interval places its anchor and its cached base: two queries.
        let right_only = root.push_back(1).push_back(2).push_back(3);
        let left_only = root.push_front(3).push_front(2).push_front(1);
        assert_queries_at_most(&arena, 2, || right_only.validate_structure().unwrap());
        assert_queries_at_most(&arena, 2, || left_only.validate_structure().unwrap());

        // Both intervals nonempty is the documented ceiling of four, not O(1) work.
        let bilateral = root.push_front(2).push_front(1).push_back(3).push_back(4);
        assert_queries_at_most(&arena, 4, || bilateral.validate_structure().unwrap());
    }

    #[test]
    fn a_shared_arena_serves_independent_deque_families() {
        let (arena, root) = with_myers::<i32>();
        let first = root.push_back(1).push_back(2);
        let second = root.push_front(9).push_front(8);

        assert_deque(&[1, 2], &first);
        assert_deque(&[8, 9], &second);
        assert_eq!(arena.statistics().published_node_count, 4);
        assert_eq!(root.arena().published_node_count(), 4);
    }

    #[test]
    fn handles_are_send_and_sync_and_survive_concurrent_branching() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<BilateralAncestralDeque<i32>>();
        assert_send_sync::<BilateralAncestralDequeIter<i32>>();

        const WORKER_COUNT: usize = 8;
        const STEPS: usize = 240;

        let (arena, mut root) = with_myers::<i32>();
        for value in 0..96 {
            root = root.push_back(value);
        }
        let root_model: Vec<i32> = (0..96).collect();

        let mut handles = Vec::new();
        for worker in 0..WORKER_COUNT {
            let root = root.clone();
            let root_model = root_model.clone();
            handles.push(thread::spawn(move || {
                let mut deque = root.clone();
                let mut model = root_model.clone();
                let mut additions = 0usize;
                for step in 0..STEPS {
                    match step % 8 {
                        0 => {
                            let value = -1 - (worker * STEPS + step) as i32;
                            deque = deque.push_front(value);
                            model.insert(0, value);
                            additions += 1;
                        }
                        1 => {
                            let value = 10_000 + (worker * STEPS + step) as i32;
                            deque = deque.push_back(value);
                            model.push(value);
                            additions += 1;
                        }
                        2 => {
                            deque = deque.reverse();
                            model.reverse();
                        }
                        3 if !model.is_empty() => {
                            deque = deque.remove_first().expect("a nonempty deque");
                            model.remove(0);
                        }
                        4 if !model.is_empty() => {
                            deque = deque.remove_last().expect("a nonempty deque");
                            model.pop();
                        }
                        5 => {
                            let boundary = (worker * 17 + step) % (model.len() + 1);
                            let split = deque.split_at(boundary).expect("a valid boundary");
                            if (worker + step) % 2 == 0 {
                                deque = split.left;
                                model.truncate(boundary);
                            } else {
                                deque = split.right;
                                model.drain(..boundary);
                            }
                        }
                        6 => assert_eq!(values(&root), root_model),
                        _ => {
                            if step % 31 == 7 {
                                deque = root.clone();
                                model = root_model.clone();
                            }
                        }
                    }
                }

                (deque, model, additions)
            }));
        }

        let mut expected_publications = root_model.len();
        for handle in handles {
            let (deque, model, additions) = handle.join().expect("worker thread failed");
            assert_deque(&model, &deque);
            expected_publications += additions;
        }

        assert_deque(&root_model, &root);
        assert_eq!(
            arena.statistics().published_node_count,
            expected_publications
        );
    }
}
