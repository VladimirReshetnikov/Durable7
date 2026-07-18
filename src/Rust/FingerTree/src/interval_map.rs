use crate::interval_tree::Interval;
use crate::measured::{FingerTree, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;

/// One interval key and its payload.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IntervalMapEntry<T, V> {
    pub interval: Interval<T>,
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
    #[must_use]
    pub fn new() -> Self {
        Self {
            entries: IntervalMapStorage::new(),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    pub fn iter(&self) -> impl ExactSizeIterator<Item = &IntervalMapEntry<T, V>> {
        self.entries.iter()
    }

    pub fn keys(&self) -> impl ExactSizeIterator<Item = &Interval<T>> {
        self.entries.iter().map(|entry| &entry.interval)
    }

    pub fn values(&self) -> impl ExactSizeIterator<Item = &V> {
        self.entries.iter().map(|entry| &entry.value)
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
    }

    pub fn get(&self, interval: &Interval<T>) -> Result<Option<&V>, IntervalMapError> {
        self.get_entry(interval)
            .map(|entry| entry.map(|entry| &entry.value))
    }

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

    pub fn remove(&self, interval: &Interval<T>) -> Result<Self, IntervalMapError> {
        self.try_remove(interval)
            .map(|removed| removed.map_or_else(|| self.clone(), |(map, _)| map))
    }

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

    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::new()
        }
    }

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

    pub fn find_containing(&self, point: &T) -> Option<&IntervalMapEntry<T, V>> {
        let end = self.upper_low_bound_index(point);
        let index = self.first_possible_overlap_index(point)?;
        (index < end).then(|| {
            self.entries
                .get(index)
                .expect("measured containment index is in range")
        })
    }

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
    pub fn cursor_at(&self, position: usize) -> Option<PersistentIntervalMapCursor<T, V>> {
        (position <= self.len()).then(|| PersistentIntervalMapCursor {
            map: self.clone(),
            position,
        })
    }
    pub fn cursor_at_lower_bound(
        &self,
        interval: &Interval<T>,
    ) -> Result<PersistentIntervalMapCursor<T, V>, IntervalMapError> {
        validate_interval(interval)?;
        Ok(self
            .cursor_at(self.lower_bound(interval))
            .expect("lower bound is valid"))
    }
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
    pub fn find_overlap_cursor(
        &self,
        probe: &Interval<T>,
    ) -> Result<IntervalMapCursorSearch<T, V>, IntervalMapError> {
        self.find_overlap_cursor_from(0, probe)
    }
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
    pub fn len(&self) -> usize {
        self.map.len()
    }
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }
    pub fn position(&self) -> usize {
        self.position
    }
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }
    pub fn peek_previous(&self) -> Option<&IntervalMapEntry<T, V>> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.map.entries.get(rank))
    }
    pub fn peek_next(&self) -> Option<&IntervalMapEntry<T, V>> {
        self.map.entries.get(self.position)
    }
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.map.cursor_at(position))
    }
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            map: self.map.clone(),
            position: self.position + 1,
        })
    }
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.map.cursor_at(position)
        }
    }
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
    pub fn insert(&self, interval: Interval<T>, value: V) -> Result<Self, IntervalMapError> {
        let position = self.map.lower_bound(&interval);
        self.map.add(interval, value).map(|map| Self {
            map,
            position: position + 1,
        })
    }
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
    pub fn snapshot(&self) -> &PersistentIntervalMap<T, V> {
        &self.map
    }
}

impl<T: Ord + Clone, V: Clone + PartialEq> PersistentIntervalMapCursor<T, V> {
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntervalMapError {
    InvalidInterval,
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IntervalMapStatistics {
    pub count: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntervalMapInvariantError {
    InvalidInterval,
    KeysNotStrictlyOrdered,
    LastKeyMismatch,
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
