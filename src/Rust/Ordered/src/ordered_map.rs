//! Persistent insertion-ordered map with explicit repositioning.
//!
//! [`PersistentOrderedMap`] keeps a CHAMP map from key to order stamp alongside a
//! [`PersistentDeque`] of stamped entries, so lookup stays O(1) while iteration follows insertion
//! order. Re-adding an existing key keeps its original position and its first stored key
//! representative while replacing the payload, which separates a key's identity from its value.
//!
//! Order is carried by monotone `i64` stamps spaced [`STAMP_GAP`] apart rather than by array
//! indices. The gap is what makes repositioning cheap: a move can usually pick a stamp strictly
//! between its new neighbors, leaving every other entry's stamp untouched, and only falls back to
//! restamping when a gap is exhausted. Movement is explicit and fallible — see
//! [`OrderedMapMoveError`] — so a missing key or an out-of-range position is reported rather than
//! guessed at.

use std::cmp::Ordering;
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

use durable7_fingertree::PersistentDeque;
use durable7_hamt::{BulkBuilder, DuplicateKey, PersistentHashMap};

/// The spacing between adjacent order stamps.
///
/// Leaving room between stamps lets a repositioning operation choose a new stamp between two
/// neighbors without disturbing any other entry.
const STAMP_GAP: i64 = 1_i64 << 20;

#[derive(Clone)]
struct MapEntry<K, V> {
    stamp: i64,
    key: K,
    value: V,
}

/// Failure reported by explicit ordered-map movement.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OrderedMapMoveError {
    /// The requested final position does not identify an entry in the resulting map.
    IndexOutOfRange,
    /// No entry with the requested key is present.
    MissingKey,
}

impl fmt::Display for OrderedMapMoveError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::IndexOutOfRange => "the final index is out of range",
            Self::MissingKey => "the key is absent from the ordered map",
        })
    }
}

impl std::error::Error for OrderedMapMoveError {}

/// Result of a keyed ordered-map removal.
#[must_use]
pub struct OrderedMapRemoveResult<K, V, S = RandomState> {
    /// The stored key representative and value that were removed, or `None` when the key was
    /// absent.
    pub removed: Option<(K, V)>,
    /// The resulting map, which is the unchanged receiver when nothing was removed.
    pub map: PersistentOrderedMap<K, V, S>,
}

/// Immutable insertion-ordered map with explicit positional reordering.
///
/// Rust `Eq`/`Hash` define key classes and `S` retains their hash-building policy. The first key
/// representative remains authoritative until removal. Values occur only in the positional tree;
/// the CHAMP index stores key-to-stamp navigation, avoiding a second copy of arbitrary payloads.
pub struct PersistentOrderedMap<K, V, S = RandomState> {
    order: PersistentDeque<MapEntry<K, V>>,
    stamps: PersistentHashMap<K, i64, S>,
}

impl<K, V, S> Clone for PersistentOrderedMap<K, V, S>
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

