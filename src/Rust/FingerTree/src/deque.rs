use std::cmp::Ordering;
use std::sync::Arc;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PersistentDeque<T> {
    items: Arc<Vec<T>>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeSplit<T> {
    pub left: PersistentDeque<T>,
    pub right: PersistentDeque<T>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeItemSplit<T> {
    pub left: PersistentDeque<T>,
    pub item: T,
    pub right: PersistentDeque<T>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeRangeSplit<T> {
    pub before: PersistentDeque<T>,
    pub range: PersistentDeque<T>,
    pub after: PersistentDeque<T>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequePop<T> {
    pub value: T,
    pub rest: PersistentDeque<T>,
}

impl<T> PersistentDeque<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Arc::new(Vec::new()),
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self {
            items: Arc::new(items),
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
        self.items.first()
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.items.last()
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.items.get(index)
    }

    pub fn iter(&self) -> std::slice::Iter<'_, T> {
        self.items.iter()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.items, &other.items)
    }
}

impl<T> Default for PersistentDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for PersistentDeque<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> IntoIterator for PersistentDeque<T>
where
    T: Clone,
{
    type Item = T;
    type IntoIter = std::vec::IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.to_vec().into_iter()
    }
}

impl<'a, T> IntoIterator for &'a PersistentDeque<T> {
    type Item = &'a T;
    type IntoIter = std::slice::Iter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T> PersistentDeque<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.as_ref().clone()
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        let mut next = Vec::with_capacity(self.len() + 1);
        next.push(item);
        next.extend(self.items.iter().cloned());
        Self::from_vec(next)
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        let mut next = self.to_vec();
        next.push(item);
        Self::from_vec(next)
    }

    #[must_use]
    pub fn add_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut next = self.to_vec();
        next.extend(items);
        Self::from_vec(next)
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        let mut next = Vec::with_capacity(self.len() + other.len());
        next.extend(self.items.iter().cloned());
        next.extend(other.items.iter().cloned());
        Self::from_vec(next)
    }

    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        (!self.is_empty()).then(|| Self::from_vec(self.items[1..].to_vec()))
    }

    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        if self.is_empty() {
            return None;
        }

        Some(Self::from_vec(self.items[..self.len() - 1].to_vec()))
    }

    #[must_use]
    pub fn pop_first(&self) -> Option<DequePop<T>> {
        let value = self.front()?.clone();
        let rest = self.remove_first().expect("non-empty deque has a tail");
        Some(DequePop { value, rest })
    }

    #[must_use]
    pub fn pop_last(&self) -> Option<DequePop<T>> {
        let value = self.back()?.clone();
        let rest = self.remove_last().expect("non-empty deque has an init");
        Some(DequePop { value, rest })
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next[index] = item;
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn update_at<F>(&self, index: usize, updater: F) -> Option<Self>
    where
        F: FnOnce(&T) -> T,
    {
        let current = self.get(index)?;
        self.set_item(index, updater(current))
    }

    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next.insert(index, item);
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        if index > self.len() {
            return None;
        }

        let inserted: Vec<T> = items.into_iter().collect();
        let mut next = Vec::with_capacity(self.len() + inserted.len());
        next.extend(self.items[..index].iter().cloned());
        next.extend(inserted);
        next.extend(self.items[index..].iter().cloned());
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next.remove(index);
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = checked_range_end(self.len(), index, count)?;
        let mut next = Vec::with_capacity(self.len() - count);
        next.extend(self.items[..index].iter().cloned());
        next.extend(self.items[end..].iter().cloned());
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = checked_range_end(self.len(), index, count)?;
        Some(Self::from_vec(self.items[index..end].to_vec()))
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<DequeSplit<T>> {
        if index > self.len() {
            return None;
        }

        Some(DequeSplit {
            left: Self::from_vec(self.items[..index].to_vec()),
            right: Self::from_vec(self.items[index..].to_vec()),
        })
    }

    #[must_use]
    pub fn split_item_at(&self, index: usize) -> Option<DequeItemSplit<T>> {
        if index >= self.len() {
            return None;
        }

        Some(DequeItemSplit {
            left: Self::from_vec(self.items[..index].to_vec()),
            item: self.items[index].clone(),
            right: Self::from_vec(self.items[index + 1..].to_vec()),
        })
    }

    #[must_use]
    pub fn split_range(&self, index: usize, count: usize) -> Option<DequeRangeSplit<T>> {
        let end = checked_range_end(self.len(), index, count)?;
        Some(DequeRangeSplit {
            before: Self::from_vec(self.items[..index].to_vec()),
            range: Self::from_vec(self.items[index..end].to_vec()),
            after: Self::from_vec(self.items[end..].to_vec()),
        })
    }
}

impl<T> PersistentDeque<T>
where
    T: Ord,
{
    #[must_use]
    pub fn sorted_lower_bound(&self, item: &T) -> usize {
        self.sorted_lower_bound_by(item, T::cmp)
    }

    #[must_use]
    pub fn sorted_upper_bound(&self, item: &T) -> usize {
        self.sorted_upper_bound_by(item, T::cmp)
    }

    pub fn sorted_binary_search(&self, item: &T) -> Result<usize, usize> {
        let index = self.sorted_lower_bound(item);
        if index < self.len() && self.items[index].cmp(item) == Ordering::Equal {
            Ok(index)
        } else {
            Err(index)
        }
    }

    #[must_use]
    pub fn sorted_contains(&self, item: &T) -> bool {
        self.sorted_binary_search(item).is_ok()
    }
}

