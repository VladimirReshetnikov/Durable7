use crate::measured::{FingerTree, MeasurePolicy, MeasuredSplit, OrderStatisticMeasure, RankedKey};
use std::marker::PhantomData;

type SortedStorage<T> = FingerTree<T, OrderStatisticMeasure<T>>;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct EntryMeasure<K, V>(PhantomData<(K, V)>);

impl<K, V> MeasurePolicy<(K, V)> for EntryMeasure<K, V>
where
    K: Clone,
{
    type Measure = RankedKey<K>;

    fn empty() -> Self::Measure {
        RankedKey {
            count: 0,
            key: None,
        }
    }

    fn measure(element: &(K, V)) -> Self::Measure {
        RankedKey {
            count: 1,
            key: Some(element.0.clone()),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        RankedKey {
            count: left.count + right.count,
            key: right.key.clone().or_else(|| left.key.clone()),
        }
    }
}

type MapStorage<K, V> = FingerTree<(K, V), EntryMeasure<K, V>>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DuplicateKeyError;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedBag<T>
where
    T: Clone,
{
    items: SortedStorage<T>,
}

impl<T> SortedBag<T>
where
    T: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_items(SortedStorage::new())
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.items.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    #[must_use]
    pub fn min(&self) -> Option<&T> {
        self.items.front()
    }

    #[must_use]
    pub fn max(&self) -> Option<&T> {
        self.items.back()
    }

    #[must_use]
    pub fn get(&self, rank: usize) -> Option<&T> {
        self.items.get(rank)
    }

    #[must_use]
    pub fn contains(&self, value: &T) -> bool {
        self.count_of(value) > 0
    }

    #[must_use]
    pub fn count_less_than(&self, value: &T) -> usize {
        lower_bound(&self.items, value)
    }

    #[must_use]
    pub fn count_at_most(&self, value: &T) -> usize {
        upper_bound(&self.items, value)
    }

    #[must_use]
    pub fn count_of(&self, value: &T) -> usize {
        self.count_at_most(value) - self.count_less_than(value)
    }

    #[must_use]
    pub fn add(&self, value: T) -> Self {
        let split = split_above(&self.items, &value);
        Self::from_items(split.left.append(value).concat(&split.right))
    }

    #[must_use]
    pub fn add_range<I>(&self, values: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = self.clone();
        for value in values {
            result = result.add(value);
        }

        result
    }

    #[must_use]
    pub fn remove(&self, value: &T) -> Self {
        let split = split_at_least(&self.items, value);
        if split.right.front().is_some_and(|item| item == value) {
            let tail = split
                .right
                .split_at_index(1)
                .expect("right split after found item is valid")
                .right;
            Self::from_items(split.left.concat(&tail))
        } else {
            self.clone()
        }
    }

    #[must_use]
    pub fn remove_all(&self, value: &T) -> Self {
        let less_split = split_at_least(&self.items, value);
        let greater_split = split_above(&less_split.right, value);
        if greater_split.left.is_empty() {
            return self.clone();
        }

        Self::from_items(less_split.left.concat(&greater_split.right))
    }

    #[must_use]
    pub fn get_range(&self, start: usize, count: usize) -> Option<Self> {
        self.items.split_at_index(start).and_then(|split| {
            split
                .right
                .split_at_index(count)
                .map(|range_split| Self::from_items(range_split.left))
        })
    }

    #[must_use]
    pub fn get_value_range(&self, low: &T, high: &T) -> Self {
        let at_least = split_at_least(&self.items, low);
        let in_range = split_above(&at_least.right, high);
        Self::from_items(in_range.left)
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
    }

    fn from_items(items: SortedStorage<T>) -> Self {
        Self { items }
    }
}

impl<T> Default for SortedBag<T>
where
    T: Ord + Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for SortedBag<T>
where
    T: Ord + Clone,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        let mut values: Vec<T> = iter.into_iter().collect();
        values.sort();
        Self::from_items(SortedStorage::from_vec(values))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedSet<T>
where
    T: Clone,
{
    items: SortedStorage<T>,
}

