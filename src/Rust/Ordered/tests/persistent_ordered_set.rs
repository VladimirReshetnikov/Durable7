use std::collections::hash_map::DefaultHasher;
use std::hash::{BuildHasher, Hash, Hasher};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
use std::thread;

use durable7_ordered::{OrderedSetMoveError, PersistentOrderedSet};

fn assert_values<T, S>(set: &PersistentOrderedSet<T, S>, expected: &[T])
where
    T: Eq + Hash + Clone + std::fmt::Debug,
    S: BuildHasher + Clone,
{
    assert_eq!(set.to_vec(), expected);
    assert_eq!(set.len(), expected.len());
    assert_eq!(set.first(), expected.first());
    assert_eq!(set.last(), expected.last());
    for (index, item) in expected.iter().enumerate() {
        assert_eq!(set.get(index), Some(item));
        assert_eq!(&set[index], item);
        assert_eq!(set.index_of(item), Some(index));
        assert!(set.contains(item));
    }
    assert_eq!(set.validate_structure().unwrap().count, expected.len());
}

#[derive(Clone, Default)]
struct ConstantState;

struct ConstantHasher;

impl Hasher for ConstantHasher {
    fn finish(&self) -> u64 {
        0
    }

    fn write(&mut self, _bytes: &[u8]) {}
}

impl BuildHasher for ConstantState {
    type Hasher = ConstantHasher;

    fn build_hasher(&self) -> Self::Hasher {
        ConstantHasher
    }
}

#[derive(Clone, Debug)]
struct Folded {
    text: Arc<str>,
}

impl Folded {
    fn new(text: &str) -> Self {
        Self {
            text: Arc::from(text),
        }
    }
}

impl PartialEq for Folded {
    fn eq(&self, other: &Self) -> bool {
        self.text.eq_ignore_ascii_case(&other.text)
    }
}

impl Eq for Folded {}

impl Hash for Folded {
    fn hash<H: Hasher>(&self, state: &mut H) {
        for byte in self.text.bytes() {
            state.write_u8(byte.to_ascii_lowercase());
        }
    }
}

#[derive(Clone)]
struct CountingState {
    builds: Arc<AtomicUsize>,
}

impl CountingState {
    fn new() -> Self {
        Self {
            builds: Arc::new(AtomicUsize::new(0)),
        }
    }
}

impl BuildHasher for CountingState {
    type Hasher = DefaultHasher;

    fn build_hasher(&self) -> Self::Hasher {
        self.builds.fetch_add(1, AtomicOrdering::SeqCst);
        DefaultHasher::new()
    }
}

#[test]
fn construction_lookup_and_duplicate_additions_preserve_order() {
    let set = PersistentOrderedSet::from_items([1, 2, 1, 3, 2]);
    assert_values(&set, &[1, 2, 3]);
    assert_eq!(set.get_stored(&2), Some(&2));
    assert_eq!(set.get_stored(&99), None);
    assert_eq!(set.index_of(&99), None);

    let appended = set.add(4);
    assert_values(&appended, &[1, 2, 3, 4]);
    let prepended = appended.add_first(0);
    assert_values(&prepended, &[0, 1, 2, 3, 4]);
    let inserted = prepended.insert(2, 9).unwrap();
    assert_values(&inserted, &[0, 1, 9, 2, 3, 4]);
    assert!(inserted.insert(inserted.len() + 1, 10).is_none());

    let duplicate = inserted.add_first(9);
    assert_values(&duplicate, &[0, 1, 9, 2, 3, 4]);
    assert!(duplicate.shares_roots_with(&inserted));
}

