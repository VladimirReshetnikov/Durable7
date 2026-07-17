use tools_data_structures_hamt::PersistentRelation;

#[test]
fn represents_many_to_many_adjacency() {
    let relation = PersistentRelation::new()
        .insert("a", 1)
        .insert("a", 2)
        .insert("b", 1);

    assert_eq!(relation.left_count(), 2);
    assert_eq!(relation.right_count(), 2);
    assert_eq!(relation.pair_count(), 3);
    assert_eq!(relation.count_rights(&"a"), 2);
    assert_eq!(relation.count_lefts(&1), 2);
    assert!(relation.contains(&"b", &1));
    relation.validate().unwrap();
}

#[test]
fn duplicate_pairs_share_both_indexes() {
    let relation = PersistentRelation::new().insert("a", 1);
    let duplicate = relation.insert("a", 1);
    let (try_duplicate, added) = relation.try_insert("a", 1);

    assert!(!added);
    assert!(relation.shares_indexes_with(&duplicate));
    assert!(relation.shares_indexes_with(&try_duplicate));
}

#[test]
fn reuses_global_representatives_across_adjacency_groups() {
    #[derive(Clone, Debug)]
    struct Token(&'static str, &'static str);
    impl PartialEq for Token {
        fn eq(&self, other: &Self) -> bool {
            self.0.eq_ignore_ascii_case(other.0)
        }
    }
    impl Eq for Token {}
    impl std::hash::Hash for Token {
        fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
            std::hash::Hash::hash(&self.0.to_ascii_lowercase(), state);
        }
    }

    let relation = PersistentRelation::new()
        .insert(Token("left-a", "stored-a"), Token("right", "stored-right"))
        .insert(Token("left-b", "stored-b"), Token("RIGHT", "caller-right"))
        .insert(Token("LEFT-A", "caller-a"), Token("other", "stored-other"));

    assert_eq!(
        relation.get_left(&Token("left-a", "probe")).unwrap().1,
        "stored-a"
    );
    assert_eq!(
        relation.get_right(&Token("right", "probe")).unwrap().1,
        "stored-right"
    );
    assert!(
        relation
            .get_rights(&Token("left-b", "probe"))
            .unwrap()
            .iter()
            .any(|right| right.1 == "stored-right")
    );
    relation.validate().unwrap();
}

#[test]
fn inverse_swaps_existing_roots_without_rebuilding_pairs() {
    let relation = PersistentRelation::new().insert("a", 1).insert("b", 1);
    let inverse = relation.inverse();
    let round_trip = inverse.inverse();

    assert!(inverse.contains(&1, &"a"));
    assert_eq!(inverse.pair_count(), relation.pair_count());
    assert!(relation.shares_indexes_with(&round_trip));
    inverse.validate().unwrap();
}

#[test]
fn pair_and_whole_domain_removal_are_symmetric() {
    let source = PersistentRelation::new()
        .insert("a", 1)
        .insert("a", 2)
        .insert("b", 1);
    let pair = source.remove(&"b", &1);
    let (no_a, actual_a, rights) = source.try_remove_left(&"a").unwrap();
    let (no_one, actual_one, lefts) = source.try_remove_right(&1).unwrap();

    assert!(!pair.contains_left(&"b"));
    assert!(pair.contains_right(&1));
    assert_eq!(actual_a, "a");
    assert_eq!(rights.len(), 2);
    assert_eq!(no_a.pair_count(), 1);
    assert_eq!(actual_one, 1);
    assert_eq!(lefts.len(), 2);
    assert_eq!(no_one.pair_count(), 1);
    pair.validate().unwrap();
    no_a.validate().unwrap();
    no_one.validate().unwrap();
}

#[test]
fn retained_branches_and_clear_remain_independent() {
    let root = PersistentRelation::from_pairs([(1, "a"), (2, "b")]);
    let left = root.insert(1, "b");
    let right = root.remove(&2, &"b").insert(3, "c");

    assert_eq!(root.pair_count(), 2);
    assert_eq!(left.pair_count(), 3);
    assert_eq!(right.pair_count(), 2);
    assert!(root.clear().is_empty());
    assert!(root.remove(&9, &"z").shares_indexes_with(&root));
    root.validate().unwrap();
    left.validate().unwrap();
    right.validate().unwrap();
}