impl<T> SortedSet<T>
where
    T: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_items(SortedStorage::new())
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.items.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    #[must_use]
    pub fn min(&self) -> Option<&T> {
        self.items.front()
    }

    #[must_use]
    pub fn max(&self) -> Option<&T> {
        self.items.back()
    }

    #[must_use]
    pub fn get(&self, rank: usize) -> Option<&T> {
        self.items.get(rank)
    }

    #[must_use]
    pub fn contains(&self, value: &T) -> bool {
        self.index_of(value).is_some()
    }

    #[must_use]
    pub fn index_of(&self, value: &T) -> Option<usize> {
        let index = lower_bound(&self.items, value);
        (index < self.len() && self.items.get(index).is_some_and(|item| item == value))
            .then_some(index)
    }

    #[must_use]
    pub fn add(&self, value: T) -> Self {
        let split = split_at_least(&self.items, &value);
        if split.right.front().is_some_and(|item| item == &value) {
            return self.clone();
        }

        Self::from_items(split.left.append(value).concat(&split.right))
    }

    #[must_use]
    pub fn add_range<I>(&self, values: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = self.clone();
        for value in values {
            result = result.add(value);
        }

        result
    }

    #[must_use]
    pub fn remove(&self, value: &T) -> Self {
        let split = split_at_least(&self.items, value);
        if split.right.front().is_some_and(|item| item == value) {
            let tail = split
                .right
                .split_at_index(1)
                .expect("right split after found item is valid")
                .right;
            Self::from_items(split.left.concat(&tail))
        } else {
            self.clone()
        }
    }

    #[must_use]
    pub fn floor(&self, value: &T) -> Option<&T> {
        let index = upper_bound(&self.items, value).checked_sub(1)?;
        self.items.get(index)
    }

    #[must_use]
    pub fn ceiling(&self, value: &T) -> Option<&T> {
        self.items.get(lower_bound(&self.items, value))
    }

    #[must_use]
    pub fn lower(&self, value: &T) -> Option<&T> {
        let index = lower_bound(&self.items, value).checked_sub(1)?;
        self.items.get(index)
    }

    #[must_use]
    pub fn higher(&self, value: &T) -> Option<&T> {
        self.items.get(upper_bound(&self.items, value))
    }

    #[must_use]
    pub fn get_range(&self, start: usize, count: usize) -> Option<Self> {
        self.items.split_at_index(start).and_then(|split| {
            split
                .right
                .split_at_index(count)
                .map(|range_split| Self::from_items(range_split.left))
        })
    }

    #[must_use]
    pub fn get_value_range(&self, low: &T, high: &T) -> Self {
        let at_least = split_at_least(&self.items, low);
        let in_range = split_above(&at_least.right, high);
        Self::from_items(in_range.left)
    }

    #[must_use]
    pub fn union(&self, other: &Self) -> Self {
        self.merge(other, |in_left, in_right| in_left || in_right)
    }

    #[must_use]
    pub fn intersect(&self, other: &Self) -> Self {
        self.merge(other, |in_left, in_right| in_left && in_right)
    }

    #[must_use]
    pub fn except(&self, other: &Self) -> Self {
        self.merge(other, |in_left, in_right| in_left && !in_right)
    }

    #[must_use]
    pub fn symmetric_except(&self, other: &Self) -> Self {
        self.merge(other, |in_left, in_right| in_left ^ in_right)
    }

    #[must_use]
    pub fn is_subset_of(&self, other: &Self) -> bool {
        self.items.iter().all(|value| other.contains(value))
    }

    #[must_use]
    pub fn is_superset_of(&self, other: &Self) -> bool {
        other.is_subset_of(self)
    }

    #[must_use]
    pub fn is_proper_subset_of(&self, other: &Self) -> bool {
        self.len() < other.len() && self.is_subset_of(other)
    }

    #[must_use]
    pub fn is_proper_superset_of(&self, other: &Self) -> bool {
        self.len() > other.len() && self.is_superset_of(other)
    }

    #[must_use]
    pub fn overlaps(&self, other: &Self) -> bool {
        self.items.iter().any(|value| other.contains(value))
    }

    #[must_use]
    pub fn set_equals(&self, other: &Self) -> bool {
        self.items == other.items
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
    }

    fn merge<F>(&self, other: &Self, keep: F) -> Self
    where
        F: Fn(bool, bool) -> bool,
    {
        let mut next = Vec::new();
        let mut left = self.items.iter().peekable();
        let mut right = other.items.iter().peekable();
        while left.peek().is_some() || right.peek().is_some() {
            match (left.peek().copied(), right.peek().copied()) {
                (Some(left_value), Some(right_value)) => match left_value.cmp(right_value) {
                    std::cmp::Ordering::Less => {
                        if keep(true, false) {
                            next.push(left_value.clone());
                        }
                        left.next();
                    }
                    std::cmp::Ordering::Greater => {
                        if keep(false, true) {
                            next.push(right_value.clone());
                        }
                        right.next();
                    }
                    std::cmp::Ordering::Equal => {
                        if keep(true, true) {
                            next.push(left_value.clone());
                        }
                        left.next();
                        right.next();
                    }
                },
                (Some(left_value), None) => {
                    if keep(true, false) {
                        next.push(left_value.clone());
                    }
                    left.next();
                }
                (None, Some(right_value)) => {
                    if keep(false, true) {
                        next.push(right_value.clone());
                    }
                    right.next();
                }
                (None, None) => break,
            }
        }

        Self::from_items(SortedStorage::from_vec(next))
    }

    fn from_items(items: SortedStorage<T>) -> Self {
        Self { items }
    }
}

