//! A persistent sequence that ranks and selects events whose occurrence depends on finite left
//! context.
//!
//! An ordinary sum-measured sequence can rank elements that carry a fixed local weight. It cannot
//! rank a comma that is a field separator in one prefix context and quoted data in another, because
//! no single weight is correct for both. A streaming automaton keeps that context but has to replay
//! a prefix after every edit.
//!
//! [`ContextualRankSequence`] resolves this by lifting a finite deterministic machine into the
//! measure of the crate's measured finger tree. Every subtree caches, *for every possible incoming
//! state*, the outgoing state and the number of events emitted while consuming that subtree.
//! Ordered composition feeds the left subtree's outgoing state into the right subtree's summary,
//! which turns a context-dependent scan into an associative measure:
//!
//! ```text
//! (A * B).next(q)   = B.next(A.next(q))
//! (A * B).events(q) = A.events(q) + B.events(A.next(q))
//! (A * B).count     = A.count + B.count
//! ```
//!
//! The identity maps every state to itself, emits zero events, and holds zero elements. Because
//! event counts are nonnegative, the select predicate `prefix.events(q0) > r` is monotone, so a
//! single measure-directed descent finds the element that emitted event `r` — no binary search and
//! no prefix replay.
//!
//! # Bounds
//!
//! Let `n` be the element count and `s` the machine's fixed state count. Full-sequence evaluation
//! is O(1): it reads the cached root summary. Indexing is O(log n) and composes no summary. Prefix
//! evaluation, contextual event rank and select, insertion, replacement, removal, splitting, and
//! slicing are O(s log n): a measure-directed descent over O(log n) levels, each level composing
//! one O(s) effect table. Enumeration is O(n), and building from a source is O(s n).
//!
//! Endpoint updates are Θ(s log n) per operation, not O(s). This is a Rust-substrate divergence
//! from the managed reference rather than a different algorithm: the C# reference sits on a lazy
//! finger tree, whose digits give amortized O(1) node work at either end, so the same push there
//! costs O(s). This crate's measured tree is a height-balanced join tree with no finger, so
//! pushing one element joins a singleton against a tree of height Θ(log n) and rebuilds every node
//! on that spine, each rebuild composing an O(s) effect table.
//!
//! Concatenation costs Θ(s · |h_left − h_right|), proportional to the operands' height difference:
//! the join descends the taller operand's spine until the heights meet and rebuilds exactly that
//! many nodes. For operands of comparable size that is O(s log(min(n, m))), the bound the C# lazy
//! finger tree meets in general; for asymmetric operands — appending a short run to a long
//! sequence — it degrades to O(s log(max(n, m))), so the log(min(n, m)) form does not hold here.
//!
//! Each retained changed spine adds O(s log n) summary storage while untouched structure stays
//! shared, so the structure costs O(s n) summary space rather than O(n).
//!
//! When the machine is fixed, `s` is a constant and the contextual queries are O(log n) instead of
//! the Θ(n) replay a state-free persistent sequence needs. If `s` is treated as an input rather
//! than a fixed policy, the structure does *not* dominate a plain persistent sequence: its edits
//! and its storage explicitly carry the `s` factor. The substrate is strict and does no lazy
//! amortization, so none of the figures above is an average earned back over a run of operations;
//! each is the cost of the single operation it describes.
//!
//! # Deliberate limits
//!
//! Context must be finite and must flow left to right. Event weights must be nonnegative so select
//! stays monotone. One input element occupies one measured leaf, so this is not a chunked text rope
//! and makes no cache-density claim. There is no way to change the machine policy of an existing
//! root; rebuilding under another machine costs O(s n).
//!
//! Snapshots are immutable, clone in O(1), and are `Send + Sync` whenever `T` is, so any number of
//! readers can share one version without locking.

use crate::measured::{FingerTree, Iter as MeasuredIter, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;
use std::sync::Arc;

/// One transition of a deterministic additive event machine.
///
/// `event_count` is the nonnegative number of logical events the transition emits; a single
/// transition may emit more than one.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct ContextualEventTransition {
    /// The state after consuming the element. Must be below the machine's state count.
    pub next_state: usize,
    /// The number of logical events emitted by the transition.
    pub event_count: u64,
}

impl ContextualEventTransition {
    /// Creates a transition from an outgoing state and an emitted-event count.
    #[must_use]
    pub const fn new(next_state: usize, event_count: u64) -> Self {
        Self {
            next_state,
            event_count,
        }
    }
}

/// A finite deterministic machine whose transitions emit nonnegative event counts.
///
/// Implementations are policy *types*: no value of the implementing type is ever created, which is
/// the Rust equivalent of the C# phantom policy type carrying static abstract members.
///
/// [`STATE_COUNT`](Self::STATE_COUNT) must be positive. For every state in `0..STATE_COUNT`,
/// [`transition`](Self::transition) must return a state in the same range, and it must be
/// deterministic and free of side effects. Violating either requirement is a policy bug and panics;
/// see [`ContextualRankSequence`] for the exact failure contract.
pub trait ContextualEventMachine<T> {
    /// The finite number of machine states. Must be greater than zero.
    const STATE_COUNT: usize;

    /// Consumes one element from one valid state and reports the outgoing state and event count.
    fn transition(state: usize, element: &T) -> ContextualEventTransition;
}

/// The result of running a contextual event machine over a prefix.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct ContextualPrefixSummary {
    /// The machine state after the prefix.
    pub final_state: usize,
    /// The number of events emitted by the prefix.
    pub event_count: u64,
}

impl ContextualPrefixSummary {
    /// Creates a prefix summary from a final state and an emitted-event count.
    #[must_use]
    pub const fn new(final_state: usize, event_count: u64) -> Self {
        Self {
            final_state,
            event_count,
        }
    }
}

