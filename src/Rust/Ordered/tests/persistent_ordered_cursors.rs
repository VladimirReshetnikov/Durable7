use tools_data_structures_ordered::{
    OrderedCursorInsert, PersistentOrderedMap, PersistentOrderedMultimap, PersistentOrderedSet,
};

#[test]
fn set_cursor_edits_explicit_gaps_and_retains_source() {
    let source = PersistentOrderedSet::from_items(["a", "b", "c"]);
    let mut cursor = source.cursor_at(1).unwrap();
    assert_eq!(cursor.peek_previous(), Some(&"a"));
    assert_eq!(cursor.peek_next(), Some(&"b"));

    cursor = cursor.insert("x");
    assert_eq!(cursor.position(), 2);
    assert_eq!(
        cursor.snapshot().iter().copied().collect::<Vec<_>>(),
        ["a", "x", "b", "c"]
    );
    let duplicate = cursor.insert("b");
    assert_eq!(duplicate.position(), 2);
    assert!(duplicate.snapshot().shares_roots_with(cursor.snapshot()));
    cursor = cursor.delete_previous().unwrap();
    assert_eq!(
        cursor.snapshot().iter().copied().collect::<Vec<_>>(),
        ["a", "b", "c"]
    );
    assert_eq!(source.iter().copied().collect::<Vec<_>>(), ["a", "b", "c"]);
    assert_eq!(source.find_cursor(&"b").cursor.position(), 1);
}

#[test]
fn map_cursor_inserts_updates_and_deletes_in_explicit_order() {
    let source = PersistentOrderedMap::from_entries([("a", 1), ("b", 2), ("c", 3)]);
    let mut cursor = source.cursor_at(1).unwrap().insert("x", 9).unwrap();
    assert_eq!(cursor.position(), 2);
    assert_eq!(
        cursor.snapshot().keys().copied().collect::<Vec<_>>(),
        ["a", "x", "b", "c"]
    );
    cursor = cursor.set_next_value(20).unwrap();
    assert_eq!(cursor.peek_next(), Some((&"b", &20)));
    let duplicate = cursor.try_insert("b", 200);
    assert!(!duplicate.added);
    assert_eq!(duplicate.cursor.position(), 2);
    cursor = cursor.delete_previous().unwrap();
    assert_eq!(
        cursor.snapshot().keys().copied().collect::<Vec<_>>(),
        ["a", "b", "c"]
    );
    assert_eq!(source.get(&"b"), Some(&2));
}

#[test]
fn multimap_cursor_uses_flattened_grouped_pair_ranks() {
    let source = PersistentOrderedMultimap::from_pairs([("b", 2), ("a", 9), ("b", 1), ("c", 7)]);
    let located = source.find_cursor(&"b", &1);
    assert!(located.found);
    assert_eq!(located.cursor.position(), 1);
    let mut cursor = located.cursor.insert("b", 3);
    assert_eq!(cursor.position(), 3);
    assert_eq!(
        cursor
            .snapshot()
            .iter()
            .map(|(key, value)| (*key, *value))
            .collect::<Vec<_>>(),
        [("b", 2), ("b", 1), ("b", 3), ("a", 9), ("c", 7)]
    );
    let duplicate = cursor.insert("b", 3);
    assert_eq!(duplicate.position(), 3);
    assert!(
        duplicate
            .snapshot()
            .shares_groups_root_with(cursor.snapshot())
    );
    cursor = cursor.delete_previous().unwrap().delete_next().unwrap();
    assert_eq!(cursor.position(), 2);
    assert_eq!(cursor.peek_next(), Some((&"c", &7)));
    assert_eq!(
        cursor
            .snapshot()
            .iter()
            .map(|(key, value)| (*key, *value))
            .collect::<Vec<_>>(),
        [("b", 2), ("b", 1), ("c", 7)]
    );
    assert_eq!(source.pair_count(), 4);
    assert_eq!(source.find_group_cursor(&"a").cursor.position(), 2);
}

