use std::cmp::Ordering;

use tools_data_structures_fingertree::{
    CanonicalSortedSet, Interval, IntervalTree, PersistentChunkedBitSet, PersistentIntervalMap,
    PrioritySearchQueue, SortedBag, SortedMap, SortedSet, ZipTreeRankPolicy,
};

#[derive(Clone, Debug)]
struct Ranked {
    rank: i32,
    label: &'static str,
}

impl PartialEq for Ranked {
    fn eq(&self, other: &Self) -> bool {
        self.rank == other.rank
    }
}

impl Eq for Ranked {}

impl PartialOrd for Ranked {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Ranked {
    fn cmp(&self, other: &Self) -> Ordering {
        self.rank.cmp(&other.rank)
    }
}

#[test]
fn sorted_collection_cursors_preserve_gaps_and_selected_occurrences() {
    let bag: SortedBag<_> = [
        Ranked {
            rank: 1,
            label: "low",
        },
        Ranked {
            rank: 2,
            label: "first",
        },
        Ranked {
            rank: 2,
            label: "second",
        },
        Ranked {
            rank: 2,
            label: "third",
        },
    ]
    .into_iter()
    .collect();
    let selected = bag.cursor_at(2).unwrap();
    assert_eq!(selected.peek_next().unwrap().label, "second");
    let edited = selected.delete_next().unwrap();
    assert_eq!(edited.position(), 2);
    assert_eq!(
        edited
            .snapshot()
            .to_vec()
            .iter()
            .map(|item| item.label)
            .collect::<Vec<_>>(),
        ["low", "first", "third"]
    );
    assert_eq!(bag.len(), 4);

    let set: SortedSet<_> = [1, 3, 5].into_iter().collect();
    let gap = set.cursor_at_lower_bound(&4);
    assert_eq!(gap.position(), 2);
    assert_eq!(gap.peek_previous(), Some(&3));
    assert_eq!(gap.peek_next(), Some(&5));
    let inserted = gap.add(4);
    assert_eq!(inserted.position(), 3);
    assert_eq!(inserted.snapshot().to_vec(), [1, 3, 4, 5]);

    let map = SortedMap::new()
        .set_item(1, "one")
        .set_item(3, "three")
        .set_item(5, "five");
    let found = map.find_cursor(&3);
    assert!(found.found);
    let changed = found.cursor.set_next_value("THREE").unwrap();
    assert_eq!(changed.peek_next(), Some((&3, &"THREE")));
    let inserted = changed.insert(4, "four").unwrap();
    assert_eq!(inserted.position(), 3);
    assert_eq!(
        inserted.snapshot().to_vec(),
        [(1, "one"), (3, "THREE"), (4, "four"), (5, "five")]
    );
}

#[test]
fn canonical_and_priority_search_cursors_honor_retained_policies() {
    let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x5a17);
    let set = CanonicalSortedSet::from_items(policy.clone(), [1, 3, 5]).unwrap();
    let cursor = set.cursor_at_lower_bound(&4);
    assert_eq!(cursor.position(), 2);
    assert_eq!(cursor.peek_previous(), Some(&3));
    assert_eq!(cursor.peek_next(), Some(&5));
    let inserted = cursor.insert(4).unwrap();
    assert_eq!(inserted.position(), 3);
    assert!(inserted.snapshot().policy().is_same_policy(&policy));
    assert_eq!(
        inserted.snapshot().iter().copied().collect::<Vec<_>>(),
        [1, 3, 4, 5]
    );

    let queue = PrioritySearchQueue::new()
        .set_item(1, 30, "one")
        .set_item(3, 10, "three")
        .set_item(5, 20, "five");
    let minimum = queue.cursor_at_minimum_priority();
    assert_eq!(minimum.peek_next().unwrap().key(), &3);
    let reprioritized = minimum.set_next(40, "THREE").unwrap();
    assert_eq!(reprioritized.peek_next().unwrap().value(), &"THREE");
    assert_eq!(reprioritized.snapshot().minimum().unwrap().key(), &5);
    let inserted = match reprioritized.insert(4, 5, "four") {
        Ok(cursor) => cursor,
        Err(_) => panic!("new key should be inserted"),
    };
    assert_eq!(inserted.position(), 3);
    assert_eq!(inserted.snapshot().minimum().unwrap().key(), &4);
}

