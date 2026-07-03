use crate::measured::{FingerTree, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PriorityEntry<T, P> {
    pub value: T,
    pub priority: P,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct PriorityMeasure<T, P>(PhantomData<(T, P)>);

impl<T, P> MeasurePolicy<PriorityEntry<T, P>> for PriorityMeasure<T, P>
where
    P: Ord + Clone,
{
    type Measure = Option<P>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &PriorityEntry<T, P>) -> Self::Measure {
        Some(element.priority.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.min(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

type PriorityTree<T, P> = FingerTree<PriorityEntry<T, P>, PriorityMeasure<T, P>>;
type PrioritySplit<T, P> = (PriorityTree<T, P>, PriorityEntry<T, P>, PriorityTree<T, P>);

pub struct PriorityQueue<T, P>
where
    P: Ord + Clone,
{
    entries: PriorityTree<T, P>,
}

impl<T, P> Clone for PriorityQueue<T, P>
where
    P: Ord + Clone,
{
    fn clone(&self) -> Self {
        Self {
            entries: self.entries.clone(),
        }
    }
}

impl<T, P> fmt::Debug for PriorityQueue<T, P>
where
    T: fmt::Debug,
    P: Ord + Clone + fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.entries.iter()).finish()
    }
}

impl<T, P> PartialEq for PriorityQueue<T, P>
where
    T: PartialEq,
    P: Ord + Clone + PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.entries.len() == other.entries.len() && self.entries.iter().eq(other.entries.iter())
    }
}

impl<T, P> Eq for PriorityQueue<T, P>
where
    T: Eq,
    P: Ord + Clone + Eq,
{
}

impl<T, P> PriorityQueue<T, P>
where
    T: Clone,
    P: Ord + Clone,
{
    #[must_use]
    pub fn new() -> Self {
        Self {
            entries: PriorityTree::new(),
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
    pub fn enqueue(&self, value: T, priority: P) -> Self {
        Self {
            entries: self.entries.append(PriorityEntry { value, priority }),
        }
    }

    #[must_use]
    pub fn meld(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        Self {
            entries: self.entries.concat(&other.entries),
        }
    }

    #[must_use]
    pub fn peek_priority(&self) -> Option<&P> {
        self.min_index()
            .and_then(|index| self.entries.get(index).map(|entry| &entry.priority))
    }

    #[must_use]
    pub fn peek(&self) -> Option<(&T, &P)> {
        self.min_index().and_then(|index| {
            self.entries
                .get(index)
                .map(|entry| (&entry.value, &entry.priority))
        })
    }

    #[must_use]
    pub fn dequeue(&self) -> Option<(PriorityEntry<T, P>, Self)> {
        let (left, entry, right) = self.split_min()?;
        Some((
            entry,
            Self {
                entries: left.concat(&right),
            },
        ))
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<PriorityEntry<T, P>> {
        self.entries.to_vec()
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
    }

    fn min_index(&self) -> Option<usize> {
        let minimum = self.entries.measure().as_ref()?;
        let located = self
            .entries
            .try_locate(|prefix_minimum| prefix_minimum.as_ref().is_some_and(|p| p == minimum));
        located.item.map(|_| located.index)
    }

    fn split_min(&self) -> Option<PrioritySplit<T, P>> {
        let minimum = self.entries.measure().as_ref()?;
        self.entries
            .try_split_find(|prefix_minimum| prefix_minimum.as_ref().is_some_and(|p| p == minimum))
    }
}

impl<T, P> Default for PriorityQueue<T, P>
where
    T: Clone,
    P: Ord + Clone,
{
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn queue_dequeues_lowest_priority_stably() {
        let queue = PriorityQueue::new()
            .enqueue("first", 2)
            .enqueue("second", 1)
            .enqueue("third", 1);

        assert_eq!(queue.peek(), Some((&"second", &1)));

        let (entry, rest) = queue.dequeue().unwrap();
        assert_eq!(entry.value, "second");
        assert_eq!(rest.peek(), Some((&"third", &1)));
        assert_eq!(queue.len(), 3);
    }

    #[test]
    fn meld_preserves_insertion_order_for_equal_priorities() {
        let left = PriorityQueue::new().enqueue("a", 1);
        let right = PriorityQueue::new().enqueue("b", 1);
        let melded = left.meld(&right);

        let (entry, rest) = melded.dequeue().unwrap();
        assert_eq!(entry.value, "a");
        assert_eq!(rest.dequeue().unwrap().0.value, "b");
    }

    #[test]
    fn priority_queue_uses_measured_tree_for_min_priority() {
        let queue = (0..256).fold(PriorityQueue::new(), |queue, value| {
            queue.enqueue(value, value)
        });
        let enqueued = queue.enqueue(999, -1);
        let melded = queue.meld(&(256..320).fold(PriorityQueue::new(), |queue, value| {
            queue.enqueue(value, value)
        }));
        let (entry, dequeued) = enqueued.dequeue().unwrap();

        assert_eq!(entry.value, 999);
        assert_eq!(dequeued.peek(), Some((&0, &0)));
        assert_eq!(melded.len(), 320);
        assert_eq!(enqueued.entries.measure(), &Some(-1));
        assert!(queue.entries.shared_node_count_with(&enqueued.entries) > 100);
        assert!(queue.entries.shared_node_count_with(&melded.entries) > 100);
        assert!(enqueued.entries.shared_node_count_with(&dequeued.entries) > 100);
        assert!(queue.entries.tree_depth() < 24);
        enqueued.entries.validate_invariants();
        melded.entries.validate_invariants();
        dequeued.entries.validate_invariants();
    }
}