/// Identifies one contextual event and the input element that emitted it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct ContextualEventLocation {
    /// The zero-based index of the emitting element.
    pub element_index: usize,
    /// The event's zero-based ordinal among the events emitted by that one transition.
    pub event_index_in_element: u64,
    /// The machine state before the element.
    pub state_before: usize,
    /// The machine state after the element.
    pub state_after: usize,
    /// The total number of events emitted by that transition.
    pub element_event_count: u64,
}

/// The two halves produced by [`ContextualRankSequence::split_at`].
///
/// Both halves are complete sequences that share structure with the original, which itself stays
/// valid.
pub struct ContextualRankSequenceSplit<T, M>
where
    M: ContextualEventMachine<T>,
{
    /// The elements before the boundary.
    pub left: ContextualRankSequence<T, M>,
    /// The elements at and after the boundary.
    pub right: ContextualRankSequence<T, M>,
}

/// Statistics reported by a successful [`ContextualRankSequence::validate_structure`].
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct ContextualRankSequenceStatistics {
    /// The number of elements, recounted by enumeration.
    pub count: usize,
    /// The machine's state count.
    pub state_count: usize,
    /// The largest total event count over all initial states, recomputed by direct scan.
    pub maximum_total_event_count: u64,
}

/// A structural invariant violation found by [`ContextualRankSequence::validate_structure`].
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ContextualRankSequenceInvariantError {
    /// The cached summary does not describe one effect per machine state.
    SummaryWidthMismatch,
    /// The cached element count disagrees with enumeration.
    ElementCountMismatch,
    /// The machine returned a state outside `0..STATE_COUNT` during the audit.
    InvalidNextState,
    /// A cached outgoing state disagrees with a direct machine scan.
    FinalStateMismatch,
    /// A cached event count disagrees with a direct machine scan.
    EventCountMismatch,
    /// Rescanning the elements overflowed the `u64` event total.
    EventCountOverflow,
}

impl fmt::Display for ContextualRankSequenceInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::SummaryWidthMismatch => {
                "the cached contextual summary does not cover every machine state"
            }
            Self::ElementCountMismatch => "the cached element count disagrees with enumeration",
            Self::InvalidNextState => "the machine returned a state outside its declared range",
            Self::FinalStateMismatch => "a cached outgoing state disagrees with a direct scan",
            Self::EventCountMismatch => "a cached event count disagrees with a direct scan",
            Self::EventCountOverflow => "rescanning the elements overflowed the event total",
        })
    }
}

impl std::error::Error for ContextualRankSequenceInvariantError {}

/// The effect of a subtree on one particular incoming state.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct StateEffect {
    next_state: usize,
    event_count: u64,
}

/// A subtree summary: one [`StateEffect`] per machine state, plus the element count.
///
/// The effect table sits behind an [`Arc`], so cloning a cached measure is O(1) even though the
/// table itself is O(s) wide.
#[derive(Clone, Debug, PartialEq, Eq)]
struct ContextualSummary {
    count: usize,
    effects: Arc<[StateEffect]>,
}

impl ContextualSummary {
    /// The monoid identity: every state maps to itself, no events, no elements.
    fn identity(state_count: usize) -> Self {
        Self {
            count: 0,
            effects: (0..state_count)
                .map(|state| StateEffect {
                    next_state: state,
                    event_count: 0,
                })
                .collect(),
        }
    }

    fn final_state(&self, initial_state: usize) -> usize {
        self.effects[initial_state].next_state
    }

    fn event_count(&self, initial_state: usize) -> u64 {
        self.effects[initial_state].event_count
    }

    fn evaluate(&self, initial_state: usize) -> ContextualPrefixSummary {
        let effect = self.effects[initial_state];
        ContextualPrefixSummary::new(effect.next_state, effect.event_count)
    }

    /// Ordered composition, `left` preceding `right`.
    fn combine(left: &Self, right: &Self) -> Self {
        if left.count == 0 {
            return right.clone();
        }

        if right.count == 0 {
            return left.clone();
        }

        let effects = left
            .effects
            .iter()
            .map(|effect| {
                let middle = right.effects[effect.next_state];
                StateEffect {
                    next_state: middle.next_state,
                    event_count: effect
                        .event_count
                        .checked_add(middle.event_count)
                        .expect("contextual event count overflowed u64"),
                }
            })
            .collect();
        Self {
            count: left
                .count
                .checked_add(right.count)
                .expect("contextual sequence length overflowed usize"),
            effects,
        }
    }
}

/// Reads the machine's state count, rejecting a machine that declares none.
fn machine_state_count<T, M>() -> usize
where
    M: ContextualEventMachine<T>,
{
    let state_count = M::STATE_COUNT;
    assert!(
        state_count > 0,
        "a contextual event machine must declare at least one state"
    );
    state_count
}

/// Builds a leaf summary by running the machine once from every incoming state.
fn element_summary<T, M>(element: &T) -> ContextualSummary
where
    M: ContextualEventMachine<T>,
{
    let state_count = machine_state_count::<T, M>();
    let effects = (0..state_count)
        .map(|state| {
            let transition = M::transition(state, element);
            assert!(
                transition.next_state < state_count,
                "the contextual event machine returned an invalid next state"
            );
            StateEffect {
                next_state: transition.next_state,
                event_count: transition.event_count,
            }
        })
        .collect();
    ContextualSummary { count: 1, effects }
}

