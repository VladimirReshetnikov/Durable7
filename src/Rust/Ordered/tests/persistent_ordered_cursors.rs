use tools_data_structures_ordered::{
    PersistentOrderedMap, PersistentOrderedMultimap, PersistentOrderedSet,
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
    assert!(!duplicate.found);
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
