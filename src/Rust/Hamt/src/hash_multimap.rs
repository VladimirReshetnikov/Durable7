//! Persistent set-valued hash multimap.
//!
//! [`PersistentHashMultimap`] maps each key to a nonempty [`PersistentHashSet`] of values. Storing
//! sets rather than lists makes adding an already-present pair a no-op, and the "nonempty"
//! invariant is what keeps the structure canonical: removing a key's last value removes the key
//! itself, so a key is present exactly when it has at least one value and there is no way to
//! observe an empty group.
//!
//! Distinct-key and pair cardinalities are tracked separately, because for a multimap they are
//! genuinely different questions and neither can be derived from the other in O(1).
//! [`PersistentHashMultimap::validate`] rechecks those counts and the nonempty invariant, returning
//! [`HashMultimapStatistics`] or the first [`HashMultimapInvariantError`].

use crate::{Iter, PersistentHashMap, PersistentHashSet, SetIter};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};
use std::iter::FusedIterator;

/// Immutable set-valued hash multimap composed from persistent CHAMP maps and sets.
pub struct PersistentHashMultimap<K, V, SK = RandomState, SV = RandomState> {
    groups: PersistentHashMap<K, PersistentHashSet<V, SV>, SK>,
    value_hasher: SV,
    pair_count: usize,
}

impl<K, V, SK, SV> Clone for PersistentHashMultimap<K, V, SK, SV>
where
    SK: Clone,
    SV: Clone,
{
    fn clone(&self) -> Self {
        Self {
            groups: self.groups.clone(),
            value_hasher: self.value_hasher.clone(),
            pair_count: self.pair_count,
        }
    }
}

impl<K, V> PersistentHashMultimap<K, V, RandomState, RandomState> {
    /// Creates an empty multimap using fresh default hash policies for keys and values.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hashers(RandomState::new(), RandomState::new())
    }
}

impl<K, V, SK, SV> PersistentHashMultimap<K, V, SK, SV> {
    /// Creates an empty multimap with independent hash-building policies.
    #[must_use]
    pub fn with_hashers(key_hasher: SK, value_hasher: SV) -> Self {
        Self {
            groups: PersistentHashMap::with_hasher(key_hasher),
            value_hasher,
            pair_count: 0,
        }
    }

    /// Returns the number of distinct keys. O(1).
    ///
    /// Because every group is nonempty, this is also the number of keys with at least one value.
    #[must_use]
    pub fn key_count(&self) -> usize {
        self.groups.len()
    }

    /// Returns the total number of `(key, value)` pairs. O(1) — it is maintained incrementally, not
    /// derived by summing group sizes.
    #[must_use]
    pub fn pair_count(&self) -> usize {
        self.pair_count
    }

    /// Returns `true` when the multimap holds no pairs, and therefore no keys either.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.pair_count == 0
    }

    /// Borrows the hash policy defining key equivalence.
    #[must_use]
    pub fn key_hasher(&self) -> &SK {
        self.groups.hasher()
    }

    /// Borrows the hash policy defining value equivalence within each group, which is independent
    /// of the key policy.
    #[must_use]
    pub fn value_hasher(&self) -> &SV {
        &self.value_hasher
    }

    /// Iterates the distinct keys, in unspecified order.
    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.groups.keys()
    }

    /// Iterates `(key, value set)` groups, in unspecified order. Every yielded set is nonempty.
    #[must_use]
    pub fn groups(&self) -> Iter<'_, K, PersistentHashSet<V, SV>> {
        self.groups.iter()
    }

    /// Reports whether two multimaps share the same group map, so neither can observe an edit made
    /// to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_groups_root_with(&self, other: &Self) -> bool {
        self.groups.shares_root_with(&other.groups)
    }
}

