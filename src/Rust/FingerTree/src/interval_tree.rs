//! Persistent interval tree supporting stabbing and overlap queries.
//!
//! [`IntervalTree`] stores [`Interval`] values ordered by low endpoint inside a
//! [`FingerTree`](crate::FingerTree) whose measure caches the maximum high endpoint of each subtree.
//! A query descends only into subtrees whose cached maximum can still reach the query point or
//! range, which is what keeps a search proportional to the tree depth plus the number of results
//! rather than to the number of stored intervals.
//!
//! Intervals are closed and are compared lexicographically by `(low, high)`. Duplicates are
//! permitted: this is a bag of intervals, not a set. [`IntervalTreeCursor`] provides immutable
//! ordered traversal over one snapshot, and [`IntervalCursorSearch`] reports whether a seek landed
//! on a match.

use crate::measured::{FingerTree, MeasurePolicy};
use std::cmp::Ordering;
use std::fmt;
use std::marker::PhantomData;

/// A closed interval `[low, high]`.
///
/// The low endpoint never exceeds the high endpoint; [`Interval::new`] panics rather than
/// constructing an inverted interval.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Interval<T> {
    /// The inclusive lower endpoint.
    pub low: T,
    /// The inclusive upper endpoint, never below [`low`](Self::low).
    pub high: T,
}

