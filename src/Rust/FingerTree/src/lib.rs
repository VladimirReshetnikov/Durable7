#![forbid(unsafe_code)]
#![doc = "Persistent FingerTree-family data structures for Rust."]

mod deque;
mod interval_tree;
mod measured;
mod priority_queue;
mod rope;
mod sorted;

pub use deque::{
    DequeItemSplit, DequePop, DequeRangeSplit, DequeSplit, PersistentDeque, ReversibleDeque,
};
pub use interval_tree::{Interval, IntervalTree};
pub use measured::{
    FingerTree, LocateResult, MaxMeasure, MeasurePolicy, MeasuredSplit, MinMeasure,
    OrderStatisticMeasure, RankedKey, SizeMeasure, SumMeasure,
};
pub use priority_queue::{PriorityEntry, PriorityQueue};
pub use rope::{
    LineColumn, MeasuredRope, MeasuredRopeLocate, MeasuredRopeSplit, NewlineMeasure, Rope,
    RopeBuilder, TextRope,
};
pub use sorted::{DuplicateKeyError, SortedBag, SortedMap, SortedSet};
