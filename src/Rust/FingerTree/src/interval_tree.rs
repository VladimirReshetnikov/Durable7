use crate::measured::{FingerTree, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;

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
struct IntervalMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<Interval<T>> for IntervalMeasure<T>
where
    T: Ord + Clone,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &Interval<T>) -> Self::Measure {
        Some(element.high.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.max(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

type IntervalStorage<T> = FingerTree<Interval<T>, IntervalMeasure<T>>;

pub struct IntervalTree<T>
where
    T: Ord + Clone,
{
    intervals: IntervalStorage<T>,
}

impl<T> Clone for IntervalTree<T>
where
    T: Ord + Clone,
{
    fn clone(&self) -> Self {
        Self {
            intervals: self.intervals.clone(),
        }
    }
}

impl<T> fmt::Debug for IntervalTree<T>
where
    T: Ord + Clone + fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_list()
            .entries(self.intervals.iter())
            .finish()
    }
}

impl<T> PartialEq for IntervalTree<T>
where
    T: Ord + Clone + PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.intervals.len() == other.intervals.len()
            && self.intervals.iter().eq(other.intervals.iter())
    }
}

impl<T> Eq for IntervalTree<T> where T: Ord + Clone + Eq {}

impl<T> IntervalTree<T>
where
    T: Ord + Clone,
{
    fn from_storage(intervals: IntervalStorage<T>) -> Self {
        Self { intervals }
    }
}

impl<T> IntervalTree<T>
where
    T: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_storage(IntervalStorage::new())
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
        let split = self
            .intervals
            .split_at_index(index)
            .expect("computed insertion rank is valid");
        Self::from_storage(split.left.append(interval).concat(&split.right))
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

        self.remove_at_index(index)
            .expect("found interval rank is valid")
            .0
    }

    #[must_use]
    pub fn try_remove(&self, interval: &Interval<T>) -> Option<(Self, Interval<T>)> {
        let index = self
            .intervals
            .iter()
            .position(|stored| stored == interval)?;
        self.remove_at_index(index)
    }

    #[must_use]
    pub fn find_overlap(&self, probe: &Interval<T>) -> Option<&Interval<T>> {
        let index = self.first_possible_overlap_index(&probe.low)?;
        self.intervals
            .iter()
            .skip(index)
            .take_while(|interval| interval.low <= probe.high)
            .find(|interval| interval.overlaps(probe))
    }

    #[must_use]
    pub fn find_containing(&self, point: &T) -> Option<&Interval<T>> {
        let index = self.first_possible_overlap_index(point)?;
        self.intervals
            .iter()
            .skip(index)
            .take_while(|interval| interval.low <= *point)
            .find(|interval| interval.contains_point(point))
    }

    #[must_use]
    pub fn find_overlaps(&self, probe: &Interval<T>) -> Vec<Interval<T>> {
        let Some(index) = self.first_possible_overlap_index(&probe.low) else {
            return Vec::new();
        };

        self.intervals
            .iter()
            .skip(index)
            .take_while(|interval| interval.low <= probe.high)
            .filter(|interval| interval.overlaps(probe))
            .cloned()
            .collect()
    }

    #[must_use]
    pub fn count_overlaps(&self, probe: &Interval<T>) -> usize {
        let Some(index) = self.first_possible_overlap_index(&probe.low) else {
            return 0;
        };

        self.intervals
            .iter()
            .skip(index)
            .take_while(|interval| interval.low <= probe.high)
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

        Self::from_storage(IntervalStorage::from_vec(merged))
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<Interval<T>> {
        self.intervals.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.intervals.shares_storage_with(&other.intervals)
    }

    fn first_possible_overlap_index(&self, low: &T) -> Option<usize> {
        let located = self
            .intervals
            .try_locate(|max_high| max_high.as_ref().is_some_and(|high| high >= low));
        located.item.map(|_| located.index)
    }

    fn remove_at_index(&self, index: usize) -> Option<(Self, Interval<T>)> {
        let split = self.intervals.split_at_index(index)?;
        let after = split.right.split_at_index(1)?;
        let removed = after.left.front()?.clone();
        Some((Self::from_storage(split.left.concat(&after.right)), removed))
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

fn upper_bound_interval<T>(intervals: &IntervalStorage<T>, value: &Interval<T>) -> usize
where
    T: Ord + Clone,
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
    fn interval_queries_use_cached_max_high_measure() {
        let tree: IntervalTree<_> = [
            Interval::new(0, 2),
            Interval::new(10, 15),
            Interval::new(20, 30),
            Interval::new(40, 50),
        ]
        .into_iter()
        .collect();

        assert_eq!(tree.intervals.measure(), &Some(50));
        assert_eq!(tree.find_overlap(&Interval::new(16, 19)), None);
        assert_eq!(
            tree.find_overlap(&Interval::new(29, 41)),
            Some(&Interval::new(20, 30))
        );
        assert_eq!(tree.find_containing(&45), Some(&Interval::new(40, 50)));
        assert_eq!(
            tree.find_overlaps(&Interval::new(14, 42)),
            vec![
                Interval::new(10, 15),
                Interval::new(20, 30),
                Interval::new(40, 50)
            ]
        );
        tree.intervals.validate_invariants();
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
    fn interval_tree_edits_share_measured_tree_storage() {
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
        assert_eq!(inserted.intervals.measure(), &Some(1001));
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