/// The [`MeasurePolicy`] that lifts a machine into the measured finger tree.
///
/// `PhantomData<fn() -> (T, M)>` keeps the policy zero-sized and unconditionally `Send + Sync`, so
/// a sequence's thread safety depends only on its elements.
struct ContextualSummaryMeasure<T, M>(PhantomData<fn() -> (T, M)>);

impl<T, M> MeasurePolicy<T> for ContextualSummaryMeasure<T, M>
where
    M: ContextualEventMachine<T>,
{
    type Measure = ContextualSummary;

    fn empty() -> Self::Measure {
        ContextualSummary::identity(machine_state_count::<T, M>())
    }

    fn measure(element: &T) -> Self::Measure {
        element_summary::<T, M>(element)
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        ContextualSummary::combine(left, right)
    }
}

type ContextualTree<T, M> = FingerTree<T, ContextualSummaryMeasure<T, M>>;

/// An immutable sequence indexed by events whose occurrence depends on finite left context.
///
/// `M` is the deterministic additive event machine. Because it is a type parameter, two sequences
/// can only be concatenated when they were built under exactly the same machine. See the
/// [module documentation](self) for the lifted monoid and the complexity bounds.
///
/// # Failure contract
///
/// Out-of-range indices, out-of-range initial states, and out-of-range event ranks are ordinary
/// misses: the corresponding methods return [`None`], matching [`RrbVector`](crate::RrbVector) and
/// the sorted collections rather than panicking.
///
/// Two conditions are *policy* failures instead, and panic:
///
/// * a machine that returns a `next_state` outside `0..STATE_COUNT`, or that declares a
///   `STATE_COUNT` of zero — the same class of programming error as a non-monoidal
///   [`MeasurePolicy`], and the analogue of the C# `InvalidOperationException`; and
/// * a composed event total that exceeds [`u64::MAX`] — the analogue of the C# checked-`long`
///   `OverflowException`, raised here by `checked_add(..).expect(..)` so it fires in release builds
///   too.
///
/// Neither failure can be reported through [`MeasurePolicy::combine`], which has no fallible
/// channel, so neither is modelled as a [`Result`]. Both are failure-atomic regardless: every
/// version is an immutable persistent value, so unwinding publishes no successor and leaves every
/// retained version readable and unchanged.
///
/// The C# contract also rejects a negative emitted-event count. That check has no Rust counterpart
/// because `event_count` is a `u64`; the type makes the state unrepresentable. For the same reason
/// C#'s `int.MaxValue` length guard becomes a `usize` overflow check that no in-memory sequence can
/// reach.
pub struct ContextualRankSequence<T, M>
where
    M: ContextualEventMachine<T>,
{
    tree: ContextualTree<T, M>,
}

impl<T, M> Clone for ContextualRankSequence<T, M>
where
    M: ContextualEventMachine<T>,
{
    fn clone(&self) -> Self {
        Self {
            tree: self.tree.clone(),
        }
    }
}

impl<T, M> Default for ContextualRankSequence<T, M>
where
    M: ContextualEventMachine<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, M> fmt::Debug for ContextualRankSequence<T, M>
where
    T: fmt::Debug,
    M: ContextualEventMachine<T>,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<T, M> PartialEq for ContextualRankSequence<T, M>
where
    T: PartialEq,
    M: ContextualEventMachine<T>,
{
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len() && self.iter().eq(other.iter())
    }
}

impl<T, M> Eq for ContextualRankSequence<T, M>
where
    T: Eq,
    M: ContextualEventMachine<T>,
{
}

impl<T, M> FromIterator<T> for ContextualRankSequence<T, M>
where
    M: ContextualEventMachine<T>,
{
    /// Builds a sequence from a source in input order. O(s n).
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self {
            tree: iter.into_iter().collect(),
        }
    }
}