#[test]
fn first_representatives_survive_duplicates_movement_and_algebra() {
    let alpha = Folded::new("Alpha");
    let alpha_duplicate = Folded::new("ALPHA");
    let beta = Folded::new("Beta");
    let beta_duplicate = Folded::new("BETA");
    let set = PersistentOrderedSet::from_items_with_hasher(
        [alpha.clone(), alpha_duplicate.clone(), beta.clone()],
        ConstantState,
    );

    let actual_alpha = set.get_stored(&alpha_duplicate).unwrap();
    assert!(Arc::ptr_eq(&actual_alpha.text, &alpha.text));
    let moved = set.move_to_last(&alpha_duplicate).unwrap();
    assert!(Arc::ptr_eq(&moved.last().unwrap().text, &alpha.text));

    let union = set.union([
        alpha_duplicate.clone(),
        beta_duplicate.clone(),
        Folded::new("Gamma"),
    ]);
    assert!(Arc::ptr_eq(
        &union.get_stored(&alpha_duplicate).unwrap().text,
        &alpha.text,
    ));
    assert!(Arc::ptr_eq(
        &union.get_stored(&beta_duplicate).unwrap().text,
        &beta.text,
    ));

    let argument_only_first = Folded::new("Delta");
    let argument_only_later = Folded::new("DELTA");
    let union = set.union([argument_only_first.clone(), argument_only_later]);
    assert!(Arc::ptr_eq(
        &union.get_stored(&Folded::new("delta")).unwrap().text,
        &argument_only_first.text,
    ));
}

#[test]
fn explicit_movement_uses_final_indices_and_reports_failures() {
    let source = PersistentOrderedSet::from_items([0, 1, 2, 3, 4]);
    assert_values(&source.move_to_first(&3).unwrap(), &[3, 0, 1, 2, 4]);
    assert_values(&source.move_to_last(&1).unwrap(), &[0, 2, 3, 4, 1]);
    assert_values(&source.move_to(3, &1).unwrap(), &[0, 2, 3, 1, 4]);
    assert_values(&source.move_to(1, &3).unwrap(), &[0, 3, 1, 2, 4]);

    let same = source.move_to(2, &2).unwrap();
    assert!(same.shares_roots_with(&source));
    assert_eq!(
        source.move_to(source.len(), &99).unwrap_err(),
        OrderedSetMoveError::IndexOutOfRange,
    );
    assert_eq!(
        source.move_to_first(&99).unwrap_err(),
        OrderedSetMoveError::MissingValue,
    );
    assert_eq!(
        PersistentOrderedSet::<i32>::new()
            .move_to_last(&1)
            .unwrap_err(),
        OrderedSetMoveError::MissingValue,
    );
    assert_values(&source, &[0, 1, 2, 3, 4]);
}

#[test]
fn removal_clear_ranges_and_reverse_preserve_snapshots() {
    let source = PersistentOrderedSet::from_items(0..7);
    let snapshot = source.clone();
    assert_values(&source.remove(&3), &[0, 1, 2, 4, 5, 6]);
    assert_values(&source.remove_at(2).unwrap(), &[0, 1, 3, 4, 5, 6]);
    assert_values(&source.remove_first().unwrap(), &[1, 2, 3, 4, 5, 6]);
    assert_values(&source.remove_last().unwrap(), &[0, 1, 2, 3, 4, 5]);
    assert!(source.remove_at(source.len()).is_none());

    let miss = source.try_remove(&99);
    assert!(!miss.removed);
    assert!(miss.set.shares_roots_with(&source));
    assert_values(&source.get_range(2, 3).unwrap(), &[2, 3, 4]);
    assert_values(&source.take(3).unwrap(), &[0, 1, 2]);
    assert_values(&source.drop(4).unwrap(), &[4, 5, 6]);
    assert!(source.get_range(5, 3).is_none());
    assert!(source.take(source.len() + 1).is_none());
    assert!(source.drop(source.len() + 1).is_none());
    assert!(
        source
            .get_range(0, source.len())
            .unwrap()
            .shares_roots_with(&source)
    );
    assert_values(&source.reverse(), &[6, 5, 4, 3, 2, 1, 0]);

    let empty = source.clear();
    assert!(empty.is_empty());
    assert!(empty.clear().shares_roots_with(&empty));
    assert_values(&snapshot, &[0, 1, 2, 3, 4, 5, 6]);
}

#[derive(Clone, Debug)]
struct SortItem {
    id: i32,
    group: i32,
}

impl PartialEq for SortItem {
    fn eq(&self, other: &Self) -> bool {
        self.id == other.id
    }
}

impl Eq for SortItem {}

impl Hash for SortItem {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.id.hash(state);
    }
}

