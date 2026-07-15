use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

use crate::{Iter, PersistentHashMap};

/// The occupied domain that prevented a strict bimap insertion or replacement.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BiMapConflict {
    Key,
    Value,
}

impl fmt::Display for BiMapConflict {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Key => formatter.write_str("an equivalent key is already present"),
            Self::Value => formatter.write_str("an equivalent value is already present"),
        }
    }
}

impl std::error::Error for BiMapConflict {}

/// Result of nonthrowing strict insertion.
#[must_use]
pub struct BiMapAddResult<K, V, SK = RandomState, SV = RandomState> {
    pub map: PersistentBiMap<K, V, SK, SV>,
    pub added: bool,
    pub conflict: Option<BiMapConflict>,
}

/// Presence-safe result of symmetric removal.
///
/// `removed` remains unambiguous when the opposite representative is itself an `Option`.
#[must_use]
pub struct BiMapRemoveResult<K, V, SK, SV, T> {
    pub map: PersistentBiMap<K, V, SK, SV>,
    pub removed: Option<T>,
}

/// Strict immutable bijection backed by independent forward and inverse CHAMP maps.
///
/// Rust equality is the key/value type's lawful `Eq`; `SK` and `SV` independently select the hash
/// builders for those two domains. Every published value owns two mutually consistent roots.
pub struct PersistentBiMap<K, V, SK = RandomState, SV = RandomState> {
    forward: PersistentHashMap<K, V, SK>,
    inverse: PersistentHashMap<V, K, SV>,
}

impl<K, V, SK: Clone, SV: Clone> Clone for PersistentBiMap<K, V, SK, SV> {
    fn clone(&self) -> Self {
        Self {
            forward: self.forward.clone(),
            inverse: self.inverse.clone(),
        }
    }
}

impl<K, V> PersistentBiMap<K, V> {
    #[must_use]
    pub fn new() -> Self {
        Self::with_hashers(RandomState::new(), RandomState::new())
    }
}

impl<K, V, SK, SV> PersistentBiMap<K, V, SK, SV> {
    #[must_use]
    pub fn with_hashers(key_hasher: SK, value_hasher: SV) -> Self {
        Self {
            forward: PersistentHashMap::with_hasher(key_hasher),
            inverse: PersistentHashMap::with_hasher(value_hasher),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.forward.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.forward.is_empty()
    }

    #[must_use]
    pub fn key_hasher(&self) -> &SK {
        self.forward.hasher()
    }

    #[must_use]
    pub fn value_hasher(&self) -> &SV {
        self.inverse.hasher()
    }

    #[must_use]
    pub fn iter(&self) -> Iter<'_, K, V> {
        self.forward.iter()
    }

    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.forward.keys()
    }

    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.forward.values()
    }

    #[must_use]
    pub fn shares_roots_with(&self, other: &Self) -> bool {
        self.forward.shares_root_with(&other.forward)
            && self.inverse.shares_root_with(&other.inverse)
    }
}

impl<K, V, SK, SV> PersistentBiMap<K, V, SK, SV>
where
    K: Eq + Hash,
    V: Eq + Hash,
    SK: BuildHasher,
    SV: BuildHasher,
{
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.forward.contains_key(key)
    }

    #[must_use]
    pub fn contains_value(&self, value: &V) -> bool {
        self.inverse.contains_key(value)
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.forward.get(key)
    }

    #[must_use]
    pub fn get_key(&self, value: &V) -> Option<&K> {
        self.inverse.get(value)
    }

    #[must_use]
    pub fn validate_structure(&self) -> bool {
        if self.forward.len() != self.inverse.len() {
            return false;
        }
        self.forward
            .iter()
            .all(|(key, value)| self.inverse.get(value) == Some(key))
            && self
                .inverse
                .iter()
                .all(|(value, key)| self.forward.get(key) == Some(value))
    }
}

