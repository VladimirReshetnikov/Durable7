//! Persistent min-priority queue built on a measured finger tree.
//!
//! [`PriorityQueue`] stores [`PriorityEntry`] values in insertion order and caches the *minimum*
//! priority of each subtree as the measure, so a smaller priority means earlier service. The
//! minimum is therefore the root measure: peeking its priority is O(1), locating and removing the
//! entry is a measure-directed split costing O(log n), and enqueueing is amortized O(1) at the end
//! of the sequence. Melding two queues is O(log(min(m, n))) because it is just a concatenation.
//!
//! When several entries tie for the minimum priority, the leftmost — that is, the earliest
//! enqueued — is served first.
//!
//! This is the priority-only queue. When entries must also be addressable by key, so that a
//! specific entry can be found, removed, or re-prioritized without scanning,
//! [`PrioritySearchQueue`](crate::PrioritySearchQueue) is the structure to use instead.

use crate::measured::{FingerTree, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;

/// One queued value together with the priority it is ordered by. Smaller priorities are served
/// first.
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

/// A persistent min-priority queue: the smallest priority is served first.
///
/// Every operation returns a new queue and leaves the receiver valid and unchanged, so earlier
/// versions remain usable and share structure with later ones. Entries with equal priorities are
/// served in enqueue order.
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
    /// Creates an empty queue.
    #[must_use]
    pub fn new() -> Self {
        Self {
            entries: PriorityTree::new(),
        }
    }

    /// Returns the number of queued entries. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Returns `true` when nothing is queued.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Returns a queue with `value` added at `priority`. Amortized O(1); the receiver is unchanged.
    ///
    /// Equal priorities are served in enqueue order.
    #[must_use]
    pub fn enqueue(&self, value: T, priority: P) -> Self {
        Self {
            entries: self.entries.append(PriorityEntry { value, priority }),
        }
    }

    /// Combines two queues into one holding every entry of both. O(log(min(m, n))).
    ///
    /// Among equal priorities, entries from `self` are served before those from `other`. Melding
    /// with an empty queue shares the other operand's representation instead of copying it.
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

    /// Borrows the smallest queued priority, or `None` when empty.
    ///
    /// O(1): the minimum is the tree's cached root measure, so no entry has to be located.
    #[must_use]
    pub fn peek_priority(&self) -> Option<&P> {
        self.entries.measure().as_ref()
    }

    /// Borrows the next entry to be served — its value and priority — or `None` when empty.
    ///
    /// O(log n), because unlike [`Self::peek_priority`] the entry itself must be located. Ties are
    /// broken in favor of the earliest enqueued entry.
    #[must_use]
    pub fn peek(&self) -> Option<(&T, &P)> {
        self.min_index().and_then(|index| {
            self.entries
                .get(index)
                .map(|entry| (&entry.value, &entry.priority))
        })
    }

    /// Removes the minimum-priority entry, returning it together with the remaining queue.
    ///
    /// Returns `None` when empty. The receiver is unchanged, so the pre-dequeue version stays
    /// valid. O(log n).
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

    /// Copies every entry into a vector in *enqueue* order, not priority order. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<PriorityEntry<T, P>> {
        self.entries.to_vec()
    }

    /// Returns `true` when `self` and `other` are the same version, so neither can observe a change
    /// made to the other. A representation test, not an equality test; see
    /// [`FingerTree::shares_storage_with`](crate::FingerTree::shares_storage_with).
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
    }

    /// Returns the index of the leftmost entry holding the queue's minimum priority.
    ///
    /// Works by locating the first prefix whose cached minimum already equals the whole queue's
    /// minimum, which is that entry's position. `None` only when the queue is empty.
    fn min_index(&self) -> Option<usize> {
        let minimum = self.entries.measure().as_ref()?;
        let located = self
            .entries
            .try_locate(|prefix_minimum| prefix_minimum.as_ref().is_some_and(|p| p == minimum));
        located.item.map(|_| located.index)
    }

    /// Splits the queue around the leftmost minimum-priority entry, yielding the entries before it,
    /// the entry itself, and the entries after it. `None` only when the queue is empty.
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
