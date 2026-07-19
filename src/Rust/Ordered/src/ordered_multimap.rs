use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

use crate::{PersistentOrderedMap, PersistentOrderedSet};

struct OrderedGroup<V, S>(PersistentOrderedSet<V, S>);

impl<V, S> Clone for OrderedGroup<V, S>
where
    S: Clone,
{
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

impl<V, S> PartialEq for OrderedGroup<V, S> {
    fn eq(&self, other: &Self) -> bool {
        self.0.shares_roots_with(&other.0)
    }
}

/// Successful structural-validation statistics for an ordered multimap.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PersistentOrderedMultimapStatistics {
    pub key_count: usize,
    pub pair_count: usize,
}

/// A disagreement among the ordered multimap's nested indexes and cached count.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PersistentOrderedMultimapInvariantError {
    OuterMap,
    EmptyGroup,
    ValueGroup,
    PairCountMismatch,
}

impl fmt::Display for PersistentOrderedMultimapInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::OuterMap => "the ordered multimap's outer map is invalid",
            Self::EmptyGroup => "the ordered multimap stores an empty value group",
            Self::ValueGroup => "an ordered multimap value group is invalid",
            Self::PairCountMismatch => "the ordered multimap pair count disagrees with its groups",
        })
    }
}

impl std::error::Error for PersistentOrderedMultimapInvariantError {}

/// Immutable set-valued multimap with insertion order for key groups and values within each group.
///
/// Enumeration is key-grouped rather than one globally interleaved pair-arrival history. Rust
/// `Eq`/`Hash` define the two equality domains, while `SK` and `SV` retain their independent hash
/// builders. Empty groups are never stored.
pub struct PersistentOrderedMultimap<K, V, SK = RandomState, SV = RandomState> {
    groups: PersistentOrderedMap<K, OrderedGroup<V, SV>, SK>,
    value_hasher: SV,
    pair_count: usize,
}

impl<K, V, SK, SV> Clone for PersistentOrderedMultimap<K, V, SK, SV>
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

impl<K, V> PersistentOrderedMultimap<K, V, RandomState, RandomState> {
    #[must_use]
    pub fn new() -> Self {
        Self::with_hashers(RandomState::new(), RandomState::new())
    }
}

impl<K, V, SK, SV> PersistentOrderedMultimap<K, V, SK, SV> {
    #[must_use]
    pub fn with_hashers(key_hasher: SK, value_hasher: SV) -> Self {
        Self {
            groups: PersistentOrderedMap::with_hasher(key_hasher),
            value_hasher,
            pair_count: 0,
        }
    }

    #[must_use]
    pub fn key_count(&self) -> usize {
        self.groups.len()
    }

    #[must_use]
    pub fn pair_count(&self) -> usize {
        self.pair_count
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.pair_count == 0
    }

    #[must_use]
    pub fn key_hasher(&self) -> &SK {
        self.groups.hasher()
    }

    #[must_use]
    pub fn value_hasher(&self) -> &SV {
        &self.value_hasher
    }

    pub fn keys(&self) -> impl ExactSizeIterator<Item = &K> {
        self.groups.keys()
    }

    pub fn groups(&self) -> impl ExactSizeIterator<Item = (&K, &PersistentOrderedSet<V, SV>)> {
        self.groups.iter().map(|(key, group)| (key, &group.0))
    }

    pub fn iter(&self) -> impl Iterator<Item = (&K, &V)> {
        self.groups
            .iter()
            .flat_map(|(key, group)| group.0.iter().map(move |value| (key, value)))
    }

    #[must_use]
    pub fn shares_groups_root_with(&self, other: &Self) -> bool {
        self.groups.shares_roots_with(&other.groups)
    }
}

impl<K, V, SK, SV> PersistentOrderedMultimap<K, V, SK, SV>
where
    K: Eq + Hash,
    V: Eq + Hash + Clone,
    SK: BuildHasher,
    SV: BuildHasher + Clone,
{
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.groups.contains_key(key)
    }

    #[must_use]
    pub fn contains(&self, key: &K, value: &V) -> bool {
        self.groups
            .get(key)
            .is_some_and(|group| group.0.contains(value))
    }

    #[must_use]
    pub fn count_values(&self, key: &K) -> usize {
        self.groups.get(key).map_or(0, |group| group.0.len())
    }

    #[must_use]
    pub fn get_values(&self, key: &K) -> Option<&PersistentOrderedSet<V, SV>> {
        self.groups.get(key).map(|group| &group.0)
    }

    #[must_use]
    pub fn get_key(&self, equal_key: &K) -> Option<&K> {
        self.groups.get_key(equal_key)
    }

    #[must_use]
    pub fn get_value(&self, key: &K, equal_value: &V) -> Option<&V> {
        self.groups.get(key)?.0.get_stored(equal_value)
    }
}

impl<K, V> PersistentOrderedMultimap<K, V, RandomState, RandomState>
where
    K: Eq + Hash + Clone,
    V: Eq + Hash + Clone,
{
    #[must_use]
    pub fn from_pairs<I>(pairs: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        Self::from_pairs_with_hashers(pairs, RandomState::new(), RandomState::new())
    }
}

