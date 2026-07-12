use std::cmp::Ordering;
use std::collections::BTreeMap;
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
use std::thread;
use tools_data_structures_fingertree::{
    OrderComparer, OrderPolicy, PrioritySearchEntry, PrioritySearchQueue,
};

#[test]
fn replacement_and_equivalent_key_semantics_are_exact() {
    let priority_policy = OrderPolicy::custom(|left: &PriorityProbe, right: &PriorityProbe| {
        left.rank.cmp(&right.rank)
    });
    let key_policy = OrderPolicy::natural();
    let queue = PrioritySearchQueue::with_policies(key_policy.clone(), priority_policy.clone())
        .set_item(7, PriorityProbe::new(10, 1), String::from("payload"));
    assert!(queue.key_policy().has_same_identity(&key_policy));
    assert!(queue.priority_policy().has_same_identity(&priority_policy));

    let representation_update =
        queue.set_item(7, PriorityProbe::new(10, 2), String::from("payload"));
    assert!(!queue.shares_root_with(&representation_update));
    assert_eq!(
        representation_update.get_entry(&7).unwrap().priority(),
        &PriorityProbe::new(10, 2),
    );
    assert_eq!(queue.minimum().unwrap().priority().equality_class, 1);

    let semantic_update =
        representation_update.set_item(7, PriorityProbe::new(1, 2), String::from("payload"));
    assert!(!representation_update.shares_root_with(&semantic_update));
    assert_eq!(semantic_update.minimum().unwrap().priority().rank, 1);
    let exact_no_op =
        semantic_update.set_item(7, PriorityProbe::new(1, 2), String::from("payload"));
    assert!(semantic_update.shares_root_with(&exact_no_op));

    let case_insensitive = OrderPolicy::custom(|left: &String, right: &String| {
        left.to_ascii_lowercase().cmp(&right.to_ascii_lowercase())
    });
    let keyed = PrioritySearchQueue::with_policies(case_insensitive, OrderPolicy::natural())
        .set_item(String::from("Alpha"), 9, String::from("first"));
    let original_handle = keyed
        .get_entry(&String::from("alpha"))
        .unwrap()
        .key_handle();
    let updated = keyed.set_item(String::from("ALPHA"), 2, String::from("second"));
    let stored = updated.get_entry(&String::from("aLpHa")).unwrap();
    assert_eq!(stored.key(), "Alpha");
    assert_eq!(stored.priority(), &2);
    assert_eq!(stored.value(), "second");
    assert!(Arc::ptr_eq(&original_handle, &stored.key_handle()));

    let duplicate = updated.try_add(String::from("alpha"), 0, String::from("third"));
    assert!(!duplicate.added);
    assert!(updated.shares_root_with(&duplicate.queue));
    let removed = updated.try_remove(&String::from("ALPHA"));
    assert!(removed.removed());
    assert!(Arc::ptr_eq(
        &original_handle,
        &removed.entry.as_ref().unwrap().key_handle(),
    ));
    assert!(removed.queue.is_empty());

    queue.validate_structure().unwrap();
    representation_update.validate_structure().unwrap();
    semantic_update.validate_structure().unwrap();
    updated.validate_structure().unwrap();
}

#[test]
fn option_keys_priorities_and_payloads_have_unambiguous_results() {
    let option_order = |left: &Option<i32>, right: &Option<i32>| left.cmp(right);
    let queue = PrioritySearchQueue::with_comparers(option_order, option_order)
        .set_item(None, None, None::<String>)
        .set_item(Some(1), Some(1), Some(String::from("one")));

    let stored = queue.get_entry(&None).unwrap();
    assert_eq!(stored.key(), &None);
    assert_eq!(stored.priority(), &None);
    assert_eq!(stored.value(), &None);
    assert!(queue.get_entry(&Some(2)).is_none());
    assert_eq!(queue.minimum().unwrap().key(), &None);
    assert!(queue.shares_root_with(&queue.set_item(None, None, None)));

    let view = queue.minimum_view().unwrap();
    assert_eq!(view.entry.key(), &None);
    assert_eq!(view.entry.priority(), &None);
    assert_eq!(view.entry.value(), &None);
    assert_eq!(view.remainder.len(), 1);
    assert_eq!(view.remainder.minimum().unwrap().key(), &Some(1));

    let removed = queue.try_remove(&None);
    assert!(removed.removed());
    assert_eq!(removed.entry.as_ref().unwrap().key(), &None);
    assert_eq!(removed.queue.len(), 1);
    let absent = queue.try_remove(&Some(2));
    assert!(!absent.removed());
    assert!(queue.shares_root_with(&absent.queue));
    queue.validate_structure().unwrap();
    removed.queue.validate_structure().unwrap();
}