impl<'a, T, M> IntoIterator for &'a ContextualRankSequence<T, M>
where
    M: ContextualEventMachine<T>,
{
    type Item = &'a T;
    type IntoIter = ContextualRankSequenceIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T, M> ContextualRankSequence<T, M>
where
    M: ContextualEventMachine<T>,
{
    /// Creates the empty sequence. Its summary maps every state to itself with zero events.
    #[must_use]
    pub fn new() -> Self {
        Self {
            tree: FingerTree::new(),
        }
    }

    /// Returns the machine's finite state count.
    ///
    /// # Panics
    ///
    /// Panics when the machine declares zero states.
    #[must_use]
    pub fn state_count() -> usize {
        machine_state_count::<T, M>()
    }

    /// Returns the number of stored elements. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.tree.len()
    }

    /// Returns whether the sequence holds no elements. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.tree.is_empty()
    }

    /// Borrows the element at `index`, or [`None`] when it is out of range. O(log n).
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.tree.get(index)
    }

    /// Iterates the elements in sequence order. O(n) overall.
    pub fn iter(&self) -> ContextualRankSequenceIter<'_, T> {
        ContextualRankSequenceIter {
            inner: self.tree.iter(),
        }
    }

    /// Returns whether two versions share the same root, so neither can observe a change made to
    /// the other.
    ///
    /// This is a representation test, not an equality test, and is how this crate expresses the
    /// C# suite's reference-identity assertions about no-op results. O(1).
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.tree.shares_storage_with(&other.tree)
    }

    /// Runs the machine over the whole sequence from `initial_state`.
    ///
    /// Returns [`None`] when `initial_state` is outside `0..STATE_COUNT`. O(1): the answer is the
    /// cached root summary.
    #[must_use]
    pub fn evaluate(&self, initial_state: usize) -> Option<ContextualPrefixSummary> {
        self.check_state(initial_state)?;
        Some(self.tree.measure().evaluate(initial_state))
    }

    /// Runs the machine over the first `element_count` elements from `initial_state`.
    ///
    /// Returns [`None`] when `element_count` exceeds the length or `initial_state` is outside
    /// `0..STATE_COUNT`. O(s log n), and O(1) at either endpoint, where the answer is either the
    /// identity or the cached root summary.
    #[must_use]
    pub fn evaluate_prefix(
        &self,
        element_count: usize,
        initial_state: usize,
    ) -> Option<ContextualPrefixSummary> {
        self.check_state(initial_state)?;
        if element_count > self.len() {
            return None;
        }

        if element_count == 0 {
            return Some(ContextualPrefixSummary::new(initial_state, 0));
        }

        if element_count == self.len() {
            return Some(self.tree.measure().evaluate(initial_state));
        }

        Some(
            self.tree
                .prefix_measure(element_count)
                .expect("a validated prefix length has a measure")
                .evaluate(initial_state),
        )
    }

    /// Returns the number of contextual events strictly before an element boundary.
    ///
    /// Returns [`None`] on the same inputs as [`Self::evaluate_prefix`]. O(s log n).
    #[must_use]
    pub fn event_rank(&self, element_count: usize, initial_state: usize) -> Option<u64> {
        self.evaluate_prefix(element_count, initial_state)
            .map(|summary| summary.event_count)
    }

    /// Locates the zero-based contextual event `event_index`, counting from `initial_state`.
    ///
    /// Returns [`None`] when `initial_state` is outside `0..STATE_COUNT` or when `event_index` is
    /// at or past the sequence's total event count. A negative rank is unrepresentable, so the C#
    /// guard against one has no counterpart here.
    ///
    /// O(s log n): a measure-directed descent to the boundary, a second descent to recover the
    /// prefix summary at it, and exactly one machine transition, for the located element.
    #[must_use]
    pub fn try_select_event(
        &self,
        event_index: u64,
        initial_state: usize,
    ) -> Option<ContextualEventLocation> {
        self.check_state(initial_state)?;
        if event_index >= self.tree.measure().event_count(initial_state) {
            return None;
        }

        let search = self
            .tree
            .cursor_by_measure(|summary| summary.event_count(initial_state) > event_index);
        if !search.found {
            return None;
        }

        let before = search.cursor.measure_before();
        let element = search.cursor.peek_next()?;
        let state_before = before.final_state(initial_state);
        let transition = M::transition(state_before, element);
        Some(ContextualEventLocation {
            element_index: before.count,
            event_index_in_element: event_index - before.event_count(initial_state),
            state_before,
            state_after: transition.next_state,
            element_event_count: transition.event_count,
        })
    }

    /// Returns a sequence with `item` prepended.
    ///
    /// Θ(s log n): the substrate has no finger, so this joins a singleton against a tree of height
    /// Θ(log n) and recomposes an O(s) effect table at every node on that spine. The C# reference's
    /// lazy finger tree does this in O(s); see the module-level `Bounds` section.
    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        Self {
            tree: Self::singleton_tree(item).concat(&self.tree),
        }
    }

    /// Returns a sequence with `item` appended. Θ(s log n), for the reason given on
    /// [`push_front`](Self::push_front).
    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        Self {
            tree: self.tree.concat(&Self::singleton_tree(item)),
        }
    }

    /// Concatenates another sequence built under the same machine.
    ///
    /// An empty operand is not copied: the result shares the non-empty operand's root.
    ///
    /// Θ(s · |h_left − h_right|), proportional to the operands' height difference: that is
    /// O(s log(min(n, m))) for operands of comparable size, but O(s log(max(n, m))) when they are
    /// asymmetric. Only the balanced case matches the C# reference's log(min(n, m)) bound.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        Self {
            tree: self.tree.concat(&other.tree),
        }
    }

    /// Inserts `item` so that `index` old elements precede it, or returns [`None`] when `index`
    /// exceeds the length. O(s log n), including at either endpoint: the endpoint cases delegate to
    /// [`push_front`](Self::push_front) and [`push_back`](Self::push_back), which are themselves
    /// Θ(s log n) on this substrate.
    #[must_use]
    pub fn insert(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        if index == 0 {
            return Some(self.push_front(item));
        }

        if index == self.len() {
            return Some(self.push_back(item));
        }

        let split = self
            .tree
            .split_at_index(index)
            .expect("a validated boundary can be split");
        Some(Self {
            tree: split
                .left
                .concat(&Self::singleton_tree(item))
                .concat(&split.right),
        })
    }

    /// Replaces the element at `index`, or returns [`None`] when `index` is out of range.
    /// O(s log n).
    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let split = self
            .tree
            .split_at_index(index)
            .expect("a validated element index can be split");
        let tail = split
            .right
            .split_at_index(1)
            .expect("a validated element index has a following element");
        Some(Self {
            tree: split
                .left
                .concat(&Self::singleton_tree(item))
                .concat(&tail.right),
        })
    }

    /// Removes the element at `index`, or returns [`None`] when `index` is out of range.
    /// O(s log n).
    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let split = self
            .tree
            .split_at_index(index)
            .expect("a validated element index can be split");
        let tail = split
            .right
            .split_at_index(1)
            .expect("a validated element index has a following element");
        Some(Self {
            tree: split.left.concat(&tail.right),
        })
    }

    /// Splits at an element boundary, or returns [`None`] when `index` exceeds the length.
    ///
    /// Splitting at either endpoint shares the receiver's root with the non-empty half.
    /// O(s log n).
    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<ContextualRankSequenceSplit<T, M>> {
        let split = self.tree.split_at_index(index)?;
        Some(ContextualRankSequenceSplit {
            left: Self { tree: split.left },
            right: Self { tree: split.right },
        })
    }

    /// Returns the `count` elements starting at `index`, or [`None`] when the range falls outside
    /// the sequence.
    ///
    /// A range covering the whole sequence shares the receiver's root. O(s log n).
    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = index.checked_add(count)?;
        if end > self.len() {
            return None;
        }

        if count == 0 {
            return Some(Self::new());
        }

        if count == self.len() {
            return Some(self.clone());
        }

        let tail = self
            .tree
            .split_at_index(index)
            .expect("a validated range start can be split")
            .right;
        Some(Self {
            tree: tail
                .split_at_index(count)
                .expect("a validated range length can be split")
                .left,
        })
    }

    /// Recomputes every cached root summary by direct machine scan and compares it with the tree.
    ///
    /// A defensive audit; ordinary operations maintain these invariants. O(s n).
    ///
    /// # Errors
    ///
    /// Returns the first [`ContextualRankSequenceInvariantError`] observed.
    pub fn validate_structure(
        &self,
    ) -> Result<ContextualRankSequenceStatistics, ContextualRankSequenceInvariantError> {
        let state_count = machine_state_count::<T, M>();
        let measure = self.tree.measure();
        if measure.effects.len() != state_count {
            return Err(ContextualRankSequenceInvariantError::SummaryWidthMismatch);
        }

        let count = self.iter().count();
        if count != measure.count || count != self.len() {
            return Err(ContextualRankSequenceInvariantError::ElementCountMismatch);
        }

        let mut maximum_total_event_count = 0;
        for initial_state in 0..state_count {
            let mut state = initial_state;
            let mut events: u64 = 0;
            for element in self.iter() {
                let transition = M::transition(state, element);
                if transition.next_state >= state_count {
                    return Err(ContextualRankSequenceInvariantError::InvalidNextState);
                }

                events = events
                    .checked_add(transition.event_count)
                    .ok_or(ContextualRankSequenceInvariantError::EventCountOverflow)?;
                state = transition.next_state;
            }

            if state != measure.final_state(initial_state) {
                return Err(ContextualRankSequenceInvariantError::FinalStateMismatch);
            }

            if events != measure.event_count(initial_state) {
                return Err(ContextualRankSequenceInvariantError::EventCountMismatch);
            }

            maximum_total_event_count = maximum_total_event_count.max(events);
        }

        Ok(ContextualRankSequenceStatistics {
            count,
            state_count,
            maximum_total_event_count,
        })
    }

    fn check_state(&self, state: usize) -> Option<()> {
        (state < machine_state_count::<T, M>()).then_some(())
    }

    fn singleton_tree(item: T) -> ContextualTree<T, M> {
        std::iter::once(item).collect()
    }
}

