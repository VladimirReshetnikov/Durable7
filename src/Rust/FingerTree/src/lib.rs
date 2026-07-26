#![forbid(unsafe_code)]
//! Persistent FingerTree-family data structures for Rust.
//!
//! Every collection in this crate is *persistent*: an operation returns a new version and leaves
//! the version it was derived from untouched and still usable. Versions share structure, so an
//! update copies a path rather than the whole collection, and holding on to an old version costs
//! only the nodes that later versions no longer share. Snapshots are `Send + Sync` whenever their
//! elements are, so readers can work concurrently without locking.
//!
//! The crate is organized around the measured finger tree [`FingerTree`], a persistent sequence
//! that caches a monoidal [`MeasurePolicy`] at every node. Choosing a different measure specializes
//! the same tree into a different structure, which is why so much of the crate reuses it:
//!
//! * **Sequences** — [`PersistentDeque`] and [`ReversibleDeque`], [`Rope`]/[`MeasuredRope`]/
//!   [`TextRope`], and [`RrbVector`].
//! * **Ordered collections** — [`SortedBag`], [`SortedSet`], [`SortedMap`], and the
//!   history-independent [`CanonicalSortedSet`].
//! * **Priority structures** — [`PriorityQueue`], [`PrioritySearchQueue`], and
//!   [`BrodalOkasakiHeap`].
//! * **Interval and bit structures** — [`IntervalTree`], [`PersistentIntervalMap`], and
//!   [`PersistentChunkedBitSet`].
//!
//! The one deliberate exception to persistence is [`DabaLite`], a mutable sliding-window aggregate
//! documented as mutable at its own definition.
//!
//! Ordered structures take comparison from a retained [`OrderPolicy`] value rather than from an
//! `Ord` bound, so a collection remembers how it was ordered and binary operations can reject
//! mismatched operands instead of silently producing malformed results. The crate forbids
//! `unsafe`.

mod brodal_okasaki_heap;
mod canonical_sorted_set;
mod chunked_bit_set;
mod daba_lite;
mod deque;
mod interval_map;
mod interval_tree;
mod measured;
mod ordering;
mod priority_queue;
mod priority_search_queue;
mod rope;
mod rrb_vector;
mod sorted;
mod text_extras;

