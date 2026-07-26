//! Strict, invertible, composable patches over persistent hash maps.
//!
//! A [`PersistentMapPatch`] is a first-class diff: each [`MapPatchEntry`] records a key together
//! with its expected `before` state and its intended `after` state, where `None` means absence and
//! `Some(v)` means presence. Recording the expected prior state is what makes a patch *strict* —
//! application preflights every entry against the target map and either applies all of them or
//! reports a conflict and applies none, so a patch cannot half-succeed against a map that has
//! drifted.
//!
//! Because both endpoints are recorded, a patch can be inverted by exchanging them, and two patches
//! can be composed into one whose effect is applying them in order. Presence is encoded through
//! `Option` at the patch level, so a value type that is itself `Option<T>` still distinguishes "key
//! absent" from "key present holding `None`".

use crate::{DuplicateKey, MapDifference, PersistentHashMap};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

/// One presence-safe map change. `None` is absence; `Some(value)` is presence, so a value type that
/// is itself `Option<T>` naturally distinguishes absence from a present `None`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MapPatchEntry<K, V> {
    pub key: K,
    pub before: Option<V>,
    pub after: Option<V>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct MapPatchChange<V> {
    before: Option<V>,
    after: Option<V>,
}

/// A strict patch expectation that did not match the supplied source map.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MapPatchConflict<K> {
    pub key: K,
}

impl<K: fmt::Debug> fmt::Display for MapPatchConflict<K> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "the source map conflicts with the patch at key {:?}",
            self.key
        )
    }
}

impl<K: fmt::Debug> std::error::Error for MapPatchConflict<K> {}

/// Two patches disagree about the state between their applications.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MapPatchComposeError<K> {
    pub key: K,
}

impl<K: fmt::Debug> fmt::Display for MapPatchComposeError<K> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "patches disagree about the intermediate state at key {:?}",
            self.key
        )
    }
}

impl<K: fmt::Debug> std::error::Error for MapPatchComposeError<K> {}

/// Immutable, invertible strict changes between persistent hash maps.
pub struct PersistentMapPatch<K, V, S = RandomState> {
    changes: PersistentHashMap<K, MapPatchChange<V>, S>,
}

impl<K, V, S> Clone for PersistentMapPatch<K, V, S>
where
    S: Clone,
{
    fn clone(&self) -> Self {
        Self {
            changes: self.changes.clone(),
        }
    }
}

impl<K, V> PersistentMapPatch<K, V, RandomState> {
    /// Creates an empty patch using a fresh default hash policy.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S> PersistentMapPatch<K, V, S> {
    /// Creates an empty patch keyed under the supplied hash policy.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            changes: PersistentHashMap::with_hasher(hasher),
        }
    }

    /// Returns the number of keys this patch changes. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.changes.len()
    }

    /// Returns `true` when the patch changes nothing, so applying it is an identity.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.changes.is_empty()
    }

    /// Borrows the retained hash policy that this patch's keys are indexed under.
    #[must_use]
    pub fn hasher(&self) -> &S {
        self.changes.hasher()
    }

    /// Reports whether two patches are backed by the same change map, so neither can observe an
    /// edit made to the other. A representation test used to confirm that a no-op avoided copying.
    #[must_use]
    pub fn shares_changes_root_with(&self, other: &Self) -> bool {
        self.changes.shares_root_with(&other.changes)
    }

    /// Iterates the keys this patch changes, in unspecified order.
    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.changes.keys()
    }

    /// Iterates every change as a borrowed [`MapPatchEntry`], in unspecified order.
    pub fn iter(&self) -> impl ExactSizeIterator<Item = MapPatchEntry<&K, &V>> {
        self.changes.iter().map(|(key, change)| MapPatchEntry {
            key,
            before: change.before.as_ref(),
            after: change.after.as_ref(),
        })
    }
}

impl<K, V, S> PersistentMapPatch<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    /// Reports whether this patch changes `key`. O(1).
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.changes.contains_key(key)
    }

    /// Borrows the change recorded for `key`, or `None` when the patch leaves it alone. O(1).
    #[must_use]
    pub fn get(&self, key: &K) -> Option<MapPatchEntry<&K, &V>> {
        self.changes
            .get_key_value(key)
            .map(|(stored, change)| MapPatchEntry {
                key: stored,
                before: change.before.as_ref(),
                after: change.after.as_ref(),
            })
    }
}

impl<K, V> PersistentMapPatch<K, V, RandomState>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
{
    /// Computes the patch that turns `source` into `target`.
    ///
    /// The result records only the keys that actually differ, so applying it to `source` yields
    /// `target` and applying its inverse to `target` yields `source`.
    #[must_use]
    pub fn between(source: &PersistentHashMap<K, V>, target: &PersistentHashMap<K, V>) -> Self {
        Self::between_with_hasher(source, target)
    }
}