impl<T, M> ContextualRankSequence<T, M>
where
    T: Clone,
    M: ContextualEventMachine<T>,
{
    /// Copies every element into a new vector, in sequence order. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.tree.to_vec()
    }
}

/// Borrowing iterator over a [`ContextualRankSequence`] in sequence order.
pub struct ContextualRankSequenceIter<'a, T> {
    inner: MeasuredIter<'a, T, ContextualSummary>,
}

impl<'a, T> Iterator for ContextualRankSequenceIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> ExactSizeIterator for ContextualRankSequenceIter<'_, T> {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::panic::{self, AssertUnwindSafe};
    use std::sync::atomic::{AtomicU64, Ordering};
    use std::thread;

    /// Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event.
    struct QuotedCommaMachine;

    impl ContextualEventMachine<char> for QuotedCommaMachine {
        const STATE_COUNT: usize = 2;

        fn transition(state: usize, element: &char) -> ContextualEventTransition {
            match element {
                '"' => ContextualEventTransition::new(1 - state, 0),
                ',' if state == 0 => ContextualEventTransition::new(state, 1),
                _ => ContextualEventTransition::new(state, 0),
            }
        }
    }

    /// Three states, with transitions that emit zero, one, or two events.
    struct TernaryMachine;

    impl ContextualEventMachine<i32> for TernaryMachine {
        const STATE_COUNT: usize = 3;

        fn transition(state: usize, element: &i32) -> ContextualEventTransition {
            let magnitude = element.unsigned_abs() as usize;
            let next = (state + magnitude) % 3;
            ContextualEventTransition::new(next, next as u64)
        }
    }

    static COUNTING_CALLS: AtomicU64 = AtomicU64::new(0);

    /// Counts every transition so tests can prove cached queries do not rescan the input.
    struct CountingMachine;

    impl ContextualEventMachine<i32> for CountingMachine {
        const STATE_COUNT: usize = 2;

        fn transition(state: usize, element: &i32) -> ContextualEventTransition {
            COUNTING_CALLS.fetch_add(1, Ordering::Relaxed);
            let element = *element as usize;
            ContextualEventTransition::new(
                (state + (element & 1)) & 1,
                u64::from(((element ^ state) & 3) == 0),
            )
        }
    }

    /// Declares one state but leaves it, violating the machine contract.
    struct InvalidStateMachine;

    impl ContextualEventMachine<i32> for InvalidStateMachine {
        const STATE_COUNT: usize = 1;

        fn transition(_state: usize, _element: &i32) -> ContextualEventTransition {
            ContextualEventTransition::new(1, 0)
        }
    }

    /// Emits the largest representable event count, so two elements overflow.
    struct HugeEventMachine;

    impl ContextualEventMachine<i32> for HugeEventMachine {
        const STATE_COUNT: usize = 1;

        fn transition(_state: usize, _element: &i32) -> ContextualEventTransition {
            ContextualEventTransition::new(0, u64::MAX)
        }
    }

    /// Declares no states at all, which is a policy bug.
    struct StatelessMachine;

