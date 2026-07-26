//! Persistent map with one automatically maintained secondary index.
//!
//! [`PersistentIndexedMap`] is a primary `K -> V` map plus a derived `I -> {K}` index, where each
//! value's index key is computed by a caller-supplied projection. The index is *nonunique*: several
//! primary keys may share an index key. Every mutation updates both structures together and
//! publishes only when both are complete, so the index can never drift out of agreement with the
//! primary map the way a hand-maintained side table can.
//!
//! Lookup by index key is therefore O(1) in the number of stored entries rather than a full scan,
//! at the cost of maintaining the second structure on every write.

use crate::{DuplicateKey, PersistentHashMap, PersistentHashMultimap, PersistentHashSet};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};
use std::sync::Arc;

#[derive(Clone, Debug, PartialEq, Eq)]
struct IndexedEntry<V, I> {
    value: V,
    index_key: I,
}

/// Successful coupled-index validation statistics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct IndexedMapStatistics {
    /// The number of primary entries.
    pub count: usize,
    /// The number of distinct index keys.
    pub index_key_count: usize,
}

/// A disagreement between an indexed map's primary and secondary indexes.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IndexedMapInvariantError {
    /// The secondary multimap failed its own validation.
    SecondaryIndex,
    /// The primary map and the secondary index disagree on how many entries are filed.
    CountMismatch,
    /// A primary entry is missing from the index group its recorded index key names.
    MissingSecondaryMembership,
    /// The index refers to a primary key that the primary map does not hold.
    MissingPrimaryRow,
    /// An entry is filed under an index key other than the one recorded for it.
    IndexKeyMismatch,
}

impl fmt::Display for IndexedMapInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::SecondaryIndex => "the indexed map's secondary multimap is invalid",
            Self::CountMismatch => "the indexed map's primary and secondary counts disagree",
            Self::MissingSecondaryMembership => "a primary row is absent from its secondary group",
            Self::MissingPrimaryRow => "a secondary membership has no primary row",
            Self::IndexKeyMismatch => "a secondary membership disagrees with its primary row",
        })
    }
}

impl std::error::Error for IndexedMapInvariantError {}

/// Immutable primary map with one automatically maintained nonunique secondary index.
pub struct PersistentIndexedMap<K, V, I, F, SK = RandomState, SI = RandomState> {
    primary: PersistentHashMap<K, IndexedEntry<V, I>, SK>,
    index: PersistentHashMultimap<I, K, SI, SK>,
    selector: Arc<F>,
}

impl<K, V, I, F, SK, SI> Clone for PersistentIndexedMap<K, V, I, F, SK, SI>
where
    SK: Clone,
    SI: Clone,
{
    fn clone(&self) -> Self {
        Self {
            primary: self.primary.clone(),
            index: self.index.clone(),
            selector: Arc::clone(&self.selector),
        }
    }
}

impl<K, V, I, F> PersistentIndexedMap<K, V, I, F, RandomState, RandomState> {
    /// Creates an empty map whose secondary index is keyed by `selector`, using fresh default hash
    /// policies.
    #[must_use]
    pub fn new(selector: F) -> Self {
        Self::with_hashers(selector, RandomState::new(), RandomState::new())
    }
}

impl<K, V, I, F, SK, SI> PersistentIndexedMap<K, V, I, F, SK, SI> {
    /// Creates an empty map with an independently chosen hash policy for primary keys and for index
    /// keys.
    #[must_use]
    pub fn with_hashers(selector: F, key_hasher: SK, index_hasher: SI) -> Self
    where
        SK: Clone,
    {
        Self {
            primary: PersistentHashMap::with_hasher(key_hasher.clone()),
            index: PersistentHashMultimap::with_hashers(index_hasher, key_hasher),
            selector: Arc::new(selector),
        }
    }

