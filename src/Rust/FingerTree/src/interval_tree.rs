use crate::deque::PersistentDeque;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Interval<T> {
    pub low: T,
    pub high: T,
}

impl<T> Interval<T>
where
    T: Ord,
{
    #[must_use]
    pub fn new(low: T, high: T) -> Self {
        assert!(
            low <= high,
            "interval low endpoint must not exceed high endpoint"
        );
        Self { low, high }
    }

    #[must_use]
    pub fn overlaps(&self, other: &Self) -> bool {
        self.low <= other.high && other.low <= self.high
    }

    #[must_use]
    pub fn contains_point(&self, point: &T) -> bool {
        self.low <= *point && *point <= self.high
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IntervalTree<T> {
    intervals: PersistentDeque<Interval<T>>,
}

impl<T> IntervalTree<T>
where
    T: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            intervals: PersistentDeque::new(),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.intervals.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.intervals.is_empty()
    }

    #[must_use]
    pub fn insert(&self, interval: Interval<T>) -> Self {
        let index = upper_bound_interval(&self.intervals, &interval);
        Self {
            intervals: self
                .intervals
                .insert_at(index, interval)
                .expect("computed insertion rank is valid"),
        }
    }

    #[must_use]
    pub fn contains(&self, interval: &Interval<T>) -> bool {
        self.intervals.iter().any(|stored| stored == interval)
    }

    #[must_use]
    pub fn remove(&self, interval: &Interval<T>) -> Self {
        let Some(index) = self.intervals.iter().position(|stored| stored == interval) else {
            return self.clone();
        };

        Self {
            intervals: self
                .intervals
                .remove_at(index)
                .expect("found interval rank is valid"),
        }
    }

    #[must_use]
    pub fn try_remove(&self, interval: &Interval<T>) -> Option<(Self, Interval<T>)> {
        let index = self
            .intervals
            .iter()
            .position(|stored| stored == interval)?;
        let removed = self.intervals.get(index)?.clone();
        Some((
            Self {
                intervals: self.intervals.remove_at(index)?,
            },
            removed,
        ))
    }

    #[must_use]
    pub fn find_overlap(&self, probe: &Interval<T>) -> Option<&Interval<T>> {
        self.intervals
            .iter()
            .find(|interval| interval.overlaps(probe))
    }

    #[must_use]
    pub fn find_containing(&self, point: &T) -> Option<&Interval<T>> {
        self.intervals
            .iter()
            .find(|interval| interval.contains_point(point))
    }

    #[must_use]
    pub fn find_overlaps(&self, probe: &Interval<T>) -> Vec<Interval<T>> {
        self.intervals
            .iter()
            .filter(|interval| interval.overlaps(probe))
            .cloned()
            .collect()
    }

    #[must_use]
    pub fn count_overlaps(&self, probe: &Interval<T>) -> usize {
        self.intervals
            .iter()
            .filter(|interval| interval.overlaps(probe))
            .count()
    }

    #[must_use]
    pub fn coalesce(&self) -> Self {
        let mut iter = self.intervals.iter();
        let Some(first) = iter.next() else {
            return self.clone();
        };

        let mut merged = Vec::new();
        let mut current = first.clone();
        for interval in iter {
            if current.overlaps(interval) || current.high >= interval.low {
                if interval.high > current.high {
                    current.high = interval.high.clone();
                }
            } else {
                merged.push(current);
                current = interval.clone();
            }
        }
        merged.push(current);

        Self {
            intervals: PersistentDeque::from_vec(merged),
        }
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<Interval<T>> {
        self.intervals.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.intervals.shares_storage_with(&other.intervals)
    }
}

impl<T> Default for IntervalTree<T>
where
    T: Ord + Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<Interval<T>> for IntervalTree<T>
where
    T: Ord + Clone,
{
    fn from_iter<I: IntoIterator<Item = Interval<T>>>(iter: I) -> Self {
        let mut tree = Self::new();
        for interval in iter {
            tree = tree.insert(interval);
        }

        tree
    }
}

fn upper_bound_interval<T>(intervals: &PersistentDeque<Interval<T>>, value: &Interval<T>) -> usize
where
    T: Ord,
{
    let mut low = 0;
    let mut high = intervals.len();
    while low < high {
        let mid = low + (high - low) / 2;
        let current = intervals
            .get(mid)
            .expect("binary search midpoint is in range");
        if (&current.low, &current.high) <= (&value.low, &value.high) {
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
    fn interval_queries_use_closed_overlap() {
        let tree: IntervalTree<_> = [
            Interval::new(10, 20),
            Interval::new(1, 5),
            Interval::new(30, 40),
        ]
        .into_iter()
        .collect();

        assert_eq!(tree.find_overlap(&Interval::new(5, 10)).unwrap().low, 1);
        assert_eq!(tree.find_containing(&35).unwrap().low, 30);
        assert_eq!(tree.count_overlaps(&Interval::new(4, 31)), 3);
    }

    #[test]
    fn coalesce_merges_overlapping_intervals() {
        let tree: IntervalTree<_> = [
            Interval::new(1, 3),
            Interval::new(2, 5),
            Interval::new(10, 12),
            Interval::new(12, 13),
        ]
        .into_iter()
        .collect();

        assert_eq!(
            tree.coalesce().to_vec(),
            vec![Interval::new(1, 5), Interval::new(10, 13)]
        );
    }

    #[test]
    fn interval_tree_edits_share_underlying_deque_tree() {
        let tree: IntervalTree<_> = (0..256)
            .map(|value| Interval::new(value * 3, value * 3 + 1))
            .collect();
        let inserted = tree.insert(Interval::new(1000, 1001));
        let removed = inserted.remove(&Interval::new(300, 301));
        let (try_removed, removed_interval) =
            inserted.try_remove(&Interval::new(303, 304)).unwrap();

        assert!(inserted.contains(&Interval::new(1000, 1001)));
        assert!(!removed.contains(&Interval::new(300, 301)));
        assert_eq!(removed_interval, Interval::new(303, 304));
        assert!(tree.intervals.shared_node_count_with(&inserted.intervals) > 100);
        assert!(
            inserted
                .intervals
                .shared_node_count_with(&removed.intervals)
                > 100
        );
        assert!(
            inserted
                .intervals
                .shared_node_count_with(&try_removed.intervals)
                > 100
        );
        assert!(tree.intervals.tree_depth() < 24);
        inserted.intervals.validate_invariants();
        removed.intervals.validate_invariants();
        try_removed.intervals.validate_invariants();
    }
}
