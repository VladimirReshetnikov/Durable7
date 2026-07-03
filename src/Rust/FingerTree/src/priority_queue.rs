use crate::deque::PersistentDeque;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PriorityEntry<T, P> {
    pub value: T,
    pub priority: P,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PriorityQueue<T, P> {
    entries: PersistentDeque<PriorityEntry<T, P>>,
}

impl<T, P> PriorityQueue<T, P>
where
    T: Clone,
    P: Ord + Clone,
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
    pub fn enqueue(&self, value: T, priority: P) -> Self {
        Self {
            entries: self.entries.push_back(PriorityEntry { value, priority }),
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
        let index = self.min_index()?;
        let entry = self.entries.get(index)?.clone();
        Some((
            entry,
            Self {
                entries: self.entries.remove_at(index)?,
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
        let mut best: Option<usize> = None;
        for (index, entry) in self.entries.iter().enumerate() {
            if best.is_none_or(|best_index| {
                entry.priority
                    < self
                        .entries
                        .get(best_index)
                        .expect("known best index is in range")
                        .priority
            }) {
                best = Some(index);
            }
        }

        best
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
    fn priority_queue_edits_share_underlying_deque_tree() {
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
        assert!(queue.entries.shared_node_count_with(&enqueued.entries) > 100);
        assert!(queue.entries.shared_node_count_with(&melded.entries) > 100);
        assert!(enqueued.entries.shared_node_count_with(&dequeued.entries) > 100);
        assert!(queue.entries.tree_depth() < 24);
        enqueued.entries.validate_invariants();
        melded.entries.validate_invariants();
        dequeued.entries.validate_invariants();
    }
}
