use std::hash::{BuildHasher, Hash};

use durable7_hamt::DuplicateKey;

use crate::{PersistentOrderedMap, PersistentOrderedMultimap, PersistentOrderedSet};

/// Presence-discriminated focused lookup result.
///
/// `found` reports whether an equivalent entry is already present. It never reports whether an
/// edit occurred; insertion results use [`OrderedCursorInsert`] so the two discriminators cannot
/// be confused by generic code written over either type.
#[derive(Clone)]
pub struct OrderedCursorSearch<C> {
    pub found: bool,
    pub cursor: C,
}

/// Insertion-discriminated focused edit result.
///
/// `added` reports whether the attempt published a new entry. A rejected attempt reports `false`
/// and returns a cursor focused on the retained equivalent entry, leaving the receiver's version
/// unchanged.
#[derive(Clone)]
pub struct OrderedCursorInsert<C> {
    pub added: bool,
    pub cursor: C,
}

/// Immutable root-plus-position gap cursor over a persistent ordered set.
pub struct PersistentOrderedSetCursor<T, S> {
    set: PersistentOrderedSet<T, S>,
    position: usize,
}

impl<T, S: Clone> Clone for PersistentOrderedSetCursor<T, S> {
    fn clone(&self) -> Self {
        Self {
            set: self.set.clone(),
            position: self.position,
        }
    }
}

impl<T, S> PersistentOrderedSet<T, S>
where
    S: Clone,
{
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<PersistentOrderedSetCursor<T, S>> {
        (position <= self.len()).then(|| PersistentOrderedSetCursor {
            set: self.clone(),
            position,
        })
    }
}

impl<T, S> PersistentOrderedSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn find_cursor(&self, value: &T) -> OrderedCursorSearch<PersistentOrderedSetCursor<T, S>> {
        let position = self.index_of(value);
        OrderedCursorSearch {
            found: position.is_some(),
            cursor: self
                .cursor_at(position.unwrap_or_else(|| self.len()))
                .expect("stored or end position is valid"),
        }
    }
}

impl<T, S> PersistentOrderedSetCursor<T, S>
where
    S: Clone,
{
    #[must_use]
    pub fn len(&self) -> usize {
        self.set.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.set.is_empty()
    }

    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    #[must_use]
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.set.get(index))
    }

    #[must_use]
    pub fn peek_next(&self) -> Option<&T> {
        self.set.get(self.position)
    }

    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        self.position.checked_sub(1).map(|position| Self {
            set: self.set.clone(),
            position,
        })
    }

    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            set: self.set.clone(),
            position: self.position + 1,
        })
    }

    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.set.cursor_at(position)
        }
    }

    #[must_use]
    pub fn snapshot(&self) -> &PersistentOrderedSet<T, S> {
        &self.set
    }
}

impl<T, S> PersistentOrderedSetCursor<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn insert(&self, value: T) -> Self {
        let set = self
            .set
            .insert(self.position, value)
            .expect("cursor insertion position is valid");
        if set.shares_roots_with(&self.set) {
            self.clone()
        } else {
            Self {
                set,
                position: self.position + 1,
            }
        }
    }

    /// Inserts an absent equality class at the focused gap.
    ///
    /// A duplicate reports `added: false` and returns a root-sharing cursor at the same gap.
    #[must_use]
    pub fn try_insert(&self, value: T) -> OrderedCursorInsert<Self> {
        let cursor = self.insert(value);
        OrderedCursorInsert {
            added: !cursor.set.shares_roots_with(&self.set),
            cursor,
        }
    }

    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        Some(Self {
            set: self
                .set
                .remove_at(position)
                .expect("focused representative exists"),
            position,
        })
    }

    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        Some(Self {
            set: self.set.remove_at(self.position)?,
            position: self.position,
        })
    }
}

/// Immutable root-plus-position gap cursor over a persistent ordered map.
pub struct PersistentOrderedMapCursor<K, V, S> {
    map: PersistentOrderedMap<K, V, S>,
    position: usize,
}

impl<K, V, S: Clone> Clone for PersistentOrderedMapCursor<K, V, S> {
    fn clone(&self) -> Self {
        Self {
            map: self.map.clone(),
            position: self.position,
        }
    }
}