impl<T> Default for SortedSet<T>
where
    T: Ord + Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for SortedSet<T>
where
    T: Ord + Clone,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        let mut values: Vec<T> = iter.into_iter().collect();
        values.sort();
        values.dedup();
        Self::from_items(SortedStorage::from_vec(values))
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedMap<K, V>
where
    K: Clone,
{
    entries: MapStorage<K, V>,
}

impl<K, V> SortedMap<K, V>
where
    K: Ord + Clone,
    V: Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_entries(MapStorage::new())
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.index_of_key(key).is_some()
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.index_of_key(key)
            .and_then(|index| self.entries.get(index).map(|(_, value)| value))
    }

    #[must_use]
    pub fn entry_at(&self, rank: usize) -> Option<(&K, &V)> {
        self.entries.get(rank).map(|(key, value)| (key, value))
    }

    #[must_use]
    pub fn min_entry(&self) -> Option<(&K, &V)> {
        self.entry_at(0)
    }

    #[must_use]
    pub fn max_entry(&self) -> Option<(&K, &V)> {
        self.len()
            .checked_sub(1)
            .and_then(|index| self.entry_at(index))
    }

    #[must_use]
    pub fn index_of_key(&self, key: &K) -> Option<usize> {
        let index = lower_bound_by_key(&self.entries, key);
        (index < self.len()
            && self
                .entries
                .get(index)
                .is_some_and(|(stored_key, _)| stored_key == key))
        .then_some(index)
    }

    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let split = split_key_at_least(&self.entries, &key);
        if let Some((stored_key, _)) = split.right.front()
            && stored_key == &key
        {
            // The C# reference (SortedDictionary.SetItem) stores the supplied
            // key instance, not the previously stored one.
            let replacement = (key, value);
            let tail = split
                .right
                .split_at_index(1)
                .expect("right split after found entry is valid")
                .right;
            return Self::from_entries(split.left.append(replacement).concat(&tail));
        }

        Self::from_entries(split.left.append((key, value)).concat(&split.right))
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
    }

    pub fn insert(&self, key: K, value: V) -> Result<Self, DuplicateKeyError> {
        let (map, inserted) = self.try_insert(key, value);
        if inserted {
            Ok(map)
        } else {
            Err(DuplicateKeyError)
        }
    }

    #[must_use]
    pub fn try_insert(&self, key: K, value: V) -> (Self, bool) {
        let split = split_key_at_least(&self.entries, &key);
        if split
            .right
            .front()
            .is_some_and(|(stored_key, _)| stored_key == &key)
        {
            return (self.clone(), false);
        }

        (
            Self::from_entries(split.left.append((key, value)).concat(&split.right)),
            true,
        )
    }

    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let split = split_key_at_least(&self.entries, key);
        if split
            .right
            .front()
            .is_some_and(|(stored_key, _)| stored_key == key)
        {
            let tail = split
                .right
                .split_at_index(1)
                .expect("right split after found entry is valid")
                .right;
            Self::from_entries(split.left.concat(&tail))
        } else {
            self.clone()
        }
    }

    #[must_use]
    pub fn try_remove(&self, key: &K) -> Option<(Self, V)> {
        let split = split_key_at_least(&self.entries, key);
        let (_, value) = split
            .right
            .front()
            .filter(|(stored_key, _)| stored_key == key)?;
        let value = value.clone();
        let tail = split
            .right
            .split_at_index(1)
            .expect("right split after found entry is valid")
            .right;
        Some((Self::from_entries(split.left.concat(&tail)), value))
    }

    #[must_use]
    pub fn floor_entry(&self, key: &K) -> Option<(&K, &V)> {
        let index = upper_bound_by_key(&self.entries, key).checked_sub(1)?;
        self.entry_at(index)
    }

    #[must_use]
    pub fn ceiling_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.entry_at(lower_bound_by_key(&self.entries, key))
    }

    #[must_use]
    pub fn lower_entry(&self, key: &K) -> Option<(&K, &V)> {
        let index = lower_bound_by_key(&self.entries, key).checked_sub(1)?;
        self.entry_at(index)
    }

    #[must_use]
    pub fn higher_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.entry_at(upper_bound_by_key(&self.entries, key))
    }

    #[must_use]
    pub fn get_range(&self, start: usize, count: usize) -> Option<Self> {
        self.entries.split_at_index(start).and_then(|split| {
            split
                .right
                .split_at_index(count)
                .map(|range_split| Self::from_entries(range_split.left))
        })
    }

    #[must_use]
    pub fn get_key_range(&self, low: &K, high: &K) -> Self {
        let at_least = split_key_at_least(&self.entries, low);
        let in_range = split_key_above(&at_least.right, high);
        Self::from_entries(in_range.left)
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<(K, V)> {
        self.entries.to_vec()
    }

    #[must_use]
    pub fn keys_to_vec(&self) -> Vec<K> {
        self.entries.iter().map(|(key, _)| key.clone()).collect()
    }

    #[must_use]
    pub fn values_to_vec(&self) -> Vec<V> {
        self.entries
            .iter()
            .map(|(_, value)| value.clone())
            .collect()
    }

    fn from_entries(entries: MapStorage<K, V>) -> Self {
        Self { entries }
    }
}

