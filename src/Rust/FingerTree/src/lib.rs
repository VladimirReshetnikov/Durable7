#![forbid(unsafe_code)]
#![doc = "Persistent FingerTree-family data structures for Rust."]

mod daba_lite;
mod deque;
mod interval_tree;
mod measured;
mod priority_queue;
mod rope;
mod rrb_vector;
mod sorted;
mod text_extras;

pub use daba_lite::{
    DabaLite, DabaLiteInvariantError, DabaLiteStatistics, DabaMonoid, EmptyDabaLiteError,
};
pub use deque::{
    DequeItemSplit, DequePop, DequeRangeSplit, DequeSplit, PersistentDeque, ReversibleDeque,
    ReversibleDequePop, ReversibleDequeSplit,
};
pub use interval_tree::{Interval, IntervalTree};
pub use measured::{
    FingerTree, KeyMeasure, LocateResult, MaxMeasure, MeasurePair, MeasurePolicy, MeasuredSplit,
    MinMeasure, OrderStatisticMeasure, ProductMeasure, RankedKey, SizeAndMaxMeasure,
    SizeAndMinMeasure, SizeAndSumMeasure, SizeMeasure, SumMeasure,
};
pub use priority_queue::{PriorityEntry, PriorityQueue};
pub use rope::{
    LineColumn, MeasuredRope, MeasuredRopeBuilder, MeasuredRopeLocate, MeasuredRopeSplit,
    NewlineMeasure, Rope, RopeBuilder, TextRope,
};
pub use rrb_vector::{
    RrbVector, RrbVectorBuilder, RrbVectorIntoIter, RrbVectorInvariantError, RrbVectorIter,
    RrbVectorPop, RrbVectorSplit, RrbVectorStatistics,
};
pub use sorted::{DuplicateKeyError, SortedBag, SortedMap, SortedSet};
pub use text_extras::NewlineStyle;

#[cfg(test)]
mod concurrency_tests {
    use super::*;
    use std::thread;

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn public_snapshots_are_send_sync_when_contents_are() {
        assert_send_sync::<PersistentDeque<i32>>();
        assert_send_sync::<ReversibleDeque<i32>>();
        assert_send_sync::<Rope<i32>>();
        assert_send_sync::<MeasuredRope<i32, SumMeasure<i32>>>();
    }

    #[test]
    fn concurrent_readers_share_public_snapshots() {
        let expected = (0..256).collect::<Vec<_>>();
        let deque: PersistentDeque<_> = expected.iter().copied().collect();
        let reversible: ReversibleDeque<_> = expected
            .iter()
            .copied()
            .collect::<ReversibleDeque<_>>()
            .reverse();
        let reverse_expected = expected.iter().rev().copied().collect::<Vec<_>>();
        let rope: Rope<_> = expected.iter().copied().collect();
        let measured_values = (1..=128).collect::<Vec<_>>();
        let measured: MeasuredRope<_, SumMeasure<i32>> = measured_values.iter().copied().collect();
        let measured_total = measured_values.iter().sum::<i32>();

        let mut handles = Vec::new();
        for _ in 0..8 {
            let expected = expected.clone();
            let reverse_expected = reverse_expected.clone();
            let measured_values = measured_values.clone();
            let deque = deque.clone();
            let reversible = reversible.clone();
            let rope = rope.clone();
            let measured = measured.clone();
            handles.push(thread::spawn(move || {
                for _ in 0..128 {
                    assert_eq!(deque.len(), 256);
                    assert_eq!(deque.get(128), Some(&128));
                    assert_eq!(deque.to_vec(), expected);

                    assert_eq!(reversible.len(), 256);
                    assert_eq!(reversible.get(0), Some(&255));
                    assert_eq!(reversible.to_vec(), reverse_expected);

                    assert_eq!(rope.len(), 256);
                    assert_eq!(rope.get(128), Some(&128));
                    assert_eq!(rope.to_vec(), expected);

                    assert_eq!(measured.len(), 128);
                    assert_eq!(measured.measure(), &measured_total);
                    assert_eq!(measured.get(63), Some(&64));
                    assert_eq!(measured.to_vec(), measured_values);
                }
            }));
        }

        for handle in handles {
            handle.join().expect("reader thread failed");
        }
    }
}
