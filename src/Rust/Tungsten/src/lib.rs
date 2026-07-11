#![forbid(unsafe_code)]
#![doc = "Persistent Tungsten List and Association-shaped collections."]

use std::cmp::Ordering;
use std::collections::hash_map::RandomState;
use std::hash::{BuildHasher, Hash};

use tools_data_structures_fingertree::PersistentDeque;
use tools_data_structures_hamt::PersistentHashMap;

const STAMP_GAP: i64 = 1_i64 << 20;

#[derive(Debug, PartialEq, Eq)]
pub struct PersistentList<T> {
    items: PersistentDeque<T>,
}

#[derive(Debug, PartialEq, Eq)]
pub struct PersistentListSplit<T> {
    pub left: PersistentList<T>,
    pub right: PersistentList<T>,
}

impl<T> Clone for PersistentListSplit<T> {
    fn clone(&self) -> Self {
        Self {
            left: self.left.clone(),
            right: self.right.clone(),
        }
    }
}

impl<T> PersistentList<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: PersistentDeque::new(),
        }
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
    pub fn front(&self) -> Option<&T> {
        self.items.front()
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.items.back()
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.items.get(index)
    }

    pub fn iter(&self) -> impl Iterator<Item = &T> {
        self.items.iter()
    }

    #[must_use]
    pub fn append(&self, item: T) -> Self {
        Self {
            items: self.items.push_back(item),
        }
    }

    #[must_use]
    pub fn prepend(&self, item: T) -> Self {
        Self {
            items: self.items.push_front(item),
        }
    }

    #[must_use]
    pub fn join(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        Self {
            items: self.items.concat(&other.items),
        }
    }

    #[must_use]
    pub fn add_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        Self {
            items: self.items.add_range(items),
        }
    }

    #[must_use]
    pub fn insert(&self, index: usize, item: T) -> Option<Self> {
        Some(Self {
            items: self.items.insert_at(index, item)?,
        })
    }

    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        Some(Self {
            items: self.items.insert_range(index, items)?,
        })
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        Some(Self {
            items: self.items.remove_at(index)?,
        })
    }

    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        Some(Self {
            items: self.items.remove_range(index, count)?,
        })
    }

    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        Some(Self {
            items: self.items.remove_first()?,
        })
    }

    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        Some(Self {
            items: self.items.remove_last()?,
        })
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        Some(Self {
            items: self.items.set_item(index, item)?,
        })
    }

    #[must_use]
    pub fn update_at<F>(&self, index: usize, updater: F) -> Option<Self>
    where
        F: FnOnce(&T) -> T,
    {
        Some(Self {
            items: self.items.update_at(index, updater)?,
        })
    }

    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        Some(Self {
            items: self.items.get_range(index, count)?,
        })
    }

    #[must_use]
    pub fn take(&self, count: usize) -> Option<Self> {
        self.get_range(0, count)
    }

    #[must_use]
    pub fn take_last(&self, count: usize) -> Option<Self> {
        (count <= self.len())
            .then(|| self.get_range(self.len() - count, count))
            .flatten()
    }

    #[must_use]
    pub fn drop(&self, count: usize) -> Option<Self> {
        (count <= self.len())
            .then(|| self.get_range(count, self.len() - count))
            .flatten()
    }

    #[must_use]
    pub fn drop_last(&self, count: usize) -> Option<Self> {
        (count <= self.len())
            .then(|| self.get_range(0, self.len() - count))
            .flatten()
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<PersistentListSplit<T>> {
        let split = self.items.split_at(index)?;
        Some(PersistentListSplit {
            left: Self { items: split.left },
            right: Self { items: split.right },
        })
    }

    #[must_use]
    pub fn map<U, F>(&self, selector: F) -> PersistentList<U>
    where
        F: FnMut(&T) -> U,
    {
        PersistentList {
            items: self.iter().map(selector).collect(),
        }
    }

    #[must_use]
    pub fn index_of_by<F>(&self, item: &T, mut equal: F) -> Option<usize>
    where
        F: FnMut(&T, &T) -> bool,
    {
        self.iter().position(|candidate| equal(candidate, item))
    }

    #[must_use]
    pub fn contains_by<F>(&self, item: &T, equal: F) -> bool
    where
        F: FnMut(&T, &T) -> bool,
    {
        self.index_of_by(item, equal).is_some()
    }
}