#[test]
fn interval_tree_cursors_checkpoint_queries_and_delete_exact_occurrences() {
    let first = Interval::new(
        Ranked {
            rank: 1,
            label: "first-low",
        },
        Ranked {
            rank: 5,
            label: "first-high",
        },
    );
    let second = Interval::new(
        Ranked {
            rank: 1,
            label: "second-low",
        },
        Ranked {
            rank: 5,
            label: "second-high",
        },
    );
    let tree = IntervalTree::new()
        .insert(first.clone())
        .insert(second.clone())
        .insert(Interval::new(
            Ranked {
                rank: 7,
                label: "late-low",
            },
            Ranked {
                rank: 9,
                label: "late-high",
            },
        ));
    assert_eq!(
        tree.cursor_at(0).unwrap().peek_next().unwrap().low.label,
        "second-low"
    );
    let selected = tree.cursor_at(1).unwrap();
    assert_eq!(selected.peek_next().unwrap().low.label, "first-low");
    let edited = selected.delete_next().unwrap();
    assert_eq!(edited.peek_previous().unwrap().low.label, "second-low");
    assert_eq!(edited.peek_next().unwrap().low.label, "late-low");
    assert_eq!(tree.len(), 3);

    let probe = Interval::new(
        Ranked {
            rank: 4,
            label: "probe-low",
        },
        Ranked {
            rank: 8,
            label: "probe-high",
        },
    );
    let first_hit = tree.find_overlap_cursor(&probe);
    assert!(first_hit.found);
    assert_eq!(first_hit.cursor.position(), 0);
    let second_hit = first_hit.cursor.seek_next_overlap(&probe);
    assert!(second_hit.found);
    assert_eq!(second_hit.cursor.position(), 1);
}

#[test]
fn interval_map_and_chunked_bit_set_cursors_keep_snapshot_edits_local() {
    let map = PersistentIntervalMap::new()
        .add(Interval::new(1, 3), "one")
        .unwrap()
        .add(Interval::new(5, 8), "five")
        .unwrap()
        .add(Interval::new(10, 12), "ten")
        .unwrap();
    let overlap = map.find_overlap_cursor(&Interval::new(2, 11)).unwrap();
    assert!(overlap.found);
    assert_eq!(overlap.cursor.peek_next().unwrap().value, "one");
    let next = overlap
        .cursor
        .seek_next_overlap(&Interval::new(2, 11))
        .unwrap();
    assert!(next.found);
    assert_eq!(next.cursor.peek_next().unwrap().value, "five");
    let changed = next.cursor.set_next_value("FIVE").unwrap().unwrap();
    assert_eq!(changed.peek_next().unwrap().value, "FIVE");
    assert_eq!(map.get(&Interval::new(5, 8)).unwrap(), Some(&"five"));

    let bits = PersistentChunkedBitSet::from_indices([1, 64, 130]).unwrap();
    let gap = bits.cursor_at_or_after(65);
    assert_eq!(gap.position(), 2);
    assert_eq!(gap.peek_previous(), Some(64));
    assert_eq!(gap.peek_next(), Some(130));
    let inserted = gap.insert(100).unwrap();
    assert_eq!(inserted.position(), 3);
    assert_eq!(
        inserted.snapshot().iter().collect::<Vec<_>>(),
        [1, 64, 100, 130]
    );
    let deleted = inserted.delete_previous().unwrap();
    assert_eq!(deleted.snapshot().iter().collect::<Vec<_>>(), [1, 64, 130]);
    assert_eq!(bits.iter().collect::<Vec<_>>(), [1, 64, 130]);
}
