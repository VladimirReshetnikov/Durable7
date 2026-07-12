use std::cmp::Ordering;
use std::collections::BTreeMap;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
use std::thread;
use tools_data_structures_fingertree::{
    BrodalOkasakiHeap, NaturalOrderComparer, OrderComparer, OrderPolicy,
};

#[test]
fn adversarial_shapes_validate_and_drain_in_order() {
    const COUNT: i32 = 4_096;
    let ascending = BrodalOkasakiHeap::from_items(0..COUNT);
    let descending = BrodalOkasakiHeap::from_items((0..COUNT).rev());
    let equal = BrodalOkasakiHeap::from_items(std::iter::repeat_n(7, COUNT as usize));
    let evens = BrodalOkasakiHeap::from_items((0..COUNT / 2).map(|value| value * 2));
    let odds = BrodalOkasakiHeap::from_items((0..COUNT / 2).map(|value| value * 2 + 1));
    let melded = evens.meld(&odds).unwrap();

    for heap in [&ascending, &descending, &equal, &melded] {
        let statistics = heap.validate_structure().unwrap();
        assert_eq!(statistics.len, COUNT as usize);
        assert!(statistics.maximum_rank <= 32);
        assert!(statistics.root_forest_len <= 32);
    }
    assert_eq!(drain_i32(ascending), (0..COUNT).collect::<Vec<_>>());
    assert_eq!(drain_i32(descending), (0..COUNT).collect::<Vec<_>>());
    assert_eq!(drain_i32(equal), vec![7; COUNT as usize]);
    assert_eq!(drain_i32(melded), (0..COUNT).collect::<Vec<_>>());
}

#[test]
fn branching_randomized_history_matches_retained_multisets() {
    const OPERATIONS: usize = 20_000;
    const MAXIMUM_ELEMENTS: usize = 2_048;
    let mut random = Lcg::new(0x005b_0da1);
    let mut current = BrodalVersion::empty();
    let mut retained = vec![current.clone()];
    let mut inserts = 0;
    let mut melds = 0;
    let mut deletes = 0;

    for step in 0..OPERATIONS {
        let source = if retained.len() > 1 && random.below(5) == 0 {
            retained[random.below(retained.len())].clone()
        } else {
            current.clone()
        };
        let choice = random.below(100);
        current = if choice < 52 || source.len == 0 {
            inserts += 1;
            source.insert(random.below(257) as i32 - 128)
        } else if choice < 76 {
            deletes += 1;
            source.delete_minimum()
        } else {
            let partner = (0..12)
                .map(|_| &retained[random.below(retained.len())])
                .find(|candidate| source.len + candidate.len <= MAXIMUM_ELEMENTS)
                .cloned();
            if let Some(partner) = partner {
                melds += 1;
                source.meld(&partner)
            } else {
                deletes += 1;
                source.delete_minimum()
            }
        };

        assert_eq!(current.heap.len(), current.len);
        assert_eq!(current.heap.minimum().copied(), current.minimum());
        if step & 127 == 0 {
            current.heap.validate_structure().unwrap();
            assert_eq!(sorted_values(&current.heap), current.to_sorted_vec());
        }
        if step % 37 == 0 {
            if retained.len() < 256 {
                retained.push(current.clone());
            } else {
                let index = random.below(retained.len());
                retained[index] = current.clone();
            }
        }
    }

    assert!(inserts > 6_000);
    assert!(melds > 500);
    assert!(deletes > 2_000);
    for snapshot in retained.iter().step_by(7) {
        snapshot.heap.validate_structure().unwrap();
        assert_eq!(snapshot.heap.len(), snapshot.len);
        assert_eq!(sorted_values(&snapshot.heap), snapshot.to_sorted_vec());
    }
}

