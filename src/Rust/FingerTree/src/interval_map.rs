//! Persistent map keyed by complete intervals.
//!
//! [`PersistentIntervalMap`] attaches one payload to each whole `(low, high)` interval key. Unlike
//! [`IntervalTree`](crate::IntervalTree), which stores intervals as a searchable bag, this map
//! treats the interval as an identity: two entries collide only when their endpoints are equal, and
//! a partial overlap is a distinct key.
//!
//! Entries are held in a [`FingerTree`](crate::FingerTree) ordered by `(low, high)`, whose measure
//! caches the maximum high endpoint of each subtree. That cached maximum lets stabbing and overlap
//! queries skip subtrees that cannot contain a match, so they cost O(log n) plus output size rather
//! than a full scan. Failures are reported as [`IntervalMapError`]; a full audit returns
//! [`IntervalMapStatistics`] or the first [`IntervalMapInvariantError`].

use crate::interval_tree::Interval;
use crate::measured::{FingerTree, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;

/// One interval key and its payload.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IntervalMapEntry<T, V> {
    /// The whole-interval key.
    pub interval: Interval<T>,
    /// The payload filed under that key.
    pub value: V,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct IntervalMapMeasure<T, V>(PhantomData<(T, V)>);

#[derive(Clone, Debug, PartialEq, Eq)]
struct IntervalMapSummary<T> {
    last_interval: Option<Interval<T>>,
    max_high: Option<T>,
}

impl<T, V> MeasurePolicy<IntervalMapEntry<T, V>> for IntervalMapMeasure<T, V>
where
    T: Ord + Clone,
    V: Clone,
{
    type Measure = IntervalMapSummary<T>;

    fn empty() -> Self::Measure {
        IntervalMapSummary {
            last_interval: None,
            max_high: None,
        }
    }

    fn measure(element: &IntervalMapEntry<T, V>) -> Self::Measure {
        IntervalMapSummary {
            last_interval: Some(element.interval.clone()),
            max_high: Some(element.interval.high.clone()),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        let last_interval = right
            .last_interval
            .clone()
            .or_else(|| left.last_interval.clone());
        let max_high = match (&left.max_high, &right.max_high) {
            (Some(left), Some(right)) => Some(left.max(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        };
        IntervalMapSummary {
            last_interval,
            max_high,
        }
    }
}

type IntervalMapStorage<T, V> = FingerTree<IntervalMapEntry<T, V>, IntervalMapMeasure<T, V>>;

/// Immutable map from validated closed intervals to payload values.
///
/// Keys are unique and ordered lexicographically by `(low, high)`. Distinct overlapping intervals
/// remain independent. One measured finger tree carries both the exact-key ordering signpost and
/// maximum high endpoint used by overlap queries.
pub struct PersistentIntervalMap<T, V>
where
    T: Ord + Clone,
    V: Clone,
{
    entries: IntervalMapStorage<T, V>,
}

impl<T, V> Clone for PersistentIntervalMap<T, V>
where
    T: Ord + Clone,
    V: Clone,
{
    fn clone(&self) -> Self {
        Self {
            entries: self.entries.clone(),
        }
    }
}

impl<T, V> PersistentIntervalMap<T, V>
where
    T: Ord + Clone,
    V: Clone,
{
    /// Creates an empty interval map.
    #[must_use]
    pub fn new() -> Self {
        Self {
            entries: IntervalMapStorage::new(),
        }
    }

    /// Returns the number of interval keys. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Returns `true` when the map holds no entries.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Iterates the entries in ascending `(low, high)` key order.
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &IntervalMapEntry<T, V>> {
        self.entries.iter()
    }

    /// Iterates the interval keys in ascending `(low, high)` order.
    pub fn keys(&self) -> impl ExactSizeIterator<Item = &Interval<T>> {
        self.entries.iter().map(|entry| &entry.interval)
    }

    /// Iterates the payloads in their keys' ascending order.
    pub fn values(&self) -> impl ExactSizeIterator<Item = &V> {
        self.entries.iter().map(|entry| &entry.value)
    }

    /// Reports whether two maps are backed by the same entry sequence, so neither can observe an edit
    /// made to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
    }

    /// Borrows the payload filed under exactly this interval key, or `Ok(None)` when absent.
    ///
    /// The interval must be well formed; an inverted one yields [`IntervalMapError::InvalidInterval`]
    /// rather than a silent miss. Lookup is by whole-key identity, not overlap. O(log n).
    pub fn get(&self, interval: &Interval<T>) -> Result<Option<&V>, IntervalMapError> {
        self.get_entry(interval)
            .map(|entry| entry.map(|entry| &entry.value))
    }

    /// Borrows the whole stored entry for exactly this interval key, or `Ok(None)` when absent.
    ///
    /// Recovers the *stored* interval representative, which set-item replacement retains.
    pub fn get_entry(
        &self,
        interval: &Interval<T>,
    ) -> Result<Option<&IntervalMapEntry<T, V>>, IntervalMapError> {
        validate_interval(interval)?;
        let index = self.lower_bound(interval);
        Ok(self
            .entries
            .get(index)
            .filter(|entry| compare_intervals(&entry.interval, interval).is_eq()))
    }

    /// Reports whether exactly this interval key is present. O(log n).
    pub fn contains_key(&self, interval: &Interval<T>) -> Result<bool, IntervalMapError> {
        self.get_entry(interval).map(|entry| entry.is_some())
    }

    /// Strictly adds an interval key.
    pub fn add(&self, interval: Interval<T>, value: V) -> Result<Self, IntervalMapError> {
        validate_interval(&interval)?;
        let index = self.lower_bound(&interval);
        if self
            .entries
            .get(index)
            .is_some_and(|entry| compare_intervals(&entry.interval, &interval).is_eq())
        {
            return Err(IntervalMapError::DuplicateInterval);
        }
        Ok(self.insert_at(index, IntervalMapEntry { interval, value }))
    }

    /// Adds or replaces a payload, retaining the first interval representative.
    pub fn set_item(&self, interval: Interval<T>, value: V) -> Result<Self, IntervalMapError>
    where
        V: PartialEq,
    {
        validate_interval(&interval)?;
        let index = self.lower_bound(&interval);
        if let Some(stored) = self.entries.get(index)
            && compare_intervals(&stored.interval, &interval).is_eq()
        {
            if stored.value == value {
                return Ok(self.clone());
            }
            return Ok(self.replace_at(
                index,
                IntervalMapEntry {
                    interval: stored.interval.clone(),
                    value,
                },
            ));
        }
        Ok(self.insert_at(index, IntervalMapEntry { interval, value }))
    }

    /// Removes the entry filed under exactly this interval key. Removing an absent key is a no-op
    /// that shares the receiver's storage.
    pub fn remove(&self, interval: &Interval<T>) -> Result<Self, IntervalMapError> {
        self.try_remove(interval)
            .map(|removed| removed.map_or_else(|| self.clone(), |(map, _)| map))
    }

    /// Removes the entry filed under exactly this interval key, returning the resulting map and the
    /// removed entry, or `Ok(None)` when the key was absent.
    pub fn try_remove(
        &self,
        interval: &Interval<T>,
    ) -> Result<Option<(Self, IntervalMapEntry<T, V>)>, IntervalMapError> {
        validate_interval(interval)?;
        let index = self.lower_bound(interval);
        if !self
            .entries
            .get(index)
            .is_some_and(|entry| compare_intervals(&entry.interval, interval).is_eq())
        {
            return Ok(None);
        }
        Ok(self.remove_at(index))
    }

    /// Returns an empty map. Clearing an already empty map is a no-op that shares the receiver's
    /// storage.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::new()
        }
    }

    /// Borrows some entry whose key overlaps `probe`, or `None` when none does.
    ///
    /// O(log n): the cached maximum high endpoint lets the search skip subtrees that cannot reach
    /// `probe`. Which of several overlapping entries is returned is unspecified.
    pub fn find_overlap(
        &self,
        probe: &Interval<T>,
    ) -> Result<Option<&IntervalMapEntry<T, V>>, IntervalMapError> {
        validate_interval(probe)?;
        let end = self.upper_low_bound_index(&probe.high);
        let Some(index) = self.first_possible_overlap_index(&probe.low) else {
            return Ok(None);
        };
        Ok((index < end).then(|| {
            self.entries
                .get(index)
                .expect("measured overlap index is in range")
        }))
    }

    /// Borrows some entry whose key contains `point`, or `None` when none does. O(log n).
    pub fn find_containing(&self, point: &T) -> Option<&IntervalMapEntry<T, V>> {
        let end = self.upper_low_bound_index(point);
        let index = self.first_possible_overlap_index(point)?;
        (index < end).then(|| {
            self.entries
                .get(index)
                .expect("measured containment index is in range")
        })
    }

    /// Collects every entry whose key overlaps `probe`, in ascending key order.
    ///
    /// O(log n + k) for `k` results, rather than a scan of the whole map.
    pub fn find_overlaps(
        &self,
        probe: &Interval<T>,
    ) -> Result<Vec<IntervalMapEntry<T, V>>, IntervalMapError> {
        validate_interval(probe)?;
        let mut remaining = self.candidate_prefix(&probe.high);
        let mut overlaps = Vec::new();

        // Each split discards a maximal prefix whose high endpoints are all too small. Continuing
        // with the suffix after the hit keeps the traversal output-sensitive.
        while let Some((_, entry, right)) = remaining.try_split_find(|summary| {
            summary
                .max_high
                .as_ref()
                .is_some_and(|high| high >= &probe.low)
        }) {
            overlaps.push(entry);
            remaining = right;
        }
        Ok(overlaps)
    }

    /// Counts the entries whose keys overlap `probe`, without materializing them.
    pub fn count_overlaps(&self, probe: &Interval<T>) -> Result<usize, IntervalMapError> {
        validate_interval(probe)?;
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
        Ok(count)
    }

    /// Checks that every stored interval is well formed, that keys are strictly ascending, and that
    /// the cached last-key and maximum-high annotations agree with the entries.
    ///
    /// A defensive audit over the whole map; ordinary operations maintain these invariants.
    pub fn validate(&self) -> Result<IntervalMapStatistics, IntervalMapInvariantError> {
        let mut previous: Option<&Interval<T>> = None;
        let mut maximum: Option<&T> = None;
        for entry in self.entries.iter() {
            if entry.interval.low > entry.interval.high {
                return Err(IntervalMapInvariantError::InvalidInterval);
            }
            if previous.is_some_and(|prior| !compare_intervals(prior, &entry.interval).is_lt()) {
                return Err(IntervalMapInvariantError::KeysNotStrictlyOrdered);
            }
            previous = Some(&entry.interval);
            if maximum.is_none_or(|high| entry.interval.high > *high) {
                maximum = Some(&entry.interval.high);
            }
        }
        let summary = self.entries.measure();
        if previous != summary.last_interval.as_ref() {
            return Err(IntervalMapInvariantError::LastKeyMismatch);
        }
        if maximum != summary.max_high.as_ref() {
            return Err(IntervalMapInvariantError::MaximumHighMismatch);
        }
        Ok(IntervalMapStatistics { count: self.len() })
    }

    fn lower_bound(&self, interval: &Interval<T>) -> usize {
        self.entries
            .try_locate(|summary| {
                summary
                    .last_interval
                    .as_ref()
                    .is_some_and(|stored| !compare_intervals(stored, interval).is_lt())
            })
            .index
    }

    fn upper_low_bound_index(&self, high: &T) -> usize {
        self.entries
            .try_locate(|summary| {
                summary
                    .last_interval
                    .as_ref()
                    .is_some_and(|interval| interval.low > *high)
            })
            .index
    }

    fn first_possible_overlap_index(&self, low: &T) -> Option<usize> {
        let located = self
            .entries
            .try_locate(|summary| summary.max_high.as_ref().is_some_and(|high| high >= low));
        located.item.map(|_| located.index)
    }

    fn candidate_prefix(&self, high: &T) -> IntervalMapStorage<T, V> {
        self.entries
            .split(|summary| {
                summary
                    .last_interval
                    .as_ref()
                    .is_some_and(|interval| interval.low > *high)
            })
            .left
    }

    fn insert_at(&self, index: usize, entry: IntervalMapEntry<T, V>) -> Self {
        let split = self
            .entries
            .split_at_index(index)
            .expect("computed insertion rank is valid");
        Self {
            entries: split.left.append(entry).concat(&split.right),
        }
    }

    fn replace_at(&self, index: usize, entry: IntervalMapEntry<T, V>) -> Self {
        let split = self
            .entries
            .split_at_index(index)
            .expect("computed replacement rank is valid");
        let after = split
            .right
            .split_at_index(1)
            .expect("replacement key is present");
        Self {
            entries: split.left.append(entry).concat(&after.right),
        }
    }

    fn remove_at(&self, index: usize) -> Option<(Self, IntervalMapEntry<T, V>)> {
        let split = self.entries.split_at_index(index)?;
        let after = split.right.split_at_index(1)?;
        let removed = after.left.front()?.clone();
        Some((
            Self {
                entries: split.left.concat(&after.right),
            },
            removed,
        ))
    }
}

/// Presence-discriminated interval-map cursor search.
pub struct IntervalMapCursorSearch<T: Ord + Clone, V: Clone> {
    pub found: bool,
    pub cursor: PersistentIntervalMapCursor<T, V>,
}

/// Immutable interval-key-order root-plus-rank cursor.
#[derive(Clone)]
pub struct PersistentIntervalMapCursor<T: Ord + Clone, V: Clone> {
    map: PersistentIntervalMap<T, V>,
    position: usize,
}

impl<T: Ord + Clone, V: Clone> PersistentIntervalMap<T, V> {
    /// Creates a cursor at the gap `position` in `0..=len` of the key-ordered sequence, or `None` when
    /// it exceeds the entry count.
    pub fn cursor_at(&self, position: usize) -> Option<PersistentIntervalMapCursor<T, V>> {
        (position <= self.len()).then(|| PersistentIntervalMapCursor {
            map: self.clone(),
            position,
        })
    }
    /// Creates a cursor before the first entry whose key is not below `interval`. O(log n).
    pub fn cursor_at_lower_bound(
        &self,
        interval: &Interval<T>,
    ) -> Result<PersistentIntervalMapCursor<T, V>, IntervalMapError> {
        validate_interval(interval)?;
        Ok(self
            .cursor_at(self.lower_bound(interval))
            .expect("lower bound is valid"))
    }
    /// Creates a cursor after any entry whose key equals `interval`. O(log n).
    pub fn cursor_at_upper_bound(
        &self,
        interval: &Interval<T>,
    ) -> Result<PersistentIntervalMapCursor<T, V>, IntervalMapError> {
        let cursor = self.cursor_at_lower_bound(interval)?;
        Ok(
            if cursor
                .peek_next()
                .is_some_and(|entry| compare_intervals(&entry.interval, interval).is_eq())
            {
                cursor
                    .move_next()
                    .expect("matching entry has successor gap")
            } else {
                cursor
            },
        )
    }
    /// Seeks to exactly this interval key and reports whether it is present. On a miss the cursor
    /// sits at the lower bound, which is where the key would be inserted.
    pub fn find_cursor(
        &self,
        interval: &Interval<T>,
    ) -> Result<IntervalMapCursorSearch<T, V>, IntervalMapError> {
        let cursor = self.cursor_at_lower_bound(interval)?;
        Ok(IntervalMapCursorSearch {
            found: cursor
                .peek_next()
                .is_some_and(|entry| compare_intervals(&entry.interval, interval).is_eq()),
            cursor,
        })
    }
    /// Seeks to the first entry whose key overlaps `probe` and reports whether one exists.
    ///
    /// Advancing with [`PersistentIntervalMapCursor::seek_next_overlap`] enumerates the remaining
    /// matches without collecting them all first.
    pub fn find_overlap_cursor(
        &self,
        probe: &Interval<T>,
    ) -> Result<IntervalMapCursorSearch<T, V>, IntervalMapError> {
        self.find_overlap_cursor_from(0, probe)
    }
    /// Seeks to the first entry whose key contains `point` and reports whether one exists.
    pub fn find_containing_cursor(&self, point: &T) -> IntervalMapCursorSearch<T, V> {
        self.find_overlap_cursor_from(
            0,
            &Interval {
                low: point.clone(),
                high: point.clone(),
            },
        )
        .expect("point interval is valid")
    }
    fn find_overlap_cursor_from(
        &self,
        start: usize,
        probe: &Interval<T>,
    ) -> Result<IntervalMapCursorSearch<T, V>, IntervalMapError> {
        validate_interval(probe)?;
        assert!(start <= self.len(), "cursor start is outside interval map");
        let found = self
            .entries
            .iter()
            .enumerate()
            .skip(start)
            .take_while(|(_, entry)| entry.interval.low <= probe.high)
            .find(|(_, entry)| entry.interval.overlaps(probe))
            .map(|(rank, _)| rank);
        Ok(IntervalMapCursorSearch {
            found: found.is_some(),
            cursor: self
                .cursor_at(found.unwrap_or(self.len()))
                .expect("overlap rank is valid"),
        })
    }
}

impl<T: Ord + Clone, V: Clone> PersistentIntervalMapCursor<T, V> {
    /// Returns the entry count of the map version this cursor is positioned in.
    pub fn len(&self) -> usize {
        self.map.len()
    }
    /// Returns `true` when that map version holds no entries.
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }
    /// Returns the cursor's gap index in `0..=len`.
    pub fn position(&self) -> usize {
        self.position
    }
    /// Returns `true` when the gap precedes the first entry.
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }
    /// Returns `true` when the gap follows the last entry.
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }
    /// Borrows the entry immediately before the gap, or `None` at the start.
    pub fn peek_previous(&self) -> Option<&IntervalMapEntry<T, V>> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.map.entries.get(rank))
    }
    /// Borrows the entry immediately after the gap, or `None` at the end.
    pub fn peek_next(&self) -> Option<&IntervalMapEntry<T, V>> {
        self.map.entries.get(self.position)
    }
    /// Returns a cursor one position earlier, or `None` at the start. The receiver is unchanged.
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.map.cursor_at(position))
    }
    /// Returns a cursor one position later, or `None` at the end. The receiver is unchanged.
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            map: self.map.clone(),
            position: self.position + 1,
        })
    }
    /// Jumps to the gap at `position` within the same map version, or `None` when `position` exceeds
    /// the entry count.
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.map.cursor_at(position)
        }
    }
    /// Advances to the next entry at or after the gap whose key overlaps `probe`, reporting whether
    /// one was found.
    ///
    /// Repeated calls stream the matches one at a time instead of collecting them, as
    /// [`PersistentIntervalMap::find_overlaps`] does.
    pub fn seek_next_overlap(
        &self,
        probe: &Interval<T>,
    ) -> Result<IntervalMapCursorSearch<T, V>, IntervalMapError> {
        self.map.find_overlap_cursor_from(
            if self.position < self.len() {
                self.position + 1
            } else {
                self.len()
            },
            probe,
        )
    }
    /// Adds an entry and returns a cursor just after it in key order.
    ///
    /// Strict: an already present interval key fails with [`IntervalMapError::DuplicateInterval`],
    /// and an inverted interval with [`IntervalMapError::InvalidInterval`]. The gap moves to the
    /// key's ordered position, since placement is decided by the endpoints.
    pub fn insert(&self, interval: Interval<T>, value: V) -> Result<Self, IntervalMapError> {
        let position = self.map.lower_bound(&interval);
        self.map.add(interval, value).map(|map| Self {
            map,
            position: position + 1,
        })
    }
    /// Removes the entry before the gap and returns a cursor in its place, or `Ok(None)` at the
    /// start.
    pub fn delete_previous(&self) -> Result<Option<Self>, IntervalMapError> {
        let Some(position) = self.position.checked_sub(1) else {
            return Ok(None);
        };
        let interval = self
            .map
            .entries
            .get(position)
            .expect("cursor previous exists")
            .interval
            .clone();
        self.map
            .remove(&interval)
            .map(|map| Some(Self { map, position }))
    }
    /// Removes the entry after the gap and returns a cursor in its place, or `Ok(None)` at the end.
    pub fn delete_next(&self) -> Result<Option<Self>, IntervalMapError> {
        let Some(entry) = self.map.entries.get(self.position) else {
            return Ok(None);
        };
        let interval = entry.interval.clone();
        self.map.remove(&interval).map(|map| {
            Some(Self {
                map,
                position: self.position,
            })
        })
    }
    /// Borrows the map version this cursor is positioned in.
    pub fn snapshot(&self) -> &PersistentIntervalMap<T, V> {
        &self.map
    }
}