impl<T> Clone for PersistentList<T> {
    fn clone(&self) -> Self {
        Self {
            items: self.items.clone(),
        }
    }
}

impl<T> PersistentList<T>
where
    T: Clone,
{
    #[must_use]
    pub fn reverse(&self) -> Self {
        if self.len() <= 1 {
            return self.clone();
        }

        let mut values = self.items.to_vec();
        values.reverse();
        values.into_iter().collect()
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.to_vec()
    }
}

impl<T> PersistentList<T>
where
    T: PartialEq,
{
    #[must_use]
    pub fn index_of(&self, item: &T) -> Option<usize> {
        self.index_of_by(item, PartialEq::eq)
    }

    #[must_use]
    pub fn contains(&self, item: &T) -> bool {
        self.index_of(item).is_some()
    }
}

impl<T> Default for PersistentList<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for PersistentList<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self {
            items: iter.into_iter().collect(),
        }
    }
}

impl<T> IntoIterator for PersistentList<T>
where
    T: Clone,
{
    type Item = T;
    type IntoIter = std::vec::IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.items.to_vec().into_iter()
    }
}

impl<'a, T> IntoIterator for &'a PersistentList<T> {
    type Item = &'a T;
    type IntoIter = Box<dyn Iterator<Item = &'a T> + 'a>;

    fn into_iter(self) -> Self::IntoIter {
        Box::new(self.items.iter())
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct Slot<T> {
    stamp: i64,
    value: T,
}

#[derive(Clone, Debug)]
struct Entry<K, V> {
    stamp: i64,
    key: Option<K>,
    value: Option<V>,
}

impl<K, V> Entry<K, V> {
    fn real(stamp: i64, key: K, value: V) -> Self {
        Self {
            stamp,
            key: Some(key),
            value: Some(value),
        }
    }

    fn probe(stamp: i64) -> Self {
        Self {
            stamp,
            key: None,
            value: None,
        }
    }

    fn key(&self) -> &K {
        self.key
            .as_ref()
            .expect("real association entries carry keys")
    }

    fn value(&self) -> &V {
        self.value
            .as_ref()
            .expect("real association entries carry values")
    }
}

impl<K, V> PartialEq for Entry<K, V> {
    fn eq(&self, other: &Self) -> bool {
        self.stamp == other.stamp
    }
}

impl<K, V> Eq for Entry<K, V> {}

impl<K, V> PartialOrd for Entry<K, V> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl<K, V> Ord for Entry<K, V> {
    fn cmp(&self, other: &Self) -> Ordering {
        self.stamp.cmp(&other.stamp)
    }
}

#[derive(Clone)]
pub struct PersistentAssociation<K, V, S = RandomState> {
    entries: PersistentDeque<Entry<K, V>>,
    index: PersistentHashMap<K, Slot<V>, S>,
}

impl<K, V> PersistentAssociation<K, V, RandomState>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
{
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S> PersistentAssociation<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            entries: PersistentDeque::new(),
            index: PersistentHashMap::with_hasher(hasher),
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

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.index.hasher()
    }

    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.index.contains_key(key)
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.index.get(key).map(|slot| &slot.value)
    }

    #[must_use]
    pub fn get_key(&self, equal_key: &K) -> Option<&K> {
        self.index.get_key_value(equal_key).map(|(key, _)| key)
    }

    #[must_use]
    pub fn first(&self) -> Option<(&K, &V)> {
        self.entries
            .front()
            .map(|entry| (entry.key(), entry.value()))
    }

    #[must_use]
    pub fn last(&self) -> Option<(&K, &V)> {
        self.entries
            .back()
            .map(|entry| (entry.key(), entry.value()))
    }

    #[must_use]
    pub fn get_at(&self, index: usize) -> Option<(&K, &V)> {
        self.entries
            .get(index)
            .map(|entry| (entry.key(), entry.value()))
    }

    #[must_use]
    pub fn index_of_key(&self, key: &K) -> Option<usize> {
        self.index
            .get(key)
            .map(|slot| self.index_of_stamp(slot.stamp))
    }

