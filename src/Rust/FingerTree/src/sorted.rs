use std::sync::Arc;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DuplicateKeyError;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedBag<T> {
    items: Arc<Vec<T>>,
}

impl<T> SortedBag<T>
where
    T: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Arc::new(Vec::new()),
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
    pub fn min(&self) -> Option<&T> {
        self.items.first()
    }

    #[must_use]
    pub fn max(&self) -> Option<&T> {
        self.items.last()
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
        let index = upper_bound(&self.items, &value);
        let mut next = self.items.as_ref().clone();
        next.insert(index, value);
        Self {
            items: Arc::new(next),
        }
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
        let Some(index) = self.index_of_first(value) else {
            return self.clone();
        };

        let mut next = self.items.as_ref().clone();
        next.remove(index);
        Self {
            items: Arc::new(next),
        }
    }

    #[must_use]
    pub fn remove_all(&self, value: &T) -> Self {
        let start = lower_bound(&self.items, value);
        let end = upper_bound(&self.items, value);
        if start == end {
            return self.clone();
        }

        let mut next = Vec::with_capacity(self.len() - (end - start));
        next.extend(self.items[..start].iter().cloned());
        next.extend(self.items[end..].iter().cloned());
        Self {
            items: Arc::new(next),
        }
    }

    #[must_use]
    pub fn get_range(&self, start: usize, count: usize) -> Option<Self> {
        let end = start.checked_add(count)?;
        (end <= self.len()).then(|| Self {
            items: Arc::new(self.items[start..end].to_vec()),
        })
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.as_ref().clone()
    }

    fn index_of_first(&self, value: &T) -> Option<usize> {
        let index = lower_bound(&self.items, value);
        (index < self.len() && &self.items[index] == value).then_some(index)
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
        Self {
            items: Arc::new(values),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedSet<T> {
    items: Arc<Vec<T>>,
}

impl<T> SortedSet<T>
where
    T: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Arc::new(Vec::new()),
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
    pub fn min(&self) -> Option<&T> {
        self.items.first()
    }

    #[must_use]
    pub fn max(&self) -> Option<&T> {
        self.items.last()
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
        (index < self.len() && &self.items[index] == value).then_some(index)
    }

    #[must_use]
    pub fn add(&self, value: T) -> Self {
        let index = lower_bound(&self.items, &value);
        if index < self.len() && self.items[index] == value {
            return self.clone();
        }

        let mut next = self.items.as_ref().clone();
        next.insert(index, value);
        Self {
            items: Arc::new(next),
        }
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
        let Some(index) = self.index_of(value) else {
            return self.clone();
        };

        let mut next = self.items.as_ref().clone();
        next.remove(index);
        Self {
            items: Arc::new(next),
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
        let end = start.checked_add(count)?;
        (end <= self.len()).then(|| Self {
            items: Arc::new(self.items[start..end].to_vec()),
        })
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
    pub fn overlaps(&self, other: &Self) -> bool {
        self.items.iter().any(|value| other.contains(value))
    }

    #[must_use]
    pub fn set_equals(&self, other: &Self) -> bool {
        self.items == other.items
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.as_ref().clone()
    }

    fn merge<F>(&self, other: &Self, keep: F) -> Self
    where
        F: Fn(bool, bool) -> bool,
    {
        let mut next = Vec::new();
        let mut left = 0;
        let mut right = 0;
        while left < self.len() || right < other.len() {
            match (self.items.get(left), other.items.get(right)) {
                (Some(left_value), Some(right_value)) => match left_value.cmp(right_value) {
                    std::cmp::Ordering::Less => {
                        if keep(true, false) {
                            next.push(left_value.clone());
                        }
                        left += 1;
                    }
                    std::cmp::Ordering::Greater => {
                        if keep(false, true) {
                            next.push(right_value.clone());
                        }
                        right += 1;
                    }
                    std::cmp::Ordering::Equal => {
                        if keep(true, true) {
                            next.push(left_value.clone());
                        }
                        left += 1;
                        right += 1;
                    }
                },
                (Some(left_value), None) => {
                    if keep(true, false) {
                        next.push(left_value.clone());
                    }
                    left += 1;
                }
                (None, Some(right_value)) => {
                    if keep(false, true) {
                        next.push(right_value.clone());
                    }
                    right += 1;
                }
                (None, None) => break,
            }
        }

        Self {
            items: Arc::new(next),
        }
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
        Self {
            items: Arc::new(values),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedMap<K, V> {
    entries: Arc<Vec<(K, V)>>,
}

impl<K, V> SortedMap<K, V>
where
    K: Ord + Clone,
    V: Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            entries: Arc::new(Vec::new()),
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
    pub fn contains_key(&self, key: &K) -> bool {
        self.index_of_key(key).is_some()
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.index_of_key(key).map(|index| &self.entries[index].1)
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
        (index < self.len() && &self.entries[index].0 == key).then_some(index)
    }

    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let index = lower_bound_by_key(&self.entries, &key);
        let mut next = self.entries.as_ref().clone();
        if index < next.len() && next[index].0 == key {
            next[index].1 = value;
        } else {
            next.insert(index, (key, value));
        }

        Self {
            entries: Arc::new(next),
        }
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
        if self.contains_key(&key) {
            return (self.clone(), false);
        }

        (self.set_item(key, value), true)
    }

    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let Some(index) = self.index_of_key(key) else {
            return self.clone();
        };

        let mut next = self.entries.as_ref().clone();
        next.remove(index);
        Self {
            entries: Arc::new(next),
        }
    }

    #[must_use]
    pub fn try_remove(&self, key: &K) -> Option<(Self, V)> {
        let index = self.index_of_key(key)?;
        let value = self.entries[index].1.clone();
        Some((self.remove(key), value))
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
        let end = start.checked_add(count)?;
        (end <= self.len()).then(|| Self {
            entries: Arc::new(self.entries[start..end].to_vec()),
        })
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<(K, V)> {
        self.entries.as_ref().clone()
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
        let mut map = Self::new();
        for (key, value) in iter {
            map = map.set_item(key, value);
        }

        map
    }
}

fn lower_bound<T>(items: &[T], value: &T) -> usize
where
    T: Ord,
{
    let mut low = 0;
    let mut high = items.len();
    while low < high {
        let mid = low + (high - low) / 2;
        if &items[mid] < value {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

fn upper_bound<T>(items: &[T], value: &T) -> usize
where
    T: Ord,
{
    let mut low = 0;
    let mut high = items.len();
    while low < high {
        let mid = low + (high - low) / 2;
        if &items[mid] <= value {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

fn lower_bound_by_key<K, V>(entries: &[(K, V)], key: &K) -> usize
where
    K: Ord,
{
    let mut low = 0;
    let mut high = entries.len();
    while low < high {
        let mid = low + (high - low) / 2;
        if &entries[mid].0 < key {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

fn upper_bound_by_key<K, V>(entries: &[(K, V)], key: &K) -> usize
where
    K: Ord,
{
    let mut low = 0;
    let mut high = entries.len();
    while low < high {
        let mid = low + (high - low) / 2;
        if &entries[mid].0 <= key {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sorted_bag_keeps_duplicates_and_rank_counts() {
        let bag: SortedBag<_> = [3, 1, 2, 2, 4].into_iter().collect();
        let added = bag.add(2);
        let removed = added.remove(&2);

        assert_eq!(bag.to_vec(), vec![1, 2, 2, 3, 4]);
        assert_eq!(added.count_of(&2), 3);
        assert_eq!(added.count_less_than(&3), 4);
        assert_eq!(removed.count_of(&2), 2);
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
    }

    #[test]
    fn sorted_map_is_last_wins_for_ranges() {
        let map: SortedMap<_, _> = [(2, "b"), (1, "a"), (2, "bb")].into_iter().collect();
        let inserted = map.insert(3, "c").unwrap();
        let duplicate = inserted.insert(3, "cc");

        assert_eq!(map.to_vec(), vec![(1, "a"), (2, "bb")]);
        assert_eq!(inserted.keys_to_vec(), vec![1, 2, 3]);
        assert!(matches!(duplicate, Err(DuplicateKeyError)));
        assert_eq!(inserted.floor_entry(&2), Some((&2, &"bb")));
    }
}