impl<T: Ord + Clone, V: Clone + PartialEq> PersistentIntervalMapCursor<T, V> {
    /// Replaces the payload of the entry after the gap, keeping its interval key and position, or
    /// returns `Ok(None)` at the end.
    ///
    /// Writing a value equal to the stored one is a no-op.
    pub fn set_next_value(&self, value: V) -> Result<Option<Self>, IntervalMapError> {
        let Some(entry) = self.peek_next() else {
            return Ok(None);
        };
        let interval = entry.interval.clone();
        self.map.set_item(interval, value).map(|map| {
            Some(Self {
                map,
                position: self.position,
            })
        })
    }
}

impl<T, V> Default for PersistentIntervalMap<T, V>
where
    T: Ord + Clone,
    V: Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, V> fmt::Debug for PersistentIntervalMap<T, V>
where
    T: Ord + Clone + fmt::Debug,
    V: Clone + fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_map()
            .entries(self.iter().map(|entry| (&entry.interval, &entry.value)))
            .finish()
    }
}

/// A rejected interval-map operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntervalMapError {
    /// The supplied interval's low endpoint exceeds its high endpoint.
    InvalidInterval,
    /// A strict insertion found an equivalent interval key already present.
    DuplicateInterval,
}

impl fmt::Display for IntervalMapError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InvalidInterval => "the interval low endpoint exceeds its high endpoint",
            Self::DuplicateInterval => "an equivalent interval key is already present",
        })
    }
}