impl<K, V> PersistentOrderedMap<K, V, RandomState> {
    /// Creates an empty map using a fresh default hash policy.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S> PersistentOrderedMap<K, V, S> {
    /// Creates an empty map whose key equivalence is defined by `hasher`.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            order: PersistentDeque::new(),
            stamps: PersistentHashMap::with_hasher(hasher),
        }
    }

    /// Returns the number of entries. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.order.len()
    }

    /// Returns `true` when the map holds no entries.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.order.is_empty()
    }

    /// Borrows the retained hash policy that defines key equivalence.
    #[must_use]
    pub fn hasher(&self) -> &S {
        self.stamps.hasher()
    }

    /// Iterates the entries in insertion order, as modified by any explicit moves or sorting.
    pub fn iter(&self) -> impl ExactSizeIterator<Item = (&K, &V)> {
        self.order.iter().map(|entry| (&entry.key, &entry.value))
    }

    /// Iterates the keys in the map's order.
    pub fn keys(&self) -> impl ExactSizeIterator<Item = &K> {
        self.order.iter().map(|entry| &entry.key)
    }

    /// Iterates the values in the map's order.
    pub fn values(&self) -> impl ExactSizeIterator<Item = &V> {
        self.order.iter().map(|entry| &entry.value)
    }

    /// Borrows the entry at ordinal `index`, or `None` when out of range.
    ///
    /// This is positional access, not lookup by key; use [`Self::get`] for the latter.
    #[must_use]
    pub fn get_at(&self, index: usize) -> Option<(&K, &V)> {
        self.order
            .get(index)
            .map(|entry| (&entry.key, &entry.value))
    }

    /// Borrows the first entry in order, or `None` when empty.
    #[must_use]
    pub fn first(&self) -> Option<(&K, &V)> {
        self.order.front().map(|entry| (&entry.key, &entry.value))
    }

    /// Borrows the last entry in order, or `None` when empty.
    #[must_use]
    pub fn last(&self) -> Option<(&K, &V)> {
        self.order.back().map(|entry| (&entry.key, &entry.value))
    }

    /// Reports whether two maps share *both* the order sequence and the membership index, so neither
    /// can observe an edit made to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_roots_with(&self, other: &Self) -> bool {
        self.order.shares_storage_with(&other.order) && self.stamps.shares_root_with(&other.stamps)
    }

    /// Reports whether two maps share the order sequence alone.
    ///
    /// A value-only replacement can leave the order sequence shared while the membership index
    /// changes, so this is finer-grained than [`Self::shares_roots_with`].
    #[must_use]
    pub fn shares_order_storage_with(&self, other: &Self) -> bool {
        self.order.shares_storage_with(&other.order)
    }

    /// Reports whether two maps share the membership index alone, the counterpart of
    /// [`Self::shares_order_storage_with`].
    #[must_use]
    pub fn shares_membership_root_with(&self, other: &Self) -> bool {
        self.stamps.shares_root_with(&other.stamps)
    }
}

impl<K, V, S> PersistentOrderedMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    /// Reports whether `key` is present. O(1) expected, through the hash index.
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.stamps.contains_key(key)
    }

    /// Returns `key`'s ordinal position, or `None` when absent.
    #[must_use]
    pub fn index_of(&self, key: &K) -> Option<usize> {
        let stamp = *self.stamps.get(key)?;
        self.index_of_stamp(stamp)
    }

    /// Borrows the value stored for `key`, or `None` when absent. O(1) expected.
    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.index_of(key)
            .and_then(|index| self.order.get(index))
            .map(|entry| &entry.value)
    }

    /// Borrows the stored key representative equivalent to `equal_key`, or `None` when absent.
    ///
    /// The stored representative is the first one inserted for its class and survives value
    /// replacement.
    #[must_use]
    pub fn get_key(&self, equal_key: &K) -> Option<&K> {
        self.stamps
            .get_key_value(equal_key)
            .map(|(stored, _)| stored)
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
}

impl<K, V> PersistentOrderedMap<K, V, RandomState>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
{
    /// Builds a map from entries under a fresh default hash policy.
    ///
    /// Each key keeps the position and representative of its first occurrence, while a repeated key's
    /// later value overwrites the earlier one.
    #[must_use]
    pub fn from_entries<I>(entries: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        Self::from_entries_with_hasher(entries, RandomState::new())
    }
}

