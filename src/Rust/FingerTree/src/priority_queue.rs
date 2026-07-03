use std::sync::Arc;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PriorityEntry<T, P> {
    pub value: T,
    pub priority: P,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PriorityQueue<T, P> {
    entries: Arc<Vec<PriorityEntry<T, P>>>,
}

impl<T, P> PriorityQueue<T, P>
where
    T: Clone,
    P: Ord + Clone,
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
    pub fn enqueue(&self, value: T, priority: P) -> Self {
        let mut next = self.entries.as_ref().clone();
        next.push(PriorityEntry { value, priority });
        Self {
            entries: Arc::new(next),
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

        let mut next = Vec::with_capacity(self.len() + other.len());
        next.extend(self.entries.iter().cloned());
        next.extend(other.entries.iter().cloned());
        Self {
            entries: Arc::new(next),
        }
    }

    #[must_use]
    pub fn peek_priority(&self) -> Option<&P> {
        self.min_index().map(|index| &self.entries[index].priority)
    }

    #[must_use]
    pub fn peek(&self) -> Option<(&T, &P)> {
        self.min_index()
            .map(|index| (&self.entries[index].value, &self.entries[index].priority))
    }

    #[must_use]
    pub fn dequeue(&self) -> Option<(PriorityEntry<T, P>, Self)> {
        let index = self.min_index()?;
        let mut next = self.entries.as_ref().clone();
        let entry = next.remove(index);
        Some((
            entry,
            Self {
                entries: Arc::new(next),
            },
        ))
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<PriorityEntry<T, P>> {
        self.entries.as_ref().clone()
    }

    fn min_index(&self) -> Option<usize> {
        let mut best: Option<usize> = None;
        for (index, entry) in self.entries.iter().enumerate() {
            if best.is_none_or(|best_index| entry.priority < self.entries[best_index].priority) {
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
}