impl<K, V, SK, SV> PersistentHashMultimap<K, V, SK, SV>
where
    K: Eq + Hash,
    V: Eq + Hash,
    SK: BuildHasher,
    SV: BuildHasher,
{
    /// Reports whether `key` has at least one value. O(1).
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.groups.contains_key(key)
    }

    /// Reports whether the pair `(key, value)` is present. O(1).
    #[must_use]
    pub fn contains(&self, key: &K, value: &V) -> bool {
        self.groups
            .get(key)
            .is_some_and(|values| values.contains(value))
    }

    /// Returns how many distinct values `key` has, or zero when the key is absent. O(1).
    #[must_use]
    pub fn count_values(&self, key: &K) -> usize {
        self.groups.get(key).map_or(0, PersistentHashSet::len)
    }

    /// Borrows `key`'s value set, or `None` when the key is absent. Never yields an empty set.
    ///
    /// The borrowed set is itself a persistent value, so a caller may clone it and keep it after
    /// the multimap moves on.
    #[must_use]
    pub fn get_values(&self, key: &K) -> Option<&PersistentHashSet<V, SV>> {
        self.groups.get(key)
    }

    /// Borrows the key representative stored for `equal_key`, or `None` when absent.
    ///
    /// The stored representative is the first one inserted for its equivalence class, which may not
    /// be the concrete value passed in.
    #[must_use]
    pub fn get_key(&self, equal_key: &K) -> Option<&K> {
        self.groups
            .get_key_value(equal_key)
            .map(|(stored, _)| stored)
    }

    /// Borrows the value representative stored under `key` for `equal_value`, or `None` when the
    /// pair is absent.
    #[must_use]
    pub fn get_value(&self, key: &K, equal_value: &V) -> Option<&V> {
        self.groups.get(key)?.get(equal_value)
    }
}

impl<K, V, SK, SV> PersistentHashMultimap<K, V, SK, SV>
where
    K: Eq + Hash + Clone,
    V: Eq + Hash + Clone,
    SK: BuildHasher + Clone,
    SV: BuildHasher + Clone,
{
    /// Builds from pair contributions, retaining first representatives and collapsing duplicates.
    #[must_use]
    pub fn from_pairs_with_hashers<I>(pairs: I, key_hasher: SK, value_hasher: SV) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        let mut result = Self::with_hashers(key_hasher, value_hasher);
        for (key, value) in pairs {
            result = result.insert(key, value);
        }
        result
    }

    /// Adds a pair. An equivalent existing pair returns a root-sharing clone.
    #[must_use]
    pub fn insert(&self, key: K, value: V) -> Self {
        let Some((stored_key, values)) = self.groups.get_key_value(&key) else {
            let values = PersistentHashSet::with_hasher(self.value_hasher.clone()).insert(value);
            return Self {
                groups: self.groups.insert(key, values),
                value_hasher: self.value_hasher.clone(),
                pair_count: self
                    .pair_count
                    .checked_add(1)
                    .expect("hash multimap pair count overflow"),
            };
        };

        let (updated, added) = values.try_add(value);
        if !added {
            return self.clone();
        }

        Self {
            groups: self.groups.insert(stored_key.clone(), updated),
            value_hasher: self.value_hasher.clone(),
            pair_count: self
                .pair_count
                .checked_add(1)
                .expect("hash multimap pair count overflow"),
        }
    }

    /// Attempts to add an absent pair.
    #[must_use]
    pub fn try_insert(&self, key: K, value: V) -> (Self, bool) {
        let result = self.insert(key, value);
        let changed = !result.groups.shares_root_with(&self.groups);
        (result, changed)
    }

    /// Removes one pair. A miss returns a root-sharing clone.
    #[must_use]
    pub fn remove(&self, key: &K, value: &V) -> Self {
        self.try_remove(key, value)
            .map_or_else(|| self.clone(), |(result, _)| result)
    }

    /// Removes one pair and returns its stored value representative.
    #[must_use]
    pub fn try_remove(&self, key: &K, value: &V) -> Option<(Self, V)> {
        let (stored_key, values) = self.groups.get_key_value(key)?;
        let (updated, actual_value) = values.try_remove(value)?;

        // Empty adjacency sets are forbidden. The last inner removal therefore contracts the
        // outer key in the same immutable successor.
        let groups = if updated.is_empty() {
            self.groups.remove(stored_key)
        } else {
            self.groups.insert(stored_key.clone(), updated)
        };
        Some((
            Self {
                groups,
                value_hasher: self.value_hasher.clone(),
                pair_count: self.pair_count - 1,
            },
            actual_value,
        ))
    }

    /// Removes a whole key group and returns its immutable value set.
    #[must_use]
    pub fn try_remove_key(&self, key: &K) -> Option<(Self, K, PersistentHashSet<V, SV>)> {
        let (groups, actual_key, values) = self.groups.try_remove_entry(key)?;
        let pair_count = self.pair_count - values.len();
        Some((
            Self {
                groups,
                value_hasher: self.value_hasher.clone(),
                pair_count,
            },
            actual_key,
            values,
        ))
    }

    /// Removes `key` together with all of its values. Removing an absent key is a no-op that
    /// shares the receiver's representation.
    ///
    /// Use [`Self::try_remove_key`] when the removed key representative and its value set are
    /// wanted.
    #[must_use]
    pub fn remove_key(&self, key: &K) -> Self {
        self.try_remove_key(key)
            .map_or_else(|| self.clone(), |(result, _, _)| result)
    }

    /// Returns an empty multimap retaining both hash policies. Clearing an empty multimap is a
    /// no-op that shares the receiver's representation.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self::with_hashers(self.key_hasher().clone(), self.value_hasher.clone())
    }

    /// Validates pair counts, nonempty groups, and value-hasher compatibility by behavior.
    pub fn validate(&self) -> Result<HashMultimapStatistics, HashMultimapInvariantError> {
        let mut pair_count = 0usize;
        for (_, values) in self.groups.iter() {
            if values.is_empty() {
                return Err(HashMultimapInvariantError::EmptyGroup);
            }
            pair_count = pair_count
                .checked_add(values.len())
                .ok_or(HashMultimapInvariantError::PairCountMismatch)?;
        }
        if pair_count != self.pair_count {
            return Err(HashMultimapInvariantError::PairCountMismatch);
        }
        Ok(HashMultimapStatistics {
            key_count: self.key_count(),
            pair_count,
        })
    }

    /// Iterates every `(key, value)` pair, flattening the groups. Order is unspecified.
    #[must_use]
    pub fn iter(&self) -> HashMultimapIter<'_, K, V, SV> {
        HashMultimapIter {
            groups: self.groups.iter(),
            current: None,
            remaining: self.pair_count,
        }
    }
}