impl<K, V, S> PersistentOrderedMap<K, V, S>
where
    S: Clone,
{
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<PersistentOrderedMapCursor<K, V, S>> {
        (position <= self.len()).then(|| PersistentOrderedMapCursor {
            map: self.clone(),
            position,
        })
    }
}

impl<K, V, S> PersistentOrderedMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn find_cursor(&self, key: &K) -> OrderedCursorSearch<PersistentOrderedMapCursor<K, V, S>> {
        let position = self.index_of(key);
        OrderedCursorSearch {
            found: position.is_some(),
            cursor: self
                .cursor_at(position.unwrap_or_else(|| self.len()))
                .expect("stored or end position is valid"),
        }
    }
}

impl<K, V, S> PersistentOrderedMapCursor<K, V, S>
where
    S: Clone,
{
    #[must_use]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    #[must_use]
    pub fn peek_previous(&self) -> Option<(&K, &V)> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.map.get_at(index))
    }

    #[must_use]
    pub fn peek_next(&self) -> Option<(&K, &V)> {
        self.map.get_at(self.position)
    }

    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        self.position.checked_sub(1).map(|position| Self {
            map: self.map.clone(),
            position,
        })
    }

    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            map: self.map.clone(),
            position: self.position + 1,
        })
    }

    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.map.cursor_at(position)
        }
    }

    #[must_use]
    pub fn snapshot(&self) -> &PersistentOrderedMap<K, V, S> {
        &self.map
    }
}

impl<K, V, S> PersistentOrderedMapCursor<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    pub fn insert(&self, key: K, value: V) -> Result<Self, DuplicateKey> {
        self.map.insert(self.position, key, value).map(|map| Self {
            map: map.expect("cursor insertion position is valid"),
            position: self.position + 1,
        })
    }

    /// Inserts an absent key at the focused gap.
    ///
    /// A duplicate reports `added: false`, leaves the receiver's version unchanged, and returns a
    /// cursor focused before the retained entry.
    #[must_use]
    pub fn try_insert(&self, key: K, value: V) -> OrderedCursorInsert<Self> {
        if let Some(position) = self.map.index_of(&key) {
            return OrderedCursorInsert {
                added: false,
                cursor: Self {
                    map: self.map.clone(),
                    position,
                },
            };
        }
        OrderedCursorInsert {
            added: true,
            cursor: self.insert(key, value).expect("prechecked key is absent"),
        }
    }

    #[must_use]
    pub fn set_next_value(&self, value: V) -> Option<Self> {
        let (key, _) = self.peek_next()?;
        Some(Self {
            map: self.map.set_item(key.clone(), value),
            position: self.position,
        })
    }

    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        Some(Self {
            map: self.map.remove_at(position)?.0,
            position,
        })
    }

    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        Some(Self {
            map: self.map.remove_at(self.position)?.0,
            position: self.position,
        })
    }
}

/// Immutable root-plus-pair-rank cursor over grouped ordered-multimap enumeration.
pub struct PersistentOrderedMultimapCursor<K, V, SK, SV> {
    map: PersistentOrderedMultimap<K, V, SK, SV>,
    position: usize,
}

impl<K, V, SK: Clone, SV: Clone> Clone for PersistentOrderedMultimapCursor<K, V, SK, SV> {
    fn clone(&self) -> Self {
        Self {
            map: self.map.clone(),
            position: self.position,
        }
    }
}