impl<T> Interval<T>
where
    T: Ord,
{
    /// Creates the closed interval `[low, high]`.
    ///
    /// # Panics
    ///
    /// Panics when `low` exceeds `high`. An inverted interval has no meaningful contents, so it is
    /// rejected at construction rather than allowed to produce silently empty query results.
    #[must_use]
    pub fn new(low: T, high: T) -> Self {
        assert!(
            low <= high,
            "interval low endpoint must not exceed high endpoint"
        );
        Self { low, high }
    }

    /// Reports whether the two closed intervals share at least one point. Touching endpoints count as
    /// overlapping.
    #[must_use]
    pub fn overlaps(&self, other: &Self) -> bool {
        self.low <= other.high && other.low <= self.high
    }

    /// Reports whether `point` lies within this closed interval, endpoints included.
    #[must_use]
    pub fn contains_point(&self, point: &T) -> bool {
        self.low <= *point && *point <= self.high
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct IntervalMeasure<T>(PhantomData<T>);

#[derive(Clone, Debug, PartialEq, Eq)]
struct IntervalSummary<T> {
    last_low: Option<T>,
    max_high: Option<T>,
}

impl<T> MeasurePolicy<Interval<T>> for IntervalMeasure<T>
where
    T: Ord + Clone,
{
    type Measure = IntervalSummary<T>;

    fn empty() -> Self::Measure {
        IntervalSummary {
            last_low: None,
            max_high: None,
        }
    }

    fn measure(element: &Interval<T>) -> Self::Measure {
        IntervalSummary {
            last_low: Some(element.low.clone()),
            max_high: Some(element.high.clone()),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        let last_low = right.last_low.clone().or_else(|| left.last_low.clone());
        let max_high = match (&left.max_high, &right.max_high) {
            (Some(left), Some(right)) => Some(left.max(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        };
        IntervalSummary { last_low, max_high }
    }
}

type IntervalStorage<T> = FingerTree<Interval<T>, IntervalMeasure<T>>;

/// A persistent bag of closed intervals supporting stabbing and overlap queries.
///
/// Intervals are held in ascending `(low, high)` order with each subtree's maximum high endpoint
/// cached, so a query visits only the subtrees that can still contain a match. Duplicates are
/// permitted. Every operation returns a new tree and leaves the receiver valid and unchanged.
pub struct IntervalTree<T>
where
    T: Ord + Clone,
{
    intervals: IntervalStorage<T>,
}

impl<T> Clone for IntervalTree<T>
where
    T: Ord + Clone,
{
    fn clone(&self) -> Self {
        Self {
            intervals: self.intervals.clone(),
        }
    }
}

impl<T> fmt::Debug for IntervalTree<T>
where
    T: Ord + Clone + fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_list()
            .entries(self.intervals.iter())
            .finish()
    }
}

impl<T> PartialEq for IntervalTree<T>
where
    T: Ord + Clone + PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.intervals.len() == other.intervals.len()
            && self.intervals.iter().eq(other.intervals.iter())
    }
}

impl<T> Eq for IntervalTree<T> where T: Ord + Clone + Eq {}

impl<T> IntervalTree<T>
where
    T: Ord + Clone,
{
    fn from_storage(intervals: IntervalStorage<T>) -> Self {
        Self { intervals }
    }
}

impl<T> IntervalTree<T>
where
    T: Ord + Clone,
{
    /// Creates an empty interval tree.
    #[must_use]
    pub fn new() -> Self {
        Self::from_storage(IntervalStorage::new())
    }

    /// Returns the number of stored intervals, counting duplicates separately. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.intervals.len()
    }

    /// Returns `true` when no interval is stored.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.intervals.is_empty()
    }

    /// Inserts before every stored interval whose low endpoint is greater
    /// than or equal to the new interval's low endpoint, matching the C#
    /// reference (`IntervalTree<T>.Insert` splits on "last low at least
    /// `interval.Low`"), so a new interval precedes existing equal-low ones.
    #[must_use]
    pub fn insert(&self, interval: Interval<T>) -> Self {
        let index = lower_bound_by_low(&self.intervals, &interval.low);
        let split = self
            .intervals
            .split_at_index(index)
            .expect("computed insertion rank is valid");
        Self::from_storage(split.left.append(interval).concat(&split.right))
    }

    /// Reports whether an interval with exactly these endpoints is stored. O(log n).
    ///
    /// This is endpoint identity, not overlap; see [`Self::find_overlap`] for the latter.
    #[must_use]
    pub fn contains(&self, interval: &Interval<T>) -> bool {
        self.index_of(interval).is_some()
    }

    /// Removes one interval with exactly these endpoints. Removing an absent interval is a no-op.
    ///
    /// Because duplicates are permitted, this removes a single occurrence.
    #[must_use]
    pub fn remove(&self, interval: &Interval<T>) -> Self {
        self.try_remove(interval)
            .map_or_else(|| self.clone(), |(tree, _)| tree)
    }

    /// Removes one interval with exactly these endpoints, returning the resulting tree and the stored
    /// interval, or `None` when absent.
    #[must_use]
    pub fn try_remove(&self, interval: &Interval<T>) -> Option<(Self, Interval<T>)> {
        let index = self.index_of(interval)?;
        self.remove_at_index(index)
    }

    /// Rank of the first stored interval matching both endpoints: a measured
    /// lower-bound descent on the low endpoint plus a scan over the equal-low run,
    /// mirroring the C# `Contains`/`TryRemove` complexity contract.
    ///
    /// Endpoints match by [`Ord::cmp`], not by [`PartialEq`]. Ordering is the sole notion of
    /// endpoint identity in this family, so a `T` whose `Eq` disagrees with its `Ord` cannot make
    /// a stored interval both order-equal and unmatchable.
    fn index_of(&self, interval: &Interval<T>) -> Option<usize> {
        let mut index = lower_bound_by_low(&self.intervals, &interval.low);
        while let Some(stored) = self.intervals.get(index) {
            if stored.low.cmp(&interval.low) != Ordering::Equal {
                return None;
            }

            if stored.high.cmp(&interval.high) == Ordering::Equal {
                return Some(index);
            }

            index += 1;
        }

        None
    }

    /// Borrows some stored interval overlapping `probe`, or `None` when none does.
    ///
    /// O(log n): the cached maximum high endpoint lets the search skip subtrees that cannot reach
    /// `probe`. Which of several overlapping intervals is returned is unspecified.
    #[must_use]
    pub fn find_overlap(&self, probe: &Interval<T>) -> Option<&Interval<T>> {
        let end = self.upper_low_bound_index(&probe.high);
        let index = self.first_possible_overlap_index(&probe.low)?;
        (index < end).then(|| {
            self.intervals
                .get(index)
                .expect("measured overlap index is in range")
        })
    }

    /// Borrows some stored interval containing `point`, or `None` when none does. O(log n).
    #[must_use]
    pub fn find_containing(&self, point: &T) -> Option<&Interval<T>> {
        let end = self.upper_low_bound_index(point);
        let index = self.first_possible_overlap_index(point)?;
        (index < end).then(|| {
            self.intervals
                .get(index)
                .expect("measured containment index is in range")
        })
    }

    /// Collects every stored interval overlapping `probe`, in ascending `(low, high)` order.
    ///
    /// O(log n + k) for `k` results, rather than a scan of all stored intervals.
    #[must_use]
    pub fn find_overlaps(&self, probe: &Interval<T>) -> Vec<Interval<T>> {
        let mut remaining = self.candidate_prefix(&probe.high);
        let mut overlaps = Vec::new();
        while let Some((_, interval, right)) = remaining.try_split_find(|summary| {
            summary
                .max_high
                .as_ref()
                .is_some_and(|high| high >= &probe.low)
        }) {
            overlaps.push(interval);
            remaining = right;
        }
        overlaps
    }

    /// Counts the stored intervals overlapping `probe`, without materializing them.
    #[must_use]
    pub fn count_overlaps(&self, probe: &Interval<T>) -> usize {
        let mut remaining = self.candidate_prefix(&probe.high);
        let mut count = 0;
        while let Some((_, _, right)) = remaining.try_split_find(|summary| {
            summary
                .max_high
                .as_ref()
                .is_some_and(|high| high >= &probe.low)
        }) {
            count += 1;
            remaining = right;
        }
        count
    }

    /// Returns a tree in which overlapping and touching intervals have been merged into maximal runs.
    ///
    /// The result covers exactly the same set of points using the fewest intervals, so duplicates and
    /// partial overlaps disappear. O(n).
    #[must_use]
    pub fn coalesce(&self) -> Self {
        let mut iter = self.intervals.iter();
        let Some(first) = iter.next() else {
            return self.clone();
        };

        let mut merged = Vec::new();
        let mut current = first.clone();
        for interval in iter {
            if current.overlaps(interval) || current.high >= interval.low {
                if interval.high > current.high {
                    current.high = interval.high.clone();
                }
            } else {
                merged.push(current);
                current = interval.clone();
            }
        }
        merged.push(current);

        Self::from_storage(IntervalStorage::from_vec(merged))
    }

    /// Copies the stored intervals into a vector in ascending `(low, high)` order. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<Interval<T>> {
        self.intervals.to_vec()
    }

    /// Reports whether two trees are backed by the same sequence, so neither can observe an edit made
    /// to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.intervals.shares_storage_with(&other.intervals)
    }

    fn first_possible_overlap_index(&self, low: &T) -> Option<usize> {
        let located = self
            .intervals
            .try_locate(|summary| summary.max_high.as_ref().is_some_and(|high| high >= low));
        located.item.map(|_| located.index)
    }

    fn upper_low_bound_index(&self, high: &T) -> usize {
        self.intervals
            .try_locate(|summary| summary.last_low.as_ref().is_some_and(|low| low > high))
            .index
    }

    fn candidate_prefix(&self, high: &T) -> IntervalStorage<T> {
        self.intervals
            .split(|summary| summary.last_low.as_ref().is_some_and(|low| low > high))
            .left
    }

    fn remove_at_index(&self, index: usize) -> Option<(Self, Interval<T>)> {
        let split = self.intervals.split_at_index(index)?;
        let after = split.right.split_at_index(1)?;
        let removed = after.left.front()?.clone();
        Some((Self::from_storage(split.left.concat(&after.right)), removed))
    }
}