impl<K, V> Default for SortedMap<K, V>
where
    K: Ord + Clone,
    V: Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<K, V> FromIterator<(K, V)> for SortedMap<K, V>
where
    K: Ord + Clone,
    V: Clone,
{
    fn from_iter<I: IntoIterator<Item = (K, V)>>(iter: I) -> Self {
        let mut entries: Vec<(K, V)> = iter.into_iter().collect();
        entries.sort_by(|(left, _), (right, _)| left.cmp(right));
        let mut compacted: Vec<(K, V)> = Vec::with_capacity(entries.len());
        for entry in entries {
            if let Some(last) = compacted.last_mut()
                && last.0 == entry.0
            {
                // The sort is stable, so an equal-key run keeps its input order; retain the
                // run's last entry wholesale (key instance and value), matching the C#
                // reference's CreateRange contract.
                *last = entry;
                continue;
            }

            compacted.push(entry);
        }

        Self::from_entries(MapStorage::from_vec(compacted))
    }
}

/// Presence-discriminated ordered cursor search.
pub struct OrderedCursorSearch<C> {
    pub found: bool,
    pub cursor: C,
}

/// Immutable root-plus-rank cursor over a persistent sorted bag.
#[derive(Clone, Debug)]
pub struct SortedBagCursor<T: Clone> {
    bag: SortedBag<T>,
    position: usize,
}

impl<T: Ord + Clone> SortedBag<T> {
    pub fn cursor_at(&self, position: usize) -> Option<SortedBagCursor<T>> {
        (position <= self.len()).then(|| SortedBagCursor {
            bag: self.clone(),
            position,
        })
    }
    pub fn cursor_at_lower_bound(&self, value: &T) -> SortedBagCursor<T> {
        self.cursor_at(self.count_less_than(value))
            .expect("lower bound is valid")
    }
    pub fn cursor_at_upper_bound(&self, value: &T) -> SortedBagCursor<T> {
        self.cursor_at(self.count_at_most(value))
            .expect("upper bound is valid")
    }
    pub fn find_cursor(&self, value: &T) -> OrderedCursorSearch<SortedBagCursor<T>> {
        let cursor = self.cursor_at_lower_bound(value);
        OrderedCursorSearch {
            found: cursor.peek_next().is_some_and(|item| item == value),
            cursor,
        }
    }
}

impl<T: Ord + Clone> SortedBagCursor<T> {
    pub fn len(&self) -> usize {
        self.bag.len()
    }
    pub fn is_empty(&self) -> bool {
        self.bag.is_empty()
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
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.bag.get(rank))
    }
    pub fn peek_next(&self) -> Option<&T> {
        self.bag.get(self.position)
    }
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.bag.cursor_at(position))
    }
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            bag: self.bag.clone(),
            position: self.position + 1,
        })
    }
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.bag.cursor_at(position)
        }
    }
    pub fn add(&self, value: T) -> Self {
        let position = self.bag.count_at_most(&value);
        Self {
            bag: self.bag.add(value),
            position: position + 1,
        }
    }
    fn delete_at(&self, rank: usize, position: usize) -> Self {
        let first = self
            .bag
            .items
            .split_at_index(rank)
            .expect("cursor rank is valid");
        let tail = first
            .right
            .split_at_index(1)
            .expect("focused occurrence exists")
            .right;
        Self {
            bag: SortedBag::from_items(first.left.concat(&tail)),
            position,
        }
    }
    pub fn delete_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .map(|rank| self.delete_at(rank, rank))
    }
    pub fn delete_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| self.delete_at(self.position, self.position))
    }
    pub fn snapshot(&self) -> &SortedBag<T> {
        &self.bag
    }
}

