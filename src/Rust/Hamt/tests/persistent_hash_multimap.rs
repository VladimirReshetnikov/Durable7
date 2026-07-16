use std::collections::hash_map::RandomState;
use tools_data_structures_hamt::PersistentHashMultimap;

#[test]
fn tracks_distinct_groups_and_pairs() {
    let map = PersistentHashMultimap::from_pairs([
        ("a".to_owned(), 1),
        ("a".to_owned(), 2),
        ("b".to_owned(), 1),
        ("a".to_owned(), 1),
    ]);

    assert_eq!(map.key_count(), 2);
    assert_eq!(map.pair_count(), 3);
    assert_eq!(map.count_values(&"a".to_owned()), 2);
    assert!(map.contains(&"b".to_owned(), &1));
    assert_eq!(map.validate().unwrap().pair_count, 3);
}

#[test]
fn retains_first_representatives_in_both_domains() {
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
            self.0.to_ascii_lowercase().hash(state);
        }
    }

    let key = Token("key", "stored-key");
    let value = Token("value", "stored-value");
    let map = PersistentHashMultimap::new()
        .insert(key.clone(), value.clone())
        .insert(Token("KEY", "caller-key"), Token("VALUE", "caller-value"));

    assert_eq!(map.get_key(&Token("key", "probe")).unwrap().1, "stored-key");
    assert_eq!(
        map.get_value(&Token("key", "probe"), &Token("value", "probe"))
            .unwrap()
            .1,
        "stored-value"
    );
    assert_eq!(map.pair_count(), 1);
}

#[test]
fn last_value_removal_contracts_outer_group() {
    let source = PersistentHashMultimap::from_pairs([("a", 1), ("a", 2), ("b", 3)]);
    let once = source.remove(&"a", &1);
    let twice = once.remove(&"a", &2);

    assert!(once.contains_key(&"a"));
    assert!(!twice.contains_key(&"a"));
    assert_eq!(twice.key_count(), 1);
    assert_eq!(twice.pair_count(), 1);
    assert_eq!(source.pair_count(), 3);
    twice.validate().unwrap();
}

#[test]
fn whole_key_removal_returns_persistent_group() {
    let source = PersistentHashMultimap::from_pairs([("a", 1), ("a", 2), ("b", 3)]);
    let (result, actual, values) = source.try_remove_key(&"a").unwrap();

    assert_eq!(actual, "a");
    assert_eq!(
        values
            .iter()
            .copied()
            .collect::<std::collections::BTreeSet<_>>(),
        [1, 2].into()
    );
    assert_eq!(result.pair_count(), 1);
    assert_eq!(source.pair_count(), 3);
}

#[test]
fn absent_edits_share_outer_root_and_clear_retains_policies() {
    let map: PersistentHashMultimap<i32, i32, RandomState, RandomState> =
        PersistentHashMultimap::new().insert(1, 2);
    let absent = map.remove(&9, &9);
    let duplicate = map.insert(1, 2);
    let empty = map.clear();

    assert!(map.shares_groups_root_with(&absent));
    assert!(map.shares_groups_root_with(&duplicate));
    assert!(empty.is_empty());
    assert!(empty.get_values(&1).is_none());
}

#[test]
fn branching_histories_remain_independent() {
    let root = PersistentHashMultimap::from_pairs([(1, "a"), (2, "b")]);
    let left = root.insert(1, "b");
    let right = root.remove(&2, &"b").insert(3, "c");

    assert_eq!(root.pair_count(), 2);
    assert_eq!(left.pair_count(), 3);
    assert_eq!(right.pair_count(), 2);
    assert!(!root.contains(&1, &"b"));
    root.validate().unwrap();
    left.validate().unwrap();
    right.validate().unwrap();
}
