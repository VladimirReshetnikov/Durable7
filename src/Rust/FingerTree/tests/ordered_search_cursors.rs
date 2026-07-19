use std::cmp::Ordering;

use tools_data_structures_fingertree::{
    CanonicalSortedSet, Interval, IntervalTree, PersistentChunkedBitSet, PersistentIntervalMap,
    PrioritySearchEntry, PrioritySearchQueue, SortedBag, SortedMap, SortedSet, ZipTreeRankPolicy,
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

/// `found` means "already present" and `added` means "this attempt published an entry", so a
/// duplicate insert must report the opposite of the search that located the same key.
#[test]
fn sorted_map_cursor_try_insert_reports_addition_not_presence() {
    let map = SortedMap::new()
        .set_item(1, "one")
        .set_item(3, "three")
        .set_item(5, "five");
    let cursor = map.cursor_at(0).unwrap();

    let duplicate = cursor.try_insert(3, "THREE");
    assert!(
        !duplicate.added,
        "an existing key must not report an addition"
    );
    assert!(
        map.find_cursor(&3).found,
        "the same key must report presence through the search discriminator"
    );
    assert_eq!(duplicate.cursor.position(), 1);
    assert_eq!(duplicate.cursor.peek_next(), Some((&3, &"three")));
    assert_eq!(duplicate.cursor.snapshot().to_vec(), map.to_vec());

    let fresh = cursor.try_insert(4, "four");
    assert!(fresh.added, "an absent key must report an addition");
    assert!(
        !map.find_cursor(&4).found,
        "the same key must report absence through the search discriminator"
    );
    assert_eq!(fresh.cursor.position(), 3);
    assert_eq!(fresh.cursor.peek_previous(), Some((&4, &"four")));
    assert_eq!(map.len(), 3);
}

/// The canonical set and the priority-search queue resolve a rank through the cached subtree
/// counts, so every rank must agree with in-order enumeration at both gap sides.
#[test]
fn canonical_and_priority_search_rank_selection_matches_enumeration() {
    let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x5a17);
    let items: Vec<i32> = (0..64).map(|value| value * 3).collect();
    let set = CanonicalSortedSet::from_items(policy, items.clone()).unwrap();
    for rank in 0..=set.len() {
        let cursor = set.cursor_at(rank).unwrap();
        assert_eq!(cursor.peek_next(), items.get(rank));
        assert_eq!(
            cursor.peek_previous(),
            rank.checked_sub(1).map(|previous| &items[previous])
        );
    }

    let queue: PrioritySearchQueue<i32, i32, i32> = (0..64)
        .map(|value| PrioritySearchEntry::new(value * 3, 100 - value, value))
        .collect();
    for rank in 0..=queue.len() {
        let cursor = queue.cursor_at(rank).unwrap();
        assert_eq!(
            cursor.peek_next().map(|entry| *entry.key()),
            items.get(rank).copied()
        );
        assert_eq!(
            cursor.peek_previous().map(|entry| *entry.key()),
            rank.checked_sub(1).map(|previous| items[previous])
        );
    }
}

/// Rank-guided deletion must remove the focused occurrence, not merely something order-equal to
/// it, at every position in the collection.
#[test]
fn canonical_and_priority_search_cursor_deletes_the_focused_rank() {
    let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x5a17);
    let items: Vec<i32> = (0..24).map(|value| value * 3).collect();
    let set = CanonicalSortedSet::from_items(policy, items.clone()).unwrap();
    for rank in 0..set.len() {
        let deleted = set.cursor_at(rank).unwrap().delete_next().unwrap();
        let mut expected = items.clone();
        expected.remove(rank);
        assert_eq!(
            deleted.snapshot().iter().copied().collect::<Vec<_>>(),
            expected
        );
        assert_eq!(deleted.position(), rank);
    }

    let queue: PrioritySearchQueue<i32, i32, i32> = (0..24)
        .map(|value| PrioritySearchEntry::new(value * 3, 100 - value, value))
        .collect();
    for rank in 1..=queue.len() {
        let deleted = queue.cursor_at(rank).unwrap().delete_previous().unwrap();
        let mut expected = items.clone();
        expected.remove(rank - 1);
        assert_eq!(
            deleted
                .snapshot()
                .iter()
                .map(|entry| *entry.key())
                .collect::<Vec<_>>(),
            expected
        );
        assert_eq!(deleted.position(), rank - 1);
    }
}

