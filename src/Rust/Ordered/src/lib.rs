#![forbid(unsafe_code)]
#![doc = "Neutral persistent insertion-ordered collections for Rust."]

mod cursors;
mod ordered_map;
mod ordered_multimap;
pub use cursors::{
    OrderedCursorInsert, OrderedCursorSearch, PersistentOrderedMapCursor,
    PersistentOrderedMultimapCursor, PersistentOrderedSetCursor,
};
pub use ordered_map::{
    OrderedMapMoveError, OrderedMapRemoveResult, PersistentOrderedMap,
    PersistentOrderedMapInvariantError, PersistentOrderedMapStatistics,
};
pub use ordered_multimap::{
    PersistentOrderedMultimap, PersistentOrderedMultimapInvariantError,
    PersistentOrderedMultimapStatistics,
};

use std::cmp::Ordering;
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};
use std::ops::Index;

use durable7_fingertree::PersistentDeque;
use durable7_hamt::{BulkBuilder, PersistentHashMap};

const STAMP_GAP: i64 = 1_i64 << 20;

#[derive(Clone)]
struct Entry<T> {
    stamp: i64,
    item: T,
}

/// Failure reported by an explicit movement operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OrderedSetMoveError {
    /// The supplied final position does not identify an element.
    IndexOutOfRange,
    /// The requested equality class is absent.
    MissingValue,
}

impl fmt::Display for OrderedSetMoveError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IndexOutOfRange => formatter.write_str("the final index is out of range"),
            Self::MissingValue => formatter.write_str("the value is absent from the ordered set"),
        }
    }
}

impl std::error::Error for OrderedSetMoveError {}

/// Result of a removal attempt. A miss returns a root-sharing clone of the receiver.
#[must_use]
pub struct OrderedSetRemoveResult<T, S = RandomState> {
    pub removed: bool,
    pub set: PersistentOrderedSet<T, S>,
}

impl<T, S> OrderedSetRemoveResult<T, S> {
    /// Splits the result into its removal flag and successor set.
    #[must_use]
    pub fn into_parts(self) -> (bool, PersistentOrderedSet<T, S>) {
        (self.removed, self.set)
    }
}

/// Successful structural-validation statistics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PersistentOrderedSetStatistics {
    pub count: usize,
}

/// A disagreement between the independently maintained order and membership indexes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PersistentOrderedSetInvariantError {
    CountMismatch,
    StampsNotStrictlyAscending,
    OrderedEntryMissingFromMembership,
    MembershipStampMismatch,
    MembershipEntryMissingFromOrder,
    RepresentativeMismatch,
}

impl fmt::Display for PersistentOrderedSetInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::CountMismatch => "the order and membership indexes have different counts",
            Self::StampsNotStrictlyAscending => {
                "order-maintenance stamps are not strictly ascending"
            }
            Self::OrderedEntryMissingFromMembership => {
                "an ordered representative has no matching membership entry"
            }
            Self::MembershipStampMismatch => {
                "an ordered representative has a different membership stamp"
            }
            Self::MembershipEntryMissingFromOrder => {
                "a membership entry has no matching ordered entry"
            }
            Self::RepresentativeMismatch => {
                "the order and membership indexes retain different representatives"
            }
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for PersistentOrderedSetInvariantError {}

/// Immutable insertion-ordered set with explicit positional reordering.
///
/// Rust's `Eq` and `Hash` define equality classes. `S` is the retained hash-building policy.
/// Enumeration follows insertion order or a later explicit movement, reversal, or one-shot sort.
/// The first value installed for an equality class remains its stored representative until that
/// class is removed.
///
/// This is a neutral general-purpose collection. Its only implementation substrates are the
/// repository's public HAMT and FingerTree crates
/// baseline.
pub struct PersistentOrderedSet<T, S = RandomState> {
    order: PersistentDeque<Entry<T>>,
    stamps: PersistentHashMap<T, i64, S>,
}

impl<T, S> Clone for PersistentOrderedSet<T, S>
where
    S: Clone,
{
    fn clone(&self) -> Self {
        Self {
            order: self.order.clone(),
            stamps: self.stamps.clone(),
        }
    }
}

