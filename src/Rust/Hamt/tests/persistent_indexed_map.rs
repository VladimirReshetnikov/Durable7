use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

use durable7_hamt::PersistentIndexedMap;

#[test]
fn added_rows_populate_nonunique_secondary_groups() {
    let map = PersistentIndexedMap::new(|_: &i32, value: &String| value.len())
        .add(1, "a".to_owned())
        .unwrap()
        .add(2, "bb".to_owned())
        .unwrap()
        .add(3, "cc".to_owned())
        .unwrap();

    assert_eq!(map.len(), 3);
    assert_eq!(map.index_key_count(), 2);
    assert_eq!(map.count_by_index(&2), 2);
    assert_eq!(map.get(&2).map(String::as_str), Some("bb"));
    map.validate().unwrap();
}

#[test]
fn duplicate_add_does_not_invoke_selector() {
    let calls = Arc::new(AtomicUsize::new(0));
    let observed = Arc::clone(&calls);
    let map = PersistentIndexedMap::new(move |_: &i32, value: &String| {
        observed.fetch_add(1, Ordering::SeqCst);
        value.len()
    })
    .add(1, "one".to_owned())
    .unwrap();

    assert!(map.add(1, "replacement".to_owned()).is_err());
    let (_, added) = map.try_add(1, "replacement".to_owned());
    assert!(!added);
    assert_eq!(calls.load(Ordering::SeqCst), 1);
}

#[test]
fn value_no_op_skips_selector_and_changed_value_moves_group() {
    let calls = Arc::new(AtomicUsize::new(0));
    let observed = Arc::clone(&calls);
    let source = PersistentIndexedMap::new(move |_: &&str, value: &String| {
        observed.fetch_add(1, Ordering::SeqCst);
        value.len()
    })
    .add("key", "one".to_owned())
    .unwrap();
    let unchanged = source.set_item("key", "one".to_owned());
    let changed = source.set_item("key", "longer".to_owned());

    assert!(unchanged.shares_roots_with(&source));
    assert_eq!(calls.load(Ordering::SeqCst), 2);
    assert!(!changed.contains_index_key(&3));
    assert_eq!(changed.count_by_index(&6), 1);
    assert_eq!(source.get(&"key").map(String::as_str), Some("one"));
}

#[test]
fn selector_panic_leaves_source_snapshot_intact() {
    let source = PersistentIndexedMap::new(|_: &i32, value: &String| {
        assert_ne!(value, "boom", "selector failed");
        value.len()
    })
    .add(1, "one".to_owned())
    .unwrap();

    assert!(catch_unwind(AssertUnwindSafe(|| source.set_item(1, "boom".to_owned()))).is_err());
    assert!(catch_unwind(AssertUnwindSafe(|| source.add(2, "boom".to_owned()))).is_err());
    assert_eq!(source.get(&1).map(String::as_str), Some("one"));
    assert_eq!(source.count_by_index(&3), 1);
}

#[test]
fn removal_does_not_invoke_selector_and_contracts_groups() {
    let calls = Arc::new(AtomicUsize::new(0));
    let observed = Arc::clone(&calls);
    let source = PersistentIndexedMap::new(move |_: &i32, value: &String| {
        observed.fetch_add(1, Ordering::SeqCst);
        value.len()
    })
    .add(1, "a".to_owned())
    .unwrap()
    .add(2, "b".to_owned())
    .unwrap();
    let empty = source.remove(&1).remove(&2);

    assert_eq!(calls.load(Ordering::SeqCst), 2);
    assert!(empty.is_empty());
    assert_eq!(empty.index_key_count(), 0);
    assert!(empty.remove(&99).shares_roots_with(&empty));
}

#[test]
fn construction_and_retained_branches_are_independent() {
    let root = PersistentIndexedMap::from_entries(
        [(1, "apple".to_owned()), (2, "banana".to_owned())],
        |_: &i32, value: &String| value.as_bytes()[0],
    );
    let left = root
        .set_item(1, "apricot".to_owned())
        .add(3, "avocado".to_owned())
        .unwrap();
    let right = root.remove(&1).set_item(2, "cherry".to_owned());

    assert_eq!(root.len(), 2);
    assert_eq!(left.len(), 3);
    assert_eq!(right.len(), 1);
    assert_eq!(left.count_by_index(&b'a'), 2);
    assert_eq!(right.count_by_index(&b'c'), 1);
    assert!(root.contains_index_key(&b'a'));
    assert!(root.contains_index_key(&b'b'));
    root.validate().unwrap();
    left.validate().unwrap();
    right.validate().unwrap();
}