    /// Returns the number of primary entries. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.primary.len()
    }

    /// Returns `true` when the map holds no entries.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.primary.is_empty()
    }

    /// Returns the number of distinct index keys. O(1). Never exceeds [`Self::len`], since the index
    /// is nonunique.
    #[must_use]
    pub fn index_key_count(&self) -> usize {
        self.index.key_count()
    }

    /// Borrows the hash policy defining primary-key equivalence.
    #[must_use]
    pub fn key_hasher(&self) -> &SK {
        self.primary.hasher()
    }

    /// Borrows the hash policy defining index-key equivalence.
    #[must_use]
    pub fn index_hasher(&self) -> &SI {
        self.index.key_hasher()
    }

    /// Borrows the projection that derives an index key from a value.
    ///
    /// The map calls it only when a value is added or actually changes, never on lookup, so the index
    /// cannot drift from the primary map.
    #[must_use]
    pub fn selector(&self) -> &F {
        &self.selector
    }

    /// Iterates the primary keys, in unspecified order.
    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.primary.keys()
    }

    /// Iterates the values, in unspecified order.
    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.primary.values().map(|entry| &entry.value)
    }

    /// Iterates the primary entries, in unspecified order.
    pub fn iter(&self) -> impl ExactSizeIterator<Item = (&K, &V)> {
        self.primary.iter().map(|(key, entry)| (key, &entry.value))
    }

    /// Iterates the distinct index keys, in unspecified order.
    pub fn index_keys(&self) -> impl Iterator<Item = &I> {
        self.index.keys()
    }

    /// Iterates each index key together with the set of primary keys filed under it. Every yielded
    /// set is nonempty.
    pub fn index_groups(&self) -> impl Iterator<Item = (&I, &PersistentHashSet<K, SK>)> {
        self.index.groups()
    }

    /// Reports whether two maps share both the primary and index representations, so neither can
    /// observe an edit made to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_roots_with(&self, other: &Self) -> bool {
        self.primary.shares_root_with(&other.primary)
            && self.index.shares_groups_root_with(&other.index)
    }
}

impl<K, V, I, F, SK, SI> PersistentIndexedMap<K, V, I, F, SK, SI>
where
    K: Eq + Hash,
    I: Eq + Hash,
    SK: BuildHasher,
    SI: BuildHasher,
{
    /// Reports whether `key` is present. O(1) expected.
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.primary.contains_key(key)
    }

    /// Borrows the value stored for `key`, or `None` when absent. O(1) expected.
    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.primary.get(key).map(|entry| &entry.value)
    }

    /// Borrows the stored key representative equivalent to `equal_key`, or `None` when absent.
    #[must_use]
    pub fn get_key(&self, equal_key: &K) -> Option<&K> {
        self.primary.get_key_value(equal_key).map(|(key, _)| key)
    }

    /// Borrows the index key currently filed for `key`, or `None` when the key is absent.
    ///
    /// Returns the value recorded at write time rather than re-running the selector.
    #[must_use]
    pub fn get_index_key(&self, key: &K) -> Option<&I> {
        self.primary.get(key).map(|entry| &entry.index_key)
    }

    /// Reports whether at least one entry is filed under `index_key`. O(1) expected.
    #[must_use]
    pub fn contains_index_key(&self, index_key: &I) -> bool {
        self.index.contains_key(index_key)
    }

    /// Returns how many entries are filed under `index_key`, or zero when it is absent.
    #[must_use]
    pub fn count_by_index(&self, index_key: &I) -> usize {
        self.index.count_values(index_key)
    }

    /// Borrows the primary keys filed under `index_key`, or `None` when it is absent.
    ///
    /// This is the point of the secondary index: the lookup is O(1) expected rather than a scan over
    /// every entry. Never yields an empty set.
    #[must_use]
    pub fn keys_by_index(&self, index_key: &I) -> Option<&PersistentHashSet<K, SK>> {
        self.index.get_values(index_key)
    }
}