impl<T> Default for IntervalTree<T>
where
    T: Ord + Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

/// Presence-discriminated interval cursor search.
pub struct IntervalCursorSearch<T: Ord + Clone> {
    pub found: bool,
    pub cursor: IntervalTreeCursor<T>,
}

/// Immutable low-endpoint-order root-plus-rank cursor.
#[derive(Clone, Debug)]
pub struct IntervalTreeCursor<T: Ord + Clone> {
    tree: IntervalTree<T>,
    position: usize,
}

impl<T: Ord + Clone> IntervalTree<T> {
    /// Creates a cursor at the gap `position` in `0..=len` of the ascending interval sequence, or
    /// `None` when it exceeds the interval count.
    pub fn cursor_at(&self, position: usize) -> Option<IntervalTreeCursor<T>> {
        (position <= self.len()).then(|| IntervalTreeCursor {
            tree: self.clone(),
            position,
        })
    }
    /// Creates a cursor before the first interval whose low endpoint is not below `low`. O(log n).
    pub fn cursor_at_lower_bound(&self, low: &T) -> IntervalTreeCursor<T> {
        self.cursor_at(lower_bound_by_low(&self.intervals, low))
            .expect("lower bound is valid")
    }
    /// Creates a cursor after every interval whose low endpoint equals `low`. O(log n).
    pub fn cursor_at_upper_bound(&self, low: &T) -> IntervalTreeCursor<T> {
        self.cursor_at(self.upper_low_bound_index(low))
            .expect("upper bound is valid")
    }
    /// Locates the first stored interval matching both endpoints by [`Ord::cmp`], not by
    /// [`PartialEq`], agreeing with [`IntervalTree::remove`] and the sibling ports.
    ///
    /// A miss returns a cursor at the low-endpoint lower bound.
    pub fn find_cursor(&self, interval: &Interval<T>) -> IntervalCursorSearch<T> {
        let position = lower_bound_by_low(&self.intervals, &interval.low);
        let found = self
            .intervals
            .iter()
            .enumerate()
            .skip(position)
            .take_while(|(_, item)| item.low.cmp(&interval.low) == Ordering::Equal)
            .find(|(_, item)| item.high.cmp(&interval.high) == Ordering::Equal)
            .map(|(rank, _)| rank);
        IntervalCursorSearch {
            found: found.is_some(),
            cursor: self
                .cursor_at(found.unwrap_or(position))
                .expect("search rank is valid"),
        }
    }
    /// Seeks to the first interval overlapping `probe` and reports whether one exists.
    ///
    /// Advancing from the returned cursor with [`IntervalTreeCursor::seek_next_overlap`] enumerates
    /// the remaining matches without collecting them all first.
    pub fn find_overlap_cursor(&self, probe: &Interval<T>) -> IntervalCursorSearch<T> {
        self.find_overlap_cursor_from(0, probe)
    }
    /// Seeks to the first interval containing `point` and reports whether one exists.
    pub fn find_containing_cursor(&self, point: &T) -> IntervalCursorSearch<T> {
        self.find_overlap_cursor(&Interval::new(point.clone(), point.clone()))
    }
    fn find_overlap_cursor_from(
        &self,
        start: usize,
        probe: &Interval<T>,
    ) -> IntervalCursorSearch<T> {
        assert!(start <= self.len(), "cursor start is outside interval tree");
        let found = self
            .intervals
            .iter()
            .enumerate()
            .skip(start)
            .take_while(|(_, item)| item.low <= probe.high)
            .find(|(_, item)| item.overlaps(probe))
            .map(|(rank, _)| rank);
        IntervalCursorSearch {
            found: found.is_some(),
            cursor: self
                .cursor_at(found.unwrap_or(self.len()))
                .expect("overlap rank is valid"),
        }
    }
}