pub use brodal_okasaki_heap::{
    BrodalInvariantError, BrodalIter, BrodalMeldError, BrodalMinimumView, BrodalOkasakiHeap,
    BrodalOkasakiHeapStatistics,
};
pub use canonical_sorted_set::{
    CanonicalCursorSearch, CanonicalSetDifference, CanonicalSetError, CanonicalSetInvariantError,
    CanonicalSortedSet, CanonicalSortedSetCursor, CanonicalSortedSetIter,
    CanonicalSortedSetStatistics, NaturalZipTreeComparer, StableRankHash, StableZipTreeRankHash,
    ZipTreeComparer, ZipTreePolicyError, ZipTreeRankHash, ZipTreeRankPolicy,
};
pub use chunked_bit_set::{
    ChunkedBitSetCursorSearch, ChunkedBitSetInvariantError, ChunkedBitSetStatistics,
    NegativeBitIndex, PersistentChunkedBitSet, PersistentChunkedBitSetCursor,
};
pub use daba_lite::{
    DabaLite, DabaLiteInvariantError, DabaLiteStatistics, DabaMonoid, EmptyDabaLiteError,
};
pub use deque::{
    DequeItemSplit, DequePop, DequeRangeSplit, DequeSplit, PersistentDeque, PersistentDequeCursor,
    ReversibleDeque, ReversibleDequeCursor, ReversibleDequePop, ReversibleDequeSplit,
};
pub use interval_map::{
    IntervalMapCursorSearch, IntervalMapEntry, IntervalMapError, IntervalMapInvariantError,
    IntervalMapStatistics, PersistentIntervalMap, PersistentIntervalMapCursor,
};
pub use interval_tree::{Interval, IntervalCursorSearch, IntervalTree, IntervalTreeCursor};
pub use measured::{
    FingerTree, FingerTreeCursor, FingerTreeCursorSearch, KeyMeasure, LocateResult, MaxMeasure,
    MeasurePair, MeasurePolicy, MeasuredSplit, MinMeasure, OrderStatisticMeasure, ProductMeasure,
    RankedKey, SizeAndMaxMeasure, SizeAndMinMeasure, SizeAndSumMeasure, SizeMeasure, SumMeasure,
};
pub use ordering::{NaturalOrderComparer, OrderComparer, OrderPolicy};
pub use priority_queue::{PriorityEntry, PriorityQueue};
pub use priority_search_queue::{
    PrioritySearchAddResult, PrioritySearchCursorSearch, PrioritySearchEntry,
    PrioritySearchInvariantError, PrioritySearchIter, PrioritySearchMinimumView,
    PrioritySearchQueue, PrioritySearchQueueCursor, PrioritySearchQueueStatistics,
    PrioritySearchRangeError, PrioritySearchRangeIter, PrioritySearchRemoveResult,
};
pub use rope::{
    LineColumn, MeasuredRope, MeasuredRopeBuilder, MeasuredRopeCursor, MeasuredRopeCursorSearch,
    MeasuredRopeLocate, MeasuredRopeSplit, NewlineMeasure, Rope, RopeBuilder, RopeCursor, TextRope,
    TextRopeCursor, TextRopeCursorSearch,
};
pub use rrb_vector::{
    RrbVector, RrbVectorBuilder, RrbVectorCursor, RrbVectorIntoIter, RrbVectorInvariantError,
    RrbVectorIter, RrbVectorPop, RrbVectorSplit, RrbVectorStatistics,
};
pub use sorted::{
    DuplicateKeyError, OrderedCursorInsert, OrderedCursorSearch, SortedBag, SortedBagCursor,
    SortedMap, SortedMapCursor, SortedSet, SortedSetCursor,
};
pub use text_extras::NewlineStyle;

#[cfg(test)]
mod concurrency_tests {
    use super::*;
    use std::thread;

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn public_snapshots_are_send_sync_when_contents_are() {
        assert_send_sync::<PersistentDeque<i32>>();
        assert_send_sync::<CanonicalSortedSet<i32>>();
        assert_send_sync::<ReversibleDeque<i32>>();
        assert_send_sync::<Rope<i32>>();
        assert_send_sync::<RopeCursor<i32>>();
        assert_send_sync::<MeasuredRope<i32, SumMeasure<i32>>>();
        assert_send_sync::<MeasuredRopeCursor<i32, SumMeasure<i32>>>();
        assert_send_sync::<TextRopeCursor>();
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
        let measured_cursor = measured.cursor_at(63).unwrap();
        let text_cursor = TextRope::from("alpha\nbeta").cursor_at(6).unwrap();

        let mut handles = Vec::new();
        for _ in 0..8 {
            let expected = expected.clone();
            let reverse_expected = reverse_expected.clone();
            let measured_values = measured_values.clone();
            let deque = deque.clone();
            let reversible = reversible.clone();
            let rope = rope.clone();
            let measured = measured.clone();
            let measured_cursor = measured_cursor.clone();
            let text_cursor = text_cursor.clone();
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

                    assert_eq!(measured_cursor.position(), 63);
                    assert_eq!(measured_cursor.measure_before(), (1..=63).sum::<i32>());
                    assert_eq!(measured_cursor.peek_next(), Some(&64));
                    assert_eq!(
                        measured_cursor
                            .seek_by_measure(|sum| *sum >= 2_080)
                            .cursor
                            .position(),
                        63
                    );

                    assert_eq!(text_cursor.position(), 6);
                    assert_eq!(text_cursor.line_column(), LineColumn { line: 1, column: 0 });
                    assert_eq!(text_cursor.snapshot().as_string(), "alpha\nbeta");
                }
            }));
        }

        for handle in handles {
            handle.join().expect("reader thread failed");
        }
    }
}