#[test]
fn reads_adds_and_removals_do_not_require_clone_components() {
    #[derive(Debug, Eq, Ord, PartialEq, PartialOrd)]
    struct NonClone(i32);

    let queue = PrioritySearchQueue::new()
        .try_add(NonClone(2), NonClone(5), NonClone(20))
        .queue
        .set_item(NonClone(1), NonClone(3), NonClone(10));
    assert_eq!(queue.minimum().unwrap().key().0, 1);
    assert_eq!(queue.get_entry(&NonClone(2)).unwrap().value().0, 20);
    assert_eq!(
        queue.iter().map(|entry| entry.key().0).collect::<Vec<_>>(),
        [1, 2]
    );

    let removed = queue.try_remove(&NonClone(1));
    assert_eq!(removed.entry.as_ref().unwrap().value().0, 10);
    assert_eq!(removed.queue.minimum().unwrap().key().0, 2);
    let minimum = queue.delete_minimum().unwrap();
    assert_eq!(minimum.entry.key().0, 1);
    assert_eq!(minimum.remainder.len(), 1);
}

#[test]
fn rotations_deletion_rebalancing_and_ascending_stack_safety_validate() {
    let mut insertion_order = (0_i32..2_048).collect::<Vec<_>>();
    insertion_order.sort_by_key(|key| (*key as u32).reverse_bits());
    let mut queue = PrioritySearchQueue::new();
    for (index, key) in insertion_order.into_iter().enumerate() {
        queue = queue.set_item(key, key * 37 % 101, -key);
        if index & 63 == 0 {
            queue.validate_structure().unwrap();
        }
    }
    for key in 0_i32..2_048 {
        if key % 3 != 1 {
            queue = queue.remove(&key);
            if key & 63 == 0 {
                queue.validate_structure().unwrap();
            }
        }
    }
    assert_eq!(
        queue.iter().map(|entry| *entry.key()).collect::<Vec<_>>(),
        (0_i32..2_048)
            .filter(|key| key % 3 == 1)
            .collect::<Vec<_>>(),
    );
    queue.validate_structure().unwrap();

    let mut ascending = PrioritySearchQueue::new();
    for key in 0_i32..50_000 {
        ascending = ascending.set_item(key, 50_000 - key, key);
    }
    let statistics = ascending.validate_structure().unwrap();
    assert!(statistics.height < 32);
    assert_eq!(ascending.minimum().unwrap().key(), &49_999);
    assert_eq!(ascending.len(), 50_000);
}

#[test]
fn randomized_retained_history_matches_btree_model() {
    let mut random = Lcg::new(0x5a17_2026);
    let mut queue = PrioritySearchQueue::new();
    let mut model = BTreeMap::<i32, ModelValue>::new();
    let mut retained = Vec::new();

    for step in 0_i32..20_000 {
        let key = random.below(1_537) as i32 - 768;
        match random.below(8) {
            0..=3 => {
                let priority = random.below(64) as i32;
                queue = queue.set_item(key, priority, step);
                model.insert(
                    key,
                    ModelValue {
                        priority,
                        value: step,
                    },
                );
            }
            4 => {
                queue = queue.remove(&key);
                model.remove(&key);
            }
            5 => {
                let priority = random.below(64) as i32;
                let expected_added = !model.contains_key(&key);
                let result = queue.try_add(key, priority, step);
                assert_eq!(result.added, expected_added);
                queue = result.queue;
                if expected_added {
                    model.insert(
                        key,
                        ModelValue {
                            priority,
                            value: step,
                        },
                    );
                }
            }
            _ if model.is_empty() => {
                let priority = random.below(64) as i32;
                queue = queue.set_item(key, priority, step);
                model.insert(
                    key,
                    ModelValue {
                        priority,
                        value: step,
                    },
                );
            }
            _ => {
                let (&expected_key, expected) = model
                    .iter()
                    .min_by_key(|(key, value)| (value.priority, **key))
                    .unwrap();
                let expected = *expected;
                let view = queue.delete_minimum().unwrap();
                assert_entry(&view.entry, expected_key, expected.priority, expected.value);
                queue = view.remainder;
                model.remove(&expected_key);
            }
        }

        if step % 127 == 0 {
            assert_model(&queue, &model);
        }
        if step % 239 == 0 {
            retained.push((queue.clone(), model_entries(&model)));
        }
    }

    assert_model(&queue, &model);
    for (snapshot, expected) in retained {
        assert_eq!(entry_tuples(&snapshot), expected);
        snapshot.validate_structure().unwrap();
    }
}