impl<K, V, I, F, SK, SI> PersistentIndexedMap<K, V, I, F, SK, SI>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    I: Eq + Hash + Clone,
    F: Fn(&K, &V) -> I,
    SK: BuildHasher + Clone,
    SI: BuildHasher + Clone,
{
    /// Builds a map from entries under the supplied hash policies, populating the secondary index as
    /// it goes.
    #[must_use]
    pub fn from_entries_with_hashers<E>(
        entries: E,
        selector: F,
        key_hasher: SK,
        index_hasher: SI,
    ) -> Self
    where
        E: IntoIterator<Item = (K, V)>,
    {
        entries.into_iter().fold(
            Self::with_hashers(selector, key_hasher, index_hasher),
            |map, (key, value)| map.set_item(key, value),
        )
    }

    /// Adds a new entry and files it in the index, failing with [`DuplicateKey`] when `key` is already
    /// present.
    ///
    /// The selector is not invoked on a duplicate, and the receiver is left untouched.
    pub fn add(&self, key: K, value: V) -> Result<Self, DuplicateKey> {
        if self.primary.contains_key(&key) {
            return Err(DuplicateKey);
        }
        let selected = (self.selector)(&key, &value);
        let index = self.index.insert(selected.clone(), key.clone());
        let actual_index = index
            .get_key(&selected)
            .expect("inserted secondary key must be present")
            .clone();
        Ok(Self {
            primary: self.primary.add(
                key,
                IndexedEntry {
                    value,
                    index_key: actual_index,
                },
            )?,
            index,
            selector: Arc::clone(&self.selector),
        })
    }

    /// Adds a new entry, reporting whether it was added instead of returning a `Result`.
    #[must_use]
    pub fn try_add(&self, key: K, value: V) -> (Self, bool) {
        match self.add(key, value) {
            Ok(result) => (result, true),
            Err(DuplicateKey) => (self.clone(), false),
        }
    }

    /// Adds `key`, or replaces its value when present, moving it between index groups if its index
    /// key changed.
    ///
    /// Writing a value equal to the stored one is a no-op: the selector is not invoked and both
    /// representations are shared.
    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let Some((stored_key, current)) = self.primary.get_key_value(&key) else {
            return self.add(key, value).expect("absent primary key must add");
        };
        if current.value == value {
            return self.clone();
        }
        let stored_key = stored_key.clone();
        let selected = (self.selector)(&stored_key, &value);
        let (index, actual_index) = if selected == current.index_key {
            (self.index.clone(), current.index_key.clone())
        } else {
            let index = self
                .index
                .remove(&current.index_key, &stored_key)
                .insert(selected.clone(), stored_key.clone());
            let actual = index
                .get_key(&selected)
                .expect("inserted secondary key must be present")
                .clone();
            (index, actual)
        };
        Self {
            primary: self.primary.insert(
                stored_key,
                IndexedEntry {
                    value,
                    index_key: actual_index,
                },
            ),
            index,
            selector: Arc::clone(&self.selector),
        }
    }

    /// Removes `key` from the primary map and from its index group, dropping the group when it
    /// becomes empty. The selector is not invoked. Removing an absent key is a no-op.
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let Some((stored_key, current)) = self.primary.get_key_value(key) else {
            return self.clone();
        };
        let stored_key = stored_key.clone();
        let index_key = current.index_key.clone();
        Self {
            primary: self.primary.remove(&stored_key),
            index: self.index.remove(&index_key, &stored_key),
            selector: Arc::clone(&self.selector),
        }
    }

    /// Returns an empty map retaining both hash policies and the selector. Clearing an empty map is a
    /// no-op that shares the receiver's representation.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self {
                primary: self.primary.clear(),
                index: self.index.clear(),
                selector: Arc::clone(&self.selector),
            }
        }
    }

    /// Cross-checks the secondary index against the primary map: every entry filed exactly once under
    /// the index key recorded for it, and no empty groups.
    ///
    /// A defensive audit over the whole map; ordinary operations maintain these invariants.
    pub fn validate(&self) -> Result<IndexedMapStatistics, IndexedMapInvariantError> {
        self.index
            .validate()
            .map_err(|_| IndexedMapInvariantError::SecondaryIndex)?;
        if self.primary.len() != self.index.pair_count() {
            return Err(IndexedMapInvariantError::CountMismatch);
        }
        for (key, entry) in self.primary.iter() {
            if !self.index.contains(&entry.index_key, key) {
                return Err(IndexedMapInvariantError::MissingSecondaryMembership);
            }
        }
        for (index_key, key) in self.index.iter() {
            let Some(entry) = self.primary.get(key) else {
                return Err(IndexedMapInvariantError::MissingPrimaryRow);
            };
            if &entry.index_key != index_key {
                return Err(IndexedMapInvariantError::IndexKeyMismatch);
            }
        }
        Ok(IndexedMapStatistics {
            count: self.len(),
            index_key_count: self.index_key_count(),
        })
    }
}

impl<K, V, I, F> PersistentIndexedMap<K, V, I, F, RandomState, RandomState>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    I: Eq + Hash + Clone,
    F: Fn(&K, &V) -> I,
{
    /// Builds a map from entries under fresh default hash policies.
    #[must_use]
    pub fn from_entries<E>(entries: E, selector: F) -> Self
    where
        E: IntoIterator<Item = (K, V)>,
    {
        Self::from_entries_with_hashers(entries, selector, RandomState::new(), RandomState::new())
    }
}

impl<K, V, I, F, SK, SI> fmt::Debug for PersistentIndexedMap<K, V, I, F, SK, SI>
where
    K: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_map().entries(self.iter()).finish()
    }
}