impl<K, V> PersistentHashMultimap<K, V, RandomState, RandomState>
where
    K: Eq + Hash + Clone,
    V: Eq + Hash + Clone,
{
    /// Builds a multimap from pairs under fresh default hash policies, retaining the first
    /// representative of each key and value class and collapsing duplicate pairs.
    #[must_use]
    pub fn from_pairs<I>(pairs: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        Self::from_pairs_with_hashers(pairs, RandomState::new(), RandomState::new())
    }
}

impl<K, V, SK, SV> Default for PersistentHashMultimap<K, V, SK, SV>
where
    SK: Default,
    SV: Default,
{
    fn default() -> Self {
        Self::with_hashers(SK::default(), SV::default())
    }
}

impl<K, V, SK, SV> fmt::Debug for PersistentHashMultimap<K, V, SK, SV>
where
    K: Eq + Hash + Clone + fmt::Debug,
    V: Eq + Hash + Clone + fmt::Debug,
    SK: BuildHasher + Clone,
    SV: BuildHasher + Clone,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_map().entries(self.iter()).finish()
    }
}

/// Cardinalities recomputed by a successful [`PersistentHashMultimap::validate`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HashMultimapStatistics {
    /// The number of distinct keys.
    pub key_count: usize,
    /// The number of `(key, value)` pairs, recomputed by summing the group sizes.
    pub pair_count: usize,
}

/// A structural invariant violation found by [`PersistentHashMultimap::validate`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HashMultimapInvariantError {
    /// A key was stored with an empty value set, which the nonempty-group invariant forbids.
    EmptyGroup,
    /// The incrementally maintained pair count disagrees with the sum of the group sizes.
    PairCountMismatch,
}

impl fmt::Display for HashMultimapInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::EmptyGroup => "the multimap contains an empty value group",
            Self::PairCountMismatch => "the multimap pair count disagrees with its groups",
        })
    }
}

impl std::error::Error for HashMultimapInvariantError {}

/// Borrowing iterator over a [`PersistentHashMultimap`]'s `(key, value)` pairs, flattening the
/// groups. Order is unspecified.
pub struct HashMultimapIter<'a, K, V, SV> {
    groups: Iter<'a, K, PersistentHashSet<V, SV>>,
    current: Option<(&'a K, SetIter<'a, V>)>,
    remaining: usize,
}

impl<'a, K, V, SV> Iterator for HashMultimapIter<'a, K, V, SV> {
    type Item = (&'a K, &'a V);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Some((key, values)) = &mut self.current
                && let Some(value) = values.next()
            {
                self.remaining -= 1;
                return Some((*key, value));
            }
            let (key, values) = self.groups.next()?;
            self.current = Some((key, values.iter()));
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<K, V, SV> ExactSizeIterator for HashMultimapIter<'_, K, V, SV> {}
impl<K, V, SV> FusedIterator for HashMultimapIter<'_, K, V, SV> {}