/// Deletion only has to name a key, and `PrioritySearchEntry` retains one behind an `Arc`, so the
/// cursor's delete operations must not require `Clone` on `K`, `P`, or `V`.
///
/// This is primarily a compile-level assertion: none of the three payload types is `Clone`, so the
/// test would not build if the delete operations regained those bounds.
#[test]
fn priority_search_cursor_deletes_without_clone_payloads() {
    #[derive(PartialEq, Eq, PartialOrd, Ord, Debug)]
    struct Key(i32);

    #[derive(PartialEq, Eq, PartialOrd, Ord, Debug)]
    struct Priority(i32);

    #[derive(Debug)]
    struct Payload(&'static str);

    let queue = PrioritySearchQueue::new()
        .try_add(Key(1), Priority(30), Payload("one"))
        .queue
        .try_add(Key(3), Priority(10), Payload("three"))
        .queue
        .try_add(Key(5), Priority(20), Payload("five"))
        .queue;

    let cursor = queue.cursor_at_lower_bound(&Key(3));
    assert_eq!(cursor.position(), 1);
    assert_eq!(cursor.peek_next().unwrap().value().0, "three");

    let deleted = cursor.delete_next().unwrap();
    assert_eq!(deleted.position(), 1);
    assert_eq!(deleted.peek_next().unwrap().value().0, "five");
    assert_eq!(
        deleted
            .snapshot()
            .iter()
            .map(|entry| entry.value().0)
            .collect::<Vec<_>>(),
        ["one", "five"]
    );

    let deleted = deleted.delete_previous().unwrap();
    assert_eq!(deleted.position(), 0);
    assert_eq!(
        deleted
            .snapshot()
            .iter()
            .map(|entry| entry.value().0)
            .collect::<Vec<_>>(),
        ["five"]
    );
    assert_eq!(queue.len(), 3);
}

/// Interval endpoints identify a stored interval by comparison, not by `PartialEq`. `Tagged`
/// orders on `rank` alone while its `Eq` also inspects `tag`, so an equality-based match would
/// fail to locate an order-equal stored interval carrying a different tag.
#[test]
fn interval_tree_matches_endpoints_by_comparison_not_equality() {
    #[derive(Clone, Debug, PartialEq, Eq)]
    struct Tagged {
        rank: i32,
        tag: &'static str,
    }

    impl PartialOrd for Tagged {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
            Some(self.cmp(other))
        }
    }

    impl Ord for Tagged {
        fn cmp(&self, other: &Self) -> Ordering {
            self.rank.cmp(&other.rank)
        }
    }

    fn tagged(rank: i32, tag: &'static str) -> Tagged {
        Tagged { rank, tag }
    }

    let stored = Interval::new(tagged(1, "stored-low"), tagged(5, "stored-high"));
    let tree = IntervalTree::new()
        .insert(stored.clone())
        .insert(Interval::new(tagged(7, "late-low"), tagged(9, "late-high")));

    // Order-equal endpoints, different tags: `==` on the endpoints is false, `cmp` is `Equal`.
    let probe = Interval::new(tagged(1, "probe-low"), tagged(5, "probe-high"));
    assert_ne!(probe.low, stored.low);
    assert_eq!(probe.low.cmp(&stored.low), Ordering::Equal);

    assert!(
        tree.contains(&probe),
        "an order-equal interval must be found"
    );
    let located = tree.find_cursor(&probe);
    assert!(located.found, "the cursor search must agree with contains");
    assert_eq!(located.cursor.position(), 0);
    assert_eq!(located.cursor.peek_next().unwrap().low.tag, "stored-low");

    let removed = tree.remove(&probe);
    assert_eq!(removed.len(), 1);
    assert_eq!(removed.to_vec()[0].low.tag, "late-low");

    // A genuinely absent interval still misses and lands on the low-endpoint lower bound.
    let absent = tree.find_cursor(&Interval::new(tagged(1, "x"), tagged(6, "y")));
    assert!(!absent.found);
    assert_eq!(absent.cursor.position(), 0);
}