    impl ContextualEventMachine<i32> for StatelessMachine {
        const STATE_COUNT: usize = 0;

        fn transition(state: usize, _element: &i32) -> ContextualEventTransition {
            ContextualEventTransition::new(state, 0)
        }
    }

    /// Checks a sequence against an independent list plus a direct machine scan, for every state,
    /// every prefix boundary, and every event.
    fn assert_matches_model<T, M>(sequence: &ContextualRankSequence<T, M>, model: &[T])
    where
        T: Clone + PartialEq + fmt::Debug,
        M: ContextualEventMachine<T>,
    {
        assert_eq!(sequence.to_vec(), model);
        assert_eq!(sequence.len(), model.len());
        assert_eq!(sequence.is_empty(), model.is_empty());
        sequence.validate_structure().expect("valid structure");

        for initial_state in 0..ContextualRankSequence::<T, M>::state_count() {
            let mut state = initial_state;
            let mut events = 0;
            let mut expected_locations = Vec::new();
            assert_eq!(
                sequence.evaluate_prefix(0, initial_state),
                Some(ContextualPrefixSummary::new(state, events))
            );

            for (index, element) in model.iter().enumerate() {
                let step = M::transition(state, element);
                for ordinal in 0..step.event_count {
                    expected_locations.push(ContextualEventLocation {
                        element_index: index,
                        event_index_in_element: ordinal,
                        state_before: state,
                        state_after: step.next_state,
                        element_event_count: step.event_count,
                    });
                }

                state = step.next_state;
                events += step.event_count;
                assert_eq!(
                    sequence.evaluate_prefix(index + 1, initial_state),
                    Some(ContextualPrefixSummary::new(state, events))
                );
                assert_eq!(sequence.event_rank(index + 1, initial_state), Some(events));
                assert_eq!(sequence.get(index), Some(element));
            }

            assert_eq!(
                sequence.evaluate(initial_state),
                Some(ContextualPrefixSummary::new(state, events))
            );

            for (event_index, expected) in expected_locations.iter().enumerate() {
                assert_eq!(
                    sequence.try_select_event(event_index as u64, initial_state),
                    Some(*expected)
                );
            }

            assert_eq!(
                sequence.try_select_event(expected_locations.len() as u64, initial_state),
                None
            );
        }
    }

    fn next_random(state: &mut u64) -> u64 {
        *state = state
            .wrapping_mul(6_364_136_223_846_793_005)
            .wrapping_add(1_442_695_040_888_963_407);
        *state >> 32
    }

    fn random_below(state: &mut u64, bound: usize) -> usize {
        (next_random(state) as usize) % bound
    }

    /// Runs `action`, capturing an expected panic without printing a backtrace.
    fn silently<R>(action: impl FnOnce() -> R) -> std::thread::Result<R> {
        let previous = panic::take_hook();
        panic::set_hook(Box::new(|_| {}));
        let result = panic::catch_unwind(AssertUnwindSafe(action));
        panic::set_hook(previous);
        result
    }

    #[test]
    fn quoted_delimiter_machine_ranks_and_selects_context_dependent_events() {
        let text: ContextualRankSequence<char, QuotedCommaMachine> = "\"a,b\",c,d".chars().collect();

        assert_eq!(text.evaluate(0), Some(ContextualPrefixSummary::new(0, 2)));
        assert_eq!(text.evaluate(1), Some(ContextualPrefixSummary::new(1, 1)));
        assert_eq!(text.event_rank(5, 0), Some(0));
        assert_eq!(text.event_rank(6, 0), Some(1));

        assert_eq!(
            text.try_select_event(0, 0),
            Some(ContextualEventLocation {
                element_index: 5,
                event_index_in_element: 0,
                state_before: 0,
                state_after: 0,
                element_event_count: 1,
            })
        );
        assert_eq!(
            text.try_select_event(1, 0).map(|found| found.element_index),
            Some(7)
        );
        assert_eq!(text.try_select_event(2, 0), None);

        // Starting inside a quoted region makes the comma at index 2 an event instead.
        assert_eq!(
            text.try_select_event(0, 1).map(|found| found.element_index),
            Some(2)
        );
        assert_eq!(text.to_vec(), "\"a,b\",c,d".chars().collect::<Vec<_>>());

        // Editing does not disturb the retained version.
        let edited = text.insert(0, '"').expect("valid boundary");
        assert_eq!(text.evaluate(0), Some(ContextualPrefixSummary::new(0, 2)));
        assert_eq!(edited.evaluate(0), Some(ContextualPrefixSummary::new(1, 1)));
    }

    #[test]
    fn exhaustive_small_words_match_independent_scanner() {
        let alphabet = ['"', ',', 'x'];
        for length in 0..=7_u32 {
            for code in 0..alphabet.len().pow(length) {
                let mut value = code;
                let mut word = Vec::with_capacity(length as usize);
                for _ in 0..length {
                    word.push(alphabet[value % alphabet.len()]);
                    value /= alphabet.len();
                }

                let sequence: ContextualRankSequence<char, QuotedCommaMachine> =
                    word.iter().copied().collect();
                assert_matches_model(&sequence, &word);
            }
        }
    }

