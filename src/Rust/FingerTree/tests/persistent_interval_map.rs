use durable7_fingertree::{
    Interval, IntervalMapEntry, IntervalMapError, PersistentIntervalMap,
};

#[test]
fn exact_updates_are_lexicographic_and_strict() {
    let map = PersistentIntervalMap::new()
        .add(Interval::new(5, 10), "wide")
        .unwrap()
        .add(Interval::new(5, 7), "short")
        .unwrap()
        .add(Interval::new(1, 100), "cover")
        .unwrap();

    assert_eq!(
        map.keys().cloned().collect::<Vec<_>>(),
        [
            Interval::new(1, 100),
            Interval::new(5, 7),
            Interval::new(5, 10)
        ]
    );
    assert_eq!(map.get(&Interval::new(5, 7)).unwrap(), Some(&"short"));
    assert_eq!(
        map.add(Interval::new(5, 7), "duplicate").unwrap_err(),
        IntervalMapError::DuplicateInterval
    );
    map.validate().unwrap();
}

#[test]
fn set_item_retains_key_and_equal_value_is_storage_sharing_no_op() {
    #[derive(Clone, Debug)]
    struct Endpoint(i32, &'static str);
    impl PartialEq for Endpoint {
        fn eq(&self, other: &Self) -> bool {
            self.0 == other.0
        }
    }
    impl Eq for Endpoint {}
    impl PartialOrd for Endpoint {
        fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
            Some(self.cmp(other))
        }
    }
    impl Ord for Endpoint {
        fn cmp(&self, other: &Self) -> std::cmp::Ordering {
            self.0.cmp(&other.0)
        }
    }

    let stored = Interval::new(Endpoint(1, "stored-low"), Endpoint(3, "stored-high"));
    let equal = Interval::new(Endpoint(1, "caller-low"), Endpoint(3, "caller-high"));
    let map = PersistentIntervalMap::new()
        .set_item(stored, "alpha".to_owned())
        .unwrap();
    let unchanged = map.set_item(equal.clone(), "alpha".to_owned()).unwrap();
    let changed = map.set_item(equal.clone(), "beta".to_owned()).unwrap();

    assert!(map.shares_storage_with(&unchanged));
    assert_eq!(changed.get(&equal).unwrap().unwrap(), "beta");
    assert_eq!(
        changed.get_entry(&equal).unwrap().unwrap().interval.low.1,
        "stored-low"
    );
}

#[test]
fn overlap_queries_match_a_linear_model() {
    let entries = (0..180)
        .map(|index| {
            let low = (index * 37) % 500;
            IntervalMapEntry {
                interval: Interval::new(low, low + (index * 13) % 40),
                value: index,
            }
        })
        .collect::<Vec<_>>();
    let mut map = PersistentIntervalMap::new();
    for entry in &entries {
        map = map.set_item(entry.interval.clone(), entry.value).unwrap();
    }

    for index in 0..200 {
        let low = (index * 29) % 520;
        let probe = Interval::new(low, low + (index * 11) % 40);
        let mut expected = entries
            .iter()
            .filter(|entry| entry.interval.overlaps(&probe))
            .cloned()
            .collect::<Vec<_>>();
        expected.sort_by(|left, right| {
            left.interval
                .low
                .cmp(&right.interval.low)
                .then(left.interval.high.cmp(&right.interval.high))
        });
        expected.dedup_by(|left, right| left.interval == right.interval);
        let actual = map.find_overlaps(&probe).unwrap();
        assert_eq!(actual, expected);
        assert_eq!(map.count_overlaps(&probe).unwrap(), expected.len());
        assert_eq!(
            map.find_overlap(&probe).unwrap().is_some(),
            !expected.is_empty()
        );
    }
}

#[test]
fn point_stabbing_removal_and_retained_versions_work() {
    let root = PersistentIntervalMap::new()
        .add(Interval::new(1, 5), "a")
        .unwrap()
        .add(Interval::new(10, 15), "b")
        .unwrap();
    let left = root.set_item(Interval::new(1, 5), "changed").unwrap();
    let (right, removed) = root.try_remove(&Interval::new(10, 15)).unwrap().unwrap();

    assert_eq!(root.find_containing(&4).unwrap().value, "a");
    assert!(root.find_containing(&9).is_none());
    assert_eq!(left.get(&Interval::new(1, 5)).unwrap(), Some(&"changed"));
    assert_eq!(removed.value, "b");
    assert_eq!(right.len(), 1);
    assert_eq!(root.len(), 2);
    root.validate().unwrap();
    left.validate().unwrap();
    right.validate().unwrap();
}

#[test]
fn invalid_intervals_are_rejected_across_surfaces() {
    let invalid = Interval { low: 9, high: 2 };
    let map = PersistentIntervalMap::<i32, &str>::new();

    assert_eq!(
        map.add(invalid.clone(), "x").unwrap_err(),
        IntervalMapError::InvalidInterval
    );
    assert_eq!(
        map.set_item(invalid.clone(), "x").unwrap_err(),
        IntervalMapError::InvalidInterval
    );
    assert_eq!(
        map.contains_key(&invalid).unwrap_err(),
        IntervalMapError::InvalidInterval
    );
    assert_eq!(
        map.find_overlaps(&invalid).unwrap_err(),
        IntervalMapError::InvalidInterval
    );
}

#[test]
fn distinct_overlapping_keys_are_not_coalesced() {
    let map = PersistentIntervalMap::new()
        .add(Interval::new(1, 10), "outer")
        .unwrap()
        .add(Interval::new(2, 3), "inner")
        .unwrap()
        .add(Interval::new(10, 12), "touch")
        .unwrap();

    assert_eq!(map.count_overlaps(&Interval::new(3, 10)).unwrap(), 3);
    assert_eq!(map.len(), 3);
}
