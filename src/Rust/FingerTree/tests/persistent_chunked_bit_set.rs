use std::collections::BTreeSet;

use durable7_fingertree::PersistentChunkedBitSet;

#[test]
fn empty_point_updates_and_largest_index_obey_domain_contract() {
    let empty = PersistentChunkedBitSet::new();
    assert!(empty.is_empty());
    assert!(!empty.contains(-1));
    assert!(empty.remove(-1).shares_storage_with(&empty));
    assert!(empty.insert(-1).is_err());

    let set = empty.insert(0).unwrap().insert(i32::MAX).unwrap();
    assert!(set.contains(0));
    assert!(set.contains(i32::MAX));
    assert_eq!(set.len(), 2);
    assert_eq!(set.chunk_count(), 2);
    assert!(set.insert(i32::MAX).unwrap().shares_storage_with(&set));
}

#[test]
fn construction_orders_and_deduplicates_across_word_boundaries() {
    let expected = [0, 1, 63, 64, 65, 130, i32::MAX];
    let set = PersistentChunkedBitSet::from_indices([65, 0, 64, 63, 1, 130, 65, i32::MAX]).unwrap();

    assert_eq!(set.iter().collect::<Vec<_>>(), expected);
    assert_eq!(set.len(), expected.len() as u64);
    assert_eq!(set.chunk_count(), 4);
    set.validate().unwrap();
}

#[test]
fn rank_is_inclusive() {
    let set = PersistentChunkedBitSet::from_indices([0, 2, 63, 64, 100, 130]).unwrap();
    assert_eq!(set.rank(-1), 0);
    assert_eq!(set.rank(0), 1);
    assert_eq!(set.rank(1), 1);
    assert_eq!(set.rank(2), 2);
    assert_eq!(set.rank(63), 3);
    assert_eq!(set.rank(64), 4);
    assert_eq!(set.rank(99), 4);
    assert_eq!(set.rank(100), 5);
    assert_eq!(set.rank(i32::MAX), 6);
}

#[test]
fn select_returns_zero_based_population_order() {
    let expected = [0, 1, 63, 64, 65, 511, i32::MAX];
    let set = PersistentChunkedBitSet::from_indices(expected).unwrap();
    for (rank, expected) in expected.into_iter().enumerate() {
        assert_eq!(set.select(rank as u64), Some(expected));
    }
    assert_eq!(set.select(expected.len() as u64), None);
}

#[test]
fn removal_contracts_empty_chunks_and_preserves_snapshots() {
    let source = PersistentChunkedBitSet::from_indices([1, 64, 65]).unwrap();
    let reduced = source.remove(64).remove(65);
    assert_eq!(reduced.iter().collect::<Vec<_>>(), [1]);
    assert_eq!(reduced.chunk_count(), 1);
    assert!(reduced.remove(65).shares_storage_with(&reduced));
    assert_eq!(source.iter().collect::<Vec<_>>(), [1, 64, 65]);
}

#[test]
fn set_algebra_matches_mathematical_sets_and_receiver_identity() {
    let left = PersistentChunkedBitSet::from_indices([0, 1, 64, 130]).unwrap();
    let right = PersistentChunkedBitSet::from_indices([1, 2, 64, 65]).unwrap();
    assert_eq!(
        left.union(&right).iter().collect::<Vec<_>>(),
        [0, 1, 2, 64, 65, 130]
    );
    assert_eq!(left.intersect(&right).iter().collect::<Vec<_>>(), [1, 64]);
    assert_eq!(left.except(&right).iter().collect::<Vec<_>>(), [0, 130]);
    assert_eq!(
        left.symmetric_except(&right).iter().collect::<Vec<_>>(),
        [0, 2, 65, 130]
    );
    assert!(
        left.union(&PersistentChunkedBitSet::from_indices([1, 64]).unwrap())
            .shares_storage_with(&left)
    );
    assert!(
        left.intersect(&left.union(&right))
            .shares_storage_with(&left)
    );
    assert!(left.except(&left).is_empty());
}

#[test]
fn deterministic_point_history_matches_btree_set_model() {
    let mut state = 0x9e37_79b9_u32;
    let mut next = || {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        state
    };
    let mut model = BTreeSet::new();
    let mut set = PersistentChunkedBitSet::new();
    for _ in 0..700 {
        let bit = (next() % 4096) as i32;
        if next() & 1 == 0 {
            model.insert(bit);
            set = set.insert(bit).unwrap();
        } else {
            model.remove(&bit);
            set = set.remove(bit);
        }
        assert_eq!(
            set.iter().collect::<Vec<_>>(),
            model.iter().copied().collect::<Vec<_>>()
        );
        let probe = (next() % 4098) as i32 - 1;
        assert_eq!(set.rank(probe), model.range(..=probe).count() as u64);
        for (rank, expected) in model.iter().enumerate() {
            assert_eq!(set.select(rank as u64), Some(*expected));
        }
        set.validate().unwrap();
    }
}