impl<K, V, SK, SV> PersistentBiMap<K, V, SK, SV>
where
    K: Eq + Hash + Clone,
    V: Eq + Hash + Clone,
    SK: BuildHasher + Clone,
    SV: BuildHasher + Clone,
{
    pub fn from_items<I>(items: I, key_hasher: SK, value_hasher: SV) -> Result<Self, BiMapConflict>
    where
        I: IntoIterator<Item = (K, V)>,
    {
        let mut result = Self::with_hashers(key_hasher, value_hasher);
        for (key, value) in items {
            result = result.add(key, value)?;
        }
        Ok(result)
    }

    pub fn add(&self, key: K, value: V) -> Result<Self, BiMapConflict> {
        let result = self.try_add(key, value);
        result.conflict.map_or(Ok(result.map), Err)
    }

    /// Key conflict has precedence when both domains are already represented.
    pub fn try_add(&self, key: K, value: V) -> BiMapAddResult<K, V, SK, SV> {
        if self.forward.contains_key(&key) {
            return BiMapAddResult {
                map: self.clone(),
                added: false,
                conflict: Some(BiMapConflict::Key),
            };
        }
        if self.inverse.contains_key(&value) {
            return BiMapAddResult {
                map: self.clone(),
                added: false,
                conflict: Some(BiMapConflict::Value),
            };
        }

        let (forward, forward_added) = self.forward.try_add(key.clone(), value.clone());
        let (inverse, inverse_added) = self.inverse.try_add(value, key);
        assert!(
            forward_added && inverse_added,
            "bimap preflight and insertion disagreed"
        );
        BiMapAddResult {
            map: Self { forward, inverse },
            added: true,
            conflict: None,
        }
    }

    /// Adds or replaces one key's value without displacing a different key.
    pub fn set(&self, key: K, value: V) -> Result<Self, BiMapConflict> {
        let Some((stored_key, stored_value)) = self.forward.get_key_value(&key) else {
            if self.inverse.contains_key(&value) {
                return Err(BiMapConflict::Value);
            }
            return self.add(key, value);
        };
        if stored_value == &value {
            return Ok(self.clone());
        }
        if self.inverse.contains_key(&value) {
            return Err(BiMapConflict::Value);
        }

        let stored_key = stored_key.clone();
        let stored_value = stored_value.clone();
        let forward = self
            .forward
            .remove(&stored_key)
            .add(stored_key.clone(), value.clone())
            .expect("removed bimap key must be absent");
        let inverse = self
            .inverse
            .remove(&stored_value)
            .add(value, stored_key)
            .expect("removed bimap value must be absent");
        Ok(Self { forward, inverse })
    }

    #[must_use]
    pub fn remove_key(&self, key: &K) -> Self {
        self.try_remove_key(key).map
    }

    pub fn try_remove_key(&self, key: &K) -> BiMapRemoveResult<K, V, SK, SV, V> {
        let Some((stored_key, stored_value)) = self.forward.get_key_value(key) else {
            return BiMapRemoveResult {
                map: self.clone(),
                removed: None,
            };
        };
        let stored_key = stored_key.clone();
        let stored_value = stored_value.clone();
        assert!(
            self.inverse.get(&stored_value) == Some(&stored_key),
            "persistent bimap invariant failure"
        );
        BiMapRemoveResult {
            map: Self {
                forward: self.forward.remove(&stored_key),
                inverse: self.inverse.remove(&stored_value),
            },
            removed: Some(stored_value),
        }
    }

    #[must_use]
    pub fn remove_value(&self, value: &V) -> Self {
        self.try_remove_value(value).map
    }

    pub fn try_remove_value(&self, value: &V) -> BiMapRemoveResult<K, V, SK, SV, K> {
        let Some((stored_value, stored_key)) = self.inverse.get_key_value(value) else {
            return BiMapRemoveResult {
                map: self.clone(),
                removed: None,
            };
        };
        let stored_value = stored_value.clone();
        let stored_key = stored_key.clone();
        assert!(
            self.forward.get(&stored_key) == Some(&stored_value),
            "persistent bimap invariant failure"
        );
        BiMapRemoveResult {
            map: Self {
                forward: self.forward.remove(&stored_key),
                inverse: self.inverse.remove(&stored_value),
            },
            removed: Some(stored_key),
        }
    }

    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self {
            forward: self.forward.clear(),
            inverse: self.inverse.clear(),
        }
    }

    /// O(1) value-semantic inversion over the same two `Arc`-shared roots.
    #[must_use]
    pub fn inverse(&self) -> PersistentBiMap<V, K, SV, SK> {
        PersistentBiMap {
            forward: self.inverse.clone(),
            inverse: self.forward.clone(),
        }
    }
}

impl<K, V, SK: Default, SV: Default> Default for PersistentBiMap<K, V, SK, SV> {
    fn default() -> Self {
        Self::with_hashers(SK::default(), SV::default())
    }
}

impl<'a, K, V, SK, SV> IntoIterator for &'a PersistentBiMap<K, V, SK, SV> {
    type Item = (&'a K, &'a V);
    type IntoIter = Iter<'a, K, V>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<K: fmt::Debug, V: fmt::Debug, SK, SV> fmt::Debug for PersistentBiMap<K, V, SK, SV> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_map().entries(self.iter()).finish()
    }
}
