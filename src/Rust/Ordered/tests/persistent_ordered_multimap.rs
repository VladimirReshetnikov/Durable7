//! Tests for the persistent insertion-ordered multimap.
//!
//! Covers ordering at both levels - keys in the order they first acquired a value, values in the
//! order they were first added to their key - first-representative retention in both domains, and
//! duplicate pairs behaving as root-sharing no-ops that disturb neither ordering.

use std::sync::Arc;

use durable7_ordered::PersistentOrderedMultimap;

#[derive(Clone, Debug)]
struct Folded {
    class: Arc<str>,
    representation: Arc<str>,
}

impl Folded {
    fn new(class: &str, representation: &str) -> Self {
        Self {
            class: Arc::from(class),
            representation: Arc::from(representation),
        }
    }
}

impl PartialEq for Folded {
    fn eq(&self, other: &Self) -> bool {
        self.class.eq_ignore_ascii_case(&other.class)
    }
}

impl Eq for Folded {}

impl std::hash::Hash for Folded {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        for byte in self.class.bytes() {
            state.write_u8(byte.to_ascii_lowercase());
        }
    }
}

#[test]
fn pairs_enumerate_in_grouped_insertion_order() {
    let map = PersistentOrderedMultimap::new()
        .insert("b", 2)
        .insert("a", 9)
        .insert("b", 1)
        .insert("c", 7)
        .insert("a", 8);

    assert_eq!(map.keys().copied().collect::<Vec<_>>(), ["b", "a", "c"]);
    assert_eq!(
        map.iter()
            .map(|(key, value)| (*key, *value))
            .collect::<Vec<_>>(),
        [("b", 2), ("b", 1), ("a", 9), ("a", 8), ("c", 7)]
    );
    assert_eq!(map.key_count(), 3);
    assert_eq!(map.pair_count(), 5);
    map.validate_structure().unwrap();
}

#[test]
fn duplicate_pairs_are_root_sharing_no_ops() {
    let map = PersistentOrderedMultimap::new().insert("key", "value");
    let duplicate = map.insert("key", "value");
    let (tried, added) = map.try_insert("key", "value");

    assert!(duplicate.shares_groups_root_with(&map));
    assert!(!added);
    assert!(tried.shares_groups_root_with(&map));
}

#[test]
fn first_key_and_value_representatives_are_retained() {
    let first_key = Folded::new("key", "stored-key");
    let first_value = Folded::new("value", "stored-value");
    let map = PersistentOrderedMultimap::new()
        .insert(first_key.clone(), first_value.clone())
        .insert(
            Folded::new("KEY", "caller-key"),
            Folded::new("other", "other"),
        );

    assert!(Arc::ptr_eq(
        &map.get_key(&Folded::new("key", "probe"))
            .unwrap()
            .representation,
        &first_key.representation
    ));
    assert!(Arc::ptr_eq(
        &map.get_value(&Folded::new("KEY", "probe"), &Folded::new("VALUE", "probe"))
            .unwrap()
            .representation,
        &first_value.representation
    ));
}

#[test]
fn removing_last_pair_contracts_and_readding_appends_group() {
    let source = PersistentOrderedMultimap::new()
        .insert("a", 1)
        .insert("b", 2)
        .insert("a", 3);
    let without_a = source.remove(&"a", &1).remove(&"a", &3);
    let readded = without_a.insert("a", 4);

    assert_eq!(without_a.keys().copied().collect::<Vec<_>>(), ["b"]);
    assert_eq!(readded.keys().copied().collect::<Vec<_>>(), ["b", "a"]);
    assert_eq!(source.pair_count(), 3);
    assert!(source.contains(&"a", &1));
}

#[test]
fn whole_group_removal_updates_counts_and_preserves_miss_roots() {
    let map = PersistentOrderedMultimap::new()
        .insert(1, 10)
        .insert(1, 11)
        .insert(2, 20);
    let (reduced, key, values) = map.try_remove_key(&1).unwrap();

    assert_eq!(key, 1);
    assert_eq!(values.iter().copied().collect::<Vec<_>>(), [10, 11]);
    assert_eq!(reduced.key_count(), 1);
    assert_eq!(reduced.pair_count(), 1);
    assert!(reduced.remove_key(&1).shares_groups_root_with(&reduced));
}

#[test]
fn construction_and_retained_branches_remain_independent() {
    let root = PersistentOrderedMultimap::from_pairs([(1, "a"), (1, "a"), (2, "b")]);
    let left = root.insert(1, "c");
    let right = root.remove_key(&1).insert(3, "d");

    assert_eq!(root.pair_count(), 2);
    assert_eq!(left.pair_count(), 3);
    assert_eq!(right.pair_count(), 2);
    assert!(!root.contains(&1, &"c"));
    assert!(left.contains(&1, &"c"));
    assert!(right.contains(&3, &"d"));
    root.validate_structure().unwrap();
    left.validate_structure().unwrap();
    right.validate_structure().unwrap();
}
