//! Tests for the persistent insertion-ordered map.
//!
//! Covers the identity-versus-position split: construction retains the first key representative
//! and the last distinct value, re-adding an existing key keeps its position while replacing the
//! payload, and explicit movement preserves payloads. Also covers strict add, movement failures
//! for missing keys and out-of-range positions, and persistence of earlier versions across
//! removal, range operations, reversal, and stable sorting.

use durable7_ordered::{OrderedMapMoveError, PersistentOrderedMap};

#[test]
fn construction_is_first_key_last_distinct_value_and_insertion_order() {
    let map = PersistentOrderedMap::from_entries([
        ("Alpha".to_owned(), "one".to_owned()),
        ("beta".to_owned(), "two".to_owned()),
        ("Alpha".to_owned(), "changed".to_owned()),
    ]);

    assert_eq!(map.len(), 2);
    assert_eq!(
        map.keys().map(String::as_str).collect::<Vec<_>>(),
        ["Alpha", "beta"]
    );
    assert_eq!(
        map.get(&"Alpha".to_owned()).map(String::as_str),
        Some("changed")
    );
    map.validate_structure().unwrap();
}

#[test]
fn strict_add_set_item_and_key_representative_contracts_hold() {
    #[derive(Clone, Debug)]
    struct Key(&'static str, &'static str);
    impl PartialEq for Key {
        fn eq(&self, other: &Self) -> bool {
            self.0.eq_ignore_ascii_case(other.0)
        }
    }
    impl Eq for Key {}
    impl std::hash::Hash for Key {
        fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
            std::hash::Hash::hash(&self.0.to_ascii_lowercase(), state);
        }
    }

    let map = PersistentOrderedMap::new()
        .add(Key("key", "stored"), "value".to_owned())
        .unwrap();
    assert!(map.add(Key("KEY", "caller"), "other".to_owned()).is_err());
    let unchanged = map.set_item(Key("KEY", "caller"), "value".to_owned());
    let changed = map.set_item(Key("KEY", "caller"), "changed".to_owned());

    assert!(map.shares_roots_with(&unchanged));
    assert_eq!(changed.get_key(&Key("key", "probe")).unwrap().1, "stored");
    assert_eq!(changed.get(&Key("key", "probe")).unwrap(), "changed");
    assert_eq!(changed.index_of(&Key("key", "probe")), Some(0));
}

#[test]
fn explicit_insertion_and_movement_preserve_payloads() {
    let map = PersistentOrderedMap::new()
        .add("a", 1)
        .unwrap()
        .add("b", 2)
        .unwrap()
        .insert(1, "c", 3)
        .unwrap()
        .unwrap();
    let moved = map.move_to_first(&"b").unwrap().move_to_last(&"a").unwrap();

    assert_eq!(
        map.iter().map(|(key, _)| *key).collect::<Vec<_>>(),
        ["a", "c", "b"]
    );
    assert_eq!(
        moved.iter().map(|(key, _)| *key).collect::<Vec<_>>(),
        ["b", "c", "a"]
    );
    assert_eq!(moved.get(&"b"), Some(&2));
    assert_eq!(
        moved.move_to(9, &"a").unwrap_err(),
        OrderedMapMoveError::IndexOutOfRange
    );
    moved.validate_structure().unwrap();
}

#[test]
fn removal_ranges_reverse_and_sort_are_persistent() {
    let root = PersistentOrderedMap::from_entries([(3, "c"), (1, "a"), (2, "b"), (4, "d")]);
    let removed = root.try_remove(&1);
    let range = root.get_range(1, 2).unwrap();
    let reversed = root.reverse();
    let sorted = root.sort_by(|(left, _), (right, _)| left.cmp(right));

    assert_eq!(removed.removed, Some((1, "a")));
    assert_eq!(removed.map.len(), 3);
    assert_eq!(range.keys().copied().collect::<Vec<_>>(), [1, 2]);
    assert_eq!(reversed.keys().copied().collect::<Vec<_>>(), [4, 2, 1, 3]);
    assert_eq!(sorted.keys().copied().collect::<Vec<_>>(), [1, 2, 3, 4]);
    assert_eq!(root.keys().copied().collect::<Vec<_>>(), [3, 1, 2, 4]);
    range.validate_structure().unwrap();
    reversed.validate_structure().unwrap();
    sorted.validate_structure().unwrap();
}

#[test]
fn repeated_same_position_inserts_trigger_safe_relabeling() {
    let mut map = PersistentOrderedMap::new()
        .add(0, 0)
        .unwrap()
        .add(10_000, 10_000)
        .unwrap();
    for value in 1..90 {
        map = map.insert(1, value, value).unwrap().unwrap();
        map.validate_structure().unwrap();
    }

    assert_eq!(map.len(), 91);
    assert_eq!(map.first(), Some((&0, &0)));
    assert_eq!(map.last(), Some((&10_000, &10_000)));
}

#[test]
fn retained_branches_and_no_ops_share_expected_roots() {
    let root = PersistentOrderedMap::from_entries([(1, "a"), (2, "b")]);
    let left = root.set_item(1, "changed").add(3, "c").unwrap();
    let right = root.remove(&2).add_first(4, "d").unwrap();
    let absent = root.remove(&9);

    assert!(root.shares_roots_with(&absent));
    assert!(root.shares_membership_root_with(&root.set_item(1, "changed")));
    assert_eq!(root.to_vec(), [(1, "a"), (2, "b")]);
    assert_eq!(left.to_vec(), [(1, "changed"), (2, "b"), (3, "c")]);
    assert_eq!(right.to_vec(), [(4, "d"), (1, "a")]);
    root.validate_structure().unwrap();
    left.validate_structure().unwrap();
    right.validate_structure().unwrap();
}