impl<K, V, S> PersistentOrderedMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    /// Builds in input order. The first key representative and last distinct value win.
    #[must_use]
    pub fn from_entries_with_hasher<I>(entries: I, hasher: S) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        let mut result = Self::with_hasher(hasher);
        for (key, value) in entries {
            result = result.set_item(key, value);
        }
        result
    }

    /// Adds a new key at the end.
    pub fn add(&self, key: K, value: V) -> Result<Self, DuplicateKey> {
        self.insert_core(self.len(), key, value)
    }

    /// Adds a new key at the beginning.
    pub fn add_first(&self, key: K, value: V) -> Result<Self, DuplicateKey> {
        self.insert_core(0, key, value)
    }

    /// Inserts before `index`; an invalid position returns `Ok(None)`.
    pub fn insert(&self, index: usize, key: K, value: V) -> Result<Option<Self>, DuplicateKey> {
        if index > self.len() {
            return Ok(None);
        }
        self.insert_core(index, key, value).map(Some)
    }

    /// Nonthrowing append. Duplicate keys return a root-sharing clone and `false`.
    #[must_use]
    pub fn try_add(&self, key: K, value: V) -> (Self, bool) {
        match self.add(key, value) {
            Ok(map) => (map, true),
            Err(DuplicateKey) => (self.clone(), false),
        }
    }

    /// Adds or replaces a value without changing the stored key or position.
    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let Some((_, stamp)) = self.stamps.get_key_value(&key) else {
            return self
                .insert_core(self.len(), key, value)
                .expect("absent key must append");
        };
        let index = self
            .index_of_stamp(*stamp)
            .expect("membership stamp must occur in positional order");
        let stored = self.order.get(index).expect("located entry must exist");
        if stored.value == value {
            return self.clone();
        }
        let replacement = MapEntry {
            stamp: stored.stamp,
            key: stored.key.clone(),
            value,
        };
        Self {
            order: self
                .order
                .set_item(index, replacement)
                .expect("located entry must be replaceable"),
            stamps: self.stamps.clone(),
        }
    }

    /// Moves `key`'s entry to the front, keeping its key representative and value.
    ///
    /// Fails with [`OrderedMapMoveError::MissingKey`] when `key` is absent.
    pub fn move_to_first(&self, key: &K) -> Result<Self, OrderedMapMoveError> {
        self.move_existing(0, key)
    }

    /// Moves `key`'s entry to the back, keeping its key representative and value.
    ///
    /// Fails with [`OrderedMapMoveError::MissingKey`] when `key` is absent.
    pub fn move_to_last(&self, key: &K) -> Result<Self, OrderedMapMoveError> {
        let Some(index) = self.len().checked_sub(1) else {
            return Err(OrderedMapMoveError::MissingKey);
        };
        self.move_existing(index, key)
    }

    /// Moves `key`'s entry so that it ends up at ordinal `final_index`.
    ///
    /// `final_index` names the position in the *resulting* map, which is what makes a move to the
    /// end well defined. Fails when `key` is absent or `final_index` is out of range.
    pub fn move_to(&self, final_index: usize, key: &K) -> Result<Self, OrderedMapMoveError> {
        if final_index >= self.len() {
            return Err(OrderedMapMoveError::IndexOutOfRange);
        }
        self.move_existing(final_index, key)
    }

    /// Returns a map without `key`, closing the gap in the order. Removing an absent key is a no-op.
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        self.try_remove(key).map
    }

    /// Removes `key` and reports the stored key representative and value that went with it.
    ///
    /// The result distinguishes "nothing was there" from a removed entry whose value is itself
    /// `None`.
    #[must_use]
    pub fn try_remove(&self, key: &K) -> OrderedMapRemoveResult<K, V, S> {
        let Some((stamps, actual_key, stamp)) = self.stamps.try_remove_entry(key) else {
            return OrderedMapRemoveResult {
                removed: None,
                map: self.clone(),
            };
        };
        let index = self
            .index_of_stamp(stamp)
            .expect("membership stamp must occur in positional order");
        let entry = self.order.get(index).expect("located entry must exist");
        let value = entry.value.clone();
        OrderedMapRemoveResult {
            removed: Some((actual_key, value)),
            map: Self {
                order: self
                    .order
                    .remove_at(index)
                    .expect("located entry must be removable"),
                stamps,
            },
        }
    }

    /// Removes an entry by position and returns the stored pair.
    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<(Self, (K, V))> {
        let entry = self.order.get(index)?.clone();
        let (stamps, actual_key, stamp) = self.stamps.try_remove_entry(&entry.key)?;
        assert_eq!(stamp, entry.stamp, "ordered-map indexes disagree");
        Some((
            Self {
                order: self.order.remove_at(index)?,
                stamps,
            },
            (actual_key, entry.value),
        ))
    }

    /// Returns an empty map retaining the hash policy. Clearing an empty map is a no-op that shares
    /// the receiver's representation.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self {
                order: PersistentDeque::new(),
                stamps: self.stamps.clear(),
            }
        }
    }

    /// Returns the `count` entries starting at ordinal `index` as a new map preserving their relative
    /// order, or `None` when the range falls outside the map.
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
        let split = self.order.split_range(index, count)?;
        let removed = self.len() - count;
        let stamps = if count <= removed {
            Self::build_index(&split.range, self.hasher().clone())
        } else {
            let mut stamps = self.stamps.clone();
            for entry in split.before.iter().chain(split.after.iter()) {
                stamps = stamps.remove(&entry.key);
            }
            stamps
        };
        Some(Self {
            order: split.range,
            stamps,
        })
    }

    /// Returns a map with the entry order reversed. Keys, key representatives, and values are
    /// unchanged.
    #[must_use]
    pub fn reverse(&self) -> Self {
        if self.len() <= 1 {
            return self.clone();
        }
        let mut entries = self.to_vec();
        entries.reverse();
        Self::build_from_distinct(entries, self.hasher().clone())
    }

    /// Stable one-shot positional sort. Ties retain the prior explicit order.
    #[must_use]
    pub fn sort_by<F>(&self, mut compare: F) -> Self
    where
        F: FnMut((&K, &V), (&K, &V)) -> Ordering,
    {
        if self.len() <= 1 {
            return self.clone();
        }
        let original = self.order.to_vec();
        let mut sorted = original.clone();
        sorted.sort_by(|left, right| {
            compare((&left.key, &left.value), (&right.key, &right.value))
                .then_with(|| left.stamp.cmp(&right.stamp))
        });
        if sorted
            .iter()
            .zip(&original)
            .all(|(left, right)| left.stamp == right.stamp)
        {
            return self.clone();
        }
        Self::build_from_distinct(
            sorted
                .into_iter()
                .map(|entry| (entry.key, entry.value))
                .collect(),
            self.hasher().clone(),
        )
    }

    /// Cross-checks the order sequence against the membership index: equal lengths, every entry
    /// indexed exactly once, and strictly increasing order stamps.
    ///
    /// A defensive audit over the whole map; ordinary operations maintain these invariants.
    pub fn validate_structure(
        &self,
    ) -> Result<PersistentOrderedMapStatistics, PersistentOrderedMapInvariantError> {
        if self.order.len() != self.stamps.len() {
            return Err(PersistentOrderedMapInvariantError::CountMismatch);
        }
        let mut previous = None;
        for entry in self.order.iter() {
            if previous.is_some_and(|stamp| stamp >= entry.stamp) {
                return Err(PersistentOrderedMapInvariantError::StampsNotStrictlyAscending);
            }
            previous = Some(entry.stamp);
            let Some((stored, stamp)) = self.stamps.get_key_value(&entry.key) else {
                return Err(PersistentOrderedMapInvariantError::OrderedKeyMissingFromIndex);
            };
            if *stamp != entry.stamp {
                return Err(PersistentOrderedMapInvariantError::StampMismatch);
            }
            if stored != &entry.key {
                return Err(PersistentOrderedMapInvariantError::RepresentativeMismatch);
            }
        }
        for (key, stamp) in self.stamps.iter() {
            let Some(index) = self.index_of_stamp(*stamp) else {
                return Err(PersistentOrderedMapInvariantError::IndexKeyMissingFromOrder);
            };
            if &self.order.get(index).expect("located entry must exist").key != key {
                return Err(PersistentOrderedMapInvariantError::RepresentativeMismatch);
            }
        }
        Ok(PersistentOrderedMapStatistics { count: self.len() })
    }

    /// Copies the entries into a vector in the map's order. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<(K, V)> {
        self.iter()
            .map(|(key, value)| (key.clone(), value.clone()))
            .collect()
    }

    fn insert_core(&self, index: usize, key: K, value: V) -> Result<Self, DuplicateKey> {
        if self.stamps.contains_key(&key) {
            return Err(DuplicateKey);
        }
        self.len()
            .checked_add(1)
            .expect("ordered-map capacity overflow");
        if let Some(stamp) = Self::try_pick_stamp(&self.order, index) {
            let stamps = self
                .stamps
                .add(key.clone(), stamp)
                .expect("prechecked key must be absent");
            let entry = MapEntry { stamp, key, value };
            let order = if index == 0 {
                self.order.push_front(entry)
            } else if index == self.len() {
                self.order.push_back(entry)
            } else {
                self.order
                    .insert_at(index, entry)
                    .expect("validated insertion index must be accepted")
            };
            return Ok(Self { order, stamps });
        }

        // A branch-local sparse-label gap can be exhausted. Rebuild only on that deterministic
        // fallback; ordinary additions and movements continue to path-copy both existing indexes.
        let mut entries = self.to_vec();
        entries.insert(index, (key, value));
        Ok(Self::build_from_distinct(entries, self.hasher().clone()))
    }

    fn move_existing(&self, final_index: usize, key: &K) -> Result<Self, OrderedMapMoveError> {
        let Some(stamp) = self.stamps.get(key) else {
            return Err(OrderedMapMoveError::MissingKey);
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
            .expect("located entry must exist")
            .clone();
        let trimmed = self
            .order
            .remove_at(old_index)
            .expect("entry must be removable");
        let Some(next_stamp) = Self::try_pick_stamp(&trimmed, final_index) else {
            let mut entries = trimmed
                .iter()
                .map(|entry| (entry.key.clone(), entry.value.clone()))
                .collect::<Vec<_>>();
            entries.insert(final_index, (stored.key, stored.value));
            return Ok(Self::build_from_distinct(entries, self.hasher().clone()));
        };
        let entry = MapEntry {
            stamp: next_stamp,
            key: stored.key.clone(),
            value: stored.value,
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
            stamps: self.stamps.insert(stored.key, next_stamp),
        })
    }

    fn try_pick_stamp(order: &PersistentDeque<MapEntry<K, V>>, index: usize) -> Option<i64> {
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

    fn build_from_distinct(entries: Vec<(K, V)>, hasher: S) -> Self {
        let mut order = Vec::with_capacity(entries.len());
        let mut stamps = BulkBuilder::with_hasher(hasher);
        for (index, (key, value)) in entries.into_iter().enumerate() {
            let stamp = i64::try_from(index)
                .ok()
                .and_then(|position| position.checked_mul(STAMP_GAP))
                .expect("ordered map is too large to relabel with i64 stamps");
            stamps.set_item(key.clone(), stamp);
            order.push(MapEntry { stamp, key, value });
        }
        Self {
            order: order.into_iter().collect(),
            stamps: stamps.into_immutable(),
        }
    }

    fn build_index(
        order: &PersistentDeque<MapEntry<K, V>>,
        hasher: S,
    ) -> PersistentHashMap<K, i64, S> {
        let mut stamps = BulkBuilder::with_hasher(hasher);
        for entry in order.iter() {
            stamps.set_item(entry.key.clone(), entry.stamp);
        }
        stamps.into_immutable()
    }
}

