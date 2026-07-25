use durable7_hamt::{MapPatchEntry, PersistentHashMap, PersistentMapPatch};

#[test]
fn between_and_apply_transform_source_exactly() {
    let source = PersistentHashMap::new()
        .insert("same", "value")
        .insert("change", "before")
        .insert("remove", "gone");
    let target = source
        .insert("change", "after")
        .remove(&"remove")
        .insert("added", "new");
    let patch = PersistentMapPatch::between(&source, &target);
    let result = patch.apply(&source).unwrap();

    assert_eq!(patch.len(), 3);
    assert_eq!(result, target);
    assert_eq!(source.get(&"change"), Some(&"before"));
    assert!(source.contains_key(&"remove"));
    patch.validate().unwrap();
}

#[test]
fn nested_option_distinguishes_absence_from_present_none() {
    let patch = PersistentMapPatch::new()
        .add(MapPatchEntry {
            key: "key",
            before: None,
            after: Some(None::<i32>),
        })
        .unwrap();
    let result = patch
        .apply(&PersistentHashMap::<&str, Option<i32>>::new())
        .unwrap();

    assert!(result.contains_key(&"key"));
    assert_eq!(result.get(&"key"), Some(&None));
}

#[test]
fn conflict_returns_without_changing_source() {
    let source = PersistentHashMap::new().insert("a", 1).insert("b", 2);
    let patch = PersistentMapPatch::new()
        .add(MapPatchEntry {
            key: "a",
            before: Some(1),
            after: Some(10),
        })
        .unwrap()
        .add(MapPatchEntry {
            key: "b",
            before: Some(99),
            after: None,
        })
        .unwrap();

    assert_eq!(patch.apply(&source).unwrap_err().key, "b");
    assert_eq!(source.get(&"a"), Some(&1));
    assert_eq!(source.get(&"b"), Some(&2));
}

#[test]
fn inversion_restores_source() {
    let source = PersistentHashMap::new().insert(1, "one").insert(2, "two");
    let target = source.remove(&1).insert(2, "TWO").insert(3, "three");
    let patch = PersistentMapPatch::between(&source, &target);

    assert_eq!(patch.invert().apply(&target).unwrap(), source);
}

#[test]
fn composition_matches_sequential_application_and_drops_round_trips() {
    let a = PersistentHashMap::new().insert(1, "one").insert(2, "two");
    let b = a.insert(1, "ONE").remove(&2).insert(3, "three");
    let c = b.insert(1, "one").insert(3, "THREE").insert(4, "four");
    let first = PersistentMapPatch::between(&a, &b);
    let second = PersistentMapPatch::between(&b, &c);
    let composed = first.compose(&second).unwrap();

    assert_eq!(composed.apply(&a).unwrap(), c);
    assert!(!composed.contains_key(&1));
    composed.validate().unwrap();
}

#[test]
fn composition_rejects_mismatched_intermediate_state() {
    let first = PersistentMapPatch::new()
        .add(MapPatchEntry {
            key: "x",
            before: None,
            after: Some(1),
        })
        .unwrap();
    let next = PersistentMapPatch::new()
        .add(MapPatchEntry {
            key: "x",
            before: Some(9),
            after: Some(2),
        })
        .unwrap();

    assert_eq!(first.compose(&next).unwrap_err().key, "x");
}

#[test]
fn no_ops_duplicates_and_removal_obey_persistent_contracts() {
    let empty = PersistentMapPatch::new();
    let no_op = empty
        .add(MapPatchEntry {
            key: "x",
            before: Some(1),
            after: Some(1),
        })
        .unwrap();
    assert!(empty.shares_changes_root_with(&no_op));

    let patch = empty
        .add(MapPatchEntry {
            key: "Key",
            before: None,
            after: Some(1),
        })
        .unwrap();
    assert!(
        patch
            .add(MapPatchEntry {
                key: "Key",
                before: None,
                after: Some(2),
            })
            .is_err()
    );
    assert!(patch.remove(&"missing").shares_changes_root_with(&patch));
}