/// Immutable root-plus-rank cursor over a persistent sorted set.
#[derive(Clone, Debug)]
pub struct SortedSetCursor<T: Clone> {
    set: SortedSet<T>,
    position: usize,
}

impl<T: Ord + Clone> SortedSet<T> {
    pub fn cursor_at(&self, position: usize) -> Option<SortedSetCursor<T>> {
        (position <= self.len()).then(|| SortedSetCursor {
            set: self.clone(),
            position,
        })
    }
    pub fn cursor_at_lower_bound(&self, value: &T) -> SortedSetCursor<T> {
        self.cursor_at(lower_bound(&self.items, value))
            .expect("lower bound is valid")
    }
    pub fn cursor_at_upper_bound(&self, value: &T) -> SortedSetCursor<T> {
        self.cursor_at(upper_bound(&self.items, value))
            .expect("upper bound is valid")
    }
    pub fn find_cursor(&self, value: &T) -> OrderedCursorSearch<SortedSetCursor<T>> {
        let cursor = self.cursor_at_lower_bound(value);
        OrderedCursorSearch {
            found: cursor.peek_next().is_some_and(|item| item == value),
            cursor,
        }
    }
}

impl<T: Ord + Clone> SortedSetCursor<T> {
    pub fn len(&self) -> usize {
        self.set.len()
    }
    pub fn is_empty(&self) -> bool {
        self.set.is_empty()
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
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.set.get(rank))
    }
    pub fn peek_next(&self) -> Option<&T> {
        self.set.get(self.position)
    }
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.set.cursor_at(position))
    }
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            set: self.set.clone(),
            position: self.position + 1,
        })
    }
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.set.cursor_at(position)
        }
    }
    pub fn add(&self, value: T) -> Self {
        let position = lower_bound(&self.set.items, &value);
        Self {
            set: self.set.add(value),
            position: position + 1,
        }
    }
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let item = self.set.get(position)?.clone();
        Some(Self {
            set: self.set.remove(&item),
            position,
        })
    }
    pub fn delete_next(&self) -> Option<Self> {
        let item = self.set.get(self.position)?.clone();
        Some(Self {
            set: self.set.remove(&item),
            position: self.position,
        })
    }
    pub fn snapshot(&self) -> &SortedSet<T> {
        &self.set
    }
}

/// Immutable key-order root-plus-rank cursor over a persistent sorted map.
#[derive(Clone, Debug)]
pub struct SortedMapCursor<K: Clone, V> {
    map: SortedMap<K, V>,
    position: usize,
}

impl<K: Ord + Clone, V: Clone> SortedMap<K, V> {
    pub fn cursor_at(&self, position: usize) -> Option<SortedMapCursor<K, V>> {
        (position <= self.len()).then(|| SortedMapCursor {
            map: self.clone(),
            position,
        })
    }
    pub fn cursor_at_lower_bound(&self, key: &K) -> SortedMapCursor<K, V> {
        self.cursor_at(lower_bound_by_key(&self.entries, key))
            .expect("lower bound is valid")
    }
    pub fn cursor_at_upper_bound(&self, key: &K) -> SortedMapCursor<K, V> {
        self.cursor_at(upper_bound_by_key(&self.entries, key))
            .expect("upper bound is valid")
    }
    pub fn find_cursor(&self, key: &K) -> OrderedCursorSearch<SortedMapCursor<K, V>> {
        let cursor = self.cursor_at_lower_bound(key);
        OrderedCursorSearch {
            found: cursor.peek_next().is_some_and(|(stored, _)| stored == key),
            cursor,
        }
    }
}