    pub fn iter(&self) -> impl Iterator<Item = (&K, &V)> {
        self.entries
            .iter()
            .map(|entry| (entry.key(), entry.value()))
    }

    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let Some(found) = self.index.get(&key) else {
            return self.append_new(key, value);
        };

        if found.value == value {
            return self.clone();
        }

        let stamp = found.stamp;
        let position = self.index_of_stamp(stamp);
        let entries = self
            .entries
            .update_at(position, |stored| {
                Entry::real(stored.stamp, stored.key().clone(), value.clone())
            })
            .expect("stamp position is valid");
        let index = self.index.insert(key, Slot { stamp, value });
        self.make(entries, index)
    }

    #[must_use]
    pub fn set_items<I>(&self, pairs: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        pairs
            .into_iter()
            .fold(self.clone(), |current, (key, value)| {
                current.set_item(key, value)
            })
    }

    #[must_use]
    pub fn join(&self, other: &Self) -> Self {
        if other.is_empty() {
            return self.clone();
        }

        other.iter().fold(self.clone(), |current, (key, value)| {
            current.set_item(key.clone(), value.clone())
        })
    }

    #[must_use]
    pub fn append(&self, key: K, value: V) -> Self {
        let Some(found) = self.index.get(&key) else {
            return self.append_new(key, value);
        };

        // Stamps are unique, so an end-stamp comparison decides the no-op
        // fast path in O(1) like the C# reference, without the O(log n)
        // position search that the removal path below still needs.
        if self
            .entries
            .back()
            .is_some_and(|last| last.stamp == found.stamp)
            && found.value == value
        {
            return self.clone();
        }

        let position = self.index_of_stamp(found.stamp);
        let entries = self
            .entries
            .remove_at(position)
            .expect("stamp position is valid");
        let index = self.index.remove(&key);
        self.with_entry_inserted(entries, index, self.len() - 1, key, value)
    }

    #[must_use]
    pub fn prepend(&self, key: K, value: V) -> Self {
        if let Some(found) = self.index.get(&key) {
            // Mirrors append: the front-stamp comparison keeps the no-op
            // fast path O(1); only the removal path pays the search.
            if self
                .entries
                .front()
                .is_some_and(|first| first.stamp == found.stamp)
                && found.value == value
            {
                return self.clone();
            }

            let position = self.index_of_stamp(found.stamp);
            let entries = self
                .entries
                .remove_at(position)
                .expect("stamp position is valid");
            let index = self.index.remove(&key);
            return self.with_entry_inserted(entries, index, 0, key, value);
        }

        self.with_entry_inserted(self.entries.clone(), self.index.clone(), 0, key, value)
    }

    #[must_use]
    pub fn insert(&self, index: usize, key: K, value: V) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        let mut entries = self.entries.clone();
        let mut index_side = self.index.clone();
        let mut insert_at = index;
        if let Some(found) = index_side.get(&key) {
            let old_position = self.index_of_stamp(found.stamp);
            entries = entries
                .remove_at(old_position)
                .expect("stamp recorded in the index refers to a valid position");
            index_side = index_side.remove(&key);
            if old_position < insert_at {
                insert_at -= 1;
            }
        }

        Some(self.with_entry_inserted(entries, index_side, insert_at, key, value))
    }

    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        self.try_remove(key)
            .map_or_else(|| self.clone(), |(result, _)| result)
    }

    #[must_use]
    pub fn try_remove(&self, key: &K) -> Option<(Self, V)> {
        let (index, removed) = self.index.try_remove(key)?;
        let entries = self.entries.remove_at(self.index_of_stamp(removed.stamp))?;
        let result = if entries.is_empty() {
            Self::with_hasher(self.hasher().clone())
        } else {
            self.make(entries, index)
        };
        Some((result, removed.value))
    }

    #[must_use]
    pub fn remove_range<I>(&self, keys: I) -> Self
    where
        I: IntoIterator<Item = K>,
    {
        keys.into_iter()
            .fold(self.clone(), |current, key| current.remove(&key))
    }

    #[must_use]
    pub fn key_take<I>(&self, keys: I) -> Self
    where
        I: IntoIterator<Item = K>,
    {
        let mut result = Self::with_hasher(self.hasher().clone());
        for key in keys {
            if result.contains_key(&key) {
                continue;
            }

            if let Some((stored_key, slot)) = self.index.get_key_value(&key) {
                result = result.append_new(stored_key.clone(), slot.value.clone());
            }
        }

        result
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        let removed = self.entries.get(index)?;
        let entries = self.entries.remove_at(index)?;
        let index_side = self.index.remove(removed.key());
        Some(if entries.is_empty() {
            Self::with_hasher(self.hasher().clone())
        } else {
            self.make(entries, index_side)
        })
    }

    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        self.remove_at(0)
    }

    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        self.len()
            .checked_sub(1)
            .and_then(|index| self.remove_at(index))
    }

    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        let split = self.entries.split_range(index, count)?;
        let kept = split.range;
        if kept.len() == self.len() {
            return Some(self.clone());
        }

        if kept.is_empty() {
            return Some(Self::with_hasher(self.hasher().clone()));
        }

        let removed_count = self.len() - kept.len();
        let mut index_side = PersistentHashMap::with_hasher(self.hasher().clone());
        if kept.len() <= removed_count {
            for entry in &kept {
                index_side = index_side.insert(
                    entry.key().clone(),
                    Slot {
                        stamp: entry.stamp,
                        value: entry.value().clone(),
                    },
                );
            }
        } else {
            index_side = self.index.clone();
            for entry in &split.before {
                index_side = index_side.remove(entry.key());
            }

            for entry in &split.after {
                index_side = index_side.remove(entry.key());
            }
        }

        Some(self.make(kept, index_side))
    }

    #[must_use]
    pub fn take(&self, count: usize) -> Option<Self> {
        self.get_range(0, count)
    }

    #[must_use]
    pub fn drop(&self, count: usize) -> Option<Self> {
        (count <= self.len())
            .then(|| self.get_range(count, self.len() - count))
            .flatten()
    }

    #[must_use]
    pub fn reverse(&self) -> Self {
        if self.len() <= 1 {
            return self.clone();
        }

        let mut ordered = self.entries.to_vec();
        ordered.reverse();
        self.rebuilt(ordered)
    }

    #[must_use]
    pub fn key_sort(&self) -> Self
    where
        K: Ord,
    {
        if self.len() <= 1 {
            return self.clone();
        }

        let mut ordered = self.entries.to_vec();
        ordered.sort_by(|left, right| left.key().cmp(right.key()));
        self.rebuilt(ordered)
    }

    #[must_use]
    pub fn sort(&self) -> Self
    where
        V: Ord,
    {
        if self.len() <= 1 {
            return self.clone();
        }

        let mut ordered = self.entries.to_vec();
        ordered.sort_by(|left, right| left.value().cmp(right.value()));
        self.rebuilt(ordered)
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<(K, V)> {
        self.iter()
            .map(|(key, value)| (key.clone(), value.clone()))
            .collect()
    }

    #[must_use]
    pub fn keys(&self) -> Vec<K> {
        self.iter().map(|(key, _)| key.clone()).collect()
    }

    #[must_use]
    pub fn values(&self) -> Vec<V> {
        self.iter().map(|(_, value)| value.clone()).collect()
    }

    fn make(
        &self,
        entries: PersistentDeque<Entry<K, V>>,
        index: PersistentHashMap<K, Slot<V>, S>,
    ) -> Self {
        debug_assert_eq!(entries.len(), index.len());
        Self { entries, index }
    }

    fn append_new(&self, key: K, value: V) -> Self {
        self.with_entry_inserted(
            self.entries.clone(),
            self.index.clone(),
            self.entries.len(),
            key,
            value,
        )
    }

    fn with_entry_inserted(
        &self,
        entries: PersistentDeque<Entry<K, V>>,
        index: PersistentHashMap<K, Slot<V>, S>,
        insert_at: usize,
        key: K,
        value: V,
    ) -> Self {
        let Some(stamp) = try_pick_stamp(&entries, insert_at) else {
            return self.relabeled(entries, insert_at, Entry::real(0, key, value));
        };

        let inserted = Entry::real(stamp, key.clone(), value.clone());
        let grown = if insert_at == entries.len() {
            entries.push_back(inserted)
        } else if insert_at == 0 {
            entries.push_front(inserted)
        } else {
            entries
                .insert_at(insert_at, inserted)
                .expect("validated association insert position")
        };
        self.make(grown, index.insert(key, Slot { stamp, value }))
    }

    fn relabeled(
        &self,
        entries: PersistentDeque<Entry<K, V>>,
        insert_at: usize,
        inserted: Entry<K, V>,
    ) -> Self {
        let mut ordered = Vec::with_capacity(entries.len() + 1);
        for (index, entry) in entries.iter().enumerate() {
            if index == insert_at {
                ordered.push(inserted.clone());
            }

            ordered.push(entry.clone());
        }

        if insert_at == entries.len() {
            ordered.push(inserted);
        }

        self.rebuilt(ordered)
    }

    fn rebuilt(&self, mut ordered: Vec<Entry<K, V>>) -> Self {
        let mut index = PersistentHashMap::with_hasher(self.hasher().clone());
        for (position, entry) in ordered.iter_mut().enumerate() {
            let stamp = i64::try_from(position)
                .ok()
                .and_then(|value| value.checked_mul(STAMP_GAP))
                .expect("association is too large to relabel with i64 stamps");
            *entry = Entry::real(stamp, entry.key().clone(), entry.value().clone());
            index = index.insert(
                entry.key().clone(),
                Slot {
                    stamp,
                    value: entry.value().clone(),
                },
            );
        }

        self.make(ordered.into_iter().collect(), index)
    }

    fn index_of_stamp(&self, stamp: i64) -> usize {
        self.entries
            .sorted_binary_search(&Entry::probe(stamp))
            .expect("stamp recorded in keyed index must exist in association order")
    }
}