impl<T: Ord + Clone> IntervalTreeCursor<T> {
    /// Returns the interval count of the tree version this cursor is positioned in.
    pub fn len(&self) -> usize {
        self.tree.len()
    }
    /// Returns `true` when that tree version stores no intervals.
    pub fn is_empty(&self) -> bool {
        self.tree.is_empty()
    }
    /// Returns the cursor's gap index in `0..=len`.
    pub fn position(&self) -> usize {
        self.position
    }
    /// Returns `true` when the gap precedes the first interval.
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }
    /// Returns `true` when the gap follows the last interval.
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }
    /// Borrows the interval immediately before the gap, or `None` at the start.
    pub fn peek_previous(&self) -> Option<&Interval<T>> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.tree.intervals.get(rank))
    }
    /// Borrows the interval immediately after the gap, or `None` at the end.
    pub fn peek_next(&self) -> Option<&Interval<T>> {
        self.tree.intervals.get(self.position)
    }
    /// Returns a cursor one position earlier, or `None` at the start. The receiver is unchanged.
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.tree.cursor_at(position))
    }
    /// Returns a cursor one position later, or `None` at the end. The receiver is unchanged.
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            tree: self.tree.clone(),
            position: self.position + 1,
        })
    }
    /// Jumps to the gap at `position` within the same tree version, or `None` when `position` exceeds
    /// the interval count.
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.tree.cursor_at(position)
        }
    }
    /// Advances to the next interval at or after the gap that overlaps `probe`, reporting whether one
    /// was found.
    ///
    /// Repeated calls stream the matches one at a time instead of collecting them, as
    /// [`IntervalTree::find_overlaps`] does.
    pub fn seek_next_overlap(&self, probe: &Interval<T>) -> IntervalCursorSearch<T> {
        self.tree.find_overlap_cursor_from(
            if self.position < self.len() {
                self.position + 1
            } else {
                self.len()
            },
            probe,
        )
    }
    /// Inserts `interval` and returns a cursor just after it.
    ///
    /// The gap moves to the interval's sorted position rather than staying where the receiver was,
    /// since placement is decided by the endpoints. Duplicates are permitted.
    pub fn insert(&self, interval: Interval<T>) -> Self {
        let position = lower_bound_by_low(&self.tree.intervals, &interval.low);
        Self {
            tree: self.tree.insert(interval),
            position: position + 1,
        }
    }
    /// Removes the interval before the gap and returns a cursor in its place, or `None` at the start.
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        self.tree
            .remove_at_index(position)
            .map(|(tree, _)| Self { tree, position })
    }
    /// Removes the interval after the gap and returns a cursor in its place, or `None` at the end.
    pub fn delete_next(&self) -> Option<Self> {
        self.tree
            .remove_at_index(self.position)
            .map(|(tree, _)| Self {
                tree,
                position: self.position,
            })
    }
    /// Borrows the tree version this cursor is positioned in.
    pub fn snapshot(&self) -> &IntervalTree<T> {
        &self.tree
    }
}