impl<T> PersistentOrderedSet<T, RandomState> {
    /// Creates an empty set using `RandomState`.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<T, S> PersistentOrderedSet<T, S> {
    /// Creates an empty set retaining the supplied hash builder.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            order: PersistentDeque::new(),
            stamps: PersistentHashMap::with_hasher(hasher),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.order.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.order.is_empty()
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.stamps.hasher()
    }

    /// Returns the first stored representative, or `None` when empty.
    #[must_use]
    pub fn first(&self) -> Option<&T> {
        self.order.front().map(|entry| &entry.item)
    }

    /// Returns the last stored representative, or `None` when empty.
    #[must_use]
    pub fn last(&self) -> Option<&T> {
        self.order.back().map(|entry| &entry.item)
    }

    /// Returns the representative at `index`.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.order.get(index).map(|entry| &entry.item)
    }

    /// Enumerates stored representatives in positional order without hashing.
    pub fn iter(&self) -> impl ExactSizeIterator<Item = &T> {
        self.order.iter().map(|entry| &entry.item)
    }

    /// Reports whether both independently owned roots are shared.
    ///
    /// This makes Rust's no-op identity contract observable without assigning facade identity to
    /// an ordinary value type.
    #[must_use]
    pub fn shares_roots_with(&self, other: &Self) -> bool {
        self.order.shares_storage_with(&other.order) && self.stamps.shares_root_with(&other.stamps)
    }

    /// Reports whether the positional sequence shares storage with `other`.
    #[must_use]
    pub fn shares_order_storage_with(&self, other: &Self) -> bool {
        self.order.shares_storage_with(&other.order)
    }

    /// Reports whether the CHAMP membership indexes have the same root.
    #[must_use]
    pub fn shares_membership_root_with(&self, other: &Self) -> bool {
        self.stamps.shares_root_with(&other.stamps)
    }
}

impl<T> PersistentOrderedSet<T, RandomState>
where
    T: Eq + Hash + Clone,
{
    /// Builds from one pass over `items`, retaining each class's first representative and order.
    #[must_use]
    pub fn from_items<I>(items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        Self::from_items_with_hasher(items, RandomState::new())
    }
}