impl<T> PersistentDeque<T> {
    #[must_use]
    pub fn sorted_lower_bound_by<F>(&self, item: &T, mut compare: F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let mut low = 0;
        let mut high = self.len();
        while low < high {
            let mid = low + (high - low) / 2;
            if compare(&self.items[mid], item) == Ordering::Less {
                low = mid + 1;
            } else {
                high = mid;
            }
        }

        low
    }

    #[must_use]
    pub fn sorted_upper_bound_by<F>(&self, item: &T, mut compare: F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let mut low = 0;
        let mut high = self.len();
        while low < high {
            let mid = low + (high - low) / 2;
            if compare(&self.items[mid], item) == Ordering::Greater {
                high = mid;
            } else {
                low = mid + 1;
            }
        }

        low
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReversibleDeque<T> {
    items: Arc<Vec<T>>,
    reversed: bool,
}

impl<T> ReversibleDeque<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: Arc::new(Vec::new()),
            reversed: false,
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self {
            items: Arc::new(items),
            reversed: false,
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
    pub fn reverse(&self) -> Self {
        Self {
            items: Arc::clone(&self.items),
            reversed: !self.reversed,
        }
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.items, &other.items)
    }

    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.get(0)
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.len().checked_sub(1).and_then(|index| self.get(index))
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len() {
            return None;
        }

        let physical = self.physical_index(index);
        self.items.get(physical)
    }

    fn physical_index(&self, logical: usize) -> usize {
        if self.reversed {
            self.len() - 1 - logical
        } else {
            logical
        }
    }
}

impl<T> Default for ReversibleDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for ReversibleDeque<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> ReversibleDeque<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        if self.reversed {
            self.items.iter().rev().cloned().collect()
        } else {
            self.items.as_ref().clone()
        }
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        let mut next = self.to_vec();
        next.insert(0, item);
        Self::from_vec(next)
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        let mut next = self.to_vec();
        next.push(item);
        Self::from_vec(next)
    }

    #[must_use]
    pub fn pop_front(&self) -> Option<DequePop<T>> {
        let value = self.front()?.clone();
        let mut next = self.to_vec();
        next.remove(0);
        Some(DequePop {
            value,
            rest: PersistentDeque::from_vec(next),
        })
    }

    #[must_use]
    pub fn pop_back(&self) -> Option<DequePop<T>> {
        let value = self.back()?.clone();
        let mut next = self.to_vec();
        next.pop();
        Some(DequePop {
            value,
            rest: PersistentDeque::from_vec(next),
        })
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next[index] = item;
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next.insert(index, item);
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let mut next = self.to_vec();
        next.remove(index);
        Some(Self::from_vec(next))
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<DequeSplit<T>> {
        PersistentDeque::from_vec(self.to_vec()).split_at(index)
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        let mut next = self.to_vec();
        next.extend(other.to_vec());
        Self::from_vec(next)
    }
}

fn checked_range_end(len: usize, index: usize, count: usize) -> Option<usize> {
    let end = index.checked_add(count)?;
    (index <= len && end <= len).then_some(end)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn deque_updates_preserve_snapshots() {
        let deque: PersistentDeque<_> = [1, 2, 3].into_iter().collect();
        let pushed = deque.push_front(0).push_back(4);
        let removed = pushed.remove_at(2).unwrap();

        assert_eq!(deque.to_vec(), vec![1, 2, 3]);
        assert_eq!(pushed.to_vec(), vec![0, 1, 2, 3, 4]);
        assert_eq!(removed.to_vec(), vec![0, 1, 3, 4]);
    }

    #[test]
    fn deque_split_and_concat_round_trip() {
        let deque: PersistentDeque<_> = (0..6).collect();
        let split = deque.split_range(2, 3).unwrap();

        assert_eq!(split.before.to_vec(), vec![0, 1]);
        assert_eq!(split.range.to_vec(), vec![2, 3, 4]);
        assert_eq!(split.after.to_vec(), vec![5]);
        assert_eq!(
            split
                .before
                .concat(&split.range)
                .concat(&split.after)
                .to_vec(),
            deque.to_vec()
        );
    }

    #[test]
    fn sorted_search_returns_first_equal() {
        let deque: PersistentDeque<_> = [1, 2, 2, 2, 5].into_iter().collect();

        assert_eq!(deque.sorted_lower_bound(&2), 1);
        assert_eq!(deque.sorted_upper_bound(&2), 4);
        assert_eq!(deque.sorted_binary_search(&2), Ok(1));
        assert_eq!(deque.sorted_binary_search(&4), Err(4));
    }

    #[test]
    fn reversible_reverse_is_storage_sharing_view() {
        let deque: ReversibleDeque<_> = [1, 2, 3].into_iter().collect();
        let reversed = deque.reverse();
        let restored = reversed.reverse();

        assert_eq!(reversed.to_vec(), vec![3, 2, 1]);
        assert_eq!(restored.to_vec(), vec![1, 2, 3]);
        assert!(deque.shares_storage_with(&reversed));
    }
}