impl std::error::Error for IntervalMapError {}

/// Statistics returned by a successful [`PersistentIntervalMap::validate`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IntervalMapStatistics {
    /// The number of entries, recounted from the stored sequence.
    pub count: usize,
}

/// A structural invariant violation found by [`PersistentIntervalMap::validate`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntervalMapInvariantError {
    /// A stored interval has its low endpoint above its high endpoint.
    InvalidInterval,
    /// Stored keys are not strictly ascending, so lookups could not be trusted.
    KeysNotStrictlyOrdered,
    /// A cached rightmost-key annotation disagrees with the entries beneath it.
    LastKeyMismatch,
    /// A cached maximum-high annotation disagrees with the entries beneath it, which would make
    /// overlap queries prune subtrees that do contain matches.
    MaximumHighMismatch,
}

impl fmt::Display for IntervalMapInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InvalidInterval => "the interval map stores an invalid interval",
            Self::KeysNotStrictlyOrdered => "interval keys are not strictly ordered",
            Self::LastKeyMismatch => "the rightmost-key annotation is inconsistent",
            Self::MaximumHighMismatch => "the maximum-high annotation is inconsistent",
        })
    }
}

impl std::error::Error for IntervalMapInvariantError {}

fn validate_interval<T>(interval: &Interval<T>) -> Result<(), IntervalMapError>
where
    T: Ord,
{
    (interval.low <= interval.high)
        .then_some(())
        .ok_or(IntervalMapError::InvalidInterval)
}

fn compare_intervals<T>(left: &Interval<T>, right: &Interval<T>) -> std::cmp::Ordering
where
    T: Ord,
{
    left.low
        .cmp(&right.low)
        .then_with(|| left.high.cmp(&right.high))
}