#[test]
fn representatives_sharing_and_policy_identity_are_exact() {
    let policy = OrderPolicy::custom(|left: &TaggedPriority, right: &TaggedPriority| {
        left.priority.cmp(&right.priority)
    });
    let expected_tags = (0..1_024)
        .flat_map(|index| [format!("L{index}"), format!("R{index}")])
        .collect::<Vec<_>>();
    let left = BrodalOkasakiHeap::from_items_with_policy(
        (0..1_024).map(|index| TaggedPriority {
            priority: index % 7,
            tag: format!("L{index}"),
        }),
        policy.clone(),
    );
    let right = BrodalOkasakiHeap::from_items_with_policy(
        (0..1_024).map(|index| TaggedPriority {
            priority: index % 7,
            tag: format!("R{index}"),
        }),
        policy.clone(),
    );
    let mut heap = left.meld(&right).unwrap();
    let mut actual_tags = Vec::with_capacity(heap.len());
    let mut previous = 0;
    while let Some(view) = heap.minimum_view() {
        assert!(view.minimum.priority >= previous);
        previous = view.minimum.priority;
        actual_tags.push(view.minimum.tag.clone());
        heap = view.remainder;
    }
    actual_tags.sort();
    let mut expected_tags = expected_tags;
    expected_tags.sort();
    assert_eq!(actual_tags, expected_tags);

    let base = BrodalOkasakiHeap::new().insert(0).insert(10);
    let inserted = base.insert(20);
    assert!(!base.shares_root_with(&inserted));
    assert!(base.shared_tree_count(&inserted) >= 1);
    let natural_empty = BrodalOkasakiHeap::new();
    assert!(base.shares_root_with(&base.meld(&natural_empty).unwrap()));
    assert!(base.shares_root_with(&natural_empty.meld(&base).unwrap()));

    let large = BrodalOkasakiHeap::from_items((0..8_192).map(|value| value ^ 0x1555));
    let reduced = large.delete_minimum().unwrap();
    let logarithm = usize::BITS as usize - large.len().leading_zeros() as usize;
    assert!(large.shared_tree_count(&reduced) > large.len() - 32 * logarithm);
    large.validate_structure().unwrap();
    reduced.validate_structure().unwrap();

    let first = BrodalOkasakiHeap::with_comparer(NaturalOrderComparer).insert(1);
    let second = BrodalOkasakiHeap::with_comparer(NaturalOrderComparer).insert(2);
    assert!(first.meld(&second).is_err());
    let natural_first = BrodalOkasakiHeap::new().insert(1);
    let natural_second = BrodalOkasakiHeap::new().insert(2);
    assert_eq!(natural_first.meld(&natural_second).unwrap().len(), 2);

    let duplicate = natural_first.meld(&natural_first).unwrap();
    assert_eq!(drain_i32(duplicate), vec![1, 1]);
}

#[test]
fn option_values_and_owned_minimum_views_are_unambiguous() {
    let heap = BrodalOkasakiHeap::new()
        .insert(Some(1))
        .insert(None)
        .insert(Some(2));
    assert_eq!(heap.minimum(), Some(&None));

    let view = heap.minimum_view().unwrap();
    assert_eq!(view.minimum.as_ref(), &None);
    assert_eq!(view.remainder.minimum(), Some(&Some(1)));
    assert_eq!(heap.minimum(), Some(&None));
    heap.validate_structure().unwrap();
    view.remainder.validate_structure().unwrap();
}

#[test]
fn operations_respect_worst_case_comparison_ceilings() {
    let mut samples = Vec::new();
    for count in [1_024_i32, 4_096, 16_384, 65_536] {
        let comparisons = Arc::new(AtomicUsize::new(0));
        let policy = OrderPolicy::custom(CountingComparer {
            comparisons: Arc::clone(&comparisons),
        });
        let left = BrodalOkasakiHeap::from_items_with_policy(0..count, policy.clone());
        let right = BrodalOkasakiHeap::from_items_with_policy(count..count * 2, policy.clone());

        comparisons.store(0, AtomicOrdering::SeqCst);
        assert_eq!(left.minimum(), Some(&0));
        assert_eq!(comparisons.load(AtomicOrdering::SeqCst), 0);

        comparisons.store(0, AtomicOrdering::SeqCst);
        let _ = left.insert(-1);
        assert!((1..=5).contains(&comparisons.load(AtomicOrdering::SeqCst)));

        comparisons.store(0, AtomicOrdering::SeqCst);
        let _ = left.meld(&right).unwrap();
        assert!((1..=5).contains(&comparisons.load(AtomicOrdering::SeqCst)));

        comparisons.store(0, AtomicOrdering::SeqCst);
        assert_eq!(left.iter().count(), count as usize);
        assert_eq!(comparisons.load(AtomicOrdering::SeqCst), 0);

        comparisons.store(0, AtomicOrdering::SeqCst);
        let _ = left.delete_minimum().unwrap();
        let calls = comparisons.load(AtomicOrdering::SeqCst);
        let logarithm = usize::BITS as usize - (count as usize).leading_zeros() as usize;
        assert!(calls <= 32 * logarithm + 8, "{count}: {calls} comparisons");
        samples.push(calls);
    }
    assert!(samples[3] <= samples[0] + 32 * 6 + 8);
}