/// `found` means "already present" and `added` means "this attempt published an entry", so a
/// duplicate insert must report the opposite of the search that located the same entry.
#[test]
fn map_cursor_try_insert_reports_addition_not_presence() {
    let source = PersistentOrderedMap::from_entries([("a", 1), ("b", 2), ("c", 3)]);
    let cursor = source.cursor_at(0).unwrap();

    let duplicate = cursor.try_insert("b", 200);
    assert!(
        !duplicate.added,
        "an existing key must not report an addition"
    );
    assert!(
        source.find_cursor(&"b").found,
        "the same key must report presence through the search discriminator"
    );
    assert_eq!(duplicate.cursor.position(), 1);
    assert_eq!(duplicate.cursor.snapshot().get(&"b"), Some(&2));
    assert_eq!(duplicate.cursor.snapshot().len(), 3);

    let fresh = cursor.try_insert("x", 9);
    assert!(fresh.added, "an absent key must report an addition");
    assert!(
        !source.find_cursor(&"x").found,
        "the same key must report absence through the search discriminator"
    );
    assert_eq!(fresh.cursor.position(), 1);
    assert_eq!(
        fresh.cursor.snapshot().keys().copied().collect::<Vec<_>>(),
        ["x", "a", "b", "c"]
    );
    assert_eq!(source.len(), 3);
}

/// All three neutral Ordered families return the same insertion-result shape, so generic code can
/// be written once over `OrderedCursorInsert`.
#[test]
fn ordered_cursor_try_insert_shape_is_uniform_across_families() {
    fn added<C>(result: OrderedCursorInsert<C>) -> bool {
        result.added
    }

    let set = PersistentOrderedSet::from_items(["a", "b"]);
    let set_cursor = set.cursor_at(1).unwrap();
    assert!(added(set_cursor.try_insert("x")));
    assert!(!added(set_cursor.try_insert("b")));

    let map = PersistentOrderedMap::from_entries([("a", 1), ("b", 2)]);
    let map_cursor = map.cursor_at(1).unwrap();
    assert!(added(map_cursor.try_insert("x", 9)));
    assert!(!added(map_cursor.try_insert("b", 9)));

    let multimap = PersistentOrderedMultimap::from_pairs([("a", 1), ("b", 2)]);
    let multimap_cursor = multimap.cursor_at(1).unwrap();
    assert!(added(multimap_cursor.try_insert("b", 9)));
    assert!(!added(multimap_cursor.try_insert("b", 2)));
}

/// A rejected insert must publish nothing and share both roots with the receiver.
#[test]
fn ordered_cursor_rejected_try_insert_retains_the_receiver_version() {
    let set = PersistentOrderedSet::from_items(["a", "b"]);
    let rejected = set.cursor_at(0).unwrap().try_insert("b");
    assert!(!rejected.added);
    assert!(rejected.cursor.snapshot().shares_roots_with(&set));

    let multimap = PersistentOrderedMultimap::from_pairs([("a", 1)]);
    let rejected = multimap.cursor_at(0).unwrap().try_insert("a", 1);
    assert!(!rejected.added);
    assert!(
        rejected
            .cursor
            .snapshot()
            .shares_groups_root_with(&multimap)
    );
}

/// `pair_count` is maintained by checked arithmetic, so draining every pair reaches zero rather
/// than wrapping, and a missing pair leaves the count alone.
#[test]
fn multimap_pair_count_never_underflows_while_draining() {
    let mut map = PersistentOrderedMultimap::from_pairs([("a", 1), ("a", 2), ("b", 3)]);
    assert_eq!(map.pair_count(), 3);

    assert_eq!(map.remove(&"a", &99).pair_count(), 3);
    assert_eq!(map.remove(&"z", &1).pair_count(), 3);

    map = map.remove(&"a", &1);
    assert_eq!(map.pair_count(), 2);
    map = map.remove_key(&"a");
    assert_eq!(map.pair_count(), 1);
    map = map.remove(&"b", &3);
    assert_eq!(map.pair_count(), 0);
    assert!(map.is_empty());

    assert_eq!(map.remove(&"b", &3).pair_count(), 0);
    assert_eq!(map.remove_key(&"b").pair_count(), 0);
}