#[test]
fn range_threshold_pruning_has_audited_comparison_equations() {
    let key_calls = Arc::new(AtomicUsize::new(0));
    let priority_calls = Arc::new(AtomicUsize::new(0));
    let key_policy = OrderPolicy::custom(CountingComparer::new(Arc::clone(&key_calls)));
    let priority_policy = OrderPolicy::custom(CountingComparer::new(Arc::clone(&priority_calls)));
    let mut queue = PrioritySearchQueue::with_policies(key_policy, priority_policy);
    for key in 0_i32..4_095 {
        queue = queue.set_item(key, key % 17, -key);
    }
    queue.validate_structure().unwrap();

    reset(&key_calls, &priority_calls);
    let impossible = queue
        .enumerate_at_most(&0, &4_094, &-1)
        .unwrap()
        .collect::<Vec<_>>();
    assert!(impossible.is_empty());
    assert_eq!(key_calls.load(AtomicOrdering::SeqCst), 1);
    assert_eq!(priority_calls.load(AtomicOrdering::SeqCst), 1);

    reset(&key_calls, &priority_calls);
    let exact = queue
        .enumerate_at_most(&2_047, &2_047, &16)
        .unwrap()
        .collect::<Vec<_>>();
    assert_eq!(exact.len(), 1);
    assert_entry(exact[0], 2_047, 2_047 % 17, -2_047);
    let visited = priority_calls.load(AtomicOrdering::SeqCst) - exact.len();
    assert!((1..=queue.height()).contains(&visited));
    assert_eq!(key_calls.load(AtomicOrdering::SeqCst), 1 + 2 * visited,);

    reset(&key_calls, &priority_calls);
    let mixed = queue
        .enumerate_at_most(&512, &1_024, &3)
        .unwrap()
        .map(|entry| (*entry.key(), *entry.priority(), *entry.value()))
        .collect::<Vec<_>>();
    let expected = (512_i32..=1_024)
        .filter(|key| key % 17 <= 3)
        .map(|key| (key, key % 17, -key))
        .collect::<Vec<_>>();
    assert_eq!(mixed, expected);
    assert!(queue.enumerate_at_most(&5, &4, &10).is_err());

    reset(&key_calls, &priority_calls);
    assert_eq!(queue.minimum().unwrap().key(), &0);
    assert_eq!(key_calls.load(AtomicOrdering::SeqCst), 0);
    assert_eq!(priority_calls.load(AtomicOrdering::SeqCst), 0);
    let updated = queue.set_item(2_047, -1, 42);
    assert!(key_calls.load(AtomicOrdering::SeqCst) <= queue.height());
    assert_eq!(updated.minimum().unwrap().key(), &2_047);
}

#[test]
fn no_ops_path_copying_and_ties_preserve_identity() {
    let queue = PrioritySearchQueue::from_entries(
        (0_i32..256).map(|key| PrioritySearchEntry::new(key, key + 10, -key)),
    );
    let exact_no_op = queue.set_item(127, 137, -127);
    assert!(queue.shares_root_with(&exact_no_op));
    assert!(queue.shares_root_with(&queue.remove(&-1)));
    let duplicate = queue.try_add(127, 0, 0);
    assert!(!duplicate.added);
    assert!(queue.shares_root_with(&duplicate.queue));

    let updated = queue.set_item(0, -1, 42);
    assert!(!queue.shares_root_with(&updated));
    assert!(queue.shares_node_for_key(&updated, &200));
    assert!(queue.shared_node_count(&updated) >= 240);
    assert_entry(queue.get_entry(&0).unwrap(), 0, 10, 0);
    assert_entry(updated.get_entry(&0).unwrap(), 0, -1, 42);
    queue.validate_structure().unwrap();
    updated.validate_structure().unwrap();

    let descending_keys = OrderPolicy::custom(|left: &i32, right: &i32| right.cmp(left));
    let priority_buckets = OrderPolicy::custom(|left: &PriorityProbe, right: &PriorityProbe| {
        (left.rank / 10).cmp(&(right.rank / 10))
    });
    let mut ties = PrioritySearchQueue::with_policies(descending_keys, priority_buckets);
    for key in [2, 8, 0, 6, 9, 1, 7, 3, 5, 4] {
        ties = ties.set_item(
            key,
            PriorityProbe::new(10 + key, 100 + key),
            format!("v{key}"),
        );
    }
    for expected_key in (0_i32..=9).rev() {
        assert_eq!(ties.minimum().unwrap().key(), &expected_key);
        let view = ties.delete_minimum().unwrap();
        assert_eq!(view.entry.key(), &expected_key);
        assert_eq!(view.entry.value(), &format!("v{expected_key}"));
        ties = view.remainder;
        ties.validate_structure().unwrap();
    }
    assert!(ties.is_empty());
}