impl<T, S> PersistentOrderedSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    /// Builds with a supplied hash policy while retaining first representatives.
    #[must_use]
    pub fn from_items_with_hasher<I>(items: I, hasher: S) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut seen = PersistentHashMap::with_hasher(hasher.clone());
        let mut distinct = Vec::new();
        for item in items {
            let (next, added) = seen.try_add(item.clone(), ());
            seen = next;
            if added {
                distinct.push(item);
            }
        }
        Self::build_from_distinct(distinct, hasher)
    }

    #[must_use]
    pub fn contains(&self, item: &T) -> bool {
        self.stamps.contains_key(item)
    }

    /// Returns the stored first representative for `equal_value`.
    #[must_use]
    pub fn get_stored(&self, equal_value: &T) -> Option<&T> {
        self.stamps
            .get_key_value(equal_value)
            .map(|(stored, _)| stored)
    }

    /// Returns the final positional index of an equality class.
    #[must_use]
    pub fn index_of(&self, equal_value: &T) -> Option<usize> {
        let (_, stamp) = self.stamps.get_key_value(equal_value)?;
        self.index_of_stamp(*stamp)
    }

    /// Appends an absent class. A duplicate is a root-sharing no-op.
    #[must_use]
    pub fn add(&self, item: T) -> Self {
        self.insert_core(self.len(), item)
    }

    /// Prepends an absent class. A duplicate is a root-sharing no-op.
    #[must_use]
    pub fn add_first(&self, item: T) -> Self {
        self.insert_core(0, item)
    }

    /// Inserts an absent class before `index`; returns `None` for an invalid insertion index.
    #[must_use]
    pub fn insert(&self, index: usize, item: T) -> Option<Self> {
        (index <= self.len()).then(|| self.insert_core(index, item))
    }

    /// Moves an existing class to index zero while retaining its stored representative.
    pub fn move_to_first(&self, equal_value: &T) -> Result<Self, OrderedSetMoveError> {
        self.move_existing(0, equal_value)
    }

    /// Moves an existing class to the final index while retaining its stored representative.
    pub fn move_to_last(&self, equal_value: &T) -> Result<Self, OrderedSetMoveError> {
        let Some(final_index) = self.len().checked_sub(1) else {
            return Err(OrderedSetMoveError::MissingValue);
        };
        self.move_existing(final_index, equal_value)
    }

    /// Moves an existing class to its final result index.
    pub fn move_to(
        &self,
        final_index: usize,
        equal_value: &T,
    ) -> Result<Self, OrderedSetMoveError> {
        if final_index >= self.len() {
            return Err(OrderedSetMoveError::IndexOutOfRange);
        }
        self.move_existing(final_index, equal_value)
    }

    /// Removes an equality class, returning a root-sharing clone on a miss.
    #[must_use]
    pub fn remove(&self, equal_value: &T) -> Self {
        self.try_remove(equal_value).set
    }

    /// Attempts removal and exposes whether a successor was produced.
    #[must_use = "removal returns a persistent successor that must be observed"]
    pub fn try_remove(&self, equal_value: &T) -> OrderedSetRemoveResult<T, S> {
        let Some((stamps, _, stamp)) = self.stamps.try_remove_entry(equal_value) else {
            return OrderedSetRemoveResult {
                removed: false,
                set: self.clone(),
            };
        };
        let index = self
            .index_of_stamp(stamp)
            .expect("membership stamp must occur in positional order");
        let order = self
            .order
            .remove_at(index)
            .expect("membership position must be removable");
        OrderedSetRemoveResult {
            removed: true,
            set: Self { order, stamps },
        }
    }

    /// Removes the representative at `index`.
    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        let entry = self.order.get(index)?;
        let (stamps, _, stamp) = self
            .stamps
            .try_remove_entry(&entry.item)
            .expect("ordered representative must occur in membership index");
        assert_eq!(stamp, entry.stamp, "ordered-set indexes disagree");
        Some(Self {
            order: self
                .order
                .remove_at(index)
                .expect("validated order position must be removable"),
            stamps,
        })
    }

    /// Removes the first representative, or returns `None` when empty.
    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        self.remove_at(0)
    }

    /// Removes the last representative, or returns `None` when empty.
    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        self.len()
            .checked_sub(1)
            .and_then(|index| self.remove_at(index))
    }

    /// Clears the collection while retaining its hash policy.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self {
            order: PersistentDeque::new(),
            stamps: self.stamps.clear(),
        }
    }

    /// Extracts a contiguous range, or returns `None` for an invalid range.
    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        if index > self.len() || count > self.len() - index {
            return None;
        }
        if count == self.len() {
            return Some(self.clone());
        }
        if count == 0 {
            return Some(Self::with_hasher(self.hasher().clone()));
        }

        let split = self
            .order
            .split_range(index, count)
            .expect("validated range must split");
        let removed = self.len() - count;
        let stamps = if count <= removed {
            Self::build_index(&split.range, self.hasher().clone())
        } else {
            let mut stamps = self.stamps.clone();
            for entry in split.before.iter().chain(split.after.iter()) {
                stamps = stamps.remove(&entry.item);
            }
            stamps
        };
        Some(Self {
            order: split.range,
            stamps,
        })
    }

    /// Retains the first `count` representatives.
    #[must_use]
    pub fn take(&self, count: usize) -> Option<Self> {
        self.get_range(0, count)
    }

    /// Drops the first `count` representatives.
    #[must_use]
    pub fn drop(&self, count: usize) -> Option<Self> {
        (count <= self.len())
            .then(|| self.get_range(count, self.len() - count))
            .flatten()
    }

    /// Reverses positional order and assigns fresh sparse stamps.
    #[must_use]
    pub fn reverse(&self) -> Self {
        if self.len() <= 1 {
            return self.clone();
        }
        let mut items = self.to_vec();
        items.reverse();
        Self::build_from_distinct(items, self.hasher().clone())
    }

    /// Performs a stable one-shot reorder. Later absent additions still append normally.
    ///
    /// Ties retain old order. If the resulting sequence is unchanged, both roots are reused. A
    /// comparator panic propagates while the receiver and all earlier snapshots remain unchanged.
    #[must_use]
    pub fn sort_by<F>(&self, mut compare: F) -> Self
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        if self.len() <= 1 {
            return self.clone();
        }
        let original = self.order.to_vec();
        let mut sorted = original.clone();
        sorted.sort_by(|left, right| {
            compare(&left.item, &right.item).then_with(|| left.stamp.cmp(&right.stamp))
        });
        if sorted
            .iter()
            .zip(&original)
            .all(|(left, right)| left.stamp == right.stamp)
        {
            return self.clone();
        }
        Self::build_from_distinct(
            sorted.into_iter().map(|entry| entry.item).collect(),
            self.hasher().clone(),
        )
    }

    /// Receiver-order union followed by first argument-only representatives.
    #[must_use]
    pub fn union<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        let mut result = None;
        for item in argument.items {
            if self.stamps.contains_key(&item) {
                continue;
            }
            result.get_or_insert_with(|| self.to_vec()).push(item);
        }
        result.map_or_else(
            || self.clone(),
            |items| Self::build_from_distinct(items, self.hasher().clone()),
        )
    }

    /// Same-type convenience overload; the argument's hasher is never consulted.
    #[must_use]
    pub fn union_set(&self, other: &Self) -> Self {
        self.union(other.iter().cloned())
    }

    /// Retains receiver representatives and order for classes present in `other`.
    #[must_use]
    pub fn intersect<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        let items: Vec<_> = self
            .iter()
            .filter(|item| argument.membership.contains_key(*item))
            .cloned()
            .collect();
        if items.len() == self.len() {
            self.clone()
        } else {
            Self::build_from_distinct(items, self.hasher().clone())
        }
    }

    #[must_use]
    pub fn intersect_set(&self, other: &Self) -> Self {
        self.intersect(other.iter().cloned())
    }

    /// Removes normalized argument classes while retaining receiver order.
    #[must_use]
    pub fn except<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        let items: Vec<_> = self
            .iter()
            .filter(|item| !argument.membership.contains_key(*item))
            .cloned()
            .collect();
        if items.len() == self.len() {
            self.clone()
        } else {
            Self::build_from_distinct(items, self.hasher().clone())
        }
    }

    #[must_use]
    pub fn except_set(&self, other: &Self) -> Self {
        self.except(other.iter().cloned())
    }

    /// Receiver-only order followed by first normalized argument-only representatives.
    #[must_use]
    pub fn symmetric_except<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        if argument.items.is_empty() {
            return self.clone();
        }
        let mut items: Vec<_> = self
            .iter()
            .filter(|item| !argument.membership.contains_key(*item))
            .cloned()
            .collect();
        items.extend(
            argument
                .items
                .into_iter()
                .filter(|item| !self.stamps.contains_key(item)),
        );
        Self::build_from_distinct(items, self.hasher().clone())
    }

    #[must_use]
    pub fn symmetric_except_set(&self, other: &Self) -> Self {
        self.symmetric_except(other.iter().cloned())
    }

    #[must_use]
    pub fn is_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        self.len() <= argument.items.len()
            && self
                .iter()
                .all(|item| argument.membership.contains_key(item))
    }

    #[must_use]
    pub fn is_proper_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        self.len() < argument.items.len()
            && self
                .iter()
                .all(|item| argument.membership.contains_key(item))
    }

    #[must_use]
    pub fn is_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        self.len() >= argument.items.len()
            && argument
                .items
                .iter()
                .all(|item| self.stamps.contains_key(item))
    }

    #[must_use]
    pub fn is_proper_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        self.len() > argument.items.len()
            && argument
                .items
                .iter()
                .all(|item| self.stamps.contains_key(item))
    }

    #[must_use]
    pub fn overlaps<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        argument
            .items
            .iter()
            .any(|item| self.stamps.contains_key(item))
    }

    #[must_use]
    pub fn set_equals<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let argument = self.normalize(other);
        self.len() == argument.items.len()
            && argument
                .items
                .iter()
                .all(|item| self.stamps.contains_key(item))
    }

    /// Recomputes the dual-index invariants.
    pub fn validate_structure(
        &self,
    ) -> Result<PersistentOrderedSetStatistics, PersistentOrderedSetInvariantError> {
        if self.order.len() != self.stamps.len() {
            return Err(PersistentOrderedSetInvariantError::CountMismatch);
        }
        let mut previous = None;
        for entry in self.order.iter() {
            if previous.is_some_and(|stamp| stamp >= entry.stamp) {
                return Err(PersistentOrderedSetInvariantError::StampsNotStrictlyAscending);
            }
            previous = Some(entry.stamp);
            let Some((stored, stamp)) = self.stamps.get_key_value(&entry.item) else {
                return Err(PersistentOrderedSetInvariantError::OrderedEntryMissingFromMembership);
            };
            if *stamp != entry.stamp {
                return Err(PersistentOrderedSetInvariantError::MembershipStampMismatch);
            }
            if stored != &entry.item {
                return Err(PersistentOrderedSetInvariantError::RepresentativeMismatch);
            }
        }
        for (stored, stamp) in self.stamps.iter() {
            let Some(position) = self.index_of_stamp(*stamp) else {
                return Err(PersistentOrderedSetInvariantError::MembershipEntryMissingFromOrder);
            };
            if &self
                .order
                .get(position)
                .expect("located stamp must identify an order entry")
                .item
                != stored
            {
                return Err(PersistentOrderedSetInvariantError::RepresentativeMismatch);
            }
        }
        Ok(PersistentOrderedSetStatistics { count: self.len() })
    }

    fn insert_core(&self, index: usize, item: T) -> Self {
        if let Some(stamp) = Self::try_pick_stamp(&self.order, index) {
            let (stamps, added) = self.stamps.try_add(item.clone(), stamp);
            if !added {
                return self.clone();
            }
            self.len()
                .checked_add(1)
                .expect("ordered-set capacity overflow");
            let entry = Entry { stamp, item };
            let order = if index == 0 {
                self.order.push_front(entry)
            } else if index == self.len() {
                self.order.push_back(entry)
            } else {
                self.order
                    .insert_at(index, entry)
                    .expect("validated insertion index must be accepted")
            };
            return Self { order, stamps };
        }

        let (_, added) = self.stamps.try_add(item.clone(), 0);
        if !added {
            return self.clone();
        }
        self.len()
            .checked_add(1)
            .expect("ordered-set capacity overflow");
        let mut items = self.to_vec();
        items.insert(index, item);
        Self::build_from_distinct(items, self.hasher().clone())
    }

    fn move_existing(
        &self,
        final_index: usize,
        equal_value: &T,
    ) -> Result<Self, OrderedSetMoveError> {
        let Some((_, stamp)) = self.stamps.get_key_value(equal_value) else {
            return Err(OrderedSetMoveError::MissingValue);
        };
        let old_index = self
            .index_of_stamp(*stamp)
            .expect("membership stamp must occur in positional order");
        if old_index == final_index {
            return Ok(self.clone());
        }

        let stored = self
            .order
            .get(old_index)
            .expect("membership stamp must identify an order entry")
            .item
            .clone();
        let trimmed = self
            .order
            .remove_at(old_index)
            .expect("stored representative must be removable");
        let Some(next_stamp) = Self::try_pick_stamp(&trimmed, final_index) else {
            let mut items: Vec<_> = trimmed.iter().map(|entry| entry.item.clone()).collect();
            items.insert(final_index, stored);
            return Ok(Self::build_from_distinct(items, self.hasher().clone()));
        };
        let entry = Entry {
            stamp: next_stamp,
            item: stored.clone(),
        };
        let order = if final_index == 0 {
            trimmed.push_front(entry)
        } else if final_index == trimmed.len() {
            trimmed.push_back(entry)
        } else {
            trimmed
                .insert_at(final_index, entry)
                .expect("validated movement destination must be accepted")
        };
        Ok(Self {
            order,
            stamps: self.stamps.insert(stored, next_stamp),
        })
    }

    fn normalize<I>(&self, other: I) -> Normalized<T, S>
    where
        I: IntoIterator<Item = T>,
    {
        let mut membership = PersistentHashMap::with_hasher(self.hasher().clone());
        let mut items = Vec::new();
        for item in other {
            let (next, added) = membership.try_add(item.clone(), ());
            membership = next;
            if added {
                items.push(item);
            }
        }
        Normalized { items, membership }
    }

    fn index_of_stamp(&self, stamp: i64) -> Option<usize> {
        let mut low = 0;
        let mut high = self.len();
        while low < high {
            let middle = low + (high - low) / 2;
            if self.order.get(middle)?.stamp < stamp {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        (low < self.len() && self.order.get(low)?.stamp == stamp).then_some(low)
    }

    fn try_pick_stamp(order: &PersistentDeque<Entry<T>>, index: usize) -> Option<i64> {
        if order.is_empty() {
            return Some(0);
        }
        if index == 0 {
            return order.front()?.stamp.checked_sub(STAMP_GAP);
        }
        if index == order.len() {
            return order.back()?.stamp.checked_add(STAMP_GAP);
        }
        let left = order.get(index - 1)?.stamp;
        let right = order.get(index)?.stamp;
        if left.checked_add(1).is_none_or(|next| next >= right) {
            return None;
        }
        Some((left as i128 + ((right as i128 - left as i128) >> 1)) as i64)
    }

    fn build_from_distinct(items: Vec<T>, hasher: S) -> Self {
        let mut entries = Vec::with_capacity(items.len());
        let mut stamps = BulkBuilder::with_hasher(hasher);
        for (index, item) in items.into_iter().enumerate() {
            let stamp = i64::try_from(index)
                .ok()
                .and_then(|position| position.checked_mul(STAMP_GAP))
                .expect("ordered set is too large to relabel with i64 stamps");
            stamps.set_item(item.clone(), stamp);
            entries.push(Entry { stamp, item });
        }
        Self {
            order: entries.into_iter().collect(),
            stamps: stamps.into_immutable(),
        }
    }

    fn build_index(order: &PersistentDeque<Entry<T>>, hasher: S) -> PersistentHashMap<T, i64, S> {
        let mut stamps = BulkBuilder::with_hasher(hasher);
        for entry in order.iter() {
            stamps.set_item(entry.item.clone(), entry.stamp);
        }
        stamps.into_immutable()
    }
}

impl<T, S> PersistentOrderedSet<T, S>
where
    T: Eq + Hash + Clone + Ord,
    S: BuildHasher + Clone,
{
    /// Performs a stable natural-order one-shot sort.
    #[must_use]
    pub fn sort(&self) -> Self {
        self.sort_by(T::cmp)
    }
}

impl<T, S> PersistentOrderedSet<T, S>
where
    T: Clone,
{
    /// Copies representatives in positional order.
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.iter().cloned().collect()
    }
}

impl<T, S> Default for PersistentOrderedSet<T, S>
where
    S: Default,
{
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<T, S> fmt::Debug for PersistentOrderedSet<T, S>
where
    T: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_set().entries(self.iter()).finish()
    }
}

impl<T, S> Index<usize> for PersistentOrderedSet<T, S> {
    type Output = T;

    fn index(&self, index: usize) -> &Self::Output {
        self.get(index)
            .expect("persistent ordered-set index out of range")
    }
}

impl<T> FromIterator<T> for PersistentOrderedSet<T, RandomState>
where
    T: Eq + Hash + Clone,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_items(iter)
    }
}

impl<T, S> IntoIterator for PersistentOrderedSet<T, S>
where
    T: Clone,
{
    type Item = T;
    type IntoIter = std::vec::IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.to_vec().into_iter()
    }
}

struct Normalized<T, S> {
    items: Vec<T>,
    membership: PersistentHashMap<T, (), S>,
}