impl<K: Ord + Clone, V: Clone> SortedMapCursor<K, V> {
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
    pub fn peek_previous(&self) -> Option<(&K, &V)> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.map.entry_at(rank))
    }
    pub fn peek_next(&self) -> Option<(&K, &V)> {
        self.map.entry_at(self.position)
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
    pub fn insert(&self, key: K, value: V) -> Result<Self, DuplicateKeyError> {
        let position = lower_bound_by_key(&self.map.entries, &key);
        self.map.insert(key, value).map(|map| Self {
            map,
            position: position + 1,
        })
    }
    pub fn try_insert(&self, key: K, value: V) -> OrderedCursorSearch<Self> {
        let position = lower_bound_by_key(&self.map.entries, &key);
        let (map, found) = self.map.try_insert(key, value);
        OrderedCursorSearch {
            found,
            cursor: Self {
                map,
                position: if found { position + 1 } else { position },
            },
        }
    }
    pub fn set_item(&self, key: K, value: V) -> Self {
        let location = self.map.find_cursor(&key);
        Self {
            map: self.map.set_item(key, value),
            position: if location.found {
                location.cursor.position
            } else {
                location.cursor.position + 1
            },
        }
    }
    pub fn set_next_value(&self, value: V) -> Option<Self> {
        let (key, _) = self.peek_next()?;
        Some(Self {
            map: self.map.set_item(key.clone(), value),
            position: self.position,
        })
    }
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let (key, _) = self.map.entry_at(position)?;
        Some(Self {
            map: self.map.remove(key),
            position,
        })
    }
    pub fn delete_next(&self) -> Option<Self> {
        let (key, _) = self.map.entry_at(self.position)?;
        Some(Self {
            map: self.map.remove(key),
            position: self.position,
        })
    }
    pub fn snapshot(&self) -> &SortedMap<K, V> {
        &self.map
    }
}

fn split_at_least<T>(
    items: &SortedStorage<T>,
    value: &T,
) -> MeasuredSplit<T, OrderStatisticMeasure<T>>
where
    T: Ord + Clone,
{
    items.split(|measure| measure.key.as_ref().is_some_and(|key| key >= value))
}

fn split_above<T>(items: &SortedStorage<T>, value: &T) -> MeasuredSplit<T, OrderStatisticMeasure<T>>
where
    T: Ord + Clone,
{
    items.split(|measure| measure.key.as_ref().is_some_and(|key| key > value))
}

fn lower_bound<T>(items: &SortedStorage<T>, value: &T) -> usize
where
    T: Ord + Clone,
{
    items
        .try_locate(|measure| measure.key.as_ref().is_some_and(|key| key >= value))
        .index
}

fn upper_bound<T>(items: &SortedStorage<T>, value: &T) -> usize
where
    T: Ord + Clone,
{
    items
        .try_locate(|measure| measure.key.as_ref().is_some_and(|key| key > value))
        .index
}

fn split_key_at_least<K, V>(
    entries: &MapStorage<K, V>,
    key: &K,
) -> MeasuredSplit<(K, V), EntryMeasure<K, V>>
where
    K: Ord + Clone,
{
    entries.split(|measure| {
        measure
            .key
            .as_ref()
            .is_some_and(|entry_key| entry_key >= key)
    })
}

fn split_key_above<K, V>(
    entries: &MapStorage<K, V>,
    key: &K,
) -> MeasuredSplit<(K, V), EntryMeasure<K, V>>
where
    K: Ord + Clone,
{
    entries.split(|measure| {
        measure
            .key
            .as_ref()
            .is_some_and(|entry_key| entry_key > key)
    })
}

fn lower_bound_by_key<K, V>(entries: &MapStorage<K, V>, key: &K) -> usize
where
    K: Ord + Clone,
    V: Clone,
{
    entries
        .try_locate(|measure| {
            measure
                .key
                .as_ref()
                .is_some_and(|entry_key| entry_key >= key)
        })
        .index
}