#[test]
fn sorting_is_stable_one_shot_and_shares_unchanged_roots() {
    let source = PersistentOrderedSet::from_items([
        SortItem { id: 0, group: 2 },
        SortItem { id: 1, group: 1 },
        SortItem { id: 2, group: 2 },
        SortItem { id: 3, group: 1 },
        SortItem { id: 4, group: 2 },
    ]);
    let sorted = source.sort_by(|left, right| left.group.cmp(&right.group));
    assert_eq!(
        sorted.iter().map(|item| item.id).collect::<Vec<_>>(),
        [1, 3, 0, 2, 4],
    );
    let appended = sorted.add(SortItem { id: 5, group: 0 });
    assert_eq!(appended.last().unwrap().id, 5);
    let unchanged = sorted.sort_by(|left, right| left.group.cmp(&right.group));
    assert!(unchanged.shares_roots_with(&sorted));

    let integers = PersistentOrderedSet::from_items([3, 1, 2]).sort();
    assert_values(&integers, &[1, 2, 3]);
}

#[test]
fn algebra_order_and_relations_follow_receiver_classes() {
    let receiver = PersistentOrderedSet::from_items([1, 2, 3, 4]);
    assert_values(&receiver.union([3, 5, 5, 2, 6]), &[1, 2, 3, 4, 5, 6]);
    assert_values(&receiver.intersect([4, 4, 2, 9]), &[2, 4]);
    assert_values(&receiver.except([4, 4, 2, 9]), &[1, 3]);
    assert_values(&receiver.symmetric_except([4, 4, 2, 9, 5]), &[1, 3, 9, 5]);

    assert!(receiver.is_subset_of([4, 3, 2, 1, 1, 5]));
    assert!(receiver.is_proper_subset_of([4, 3, 2, 1, 5]));
    assert!(receiver.is_superset_of([4, 2, 2]));
    assert!(receiver.is_proper_superset_of([4, 2]));
    assert!(receiver.overlaps([9, 8, 2]));
    assert!(!receiver.overlaps([9, 8]));
    assert!(receiver.set_equals([4, 3, 2, 1, 1]));
    assert!(!receiver.set_equals([4, 3, 2, 1, 5]));

    assert!(receiver.union([4, 3]).shares_roots_with(&receiver));
    assert!(
        receiver
            .intersect([4, 3, 2, 1])
            .shares_roots_with(&receiver)
    );
    assert!(
        receiver
            .except(Vec::<i32>::new())
            .shares_roots_with(&receiver)
    );
    assert!(
        receiver
            .symmetric_except(Vec::<i32>::new())
            .shares_roots_with(&receiver)
    );
}

#[test]
fn same_type_algebra_normalizes_with_the_receiver_hasher_only() {
    let receiver_state = CountingState::new();
    let argument_state = CountingState::new();
    let receiver = PersistentOrderedSet::from_items_with_hasher([1, 2, 3], receiver_state.clone());
    let argument = PersistentOrderedSet::from_items_with_hasher([3, 4], argument_state.clone());
    receiver_state.builds.store(0, AtomicOrdering::SeqCst);
    argument_state.builds.store(0, AtomicOrdering::SeqCst);

    assert_values(&receiver.union_set(&argument), &[1, 2, 3, 4]);
    assert!(receiver_state.builds.load(AtomicOrdering::SeqCst) > 0);
    assert_eq!(argument_state.builds.load(AtomicOrdering::SeqCst), 0);
    assert_values(&receiver.intersect_set(&argument), &[3]);
    assert_values(&receiver.except_set(&argument), &[1, 2]);
    assert_values(&receiver.symmetric_except_set(&argument), &[1, 2, 4]);
}

#[test]
fn full_hash_collisions_and_relabel_fallback_remain_canonical() {
    let mut collision = PersistentOrderedSet::from_items_with_hasher(0..64, ConstantState);
    for value in (0..64).step_by(3) {
        collision = collision.remove(&value);
    }
    for value in 100..140 {
        collision = collision.add_first(value);
    }
    assert_eq!(
        collision.validate_structure().unwrap().count,
        collision.len()
    );

    let mut relabeled = PersistentOrderedSet::from_items([-1, 999]);
    for value in 0..256 {
        relabeled = relabeled.insert(1, value).unwrap();
        assert_eq!(
            relabeled.validate_structure().unwrap().count,
            relabeled.len()
        );
    }
    assert_eq!(relabeled.first(), Some(&-1));
    assert_eq!(relabeled.last(), Some(&999));
    assert_eq!(relabeled.get(1), Some(&255));
    for value in 0..256 {
        assert_eq!(relabeled.index_of(&value), Some(256 - value as usize));
    }
}

struct LatePanic {
    step: usize,
}