impl<K, V, SK, SV> PersistentOrderedMultimap<K, V, SK, SV>
where
    SK: Clone,
    SV: Clone,
{
    #[must_use]
    pub fn cursor_at(
        &self,
        position: usize,
    ) -> Option<PersistentOrderedMultimapCursor<K, V, SK, SV>> {
        (position <= self.pair_count()).then(|| PersistentOrderedMultimapCursor {
            map: self.clone(),
            position,
        })
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
    pub fn find_cursor(
        &self,
        key: &K,
        value: &V,
    ) -> OrderedCursorSearch<PersistentOrderedMultimapCursor<K, V, SK, SV>> {
        let position = self
            .iter()
            .position(|(stored_key, stored_value)| stored_key == key && stored_value == value);
        OrderedCursorSearch {
            found: position.is_some(),
            cursor: self
                .cursor_at(position.unwrap_or_else(|| self.pair_count()))
                .expect("stored or end position is valid"),
        }
    }

    #[must_use]
    pub fn find_group_cursor(
        &self,
        key: &K,
    ) -> OrderedCursorSearch<PersistentOrderedMultimapCursor<K, V, SK, SV>> {
        let position = self.iter().position(|(stored_key, _)| stored_key == key);
        OrderedCursorSearch {
            found: position.is_some(),
            cursor: self
                .cursor_at(position.unwrap_or_else(|| self.pair_count()))
                .expect("stored or end position is valid"),
        }
    }
}

impl<K, V, SK, SV> PersistentOrderedMultimapCursor<K, V, SK, SV>
where
    SK: Clone,
    SV: Clone,
{
    #[must_use]
    pub fn pair_count(&self) -> usize {
        self.map.pair_count()
    }

    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.pair_count()
    }

    #[must_use]
    pub fn peek_previous(&self) -> Option<(&K, &V)> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.map.iter().nth(position))
    }

    #[must_use]
    pub fn peek_next(&self) -> Option<(&K, &V)> {
        self.map.iter().nth(self.position)
    }

    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        self.position.checked_sub(1).map(|position| Self {
            map: self.map.clone(),
            position,
        })
    }

    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            map: self.map.clone(),
            position: self.position + 1,
        })
    }

    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.map.cursor_at(position)
        }
    }

    #[must_use]
    pub fn snapshot(&self) -> &PersistentOrderedMultimap<K, V, SK, SV> {
        &self.map
    }
}

impl<K, V, SK, SV> PersistentOrderedMultimapCursor<K, V, SK, SV>
where
    K: Eq + Hash + Clone,
    V: Eq + Hash + Clone,
    SK: BuildHasher + Clone,
    SV: BuildHasher + Clone,
{
    #[must_use]
    pub fn insert(&self, key: K, value: V) -> Self {
        let key_probe = key.clone();
        let map = self.map.insert(key, value);
        if map.shares_groups_root_with(&self.map) {
            return self.clone();
        }
        Self {
            position: Self::group_end(&map, &key_probe),
            map,
        }
    }

    /// Flattened pair rank of the gap immediately after the last pair of `key`'s group, or the end
    /// gap when `key` is absent. `insert` appends the value to the end of the key's existing group
    /// (or as a fresh last group), so the following gap is always a group boundary. Key groups are
    /// contiguous in the flattened enumeration, so this walks the leading pairs once and consults
    /// only the key equality — never the value equality — which keeps it total for a value whose
    /// `Eq` is not reflexive (a `NaN`-carrying wrapper, say), a pair the map accepts but a value
    /// re-scan could never find again.
    fn group_end(map: &PersistentOrderedMultimap<K, V, SK, SV>, key: &K) -> usize {
        let mut rank = 0;
        let mut in_group = false;
        for (stored_key, _) in map.iter() {
            if stored_key == key {
                in_group = true;
                rank += 1;
            } else if in_group {
                return rank;
            } else {
                rank += 1;
            }
        }
        rank
    }

    /// Inserts an absent pair into the focused key's group.
    ///
    /// A duplicate pair reports `added: false` and returns a root-sharing cursor at the same rank.
    #[must_use]
    pub fn try_insert(&self, key: K, value: V) -> OrderedCursorInsert<Self> {
        let cursor = self.insert(key, value);
        OrderedCursorInsert {
            added: !cursor.map.shares_groups_root_with(&self.map),
            cursor,
        }
    }

    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        self.deleted_pair(position)
    }

    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        self.deleted_pair(self.position)
    }

    /// Removes the pair at flattened `rank` and refocuses the gap there, or returns `None` when
    /// the removal changes nothing. `remove` locates the pair by content and returns its receiver
    /// on a miss, which a peeked pair still triggers when its value is not reflexive under `Eq`;
    /// comparing the pair count before publishing keeps a no-op delete from reporting false
    /// success.
    fn deleted_pair(&self, rank: usize) -> Option<Self> {
        let (key, value) = self.map.iter().nth(rank)?;
        let map = self.map.remove(key, value);
        (map.pair_count() != self.map.pair_count()).then_some(Self {
            map,
            position: rank,
        })
    }
}