    #[test]
    fn randomized_retained_branches_match_list_and_machine_model() {
        let mut rng = 0x61C7_2026_u64;
        let mut versions: Vec<(ContextualRankSequence<i32, TernaryMachine>, Vec<i32>)> =
            vec![(ContextualRankSequence::new(), Vec::new())];

        for step in 0..800 {
            let parent_index = random_below(&mut rng, versions.len());
            let parent = versions[parent_index].0.clone();
            let mut model = versions[parent_index].1.clone();
            let next = match random_below(&mut rng, 8) {
                0 => {
                    let item = random_below(&mut rng, 19) as i32 - 9;
                    model.insert(0, item);
                    parent.push_front(item)
                }
                1 => {
                    let item = random_below(&mut rng, 19) as i32 - 9;
                    model.push(item);
                    parent.push_back(item)
                }
                2 => {
                    let item = random_below(&mut rng, 19) as i32 - 9;
                    let index = random_below(&mut rng, model.len() + 1);
                    model.insert(index, item);
                    parent.insert(index, item).expect("valid boundary")
                }
                3 if !model.is_empty() => {
                    let index = random_below(&mut rng, model.len());
                    let item = random_below(&mut rng, 19) as i32 - 9;
                    model[index] = item;
                    parent.set_item(index, item).expect("valid index")
                }
                4 if !model.is_empty() => {
                    let index = random_below(&mut rng, model.len());
                    model.remove(index);
                    parent.remove_at(index).expect("valid index")
                }
                5 => {
                    let index = random_below(&mut rng, model.len() + 1);
                    let count = random_below(&mut rng, model.len() - index + 1);
                    model = model[index..index + count].to_vec();
                    parent.get_range(index, count).expect("valid range")
                }
                _ => {
                    let suffix_index = random_below(&mut rng, versions.len());
                    if model.len() + versions[suffix_index].1.len() > 96 {
                        parent.clone()
                    } else {
                        model.extend_from_slice(&versions[suffix_index].1);
                        parent.concat(&versions[suffix_index].0)
                    }
                }
            };

            assert_matches_model(&next, &model);
            versions.push((next, model));

            if versions.len() > 64 {
                let victim = 1 + random_below(&mut rng, versions.len() - 2);
                versions.remove(victim);
            }

            if step % 37 == 0 {
                let retained = random_below(&mut rng, versions.len());
                let (sequence, model) = &versions[retained];
                assert_matches_model(sequence, model);
            }
        }

        // Every retained branch is still exactly what it was when it was recorded.
        for (sequence, model) in &versions {
            assert_matches_model(sequence, model);
        }
    }

    #[test]
    fn retained_versions_are_independent_after_branching() {
        let source: ContextualRankSequence<char, QuotedCommaMachine> = "a,b,c".chars().collect();
        let quoted = source.insert(0, '"').expect("valid boundary");
        let trimmed = source.remove_at(1).expect("valid index");
        let replaced = source.set_item(1, '"').expect("valid index");

        assert_eq!(source.evaluate(0), Some(ContextualPrefixSummary::new(0, 2)));
        assert_eq!(quoted.evaluate(0), Some(ContextualPrefixSummary::new(1, 0)));
        assert_eq!(trimmed.evaluate(0), Some(ContextualPrefixSummary::new(0, 1)));
        assert_eq!(
            replaced.evaluate(0),
            Some(ContextualPrefixSummary::new(1, 0))
        );
        assert_eq!(source.to_vec(), "a,b,c".chars().collect::<Vec<_>>());
        assert_eq!(quoted.to_vec(), "\"a,b,c".chars().collect::<Vec<_>>());
        assert_eq!(trimmed.to_vec(), "ab,c".chars().collect::<Vec<_>>());
        assert_eq!(replaced.to_vec(), "a\"b,c".chars().collect::<Vec<_>>());
        source.validate_structure().expect("valid source");
        quoted.validate_structure().expect("valid branch");
        trimmed.validate_structure().expect("valid branch");
        replaced.validate_structure().expect("valid branch");
    }

    #[test]
    fn boundaries_multi_events_and_invalid_arguments_are_exact() {
        let sequence: ContextualRankSequence<i32, TernaryMachine> =
            [2, -3, 4, 0].into_iter().collect();
        let empty = ContextualRankSequence::<i32, TernaryMachine>::new();

        for index in 0..=sequence.len() {
            let split = sequence.split_at(index).expect("valid boundary");
            assert_eq!(split.left.concat(&split.right).to_vec(), sequence.to_vec());
            assert_eq!(split.left.len() + split.right.len(), sequence.len());
        }

        assert!(sequence.shares_storage_with(&sequence.concat(&empty)));
        assert!(sequence.shares_storage_with(&empty.concat(&sequence)));
        assert!(sequence.shares_storage_with(&sequence.get_range(0, sequence.len()).unwrap()));
        assert!(sequence.shares_storage_with(&sequence.split_at(0).unwrap().right));
        assert!(sequence.shares_storage_with(&sequence.split_at(sequence.len()).unwrap().left));

        let located = sequence
            .try_select_event(0, 0)
            .expect("the sequence emits events");
        assert!(located.event_index_in_element < located.element_event_count);
        assert!(
            (0..sequence.len()).any(|index| {
                let after = sequence.event_rank(index + 1, 0).unwrap();
                let before = sequence.event_rank(index, 0).unwrap();
                after - before > 1
            }),
            "the ternary machine must exercise multi-event transitions"
        );

        assert_eq!(sequence.evaluate(3), None);
        assert_eq!(sequence.evaluate_prefix(0, 3), None);
        assert_eq!(sequence.evaluate_prefix(sequence.len() + 1, 0), None);
        assert_eq!(sequence.event_rank(sequence.len() + 1, 0), None);
        assert_eq!(sequence.try_select_event(0, 3), None);
        assert_eq!(sequence.get(sequence.len()), None);
        assert!(sequence.insert(sequence.len() + 1, 0).is_none());
        assert!(sequence.set_item(sequence.len(), 0).is_none());
        assert!(sequence.remove_at(sequence.len()).is_none());
        assert!(sequence.split_at(sequence.len() + 1).is_none());
        assert!(sequence.get_range(1, sequence.len()).is_none());
        assert!(sequence.get_range(0, usize::MAX).is_none());
        assert!(sequence.get_range(usize::MAX, 1).is_none());

        assert!(empty.is_empty());
        assert_eq!(empty.evaluate(2), Some(ContextualPrefixSummary::new(2, 0)));
        assert_eq!(
            empty.evaluate_prefix(0, 1),
            Some(ContextualPrefixSummary::new(1, 0))
        );
        assert_eq!(empty.try_select_event(0, 0), None);
        assert!(empty.get_range(0, 0).unwrap().is_empty());
        assert_eq!(empty.validate_structure().unwrap().count, 0);

        let singleton = empty.push_back(5);
        assert_eq!(singleton.len(), 1);
        assert_eq!(singleton.get(0), Some(&5));
        assert!(singleton.remove_at(0).unwrap().is_empty());
        assert_eq!(singleton.iter().copied().collect::<Vec<_>>(), vec![5]);
        assert_matches_model(&singleton, &[5]);
    }