fn upper_bound_by_key<K, V>(entries: &MapStorage<K, V>, key: &K) -> usize
where
    K: Ord + Clone,
    V: Clone,
{
    entries
        .try_locate(|measure| {
            measure
                .key
                .as_ref()
                .is_some_and(|entry_key| entry_key > key)
        })
        .index
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cmp::Ordering;

    #[test]
    fn set_item_stores_the_supplied_key_instance() {
        // Key type whose ordering ignores the label, mirroring a
        // comparer-equal-but-distinct key in the C# reference.
        #[derive(Clone, Debug)]
        struct Key {
            id: i32,
            label: &'static str,
        }

        impl PartialEq for Key {
            fn eq(&self, other: &Self) -> bool {
                self.id == other.id
            }
        }

        impl Eq for Key {}

        impl PartialOrd for Key {
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                Some(self.cmp(other))
            }
        }

        impl Ord for Key {
            fn cmp(&self, other: &Self) -> Ordering {
                self.id.cmp(&other.id)
            }
        }

        let map = SortedMap::new().set_item(
            Key {
                id: 1,
                label: "old",
            },
            10,
        );
        let updated = map.set_item(
            Key {
                id: 1,
                label: "new",
            },
            20,
        );

        assert_eq!(updated.len(), 1);
        let (stored_key, value) = updated.min_entry().unwrap();
        assert_eq!(stored_key.label, "new");
        assert_eq!(*value, 20);
        // The original snapshot keeps the original key.
        assert_eq!(map.min_entry().unwrap().0.label, "old");
    }

    #[test]
    fn from_iter_keeps_the_last_entry_of_an_equal_key_run() {
        // Key type whose ordering ignores the label, mirroring a
        // comparer-equal-but-distinct key in the C# reference.
        #[derive(Clone, Debug)]
        struct Key {
            id: i32,
            label: &'static str,
        }

        impl PartialEq for Key {
            fn eq(&self, other: &Self) -> bool {
                self.id == other.id
            }
        }

        impl Eq for Key {}

        impl PartialOrd for Key {
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                Some(self.cmp(other))
            }
        }

        impl Ord for Key {
            fn cmp(&self, other: &Self) -> Ordering {
                self.id.cmp(&other.id)
            }
        }

        // The C# reference's CreateRange keeps the last entry of each equal-key run
        // wholesale: the retained key instance must be the last one supplied, exactly
        // as a set_item sequence over the same entries would leave it.
        let map: SortedMap<Key, i32> = [
            (
                Key {
                    id: 1,
                    label: "old",
                },
                10,
            ),
            (
                Key {
                    id: 2,
                    label: "other",
                },
                30,
            ),
            (
                Key {
                    id: 1,
                    label: "new",
                },
                20,
            ),
        ]
        .into_iter()
        .collect();

        assert_eq!(map.len(), 2);
        let (stored_key, value) = map.min_entry().unwrap();
        assert_eq!(stored_key.label, "new");
        assert_eq!(*value, 20);
    }

    #[test]
    fn sorted_bag_keeps_duplicates_and_rank_counts() {
        let bag: SortedBag<_> = [3, 1, 2, 2, 4].into_iter().collect();
        let added = bag.add(2);
        let removed = added.remove(&2);
        let value_range = added.get_value_range(&2, &3);
        let empty_range = added.get_value_range(&4, &2);

        assert_eq!(bag.to_vec(), vec![1, 2, 2, 3, 4]);
        assert_eq!(added.count_of(&2), 3);
        assert_eq!(added.count_less_than(&3), 4);
        assert_eq!(removed.count_of(&2), 2);
        assert_eq!(value_range.to_vec(), vec![2, 2, 2, 3]);
        assert!(empty_range.is_empty());
    }

    #[test]
    fn sorted_bag_edits_share_order_statistic_tree() {
        let bag: SortedBag<_> = (0..256).collect();
        let added = bag.add(128);
        let removed = bag.remove(&100);
        let removed_all = added.remove_all(&128);
        let range = bag.get_range(80, 40).unwrap();
        assert!(bag.get_range(1, usize::MAX).is_none());

        assert_eq!(
            bag.items.measure(),
            &RankedKey {
                count: 256,
                key: Some(255)
            }
        );
        assert_eq!(
            added.items.measure(),
            &RankedKey {
                count: 257,
                key: Some(255)
            }
        );
        assert_eq!(added.count_of(&128), 2);
        assert_eq!(removed.count_of(&100), 0);
        assert_eq!(removed_all.count_of(&128), 0);
        assert_eq!(range.min(), Some(&80));
        assert_eq!(range.max(), Some(&119));
        assert!(bag.items.shared_node_count_with(&added.items) > 100);
        assert!(bag.items.shared_node_count_with(&removed.items) > 100);
        assert!(added.items.shared_node_count_with(&removed_all.items) > 100);
        assert!(bag.items.shared_node_count_with(&range.items) > 16);
        assert!(bag.items.tree_depth() < 24);
        added.items.validate_invariants();
        removed.items.validate_invariants();
        removed_all.items.validate_invariants();
        range.items.validate_invariants();
    }

    #[test]
    fn sorted_set_navigation_and_algebra() {
        let left: SortedSet<_> = [1, 2, 4].into_iter().collect();
        let right: SortedSet<_> = [2, 3, 4].into_iter().collect();

        assert_eq!(left.floor(&3), Some(&2));
        assert_eq!(left.ceiling(&3), Some(&4));
        assert_eq!(left.union(&right).to_vec(), vec![1, 2, 3, 4]);
        assert_eq!(left.intersect(&right).to_vec(), vec![2, 4]);
        assert_eq!(left.except(&right).to_vec(), vec![1]);
        assert_eq!(left.symmetric_except(&right).to_vec(), vec![1, 3]);
        assert_eq!(left.get_value_range(&2, &4).to_vec(), vec![2, 4]);
        assert!(left.intersect(&right).is_proper_subset_of(&left));
        assert!(left.is_proper_superset_of(&left.intersect(&right)));
        assert!(!left.is_proper_subset_of(&left));
    }

    #[test]
    fn set_algebra_streams_large_disjoint_inputs() {
        let left: SortedSet<_> = (0..50_000).map(|value| value * 2).collect();
        let right: SortedSet<_> = (0..50_000).map(|value| value * 2 + 1).collect();

        let union = left.union(&right);
        assert_eq!(union.len(), 100_000);
        assert_eq!(union.min(), Some(&0));
        assert_eq!(union.max(), Some(&99_999));
        assert!(left.intersect(&right).is_empty());
        assert_eq!(left.except(&right), left);
        assert_eq!(left.symmetric_except(&right), union);
    }

    #[test]
    fn sorted_set_edits_share_order_statistic_tree() {
        let set: SortedSet<_> = (0..256).collect();
        let duplicate = set.add(128);
        let inserted = set.add(300);
        let removed = set.remove(&100);
        let range = set.get_range(80, 40).unwrap();
        assert!(set.get_range(1, usize::MAX).is_none());

        assert_eq!(
            set.items.measure(),
            &RankedKey {
                count: 256,
                key: Some(255)
            }
        );
        assert_eq!(
            inserted.items.measure(),
            &RankedKey {
                count: 257,
                key: Some(300)
            }
        );
        assert!(set.shares_storage_with(&duplicate));
        assert!(inserted.contains(&300));
        assert!(!removed.contains(&100));
        assert_eq!(range.to_vec(), (80..120).collect::<Vec<_>>());
        assert!(set.items.shared_node_count_with(&inserted.items) > 100);
        assert!(set.items.shared_node_count_with(&removed.items) > 100);
        assert!(set.items.shared_node_count_with(&range.items) > 16);
        assert!(set.items.tree_depth() < 24);
        inserted.items.validate_invariants();
        removed.items.validate_invariants();
        range.items.validate_invariants();
    }

    #[test]
    fn sorted_map_is_last_wins_for_ranges() {
        let map: SortedMap<_, _> = [(2, "b"), (1, "a"), (2, "bb")].into_iter().collect();
        let inserted = map.insert(3, "c").unwrap();
        let duplicate = inserted.insert(3, "cc");
        let key_range = inserted.get_key_range(&2, &3);
        let empty_range = inserted.get_key_range(&4, &2);

        assert_eq!(map.to_vec(), vec![(1, "a"), (2, "bb")]);
        assert_eq!(inserted.keys_to_vec(), vec![1, 2, 3]);
        assert!(matches!(duplicate, Err(DuplicateKeyError)));
        assert_eq!(inserted.floor_entry(&2), Some((&2, &"bb")));
        assert_eq!(key_range.to_vec(), vec![(2, "bb"), (3, "c")]);
        assert!(empty_range.is_empty());
    }

    #[test]
    fn sorted_map_edits_share_order_statistic_tree() {
        let map: SortedMap<_, _> = (0..256).map(|value| (value, value * 10)).collect();
        let updated = map.set_item(128, -1);
        let inserted = map.set_item(300, -300);
        let removed = map.remove(&100);
        let range = map.get_range(80, 40).unwrap();
        assert!(map.get_range(1, usize::MAX).is_none());

        assert_eq!(
            map.entries.measure(),
            &RankedKey {
                count: 256,
                key: Some(255)
            }
        );
        assert_eq!(
            inserted.entries.measure(),
            &RankedKey {
                count: 257,
                key: Some(300)
            }
        );
        assert_eq!(updated.get(&128), Some(&-1));
        assert_eq!(inserted.get(&300), Some(&-300));
        assert!(!removed.contains_key(&100));
        assert_eq!(range.min_entry(), Some((&80, &800)));
        assert_eq!(range.max_entry(), Some((&119, &1190)));
        assert!(map.entries.shared_node_count_with(&updated.entries) > 100);
        assert!(map.entries.shared_node_count_with(&inserted.entries) > 100);
        assert!(map.entries.shared_node_count_with(&removed.entries) > 100);
        assert!(map.entries.shared_node_count_with(&range.entries) > 16);
        assert!(map.entries.tree_depth() < 24);
        updated.entries.validate_invariants();
        inserted.entries.validate_invariants();
        removed.entries.validate_invariants();
        range.entries.validate_invariants();
    }
}