impl<K, V, SK, SV> PersistentOrderedMultimap<K, V, SK, SV>
where
    K: Eq + Hash + Clone,
    V: Eq + Hash + Clone,
    SK: BuildHasher + Clone,
    SV: BuildHasher + Clone,
{
    #[must_use]
    pub fn from_pairs_with_hashers<I>(pairs: I, key_hasher: SK, value_hasher: SV) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        pairs.into_iter().fold(
            Self::with_hashers(key_hasher, value_hasher),
            |map, (key, value)| map.insert(key, value),
        )
    }

    /// Adds one pair. An equivalent pair returns a root-sharing clone.
    #[must_use]
    pub fn insert(&self, key: K, value: V) -> Self {
        if let Some(stored_key) = self.groups.get_key(&key) {
            let group = self
                .groups
                .get(stored_key)
                .expect("stored ordered multimap key must have a group");
            let updated = group.0.add(value);
            if updated.shares_roots_with(&group.0) {
                return self.clone();
            }
            return Self {
                groups: self
                    .groups
                    .set_item(stored_key.clone(), OrderedGroup(updated)),
                value_hasher: self.value_hasher.clone(),
                pair_count: self
                    .pair_count
                    .checked_add(1)
                    .expect("ordered multimap pair count overflow"),
            };
        }

        let group = PersistentOrderedSet::with_hasher(self.value_hasher.clone()).add(value);
        Self {
            groups: self
                .groups
                .add(key, OrderedGroup(group))
                .expect("prechecked ordered multimap key must be absent"),
            value_hasher: self.value_hasher.clone(),
            pair_count: self
                .pair_count
                .checked_add(1)
                .expect("ordered multimap pair count overflow"),
        }
    }

    #[must_use]
    pub fn try_insert(&self, key: K, value: V) -> (Self, bool) {
        let result = self.insert(key, value);
        let changed = !result.groups.shares_roots_with(&self.groups);
        (result, changed)
    }

    #[must_use]
    pub fn remove(&self, key: &K, value: &V) -> Self {
        let Some(stored_key) = self.groups.get_key(key) else {
            return self.clone();
        };
        let group = self
            .groups
            .get(stored_key)
            .expect("stored ordered multimap key must have a group");
        let removal = group.0.try_remove(value);
        if !removal.removed {
            return self.clone();
        }
        let groups = if removal.set.is_empty() {
            self.groups.remove(stored_key)
        } else {
            self.groups
                .set_item(stored_key.clone(), OrderedGroup(removal.set))
        };
        Self {
            groups,
            value_hasher: self.value_hasher.clone(),
            pair_count: self
                .pair_count
                .checked_sub(1)
                .expect("ordered multimap pair count underflow"),
        }
    }

    #[must_use]
    pub fn try_remove_key(&self, key: &K) -> Option<(Self, K, PersistentOrderedSet<V, SV>)> {
        let removed = self.groups.try_remove(key);
        let (actual_key, group) = removed.removed?;
        Some((
            Self {
                groups: removed.map,
                value_hasher: self.value_hasher.clone(),
                pair_count: self
                    .pair_count
                    .checked_sub(group.0.len())
                    .expect("ordered multimap pair count underflow"),
            },
            actual_key,
            group.0,
        ))
    }

    #[must_use]
    pub fn remove_key(&self, key: &K) -> Self {
        self.try_remove_key(key)
            .map_or_else(|| self.clone(), |(result, _, _)| result)
    }

    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::with_hashers(self.key_hasher().clone(), self.value_hasher.clone())
        }
    }

    pub fn validate_structure(
        &self,
    ) -> Result<PersistentOrderedMultimapStatistics, PersistentOrderedMultimapInvariantError> {
        self.groups
            .validate_structure()
            .map_err(|_| PersistentOrderedMultimapInvariantError::OuterMap)?;
        let mut pair_count = 0usize;
        for (_, group) in self.groups.iter() {
            if group.0.is_empty() {
                return Err(PersistentOrderedMultimapInvariantError::EmptyGroup);
            }
            group
                .0
                .validate_structure()
                .map_err(|_| PersistentOrderedMultimapInvariantError::ValueGroup)?;
            pair_count = pair_count
                .checked_add(group.0.len())
                .ok_or(PersistentOrderedMultimapInvariantError::PairCountMismatch)?;
        }
        if pair_count != self.pair_count {
            return Err(PersistentOrderedMultimapInvariantError::PairCountMismatch);
        }
        Ok(PersistentOrderedMultimapStatistics {
            key_count: self.key_count(),
            pair_count,
        })
    }
}

impl<K, V, SK, SV> Default for PersistentOrderedMultimap<K, V, SK, SV>
where
    SK: Default,
    SV: Default,
{
    fn default() -> Self {
        Self::with_hashers(SK::default(), SV::default())
    }
}

impl<K, V, SK, SV> fmt::Debug for PersistentOrderedMultimap<K, V, SK, SV>
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