impl<T> FromIterator<Interval<T>> for IntervalTree<T>
where
    T: Ord + Clone,
{
    fn from_iter<I: IntoIterator<Item = Interval<T>>>(iter: I) -> Self {
        let mut tree = Self::new();
        for interval in iter {
            tree = tree.insert(interval);
        }

        tree
    }
}

fn lower_bound_by_low<T>(intervals: &IntervalStorage<T>, value: &T) -> usize
where
    T: Ord + Clone,
{
    intervals
        .try_locate(|summary| summary.last_low.as_ref().is_some_and(|low| low >= value))
        .index
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn interval_queries_use_closed_overlap() {
        let tree: IntervalTree<_> = [
            Interval::new(10, 20),
            Interval::new(1, 5),
            Interval::new(30, 40),
        ]
        .into_iter()
        .collect();

        assert_eq!(tree.find_overlap(&Interval::new(5, 10)).unwrap().low, 1);
        assert_eq!(tree.find_containing(&35).unwrap().low, 30);
        assert_eq!(tree.count_overlaps(&Interval::new(4, 31)), 3);
    }

    #[test]
    fn interval_queries_use_cached_max_high_measure() {
        let tree: IntervalTree<_> = [
            Interval::new(0, 2),
            Interval::new(10, 15),
            Interval::new(20, 30),
            Interval::new(40, 50),
        ]
        .into_iter()
        .collect();

        assert_eq!(
            tree.intervals.measure(),
            &IntervalSummary {
                last_low: Some(40),
                max_high: Some(50),
            }
        );
        assert_eq!(tree.find_overlap(&Interval::new(16, 19)), None);
        assert_eq!(
            tree.find_overlap(&Interval::new(29, 41)),
            Some(&Interval::new(20, 30))
        );
        assert_eq!(tree.find_containing(&45), Some(&Interval::new(40, 50)));
        assert_eq!(
            tree.find_overlaps(&Interval::new(14, 42)),
            vec![
                Interval::new(10, 15),
                Interval::new(20, 30),
                Interval::new(40, 50)
            ]
        );
        tree.intervals.validate_invariants();
    }

    #[test]
    fn coalesce_merges_overlapping_intervals() {
        let tree: IntervalTree<_> = [
            Interval::new(1, 3),
            Interval::new(2, 5),
            Interval::new(10, 12),
            Interval::new(12, 13),
        ]
        .into_iter()
        .collect();

        assert_eq!(
            tree.coalesce().to_vec(),
            vec![Interval::new(1, 5), Interval::new(10, 13)]
        );
    }

    #[test]
    fn insert_places_new_intervals_before_existing_equal_low_ones() {
        // Matches the C# reference: Insert splits at the first interval whose
        // low endpoint is >= the new low, so newer equal-low intervals come first.
        let tree = IntervalTree::new()
            .insert(Interval::new(1, 3))
            .insert(Interval::new(1, 5));
        assert_eq!(
            tree.to_vec(),
            vec![Interval::new(1, 5), Interval::new(1, 3)]
        );

        let tree = tree.insert(Interval::new(1, 4)).insert(Interval::new(0, 9));
        assert_eq!(
            tree.to_vec(),
            vec![
                Interval::new(0, 9),
                Interval::new(1, 4),
                Interval::new(1, 5),
                Interval::new(1, 3),
            ]
        );
    }

    #[test]
    fn contains_and_remove_handle_equal_low_runs() {
        let tree: IntervalTree<_> = (0..64)
            .map(|high| Interval::new(10, high + 100))
            .chain((0..16).map(|value| Interval::new(value, value + 1)))
            .collect();

        assert!(tree.contains(&Interval::new(10, 163)));
        assert!(!tree.contains(&Interval::new(10, 99)));
        assert!(!tree.contains(&Interval::new(9, 100)));

        let (removed, interval) = tree.try_remove(&Interval::new(10, 130)).unwrap();
        assert_eq!(interval, Interval::new(10, 130));
        assert_eq!(removed.len(), tree.len() - 1);
        assert!(!removed.contains(&Interval::new(10, 130)));
        assert!(removed.contains(&Interval::new(10, 131)));
        assert!(tree.try_remove(&Interval::new(10, 99)).is_none());
    }

    #[test]
    fn interval_tree_edits_share_measured_tree_storage() {
        let tree: IntervalTree<_> = (0..256)
            .map(|value| Interval::new(value * 3, value * 3 + 1))
            .collect();
        let inserted = tree.insert(Interval::new(1000, 1001));
        let removed = inserted.remove(&Interval::new(300, 301));
        let (try_removed, removed_interval) =
            inserted.try_remove(&Interval::new(303, 304)).unwrap();

        assert!(inserted.contains(&Interval::new(1000, 1001)));
        assert!(!removed.contains(&Interval::new(300, 301)));
        assert_eq!(removed_interval, Interval::new(303, 304));
        assert_eq!(inserted.intervals.measure().last_low, Some(1000));
        assert_eq!(inserted.intervals.measure().max_high, Some(1001));
        assert!(tree.intervals.shared_node_count_with(&inserted.intervals) > 100);
        assert!(
            inserted
                .intervals
                .shared_node_count_with(&removed.intervals)
                > 100
        );
        assert!(
            inserted
                .intervals
                .shared_node_count_with(&try_removed.intervals)
                > 100
        );
        assert!(tree.intervals.tree_depth() < 24);
        inserted.intervals.validate_invariants();
        removed.intervals.validate_invariants();
        try_removed.intervals.validate_invariants();
    }

    #[test]
    fn measured_overlap_descent_skips_large_non_overlapping_regions() {
        let tree: IntervalTree<_> = (0..100_000)
            .map(|value| Interval::new(value * 3, value * 3 + 1))
            .collect();
        let probe = Interval::new(299_997, 299_998);

        assert_eq!(tree.find_overlap(&probe), Some(&probe));
        assert_eq!(tree.find_overlaps(&probe), vec![probe.clone()]);
        assert_eq!(tree.count_overlaps(&probe), 1);
        assert_eq!(tree.find_containing(&299_998), Some(&probe));
        tree.intervals.validate_invariants();
    }
}