#[test]
fn concurrent_readers_observe_stable_snapshots() {
    fn assert_send_sync<T: Send + Sync>() {}
    assert_send_sync::<PrioritySearchQueue<i32, i32, i32>>();

    let queue = PrioritySearchQueue::from_entries(
        (0_i32..4_096).map(|key| PrioritySearchEntry::new(key, key % 17, -key)),
    );
    let updated = queue.set_item(4_096, -1, 1);
    let mut workers = Vec::new();
    for _ in 0..8 {
        let queue = queue.clone();
        let updated = updated.clone();
        workers.push(thread::spawn(move || {
            for _ in 0..32 {
                assert_eq!(queue.len(), 4_096);
                assert_eq!(queue.minimum().unwrap().key(), &0);
                assert_eq!(updated.minimum().unwrap().key(), &4_096);
                assert_eq!(queue.iter().count(), 4_096);
                assert_eq!(
                    queue.enumerate_at_most(&512, &768, &3).unwrap().count(),
                    (512_i32..=768).filter(|key| key % 17 <= 3).count(),
                );
            }
            queue.validate_structure().unwrap();
            updated.validate_structure().unwrap();
        }));
    }
    for worker in workers {
        worker.join().unwrap();
    }
}

#[derive(Debug)]
struct PriorityProbe {
    rank: i32,
    equality_class: i32,
}

impl PriorityProbe {
    fn new(rank: i32, equality_class: i32) -> Self {
        Self {
            rank,
            equality_class,
        }
    }
}

impl PartialEq for PriorityProbe {
    fn eq(&self, other: &Self) -> bool {
        self.equality_class == other.equality_class
    }
}

impl Eq for PriorityProbe {}

struct CountingComparer {
    calls: Arc<AtomicUsize>,
}

impl CountingComparer {
    fn new(calls: Arc<AtomicUsize>) -> Self {
        Self { calls }
    }
}

impl OrderComparer<i32> for CountingComparer {
    fn compare(&self, left: &i32, right: &i32) -> Ordering {
        self.calls.fetch_add(1, AtomicOrdering::SeqCst);
        left.cmp(right)
    }
}

#[derive(Clone, Copy)]
struct ModelValue {
    priority: i32,
    value: i32,
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

fn assert_model(queue: &PrioritySearchQueue<i32, i32, i32>, model: &BTreeMap<i32, ModelValue>) {
    assert_eq!(entry_tuples(queue), model_entries(model));
    let expected_minimum = model
        .iter()
        .min_by_key(|(key, value)| (value.priority, **key));
    match (queue.minimum(), expected_minimum) {
        (None, None) => {}
        (Some(actual), Some((&key, value))) => {
            assert_entry(actual, key, value.priority, value.value);
        }
        _ => panic!("queue/model minimum mismatch"),
    }
    let statistics = queue.validate_structure().unwrap();
    assert_eq!(statistics.len, queue.len());
    assert_eq!(statistics.height, queue.height());
    assert!(statistics.maximum_absolute_balance_factor <= 1);
}

fn model_entries(model: &BTreeMap<i32, ModelValue>) -> Vec<(i32, i32, i32)> {
    model
        .iter()
        .map(|(&key, value)| (key, value.priority, value.value))
        .collect()
}

fn entry_tuples(queue: &PrioritySearchQueue<i32, i32, i32>) -> Vec<(i32, i32, i32)> {
    queue
        .iter()
        .map(|entry| (*entry.key(), *entry.priority(), *entry.value()))
        .collect()
}

fn assert_entry(entry: &PrioritySearchEntry<i32, i32, i32>, key: i32, priority: i32, value: i32) {
    assert_eq!(
        (entry.key(), entry.priority(), entry.value()),
        (&key, &priority, &value)
    );
}

fn reset(key_calls: &AtomicUsize, priority_calls: &AtomicUsize) {
    key_calls.store(0, AtomicOrdering::SeqCst);
    priority_calls.store(0, AtomicOrdering::SeqCst);
}