impl<K, V, S> PersistentMapPatch<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    /// Computes the semantic patch between two maps. Shared-policy maps retain structural diff
    /// pruning; independently created hash policies use the map's semantic fallback.
    #[must_use]
    pub fn between_with_hasher(
        source: &PersistentHashMap<K, V, S>,
        target: &PersistentHashMap<K, V, S>,
    ) -> Self {
        let mut result = Self::with_hasher(source.hasher().clone());
        for difference in source.diff(target) {
            let entry = match difference {
                MapDifference::Added { key, value } => MapPatchEntry {
                    key,
                    before: None,
                    after: Some(value),
                },
                MapDifference::Removed { key, value } => MapPatchEntry {
                    key,
                    before: Some(value),
                    after: None,
                },
                MapDifference::Changed { key, before, after } => MapPatchEntry {
                    key,
                    before: Some(before),
                    after: Some(after),
                },
            };
            result = result
                .add(entry)
                .expect("map diff contains each changed key once");
        }
        result
    }

    /// Adds one non-no-op change and rejects an equivalent changed key.
    pub fn add(&self, entry: MapPatchEntry<K, V>) -> Result<Self, DuplicateKey> {
        if entry.before == entry.after {
            return Ok(self.clone());
        }
        Ok(Self {
            changes: self.changes.add(
                entry.key,
                MapPatchChange {
                    before: entry.before,
                    after: entry.after,
                },
            )?,
        })
    }

    /// Adds one change, returning the resulting patch and whether it was actually recorded.
    ///
    /// The entry is rejected — `false`, receiver returned unchanged — when its key is already
    /// covered, or when `before` equals `after` and so records no change at all. Keeping such
    /// entries out is what lets [`Self::is_empty`] mean "applying this is an identity".
    #[must_use]
    pub fn try_add(&self, entry: MapPatchEntry<K, V>) -> (Self, bool) {
        if entry.before == entry.after || self.changes.contains_key(&entry.key) {
            return (self.clone(), false);
        }
        (
            Self {
                changes: self.changes.insert(
                    entry.key,
                    MapPatchChange {
                        before: entry.before,
                        after: entry.after,
                    },
                ),
            },
            true,
        )
    }

    /// Returns a patch that no longer changes `key`. Removing an uncovered key is a no-op that
    /// shares the receiver's representation.
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let changes = self.changes.remove(key);
        if changes.shares_root_with(&self.changes) {
            self.clone()
        } else {
            Self { changes }
        }
    }

    /// Returns an empty patch under the same hash policy. Clearing an already empty patch shares
    /// the receiver's representation.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self {
                changes: self.changes.clear(),
            }
        }
    }

    /// Applies every change to `source`, or applies none of them.
    ///
    /// Each entry's recorded `before` is checked against `source` first; the first mismatch aborts
    /// with a [`MapPatchConflict`] naming the key, and `source` is returned untouched. That
    /// preflight is what makes the patch strict — it cannot half-apply to a map that has drifted
    /// from the state the patch was computed against.
    pub fn apply(
        &self,
        source: &PersistentHashMap<K, V, S>,
    ) -> Result<PersistentHashMap<K, V, S>, MapPatchConflict<K>> {
        for (key, change) in self.changes.iter() {
            if source.get(key) != change.before.as_ref() {
                return Err(MapPatchConflict { key: key.clone() });
            }
        }
        let mut result = source.clone();
        for (key, change) in self.changes.iter() {
            result = match &change.after {
                Some(value) => result.insert(key.clone(), value.clone()),
                None => result.remove(key),
            };
        }
        Ok(result)
    }

    /// Returns the patch that undoes this one, by exchanging every entry's `before` and `after`.
    ///
    /// Applying a patch and then its inverse restores the original map, which is exactly why both
    /// endpoints are recorded rather than just the intended result.
    #[must_use]
    pub fn invert(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        let mut result = Self::with_hasher(self.hasher().clone());
        for (key, change) in self.changes.iter() {
            result.changes = result.changes.insert(
                key.clone(),
                MapPatchChange {
                    before: change.after.clone(),
                    after: change.before.clone(),
                },
            );
        }
        result
    }

    /// Returns one patch equivalent to applying this patch and then `next`.
    ///
    /// Where both patches touch a key, this patch's `after` must equal `next`'s `before` — the
    /// intermediate state they disagree about otherwise — and a mismatch is reported as a
    /// [`MapPatchComposeError`]. Changes that cancel out, so that the composed `before` equals the
    /// composed `after`, are dropped rather than recorded as no-ops.
    pub fn compose(&self, next: &Self) -> Result<Self, MapPatchComposeError<K>> {
        let mut result = self.clone();
        for (next_key, next_change) in next.changes.iter() {
            let Some((stored_key, current)) = result.changes.get_key_value(next_key) else {
                result.changes = result.changes.insert(next_key.clone(), next_change.clone());
                continue;
            };
            if current.after != next_change.before {
                return Err(MapPatchComposeError {
                    key: stored_key.clone(),
                });
            }
            let key = stored_key.clone();
            let combined = MapPatchChange {
                before: current.before.clone(),
                after: next_change.after.clone(),
            };
            result.changes = result.changes.remove(&key);
            if combined.before != combined.after {
                result.changes = result.changes.insert(key, combined);
            }
        }
        Ok(result)
    }

    /// Checks the patch's own well-formedness, returning the first entry that records no actual
    /// change, that is, one whose `before` equals its `after`.
    ///
    /// [`Self::try_add`] already rejects such entries, so this is a defensive audit rather than a
    /// routine step.
    pub fn validate(&self) -> Result<(), MapPatchEntry<&K, &V>> {
        for entry in self.iter() {
            if entry.before == entry.after {
                return Err(entry);
            }
        }
        Ok(())
    }
}

impl<K, V, S> Default for PersistentMapPatch<K, V, S>
where
    S: Default,
{
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V, S> fmt::Debug for PersistentMapPatch<K, V, S>
where
    K: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}
