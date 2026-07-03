use crate::deque::PersistentDeque;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DuplicateKeyError;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedBag<T> {
    items: PersistentDeque<T>,
}

impl<T> SortedBag<T>
where
    T: Ord + Clone,
{
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
    pub fn min(&self) -> Option<&T> {
        self.items.front()
    }

    #[must_use]
    pub fn max(&self) -> Option<&T> {
        self.items.back()
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
        Self {
            items: self
                .items
                .insert_at(index, value)
                .expect("computed insertion rank is valid"),
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

        Self {
            items: self
                .items
                .remove_at(index)
                .expect("found item rank is valid"),
        }
    }

    #[must_use]
    pub fn remove_all(&self, value: &T) -> Self {
        let start = lower_bound(&self.items, value);
        let end = upper_bound(&self.items, value);
        if start == end {
            return self.clone();
        }

        Self {
            items: self
                .items
                .remove_range(start, end - start)
                .expect("computed duplicate range is valid"),
        }
    }

    #[must_use]
    pub fn get_range(&self, start: usize, count: usize) -> Option<Self> {
        self.items
            .get_range(start, count)
            .map(|items| Self { items })
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
    }

    fn index_of_first(&self, value: &T) -> Option<usize> {
        let index = lower_bound(&self.items, value);
        (index < self.len() && self.items.get(index).is_some_and(|item| item == value))
            .then_some(index)
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
            items: PersistentDeque::from_vec(values),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedSet<T> {
    items: PersistentDeque<T>,
}

impl<T> SortedSet<T>
where
    T: Ord + Clone,
{
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
    pub fn min(&self) -> Option<&T> {
        self.items.front()
    }

    #[must_use]
    pub fn max(&self) -> Option<&T> {
        self.items.back()
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
        (index < self.len() && self.items.get(index).is_some_and(|item| item == value))
            .then_some(index)
    }

    #[must_use]
    pub fn add(&self, value: T) -> Self {
        let index = lower_bound(&self.items, &value);
        if index < self.len() && self.items.get(index).is_some_and(|item| item == &value) {
            return self.clone();
        }

        Self {
            items: self
                .items
                .insert_at(index, value)
                .expect("computed insertion rank is valid"),
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

        Self {
            items: self
                .items
                .remove_at(index)
                .expect("found item rank is valid"),
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
        self.items
            .get_range(start, count)
            .map(|items| Self { items })
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
        self.items.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
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
            items: PersistentDeque::from_vec(next),
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
            items: PersistentDeque::from_vec(values),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SortedMap<K, V> {
    entries: PersistentDeque<(K, V)>,
}

impl<K, V> SortedMap<K, V>
where
    K: Ord + Clone,
    V: Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            entries: PersistentDeque::new(),
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
        self.index_of_key(key)
            .and_then(|index| self.entries.get(index).map(|(_, value)| value))
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
        (index < self.len()
            && self
                .entries
                .get(index)
                .is_some_and(|(stored_key, _)| stored_key == key))
        .then_some(index)
    }

    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let index = lower_bound_by_key(&self.entries, &key);
        if let Some((stored_key, _)) = self.entries.get(index)
            && stored_key == &key
        {
            return Self {
                entries: self
                    .entries
                    .set_item(index, (stored_key.clone(), value))
                    .expect("found entry rank is valid"),
            };
        }

        Self {
            entries: self
                .entries
                .insert_at(index, (key, value))
                .expect("computed insertion rank is valid"),
        }
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
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

        Self {
            entries: self
                .entries
                .remove_at(index)
                .expect("found entry rank is valid"),
        }
    }

    #[must_use]
    pub fn try_remove(&self, key: &K) -> Option<(Self, V)> {
        let index = self.index_of_key(key)?;
        let value = self.entries.get(index)?.1.clone();
        Some((
            Self {
                entries: self
                    .entries
                    .remove_at(index)
                    .expect("found entry rank is valid"),
            },
            value,
        ))
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
        self.entries
            .get_range(start, count)
            .map(|entries| Self { entries })
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<(K, V)> {
        self.entries.to_vec()
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

fn lower_bound<T>(items: &PersistentDeque<T>, value: &T) -> usize
where
    T: Ord,
{
    let mut low = 0;
    let mut high = items.len();
    while low < high {
        let mid = low + (high - low) / 2;
        if items.get(mid).expect("binary search midpoint is in range") < value {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

fn upper_bound<T>(items: &PersistentDeque<T>, value: &T) -> usize
where
    T: Ord,
{
    let mut low = 0;
    let mut high = items.len();
    while low < high {
        let mid = low + (high - low) / 2;
        if items.get(mid).expect("binary search midpoint is in range") <= value {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

fn lower_bound_by_key<K, V>(entries: &PersistentDeque<(K, V)>, key: &K) -> usize
where
    K: Ord,
{
    let mut low = 0;
    let mut high = entries.len();
    while low < high {
        let mid = low + (high - low) / 2;
        let (entry_key, _) = entries
            .get(mid)
            .expect("binary search midpoint is in range");
        if entry_key < key {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    low
}

fn upper_bound_by_key<K, V>(entries: &PersistentDeque<(K, V)>, key: &K) -> usize
where
    K: Ord,
{
    let mut low = 0;
    let mut high = entries.len();
    while low < high {
        let mid = low + (high - low) / 2;
        let (entry_key, _) = entries
            .get(mid)
            .expect("binary search midpoint is in range");
        if entry_key <= key {
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
    fn sorted_bag_edits_share_underlying_deque_tree() {
        let bag: SortedBag<_> = (0..256).collect();
        let added = bag.add(128);
        let removed = bag.remove(&100);
        let removed_all = added.remove_all(&128);
        let range = bag.get_range(80, 40).unwrap();

        assert_eq!(added.count_of(&128), 2);
        assert_eq!(removed.count_of(&100), 0);
        assert_eq!(removed_all.count_of(&128), 0);
        assert_eq!(range.min(), Some(&80));
        assert_eq!(range.max(), Some(&119));
        assert!(bag.items.shared_node_count_with(&added.items) > 100);
        assert!(bag.items.shared_node_count_with(&removed.items) > 100);
        assert!(added.items.shared_node_count_with(&removed_all.items) > 100);
        assert!(bag.items.shared_node_count_with(&range.items) > 16);
        assert!(bag.items.tree_depth() < 24);
        added.items.validate_invariants();
        removed.items.validate_invariants();
        removed_all.items.validate_invariants();
        range.items.validate_invariants();
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
    fn sorted_set_edits_share_underlying_deque_tree() {
        let set: SortedSet<_> = (0..256).collect();
        let duplicate = set.add(128);
        let inserted = set.add(300);
        let removed = set.remove(&100);
        let range = set.get_range(80, 40).unwrap();

        assert!(set.shares_storage_with(&duplicate));
        assert!(inserted.contains(&300));
        assert!(!removed.contains(&100));
        assert_eq!(range.to_vec(), (80..120).collect::<Vec<_>>());
        assert!(set.items.shared_node_count_with(&inserted.items) > 100);
        assert!(set.items.shared_node_count_with(&removed.items) > 100);
        assert!(set.items.shared_node_count_with(&range.items) > 16);
        assert!(set.items.tree_depth() < 24);
        inserted.items.validate_invariants();
        removed.items.validate_invariants();
        range.items.validate_invariants();
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

    #[test]
    fn sorted_map_edits_share_underlying_deque_tree() {
        let map: SortedMap<_, _> = (0..256).map(|value| (value, value * 10)).collect();
        let updated = map.set_item(128, -1);
        let inserted = map.set_item(300, -300);
        let removed = map.remove(&100);
        let range = map.get_range(80, 40).unwrap();

        assert_eq!(updated.get(&128), Some(&-1));
        assert_eq!(inserted.get(&300), Some(&-300));
        assert!(!removed.contains_key(&100));
        assert_eq!(range.min_entry(), Some((&80, &800)));
        assert_eq!(range.max_entry(), Some((&119, &1190)));
        assert!(map.entries.shared_node_count_with(&updated.entries) > 100);
        assert!(map.entries.shared_node_count_with(&inserted.entries) > 100);
        assert!(map.entries.shared_node_count_with(&removed.entries) > 100);
        assert!(map.entries.shared_node_count_with(&range.entries) > 16);
        assert!(map.entries.tree_depth() < 24);
        updated.entries.validate_invariants();
        inserted.entries.validate_invariants();
        removed.entries.validate_invariants();
        range.entries.validate_invariants();
    }
}