    #[test]
    fn cached_rank_and_select_do_not_rescan_the_input() {
        let sequence: ContextualRankSequence<i32, CountingMachine> = (0..8_192).collect();
        COUNTING_CALLS.store(0, Ordering::Relaxed);

        for boundary in (0..=sequence.len()).step_by(17) {
            let _ = sequence
                .event_rank(boundary, boundary & 1)
                .expect("valid boundary and state");
        }

        assert_eq!(
            COUNTING_CALLS.load(Ordering::Relaxed),
            0,
            "cached prefix ranks must not invoke the machine"
        );

        let total = sequence.evaluate(0).expect("valid state").event_count;
        assert!(total > 0);
        let mut successful = 0;
        let mut event_index = 0;
        while event_index < total {
            assert!(sequence.try_select_event(event_index, 0).is_some());
            successful += 1;
            event_index += 31;
        }

        assert_eq!(
            COUNTING_CALLS.load(Ordering::Relaxed),
            successful,
            "each successful select must invoke exactly one transition"
        );
    }

    #[test]
    fn invalid_transitions_and_overflow_are_failure_atomic() {
        let invalid = ContextualRankSequence::<i32, InvalidStateMachine>::new();
        assert!(silently(|| invalid.push_back(1)).is_err());
        assert!(invalid.is_empty());

        assert!(
            silently(ContextualRankSequence::<i32, StatelessMachine>::new).is_err(),
            "a zero-state machine must be rejected"
        );

        let source = ContextualRankSequence::<i32, HugeEventMachine>::new().push_back(1);
        assert_eq!(
            source.evaluate(0),
            Some(ContextualPrefixSummary::new(0, u64::MAX))
        );
        assert!(silently(|| source.push_back(2)).is_err());
        assert!(silently(|| source.concat(&source)).is_err());
        assert!(silently(|| source.insert(0, 2)).is_err());

        // The rejected successors published nothing; the retained version is untouched.
        assert_eq!(source.to_vec(), vec![1]);
        assert_eq!(
            source.evaluate(0),
            Some(ContextualPrefixSummary::new(0, u64::MAX))
        );
        source.validate_structure().expect("source stays valid");
    }

    #[test]
    fn concurrent_readers_and_independent_writers_observe_stable_versions() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<ContextualRankSequence<i32, TernaryMachine>>();

        let source: ContextualRankSequence<i32, TernaryMachine> = (-256..=256).collect();
        let expected = source.to_vec();
        let expected_summaries = (0..ContextualRankSequence::<i32, TernaryMachine>::state_count())
            .map(|state| source.evaluate(state).expect("valid state"))
            .collect::<Vec<_>>();

        let handles = (0..8_usize)
            .map(|worker| {
                let source = source.clone();
                let expected = expected.clone();
                let expected_summaries = expected_summaries.clone();
                thread::spawn(move || {
                    let mut branch = source.clone();
                    for iteration in 0..200_usize {
                        let state = (worker + iteration) % expected_summaries.len();
                        assert_eq!(source.evaluate(state), Some(expected_summaries[state]));
                        let boundary = (worker * 31 + iteration * 17) % (source.len() + 1);
                        assert!(source.event_rank(boundary, state).is_some());
                        let total = expected_summaries[state].event_count;
                        if total > 0 {
                            let rank = (worker as u64 + iteration as u64) % total;
                            assert!(source.try_select_event(rank, state).is_some());
                        }

                        branch = branch
                            .set_item(iteration % branch.len(), worker as i32 - iteration as i32)
                            .expect("valid index");
                    }

                    assert_eq!(source.to_vec(), expected);
                    assert_eq!(branch.len(), source.len());
                })
            })
            .collect::<Vec<_>>();

        for handle in handles {
            handle.join().expect("reader thread failed");
        }

        assert_eq!(source.to_vec(), expected);
        source.validate_structure().expect("shared version stays valid");
    }

    #[test]
    fn validate_structure_reports_recomputed_statistics() {
        let sequence: ContextualRankSequence<char, QuotedCommaMachine> =
            "\"a,b\",c,d".chars().collect();
        let statistics = sequence.validate_structure().expect("valid structure");

        assert_eq!(statistics.count, 9);
        assert_eq!(statistics.state_count, 2);
        assert_eq!(statistics.maximum_total_event_count, 2);
        assert_eq!(
            ContextualRankSequence::<char, QuotedCommaMachine>::state_count(),
            2
        );
        assert_eq!(format!("{:?}", sequence.get(0)), "Some('\"')");
        assert_eq!(
            sequence,
            "\"a,b\",c,d"
                .chars()
                .collect::<ContextualRankSequence<char, QuotedCommaMachine>>()
        );
    }
}