impl<K, V, S> Default for PersistentOrderedMap<K, V, S>
where
    S: Default,
{
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V, S> fmt::Debug for PersistentOrderedMap<K, V, S>
where
    K: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_map().entries(self.iter()).finish()
    }
}

/// Statistics recomputed by a successful [`PersistentOrderedMap::validate_structure`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PersistentOrderedMapStatistics {
    /// The number of entries, cross-checked between the order sequence and the membership index.
    pub count: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
/// A disagreement between the ordered map's order sequence and its membership index.
pub enum PersistentOrderedMapInvariantError {
    /// The two indexes hold different numbers of entries.
    CountMismatch,
    /// Order stamps are not strictly ascending, so positions could not be trusted.
    StampsNotStrictlyAscending,
    /// An entry in the order sequence has no membership-index entry.
    OrderedKeyMissingFromIndex,
    /// A key's indexed stamp disagrees with the stamp stored in the order sequence.
    StampMismatch,
    /// A key in the membership index does not appear in the order sequence.
    IndexKeyMissingFromOrder,
    /// The stored key representative differs between the two indexes.
    RepresentativeMismatch,
}

impl fmt::Display for PersistentOrderedMapInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::CountMismatch => "the order and key indexes have different counts",
            Self::StampsNotStrictlyAscending => {
                "order-maintenance stamps are not strictly ascending"
            }
            Self::OrderedKeyMissingFromIndex => "an ordered key is absent from the CHAMP index",
            Self::StampMismatch => "the order and CHAMP index stamps disagree",
            Self::IndexKeyMissingFromOrder => "an indexed key is absent from positional order",
            Self::RepresentativeMismatch => "the two indexes retain different key representatives",
        })
    }
}

impl std::error::Error for PersistentOrderedMapInvariantError {}