#[test]
fn non_clone_elements_need_no_clone_bound() {
    #[derive(Debug, Eq, Ord, PartialEq, PartialOrd)]
    struct NonClone(i32);

    let heap = BrodalOkasakiHeap::new()
        .insert(NonClone(3))
        .insert(NonClone(1))
        .insert(NonClone(2));
    assert_eq!(heap.minimum().map(|value| value.0), Some(1));
    let view = heap.minimum_view().unwrap();
    assert_eq!(view.minimum.0, 1);
    assert_eq!(view.remainder.minimum().map(|value| value.0), Some(2));
}

#[test]
fn concurrent_readers_observe_stable_snapshots() {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<BrodalOkasakiHeap<i32>>();

    let heap = BrodalOkasakiHeap::from_items(0..4_096);
    let updated = heap.insert(-1);
    let mut workers = Vec::new();
    for _ in 0..8 {
        let heap = heap.clone();
        let updated = updated.clone();
        workers.push(thread::spawn(move || {
            for _ in 0..32 {
                assert_eq!(heap.len(), 4_096);
                assert_eq!(heap.minimum(), Some(&0));
                assert_eq!(updated.minimum(), Some(&-1));
                assert_eq!(heap.iter().count(), 4_096);
            }
            heap.validate_structure().unwrap();
            updated.validate_structure().unwrap();
        }));
    }
    for worker in workers {
        worker.join().unwrap();
    }
}

#[derive(Debug)]
struct TaggedPriority {
    priority: i32,
    tag: String,
}

struct CountingComparer {
    comparisons: Arc<AtomicUsize>,
}

impl OrderComparer<i32> for CountingComparer {
    fn compare(&self, left: &i32, right: &i32) -> Ordering {
        self.comparisons.fetch_add(1, AtomicOrdering::SeqCst);
        left.cmp(right)
    }
}

#[derive(Clone)]
struct BrodalVersion {
    heap: BrodalOkasakiHeap<i32>,
    counts: BTreeMap<i32, usize>,
    len: usize,
}

impl BrodalVersion {
    fn empty() -> Self {
        Self {
            heap: BrodalOkasakiHeap::new(),
            counts: BTreeMap::new(),
            len: 0,
        }
    }

    fn minimum(&self) -> Option<i32> {
        self.counts.first_key_value().map(|(value, _)| *value)
    }

    fn insert(mut self, value: i32) -> Self {
        self.heap = self.heap.insert(value);
        *self.counts.entry(value).or_default() += 1;
        self.len += 1;
        self
    }

    fn delete_minimum(mut self) -> Self {
        let minimum = self.minimum().unwrap();
        self.heap = self.heap.delete_minimum().unwrap();
        let multiplicity = self.counts.get_mut(&minimum).unwrap();
        *multiplicity -= 1;
        if *multiplicity == 0 {
            self.counts.remove(&minimum);
        }
        self.len -= 1;
        self
    }

    fn meld(mut self, other: &Self) -> Self {
        self.heap = self.heap.meld(&other.heap).unwrap();
        for (&value, &multiplicity) in &other.counts {
            *self.counts.entry(value).or_default() += multiplicity;
        }
        self.len += other.len;
        self
    }

    fn to_sorted_vec(&self) -> Vec<i32> {
        self.counts
            .iter()
            .flat_map(|(&value, &multiplicity)| std::iter::repeat_n(value, multiplicity))
            .collect()
    }
}

struct Lcg(u64);

impl Lcg {
    fn new(seed: u64) -> Self {
        Self(seed)
    }

    fn below(&mut self, exclusive_maximum: usize) -> usize {
        self.0 = self
            .0
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        ((self.0 >> 32) as usize) % exclusive_maximum
    }
}

fn sorted_values(heap: &BrodalOkasakiHeap<i32>) -> Vec<i32> {
    let mut values = heap.iter().copied().collect::<Vec<_>>();
    values.sort_unstable();
    values
}

fn drain_i32(mut heap: BrodalOkasakiHeap<i32>) -> Vec<i32> {
    let mut result = Vec::with_capacity(heap.len());
    while let Some(view) = heap.minimum_view() {
        result.push(*view.minimum);
        heap = view.remainder;
        if result.len() & 255 == 0 {
            heap.validate_structure().unwrap();
        }
    }
    result
}