impl Iterator for LatePanic {
    type Item = i32;

    fn next(&mut self) -> Option<Self::Item> {
        self.step += 1;
        match self.step {
            1 => Some(2),
            2 => Some(99),
            _ => panic!("late source failure"),
        }
    }
}

#[test]
fn callback_and_enumeration_failures_leave_sources_unchanged() {
    let source = PersistentOrderedSet::from_items([1, 2, 3, 4]);
    let snapshot = source.clone();
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            let _ = source.union(LatePanic { step: 0 });
        }))
        .is_err()
    );
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            let _ = source.overlaps(LatePanic { step: 0 });
        }))
        .is_err()
    );
    assert!(
        catch_unwind(AssertUnwindSafe(|| {
            let _ = source.sort_by(|left, right| {
                if *left == 3 || *right == 3 {
                    panic!("comparator failure");
                }
                left.cmp(right)
            });
        }))
        .is_err()
    );
    assert!(source.shares_roots_with(&snapshot));
    assert_values(&source, &[1, 2, 3, 4]);
}

#[test]
fn generated_histories_match_a_vector_set_model_and_preserve_snapshots() {
    let mut state = 0x0AD0_5E71_5EED_u64;
    let mut set = PersistentOrderedSet::new();
    let mut model = Vec::<i32>::new();
    let mut snapshots = Vec::new();

    for step in 0..1_000 {
        state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1);
        let operation = ((state >> 32) % 11) as usize;
        let value = ((state >> 16) % 51) as i32 - 25;
        match operation {
            0 => {
                set = set.add(value);
                if !model.contains(&value) {
                    model.push(value);
                }
            }
            1 => {
                set = set.add_first(value);
                if !model.contains(&value) {
                    model.insert(0, value);
                }
            }
            2 => {
                let index = ((state >> 8) as usize) % (model.len() + 1);
                set = set.insert(index, value).unwrap();
                if !model.contains(&value) {
                    model.insert(index, value);
                }
            }
            3 => {
                set = set.remove(&value);
                if let Some(index) = model.iter().position(|candidate| *candidate == value) {
                    model.remove(index);
                }
            }
            4 if !model.is_empty() => {
                let index = ((state >> 8) as usize) % model.len();
                set = set.remove_at(index).unwrap();
                model.remove(index);
            }
            5 if !model.is_empty() => {
                let old = ((state >> 8) as usize) % model.len();
                let final_index = ((state >> 24) as usize) % model.len();
                let moved = model[old];
                set = set.move_to(final_index, &moved).unwrap();
                model.remove(old);
                model.insert(final_index, moved);
            }
            6 => {
                let count = ((state >> 8) as usize) % (model.len() + 1);
                set = set.take(count).unwrap();
                model.truncate(count);
            }
            7 => {
                let count = ((state >> 8) as usize) % (model.len() + 1);
                set = set.drop(count).unwrap();
                model.drain(0..count);
            }
            8 => {
                set = set.reverse();
                model.reverse();
            }
            9 => {
                set = set.sort();
                model.sort();
            }
            _ => {
                snapshots.push((set.clone(), model.clone()));
                if snapshots.len() > 32 {
                    snapshots.remove(0);
                }
            }
        }

        assert_values(&set, &model);
        assert_eq!(set.validate_structure().unwrap().count, model.len());
        if step % 97 == 0 {
            snapshots.push((set.clone(), model.clone()));
        }
    }

    for (snapshot, expected) in snapshots {
        assert_values(&snapshot, &expected);
    }
}

fn assert_send_sync<T: Send + Sync>() {}

#[test]
fn published_snapshots_are_send_sync_and_support_concurrent_readers() {
    assert_send_sync::<PersistentOrderedSet<i32>>();
    let expected = (0..128).collect::<Vec<_>>();
    let set = PersistentOrderedSet::from_items(expected.clone());
    let mut handles = Vec::new();
    for _ in 0..4 {
        let set = set.clone();
        let expected = expected.clone();
        handles.push(thread::spawn(move || {
            for _ in 0..64 {
                assert_eq!(set.to_vec(), expected);
                assert_eq!(set.get(64), Some(&64));
                assert_eq!(set.index_of(&96), Some(96));
                assert_eq!(set.validate_structure().unwrap().count, 128);
            }
        }));
    }
    for handle in handles {
        handle.join().expect("reader thread failed");
    }
}