impl<K, V, S> Default for PersistentAssociation<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone + Default,
{
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V> FromIterator<(K, V)> for PersistentAssociation<K, V, RandomState>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
{
    fn from_iter<I: IntoIterator<Item = (K, V)>>(iter: I) -> Self {
        PersistentAssociation::new().set_items(iter)
    }
}

impl<'a, K, V, S> IntoIterator for &'a PersistentAssociation<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    type Item = (&'a K, &'a V);
    type IntoIter = Box<dyn Iterator<Item = Self::Item> + 'a>;

    fn into_iter(self) -> Self::IntoIter {
        Box::new(self.iter())
    }
}

fn try_pick_stamp<K, V>(entries: &PersistentDeque<Entry<K, V>>, insert_at: usize) -> Option<i64> {
    if entries.is_empty() {
        return Some(0);
    }

    if insert_at == 0 {
        let first = entries.front()?.stamp;
        return first.checked_sub(STAMP_GAP);
    }

    if insert_at == entries.len() {
        let last = entries.back()?.stamp;
        return last.checked_add(STAMP_GAP);
    }

    let left = entries.get(insert_at - 1)?.stamp;
    let right = entries.get(insert_at)?.stamp;
    if left.checked_add(1).is_none_or(|next| next >= right) {
        return None;
    }

    // Floor midpoint, matching the C# reference (left + (gap >> 1)); a
    // truncating (left + right) / 2 would round toward zero for negative
    // sums and desynchronize relabel timing across ports.
    Some((left as i128 + ((right as i128 - left as i128) >> 1)) as i64)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::thread;

    fn assert_assoc_eq<K, V, S>(assoc: &PersistentAssociation<K, V, S>, expected: &[(K, V)])
    where
        K: Eq + Hash + Clone + std::fmt::Debug + PartialEq,
        V: Clone + PartialEq + std::fmt::Debug,
        S: BuildHasher + Clone,
    {
        assert_eq!(assoc.to_vec(), expected);
        assert_eq!(
            assoc.keys(),
            expected
                .iter()
                .map(|(key, _)| key.clone())
                .collect::<Vec<_>>()
        );
        assert_eq!(
            assoc.values(),
            expected
                .iter()
                .map(|(_, value)| value.clone())
                .collect::<Vec<_>>()
        );
        for (index, (key, value)) in expected.iter().enumerate() {
            assert_eq!(assoc.get_at(index), Some((key, value)));
            assert_eq!(assoc.index_of_key(key), Some(index));
            assert_eq!(assoc.get(key), Some(value));
        }
    }

    #[test]
    fn list_examples_match_vector_model() {
        let list: PersistentList<_> = [1, 2, 3].into_iter().collect();
        assert_eq!(list.front(), Some(&1));
        assert_eq!(list.back(), Some(&3));
        assert_eq!(list.get(1), Some(&2));

        assert_eq!(list.append(4).to_vec(), vec![1, 2, 3, 4]);
        assert_eq!(list.prepend(0).to_vec(), vec![0, 1, 2, 3]);
        assert_eq!(list.insert(1, 9).unwrap().to_vec(), vec![1, 9, 2, 3]);
        assert_eq!(list.remove_at(1).unwrap().to_vec(), vec![1, 3]);
        assert_eq!(list.set_item(1, 8).unwrap().to_vec(), vec![1, 8, 3]);
        assert_eq!(list.take(2).unwrap().to_vec(), vec![1, 2]);
        assert_eq!(list.drop(1).unwrap().to_vec(), vec![2, 3]);
        assert_eq!(list.reverse().to_vec(), vec![3, 2, 1]);
        assert_eq!(list.map(|value| value * 10).to_vec(), vec![10, 20, 30]);
        assert_eq!(list.index_of(&2), Some(1));
        assert!(list.contains(&3));
    }

    #[test]
    fn list_generated_histories_preserve_snapshots() {
        let mut state = 0x051A_7E57_u64;
        let mut list = PersistentList::new();
        let mut model = Vec::new();
        let mut snapshots = Vec::new();

        for step in 0..500 {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
            let op = ((state >> 32) % 10) as usize;
            let value = step - 250;
            match op {
                0 => {
                    list = list.append(value);
                    model.push(value);
                }
                1 => {
                    list = list.prepend(value);
                    model.insert(0, value);
                }
                2 => {
                    let index = ((state >> 24) as usize) % (model.len() + 1);
                    list = list.insert(index, value).unwrap();
                    model.insert(index, value);
                }
                3 if !model.is_empty() => {
                    let index = ((state >> 24) as usize) % model.len();
                    list = list.remove_at(index).unwrap();
                    model.remove(index);
                }
                4 if !model.is_empty() => {
                    let index = ((state >> 24) as usize) % model.len();
                    list = list.set_item(index, value).unwrap();
                    model[index] = value;
                }
                5 => {
                    let count = ((state >> 24) as usize) % (model.len() + 1);
                    list = list.take(count).unwrap();
                    model.truncate(count);
                }
                6 => {
                    let count = ((state >> 24) as usize) % (model.len() + 1);
                    list = list.drop(count).unwrap();
                    model.drain(0..count);
                }
                7 => {
                    list = list.reverse();
                    model.reverse();
                }
                8 => {
                    let suffix: PersistentList<_> = [value, value + 1].into_iter().collect();
                    list = list.join(&suffix);
                    model.extend([value, value + 1]);
                }
                _ => {
                    snapshots.push((list.clone(), model.clone()));
                    if snapshots.len() > 32 {
                        snapshots.remove(0);
                    }
                }
            }

            assert_eq!(list.to_vec(), model);
        }

        for (snapshot, expected) in snapshots {
            assert_eq!(snapshot.to_vec(), expected);
        }
    }

    #[test]
    fn association_tungsten_ordering_examples() {
        let assoc: PersistentAssociation<_, _> = [
            ("a".to_string(), 1),
            ("b".to_string(), 2),
            ("a".to_string(), 3),
        ]
        .into_iter()
        .collect();
        assert_assoc_eq(&assoc, &[("a".to_string(), 3), ("b".to_string(), 2)]);

        let assoc: PersistentAssociation<_, _> = [("a".to_string(), 1), ("b".to_string(), 2)]
            .into_iter()
            .collect();
        assert_assoc_eq(
            &assoc.set_item("a".to_string(), 5),
            &[("a".to_string(), 5), ("b".to_string(), 2)],
        );
        assert_assoc_eq(
            &assoc.append("a".to_string(), 3),
            &[("b".to_string(), 2), ("a".to_string(), 3)],
        );
        assert_assoc_eq(
            &assoc.prepend("b".to_string(), 3),
            &[("b".to_string(), 3), ("a".to_string(), 1)],
        );

        let joined = assoc.join(
            &[("a".to_string(), 9), ("c".to_string(), 4)]
                .into_iter()
                .collect(),
        );
        assert_assoc_eq(
            &joined,
            &[
                ("a".to_string(), 9),
                ("b".to_string(), 2),
                ("c".to_string(), 4),
            ],
        );
    }

    #[test]
    fn association_insert_remove_slice_sort_and_key_take() {
        let assoc: PersistentAssociation<_, _> = [
            ("a".to_string(), 1),
            ("b".to_string(), 2),
            ("c".to_string(), 3),
        ]
        .into_iter()
        .collect();

        assert_assoc_eq(
            &assoc.insert(2, "a".to_string(), 9).unwrap(),
            &[
                ("b".to_string(), 2),
                ("a".to_string(), 9),
                ("c".to_string(), 3),
            ],
        );
        assert_assoc_eq(
            &assoc.remove(&"b".to_string()),
            &[("a".to_string(), 1), ("c".to_string(), 3)],
        );
        assert_assoc_eq(
            &assoc.remove_at(1).unwrap(),
            &[("a".to_string(), 1), ("c".to_string(), 3)],
        );
        assert_assoc_eq(
            &assoc.take(2).unwrap(),
            &[("a".to_string(), 1), ("b".to_string(), 2)],
        );
        assert_assoc_eq(
            &assoc.drop(1).unwrap(),
            &[("b".to_string(), 2), ("c".to_string(), 3)],
        );
        assert_assoc_eq(
            &assoc.reverse(),
            &[
                ("c".to_string(), 3),
                ("b".to_string(), 2),
                ("a".to_string(), 1),
            ],
        );
        assert_assoc_eq(
            &assoc.key_take([
                "c".to_string(),
                "missing".to_string(),
                "a".to_string(),
                "c".to_string(),
            ]),
            &[("c".to_string(), 3), ("a".to_string(), 1)],
        );

        let unsorted: PersistentAssociation<_, _> = [
            ("b".to_string(), 2),
            ("c".to_string(), 0),
            ("a".to_string(), 1),
        ]
        .into_iter()
        .collect();
        assert_assoc_eq(
            &unsorted.key_sort(),
            &[
                ("a".to_string(), 1),
                ("b".to_string(), 2),
                ("c".to_string(), 0),
            ],
        );
        assert_assoc_eq(
            &unsorted.sort(),
            &[
                ("c".to_string(), 0),
                ("a".to_string(), 1),
                ("b".to_string(), 2),
            ],
        );
    }

    #[test]
    fn association_relabel_stress_preserves_positions_and_lookups() {
        let mut assoc: PersistentAssociation<_, _> =
            [("start".to_string(), -1), ("end".to_string(), -2)]
                .into_iter()
                .collect();
        for index in 0..100 {
            assoc = assoc.insert(1, format!("k{index}"), index).unwrap();
        }

        assert_eq!(assoc.len(), 102);
        assert_eq!(assoc.get_at(0).map(|(key, _)| key.as_str()), Some("start"));
        assert_eq!(assoc.get_at(101).map(|(key, _)| key.as_str()), Some("end"));
        for index in 0..100 {
            let key = format!("k{index}");
            assert_eq!(assoc.get(&key), Some(&index));
            assert_eq!(assoc.index_of_key(&key), Some(1 + (99 - index as usize)));
        }
    }

    #[test]
    fn association_generated_histories_match_ordered_model_and_snapshots() {
        let mut state = 0x0A55_0C1A_710B_u64;
        let mut assoc = PersistentAssociation::new();
        let mut model: Vec<(i32, i32)> = Vec::new();
        let mut snapshots = Vec::new();

        for _ in 0..700 {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1);
            let op = ((state >> 32) % 11) as usize;
            let key = ((state >> 8) % 41) as i32 - 20;
            let value = ((state >> 17) % 2001) as i32 - 1000;
            let position = ((state >> 24) as usize) % (model.len() + 1);

            match op {
                0 => {
                    assoc = assoc.set_item(key, value);
                    if let Some(found) = model.iter().position(|(candidate, _)| *candidate == key) {
                        model[found] = (key, value);
                    } else {
                        model.push((key, value));
                    }
                }
                1 => {
                    assoc = assoc.append(key, value);
                    if let Some(found) = model.iter().position(|(candidate, _)| *candidate == key) {
                        model.remove(found);
                    }
                    model.push((key, value));
                }
                2 => {
                    assoc = assoc.prepend(key, value);
                    if let Some(found) = model.iter().position(|(candidate, _)| *candidate == key) {
                        model.remove(found);
                    }
                    model.insert(0, (key, value));
                }
                3 => {
                    assoc = assoc.remove(&key);
                    if let Some(found) = model.iter().position(|(candidate, _)| *candidate == key) {
                        model.remove(found);
                    }
                }
                4 if !model.is_empty() => {
                    let index = ((state >> 24) as usize) % model.len();
                    assoc = assoc.remove_at(index).unwrap();
                    model.remove(index);
                }
                5 => {
                    assoc = assoc.insert(position, key, value).unwrap();
                    let mut insert_at = position;
                    if let Some(found) = model.iter().position(|(candidate, _)| *candidate == key) {
                        model.remove(found);
                        if found < insert_at {
                            insert_at -= 1;
                        }
                    }
                    model.insert(insert_at, (key, value));
                }
                6 => {
                    let count = ((state >> 24) as usize) % (model.len() + 1);
                    assoc = assoc.take(count).unwrap();
                    model.truncate(count);
                }
                7 => {
                    let count = ((state >> 24) as usize) % (model.len() + 1);
                    assoc = assoc.drop(count).unwrap();
                    model.drain(0..count);
                }
                8 => {
                    assoc = assoc.reverse();
                    model.reverse();
                }
                9 => {
                    let other = PersistentAssociation::new()
                        .set_item(key, value)
                        .set_item(key + 1, value + 1);
                    assoc = assoc.join(&other);
                    for (other_key, other_value) in other.to_vec() {
                        if let Some(found) = model
                            .iter()
                            .position(|(candidate, _)| *candidate == other_key)
                        {
                            model[found] = (other_key, other_value);
                        } else {
                            model.push((other_key, other_value));
                        }
                    }
                }
                _ => {
                    snapshots.push((assoc.clone(), model.clone()));
                    if snapshots.len() > 32 {
                        snapshots.remove(0);
                    }
                }
            }

            assert_assoc_eq(&assoc, &model);
        }

        for (snapshot, expected) in snapshots {
            assert_assoc_eq(&snapshot, &expected);
        }
    }

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn snapshots_are_send_sync_when_contents_are() {
        assert_send_sync::<PersistentList<i32>>();
        assert_send_sync::<PersistentAssociation<i32, i32>>();
    }

    #[test]
    fn concurrent_readers_share_retained_snapshots() {
        let expected_list = (0..256).collect::<Vec<_>>();
        let list: PersistentList<_> = expected_list.iter().copied().collect();
        let expected_assoc = (0..128).map(|key| (key, -key)).collect::<Vec<_>>();
        let association: PersistentAssociation<_, _> = expected_assoc.iter().copied().collect();

        let mut handles = Vec::new();
        for _ in 0..8 {
            let expected_list = expected_list.clone();
            let expected_assoc = expected_assoc.clone();
            let list = list.clone();
            let association = association.clone();
            handles.push(thread::spawn(move || {
                for _ in 0..128 {
                    assert_eq!(list.len(), 256);
                    assert_eq!(list.get(128), Some(&128));
                    assert_eq!(list.to_vec(), expected_list);

                    assert_eq!(association.len(), 128);
                    assert_eq!(association.get(&96), Some(&-96));
                    assert_eq!(association.index_of_key(&96), Some(96));
                    assert_assoc_eq(&association, &expected_assoc);
                }
            }));
        }

        for handle in handles {
            handle.join().expect("reader thread failed");
        }
    }
}
