//! A fixed-length persistent vector carrying a checkpoint and an exact maximal-run index of the
//! positions whose current values differ from that checkpoint.
//!
//! [`PersistentRunDeltaVector`] keeps two logical views of the same fixed-length sequence — the
//! current values and a checkpoint — plus one persistent ordered map holding a
//! `start -> end_exclusive` record for every *maximal* run of differing positions. Dirtiness is
//! relative to a retained [`EqualityPolicy`], not to write history: writing a policy-equal value is
//! a semantic no-op, and returning a position to its checkpoint equality class cancels that
//! position's delta and restores the exact checkpoint representative.
//!
//! Let `n` be the fixed length, `k` the number of dirty positions, and `r` the number of maximal
//! dirty runs, so `0 <= r <= k <= n`. Against a state-only pair of unindexed current/checkpoint RRB
//! roots, the one strict result is that exact dirty-run descriptor discovery is output-optimal
//! `Theta(r)` rather than worst-case `Theta(n)`, and no common operation loses the baseline's
//! asymptotic time or total-space bound. The gap is unbounded: one contiguous changed block gives
//! `k = n` and `r = 1`. Emitting the changed payload *values* is still `Omega(k)` and is not claimed
//! to be faster. This is not a claim of a new interval algorithm or of a new persistent-array
//! technique; the run index is an application of the Discrete Interval Encoding Tree idea, and a
//! persistent RRB vector plus a balanced DIET is an equal-bound construction of the same design.
//!
//! Shipped bounds:
//!
//! | Operation | Bound |
//! | --- | --- |
//! | Build `n` values | `Theta(n)` |
//! | Read a current or checkpoint value | `O(log n)` |
//! | Persistent point edit | `O(log n)` worst-case time and fresh nodes |
//! | Fork by retaining or cloning a handle | `O(1)` |
//! | Whole checkpoint or rollback | `O(1)` |
//! | Enumerate current values | `Theta(n)` |
//! | Dirty membership | `O(log(r + 2))` worst-case |
//! | Exact dirty-run descriptors | `Theta(r)` |
//! | Select a dirty run by rank | `O(log(r + 2))` worst-case |
//! | Accept or revert one indexed run | `O(log n)` worst-case, independent of that run's length |
//! | Live delta metadata | `Theta(r)` |
//! | Total live structure | `O(n)` |
//!
//! Accepting or reverting a run is implemented by structural RRB splitting and concatenation, never
//! by walking that run's positions, which is what makes it independent of the run's length. That
//! splice bound is inherited from RRB trees and is not itself a novelty claim. Starting from one
//! `n`-element version, `m` retained effective point edits use `O(n + m log n)` worst-case total
//! structural space; that history bound does not collapse to `O(n)`.
//!
//! Length is fixed at construction: there is deliberately no insertion, deletion, concatenation,
//! split, range assignment, rebase, or merge of unrelated branches, because checkpoint
//! correspondence would need a position-transformation policy this abstract data type does not
//! define. There is one checkpoint per version.
//!
//! Every version is immutable and every producing operation leaves its receiver usable, so any
//! retained version can be branched again. All policy calls in an edit happen before any successor
//! is constructed, so a panicking comparer publishes no partial version. Concurrent reads and
//! independent branch derivation are safe when the retained policy is safe for concurrent calls,
//! which [`EqualityComparer`] requires; the policy must stay one stable equivalence relation, and
//! values retained by a version must not mutate state that policy observes, or the maintained index
//! can go stale.
//!
//! Raw `f32` and `f64` are deliberately excluded from the plain natural policy, which is why the
//! natural-policy constructors bound `T: Eq`: IEEE equality is not reflexive, so writing `NaN` over
//! `NaN` would be recorded as a change and leave a phantom run that no invariant check can detect.
//! Float payloads pass [`EqualityPolicy::reflexive_ieee`] to
//! [`PersistentRunDeltaVector::from_values_with_policy`] instead.

use std::fmt;
use std::iter::FusedIterator;
use std::ops::Index;
use std::sync::Arc;

use crate::equality::{EqualityComparer, EqualityPolicy};
use crate::rrb_vector::{RrbVector, RrbVectorIter};
use crate::sorted::SortedMap;

/// One element slot, identified by pointer rather than by value.
///
/// The cleanliness invariant is "a clean position reuses its exact checkpoint representative", which
/// needs reference identity rather than value equality: the retained [`EqualityPolicy`] may be
/// coarser or finer than natural equality, so it cannot decide representative identity. Retaining an
/// [`Arc`] and comparing with [`Arc::ptr_eq`] supplies that identity without a second allocation
/// layer, and turns [`RrbVector::set_item`]'s equal-replacement short circuit into an identity test,
/// which is exactly the test wanted there.
struct Cell<T>(Arc<T>);

impl<T> Cell<T> {
    fn new(value: T) -> Self {
        Self(Arc::new(value))
    }

    fn value(&self) -> &T {
        &self.0
    }
}

impl<T> Clone for Cell<T> {
    fn clone(&self) -> Self {
        Self(Arc::clone(&self.0))
    }
}

impl<T> PartialEq for Cell<T> {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

/// One maximal half-open run of checkpoint-relative changes.
///
/// A run covers the positions `start .. start + length` and is never empty. The runs published by
/// one version are ordered, non-overlapping, non-adjacent, and maximal.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct PersistentDirtyRun {
    /// The zero-based first changed position.
    pub start: usize,
    /// The number of consecutive changed positions.
    pub length: usize,
}

impl PersistentDirtyRun {
    /// Creates a run descriptor covering `length` positions from `start`.
    #[must_use]
    pub fn new(start: usize, length: usize) -> Self {
        Self { start, length }
    }

    /// Returns the exclusive end position.
    #[must_use]
    pub fn end_exclusive(&self) -> usize {
        self.start + self.length
    }

    /// Reports whether `index` lies inside this run.
    #[must_use]
    pub fn contains(&self, index: usize) -> bool {
        index >= self.start && index < self.end_exclusive()
    }
}

impl fmt::Display for PersistentDirtyRun {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "[{}, {})", self.start, self.end_exclusive())
    }
}

/// Successful run-delta-vector invariant statistics.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RunDeltaVectorStatistics {
    /// The fixed number of positions.
    pub len: usize,
    /// The number of positions differing from the checkpoint, recounted from the run index.
    pub dirty_count: usize,
    /// The number of maximal dirty runs.
    pub dirty_run_count: usize,
}

/// An invariant failure detected by [`PersistentRunDeltaVector::validate_structure`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RunDeltaVectorInvariantError {
    /// The current and checkpoint roots hold different numbers of positions.
    RootLengthMismatch,
    /// The cached dirty cardinality exceeds the number of positions.
    DirtyCountOutOfRange,
    /// The current RRB root failed its own structural validation.
    CurrentRootInvalid,
    /// The checkpoint RRB root failed its own structural validation.
    CheckpointRootInvalid,
    /// The run index is empty, yet the version is not canonicalized onto its checkpoint root with a
    /// zero dirty cardinality.
    CleanVersionNotCanonical,
    /// A run record is empty, out of bounds, or not strictly after and separated from its
    /// predecessor, so the index is not an ordered non-adjacent maximal encoding.
    MalformedRunIndex,
    /// The run lengths do not sum to the cached dirty cardinality.
    DirtyCountMismatch,
    /// A position outside every run does not reuse its exact checkpoint representative.
    CleanPositionNotShared,
    /// A position inside a run is policy-equal to its checkpoint value, so that run is untruthful.
    DirtyPositionEqualsCheckpoint,
}

impl fmt::Display for RunDeltaVectorInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::RootLengthMismatch => "the current and checkpoint roots have different lengths",
            Self::DirtyCountOutOfRange => "the dirty cardinality exceeds the position count",
            Self::CurrentRootInvalid => "the current RRB root is structurally invalid",
            Self::CheckpointRootInvalid => "the checkpoint RRB root is structurally invalid",
            Self::CleanVersionNotCanonical => {
                "a clean version is not canonicalized to its checkpoint root"
            }
            Self::MalformedRunIndex => {
                "dirty runs are empty, out of range, overlapping, adjacent, or unordered"
            }
            Self::DirtyCountMismatch => "dirty run lengths do not sum to the dirty cardinality",
            Self::CleanPositionNotShared => {
                "a clean position does not reuse its exact checkpoint representative"
            }
            Self::DirtyPositionEqualsCheckpoint => {
                "a dirty position is policy-equal to its checkpoint value"
            }
        })
    }
}

impl std::error::Error for RunDeltaVectorInvariantError {}

/// A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.
///
/// See the [module documentation](self) for the representation, the shipped bounds, and the
/// deliberate limits of this abstract data type.
pub struct PersistentRunDeltaVector<T> {
    current: RrbVector<Cell<T>>,
    checkpoint: RrbVector<Cell<T>>,
    dirty_runs: SortedMap<usize, usize>,
    dirty_count: usize,
    policy: EqualityPolicy<T>,
}

impl<T> Clone for PersistentRunDeltaVector<T> {
    /// Forks the version in O(1). Both handles stay independently editable.
    fn clone(&self) -> Self {
        Self {
            current: self.current.clone(),
            checkpoint: self.checkpoint.clone(),
            dirty_runs: self.dirty_runs.clone(),
            dirty_count: self.dirty_count,
            policy: self.policy.clone(),
        }
    }
}

impl<T: Eq> PersistentRunDeltaVector<T> {
    /// Creates the clean empty vector under the canonical natural-equality policy.
    #[must_use]
    pub fn new() -> Self {
        Self::with_policy(EqualityPolicy::natural())
    }

    /// Builds a clean fixed-length vector from one pass over `items`, under natural equality.
    ///
    /// `Theta(n)`. The `T: Eq` bound excludes raw `f32` and `f64`; build those with
    /// [`Self::from_values_with_policy`] and [`EqualityPolicy::reflexive_ieee`].
    #[must_use]
    pub fn from_values<I>(items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        Self::from_values_with_policy(items, EqualityPolicy::natural())
    }
}

impl<T: Eq> Default for PersistentRunDeltaVector<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: Eq> FromIterator<T> for PersistentRunDeltaVector<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_values(iter)
    }
}

impl<T> PersistentRunDeltaVector<T> {
    /// Creates the clean empty vector retaining a distinct custom equality policy.
    #[must_use]
    pub fn with_comparer<C>(comparer: C) -> Self
    where
        C: EqualityComparer<T> + 'static,
    {
        Self::with_policy(EqualityPolicy::custom(comparer))
    }

    /// Creates the clean empty vector retaining a caller-shared comparer identity.
    #[must_use]
    pub fn with_shared_comparer(comparer: Arc<dyn EqualityComparer<T>>) -> Self {
        Self::with_policy(EqualityPolicy::shared(comparer))
    }

    /// Creates the clean empty vector retaining `policy`.
    #[must_use]
    pub fn with_policy(policy: EqualityPolicy<T>) -> Self {
        Self {
            current: RrbVector::new(),
            checkpoint: RrbVector::new(),
            dirty_runs: SortedMap::new(),
            dirty_count: 0,
            policy,
        }
    }

    /// Builds a clean fixed-length vector from one pass over `items`, retaining `policy`.
    ///
    /// `Theta(n)`. Both roots start as the same root, so the vector begins with no dirty runs.
    #[must_use]
    pub fn from_values_with_policy<I>(items: I, policy: EqualityPolicy<T>) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut builder = RrbVector::builder();
        for item in items {
            builder.push(Cell::new(item));
        }
        let values = builder.to_immutable();
        Self::clean(values, policy)
    }

    /// Returns the fixed number of positions. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.current.len()
    }

    /// Returns `true` when the vector has no positions.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns the retained policy defining checkpoint-relative value equality.
    #[must_use]
    pub fn value_policy(&self) -> &EqualityPolicy<T> {
        &self.policy
    }

    /// Returns the number of positions that differ from the checkpoint. O(1).
    #[must_use]
    pub fn dirty_count(&self) -> usize {
        self.dirty_count
    }

    /// Returns the number of maximal dirty runs. O(1).
    #[must_use]
    pub fn dirty_run_count(&self) -> usize {
        self.dirty_runs.len()
    }

    /// Returns `true` when at least one current value differs from its checkpoint value. O(1).
    #[must_use]
    pub fn has_changes(&self) -> bool {
        self.dirty_count != 0
    }

    /// Borrows the current value at `index`, or `None` when the position is out of range.
    ///
    /// `O(log n)`.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        self.current.get(index).map(Cell::value)
    }

    /// Borrows the checkpoint value at `index`, or `None` when the position is out of range.
    ///
    /// `O(log n)`.
    #[must_use]
    pub fn get_checkpoint(&self, index: usize) -> Option<&T> {
        self.checkpoint.get(index).map(Cell::value)
    }

    /// Reports whether `index` differs from its checkpoint value, or `None` when the position is
    /// out of range.
    ///
    /// `O(log(r + 2))` worst-case.
    #[must_use]
    pub fn is_dirty(&self, index: usize) -> Option<bool> {
        (index < self.len()).then(|| self.find_dirty_run(index).is_some())
    }

    /// Returns the maximal dirty run containing `index`, or `None` when that position is clean or
    /// out of range.
    ///
    /// `O(log(r + 2))` worst-case. Use [`Self::is_dirty`] to tell a clean position from an out of
    /// range one.
    #[must_use]
    pub fn dirty_run_containing(&self, index: usize) -> Option<PersistentDirtyRun> {
        self.find_dirty_run(index)
    }

    /// Returns the maximal dirty run at zero-based ascending-start `rank`, or `None` when `rank`
    /// falls outside the run index.
    ///
    /// `O(log(r + 2))` worst-case. Runs are addressed by rank only.
    #[must_use]
    pub fn dirty_run_at(&self, rank: usize) -> Option<PersistentDirtyRun> {
        self.dirty_runs.entry_at(rank).map(to_run)
    }

    /// Iterates the maximal dirty runs in ascending position order.
    ///
    /// Output-optimal `Theta(r)`: the cost depends on the number of runs, not on how many positions
    /// they cover.
    #[must_use]
    pub fn dirty_runs(&self) -> DirtyRunIter {
        DirtyRunIter {
            entries: self.dirty_runs.to_vec().into_iter(),
        }
    }

    /// Iterates the current values in positional order. `Theta(n)`.
    #[must_use]
    pub fn iter(&self) -> RunDeltaVectorIter<'_, T> {
        RunDeltaVectorIter {
            inner: self.current.iter(),
        }
    }

    /// Returns a version with the current value at `index` replaced by `value`, or `None` when the
    /// position is out of range.
    ///
    /// A policy-equal replacement is a semantic no-op and returns the receiver's contents unchanged,
    /// retaining the existing current representative. A replacement landing back in the checkpoint's
    /// equality class cancels that position's delta and restores the exact checkpoint
    /// representative; when the last dirty position clears, the current root snaps back to the
    /// checkpoint root. Otherwise the position becomes or stays dirty and at most two neighboring
    /// run records change.
    ///
    /// `O(log n)` worst-case time and fresh nodes. Every policy call happens before any successor is
    /// built, so a panicking comparer publishes no partial version.
    #[must_use]
    pub fn set_item(&self, index: usize, value: T) -> Option<Self> {
        let current = self.current.get(index)?;
        if self.policy.equals(current.value(), &value) {
            return Some(self.clone());
        }

        let checkpoint = self
            .checkpoint
            .get(index)
            .expect("the checkpoint root has the same length as the current root");
        let remains_dirty = !self.policy.equals(checkpoint.value(), &value);
        let replacement = if remains_dirty {
            Cell::new(value)
        } else {
            checkpoint.clone()
        };
        Some(self.replace_cell(index, replacement, remains_dirty))
    }

    /// Returns a version whose current value at `index` is reset to its checkpoint value, or `None`
    /// when the position is out of range.
    ///
    /// An already clean position returns the receiver's contents unchanged. `O(log n)` worst-case.
    #[must_use]
    pub fn reset_item(&self, index: usize) -> Option<Self> {
        let checkpoint = self.checkpoint.get(index)?.clone();
        let current = self
            .current
            .get(index)
            .expect("the current root has the same length as the checkpoint root");
        if self.policy.equals(current.value(), checkpoint.value()) {
            return Some(self.clone());
        }
        Some(self.replace_cell(index, checkpoint, false))
    }

    /// Accepts every current value as a new checkpoint. `O(1)`.
    ///
    /// An already clean version returns its own contents unchanged.
    #[must_use]
    pub fn checkpoint(&self) -> Self {
        if !self.has_changes() {
            return self.clone();
        }
        Self::clean(self.current.clone(), self.policy.clone())
    }

    /// Reverts every current value to the checkpoint. `O(1)`.
    ///
    /// An already clean version returns its own contents unchanged.
    #[must_use]
    pub fn rollback(&self) -> Self {
        if !self.has_changes() {
            return self.clone();
        }
        Self::clean(self.checkpoint.clone(), self.policy.clone())
    }

    /// Accepts the maximal dirty run at `rank` into the checkpoint, or `None` when `rank` falls
    /// outside the run index.
    ///
    /// Only the selected run becomes clean; every other run is untouched. Implemented by structural
    /// RRB splitting and concatenation, so it costs `O(log n)` worst-case *independently of that
    /// run's length* and makes no value-policy call at all.
    #[must_use]
    pub fn accept_dirty_run_at(&self, rank: usize) -> Option<Self> {
        let run = self.dirty_run_at(rank)?;
        Some(self.accept_run(run))
    }

    /// Accepts the maximal dirty run *containing the position* `index` into the checkpoint, or
    /// `None` when `index` falls outside the vector.
    ///
    /// The argument is a zero-based position, not a run rank, so a run descriptor obtained from
    /// [`Self::dirty_run_containing`] can be acted on without rediscovering its rank. A clean
    /// position is vacuous rather than an error: the receiver's contents are returned unchanged,
    /// sharing every root. A dirty position behaves exactly as [`Self::accept_dirty_run_at`] on the
    /// containing run.
    ///
    /// `O(log n)` worst-case, *independent of that run's length*: locating the containing run is
    /// `O(log(r + 2))` through the start-keyed run index, never a scan, and the splice itself makes
    /// no value-policy call at all.
    #[must_use]
    pub fn accept_dirty_run_containing(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }
        Some(match self.find_dirty_run(index) {
            Some(run) => self.accept_run(run),
            None => self.clone(),
        })
    }

    /// Reverts the maximal dirty run at `rank` to the checkpoint, or `None` when `rank` falls
    /// outside the run index.
    ///
    /// Only the selected run becomes clean; every other run is untouched. Implemented by structural
    /// RRB splitting and concatenation, so it costs `O(log n)` worst-case *independently of that
    /// run's length* and makes no value-policy call at all.
    #[must_use]
    pub fn revert_dirty_run_at(&self, rank: usize) -> Option<Self> {
        let run = self.dirty_run_at(rank)?;
        Some(self.revert_run(run))
    }

    /// Reverts the maximal dirty run *containing the position* `index` to the checkpoint, or `None`
    /// when `index` falls outside the vector.
    ///
    /// The argument is a zero-based position, not a run rank, so a run descriptor obtained from
    /// [`Self::dirty_run_containing`] can be acted on without rediscovering its rank. A clean
    /// position is vacuous rather than an error: the receiver's contents are returned unchanged,
    /// sharing every root. A dirty position behaves exactly as [`Self::revert_dirty_run_at`] on the
    /// containing run.
    ///
    /// `O(log n)` worst-case, *independent of that run's length*: locating the containing run is
    /// `O(log(r + 2))` through the start-keyed run index, never a scan, and the splice itself makes
    /// no value-policy call at all.
    #[must_use]
    pub fn revert_dirty_run_containing(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }
        Some(match self.find_dirty_run(index) {
            Some(run) => self.revert_run(run),
            None => self.clone(),
        })
    }

    /// Reports whether two versions are backed by the same current root. A representation test, not
    /// an equality test.
    #[must_use]
    pub fn shares_current_root_with(&self, other: &Self) -> bool {
        self.current.shares_root_with(&other.current)
    }

    /// Reports whether two versions are backed by the same checkpoint root. A representation test,
    /// not an equality test.
    #[must_use]
    pub fn shares_checkpoint_root_with(&self, other: &Self) -> bool {
        self.checkpoint.shares_root_with(&other.checkpoint)
    }

    /// Checks both RRB roots, run ordering, non-adjacency, maximality, cardinality, policy-relative
    /// truth at every dirty position, and exact representative sharing at every clean position.
    ///
    /// `O(n log n)` in the number of positions: an auditor, not a hot-path operation.
    pub fn validate_structure(
        &self,
    ) -> Result<RunDeltaVectorStatistics, RunDeltaVectorInvariantError> {
        if self.current.len() != self.checkpoint.len() {
            return Err(RunDeltaVectorInvariantError::RootLengthMismatch);
        }
        if self.dirty_count > self.len() {
            return Err(RunDeltaVectorInvariantError::DirtyCountOutOfRange);
        }
        if self.current.validate_structure().is_err() {
            return Err(RunDeltaVectorInvariantError::CurrentRootInvalid);
        }
        if self.checkpoint.validate_structure().is_err() {
            return Err(RunDeltaVectorInvariantError::CheckpointRootInvalid);
        }
        if self.dirty_runs.is_empty()
            && (self.dirty_count != 0 || !self.current.shares_root_with(&self.checkpoint))
        {
            return Err(RunDeltaVectorInvariantError::CleanVersionNotCanonical);
        }

        let mut previous_end: Option<usize> = None;
        let mut counted_dirty = 0usize;
        for run in self.dirty_runs() {
            let end = run.end_exclusive();
            if run.length == 0
                || end > self.len()
                || previous_end.is_some_and(|previous| run.start <= previous)
            {
                return Err(RunDeltaVectorInvariantError::MalformedRunIndex);
            }
            counted_dirty += run.length;
            previous_end = Some(end);
        }
        if counted_dirty != self.dirty_count {
            return Err(RunDeltaVectorInvariantError::DirtyCountMismatch);
        }

        for index in 0..self.len() {
            let dirty = self.find_dirty_run(index).is_some();
            let current = self
                .current
                .get(index)
                .expect("index is below the current root length");
            let checkpoint = self
                .checkpoint
                .get(index)
                .expect("index is below the checkpoint root length");
            if !dirty && current != checkpoint {
                return Err(RunDeltaVectorInvariantError::CleanPositionNotShared);
            }
            if dirty && self.policy.equals(current.value(), checkpoint.value()) {
                return Err(RunDeltaVectorInvariantError::DirtyPositionEqualsCheckpoint);
            }
        }

        Ok(RunDeltaVectorStatistics {
            len: self.len(),
            dirty_count: self.dirty_count,
            dirty_run_count: self.dirty_runs.len(),
        })
    }

    fn clean(root: RrbVector<Cell<T>>, policy: EqualityPolicy<T>) -> Self {
        Self {
            current: root.clone(),
            checkpoint: root,
            dirty_runs: SortedMap::new(),
            dirty_count: 0,
            policy,
        }
    }

    /// Splices one indexed run's current values into the checkpoint, leaving every other run alone.
    ///
    /// `O(log n)` worst-case and comparison-free; `run` must be a record of this version's index.
    fn accept_run(&self, run: PersistentDirtyRun) -> Self {
        if self.dirty_runs.len() == 1 {
            return self.checkpoint();
        }
        Self {
            current: self.current.clone(),
            checkpoint: replace_range(&self.checkpoint, &self.current, run),
            dirty_runs: self.dirty_runs.remove(&run.start),
            dirty_count: self.dirty_count - run.length,
            policy: self.policy.clone(),
        }
    }

    /// Splices one indexed run's checkpoint values back over the current root, leaving every other
    /// run alone.
    ///
    /// `O(log n)` worst-case and comparison-free; `run` must be a record of this version's index.
    fn revert_run(&self, run: PersistentDirtyRun) -> Self {
        if self.dirty_runs.len() == 1 {
            return self.rollback();
        }
        Self {
            current: replace_range(&self.current, &self.checkpoint, run),
            checkpoint: self.checkpoint.clone(),
            dirty_runs: self.dirty_runs.remove(&run.start),
            dirty_count: self.dirty_count - run.length,
            policy: self.policy.clone(),
        }
    }

    fn find_dirty_run(&self, index: usize) -> Option<PersistentDirtyRun> {
        let entry = self.dirty_runs.floor_entry(&index)?;
        (*entry.1 > index).then(|| to_run(entry))
    }

    fn replace_cell(&self, index: usize, replacement: Cell<T>, remains_dirty: bool) -> Self {
        let was_dirty = self.find_dirty_run(index).is_some();
        let mut next_current = self
            .current
            .set_item(index, replacement)
            .expect("the caller validated the position");
        let mut next_runs = self.dirty_runs.clone();
        let mut next_dirty_count = self.dirty_count;

        if !was_dirty && remains_dirty {
            next_runs = add_dirty_position(&next_runs, index);
            next_dirty_count += 1;
        } else if was_dirty && !remains_dirty {
            next_runs = remove_dirty_position(&next_runs, index);
            next_dirty_count -= 1;
        }

        if next_runs.is_empty() {
            next_current = self.checkpoint.clone();
        }
        Self {
            current: next_current,
            checkpoint: self.checkpoint.clone(),
            dirty_runs: next_runs,
            dirty_count: next_dirty_count,
            policy: self.policy.clone(),
        }
    }
}

impl<T: Clone> PersistentRunDeltaVector<T> {
    /// Copies the current values into a standard vector, in positional order. `Theta(n)`.
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.iter().cloned().collect()
    }
}

impl<T> Index<usize> for PersistentRunDeltaVector<T> {
    type Output = T;

    /// Borrows the current value at `index`.
    ///
    /// # Panics
    ///
    /// Panics when `index` is outside the vector. Use [`Self::get`] for the checked form.
    fn index(&self, index: usize) -> &T {
        self.get(index)
            .expect("run-delta vector index out of range")
    }
}

impl<'a, T> IntoIterator for &'a PersistentRunDeltaVector<T> {
    type Item = &'a T;
    type IntoIter = RunDeltaVectorIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T: fmt::Debug> fmt::Debug for PersistentRunDeltaVector<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PersistentRunDeltaVector")
            .field("len", &self.len())
            .field("dirty_count", &self.dirty_count)
            .field("dirty_run_count", &self.dirty_runs.len())
            .field("values", &self.iter().collect::<Vec<_>>())
            .finish_non_exhaustive()
    }
}

/// Borrowed, streaming iterator over one version's current values.
pub struct RunDeltaVectorIter<'a, T> {
    inner: RrbVectorIter<'a, Cell<T>>,
}

impl<'a, T> Iterator for RunDeltaVectorIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<&'a T> {
        self.inner.next().map(Cell::value)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> ExactSizeIterator for RunDeltaVectorIter<'_, T> {}
impl<T> FusedIterator for RunDeltaVectorIter<'_, T> {}

/// Output-optimal iterator over one version's maximal dirty runs, in ascending position order.
pub struct DirtyRunIter {
    entries: std::vec::IntoIter<(usize, usize)>,
}

impl Iterator for DirtyRunIter {
    type Item = PersistentDirtyRun;

    fn next(&mut self) -> Option<PersistentDirtyRun> {
        self.entries.next().map(entry_to_run)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.entries.size_hint()
    }
}

impl DoubleEndedIterator for DirtyRunIter {
    fn next_back(&mut self) -> Option<PersistentDirtyRun> {
        self.entries.next_back().map(entry_to_run)
    }
}

impl ExactSizeIterator for DirtyRunIter {}
impl FusedIterator for DirtyRunIter {}

impl fmt::Debug for DirtyRunIter {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("DirtyRunIter")
            .field("remaining", &self.entries.len())
            .finish_non_exhaustive()
    }
}

fn to_run(entry: (&usize, &usize)) -> PersistentDirtyRun {
    PersistentDirtyRun::new(*entry.0, *entry.1 - *entry.0)
}

fn entry_to_run((start, end): (usize, usize)) -> PersistentDirtyRun {
    PersistentDirtyRun::new(start, end - start)
}

/// Records `index` as dirty, merging the runs on both sides, extending one side, or inserting a
/// fresh singleton run.
///
/// The result stays ordered, non-overlapping, non-adjacent, and maximal, and at most two records
/// change.
fn add_dirty_position(runs: &SortedMap<usize, usize>, index: usize) -> SortedMap<usize, usize> {
    let left = runs
        .floor_entry(&index)
        .map(|(start, end)| (*start, *end))
        .filter(|&(_, end)| end == index);
    let right = runs
        .ceiling_entry(&index)
        .map(|(start, end)| (*start, *end))
        .filter(|&(start, _)| start == index + 1);
    match (left, right) {
        // The left record is overwritten in place rather than removed and reinserted: `set_item`
        // replaces a present key, so dropping the redundant `remove` saves one persistent
        // rebuild without changing the resulting map.
        (Some((left_start, _)), Some((right_start, right_end))) => {
            runs.remove(&right_start).set_item(left_start, right_end)
        }
        (Some((left_start, _)), None) => runs.set_item(left_start, index + 1),
        (None, Some((right_start, right_end))) => {
            runs.remove(&right_start).set_item(index, right_end)
        }
        (None, None) => runs.set_item(index, index + 1),
    }
}

/// Clears `index`, deleting, shrinking at either edge, or splitting its unique containing run.
///
/// # Panics
///
/// Panics when `index` is absent from the run index, which would mean the maintained invariant had
/// already been broken.
fn remove_dirty_position(runs: &SortedMap<usize, usize>, index: usize) -> SortedMap<usize, usize> {
    let (start, end) = runs
        .floor_entry(&index)
        .map(|(start, end)| (*start, *end))
        .filter(|&(_, end)| end > index)
        .expect("the cleared dirty position is present in the run index");

    if start == index && end == index + 1 {
        runs.remove(&start)
    } else if start == index {
        runs.remove(&start).set_item(index + 1, end)
    } else if end == index + 1 {
        runs.set_item(start, index)
    } else {
        runs.set_item(start, index).set_item(index + 1, end)
    }
}

/// Splices `source`'s `run` slice over `destination`'s, sharing every untouched subtree.
///
/// Four splits and two boundary-spine concatenations, so `O(log n)` regardless of the run's length.
fn replace_range<T>(
    destination: &RrbVector<Cell<T>>,
    source: &RrbVector<Cell<T>>,
    run: PersistentDirtyRun,
) -> RrbVector<Cell<T>> {
    if run.start == 0 && run.length == destination.len() {
        return source.clone();
    }

    let destination_split = destination
        .split_at(run.start)
        .expect("a run start is a valid boundary");
    let right = destination_split
        .right
        .split_at(run.length)
        .expect("a run end is a valid boundary")
        .right;
    let source_tail = source
        .split_at(run.start)
        .expect("both roots have the same length")
        .right;
    let middle = source_tail
        .split_at(run.length)
        .expect("both roots have the same length")
        .left;
    destination_split.left.concat(&middle).concat(&right)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::panic::{AssertUnwindSafe, catch_unwind};
    use std::sync::Mutex;
    use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
    use std::thread;

    /// A deterministic xorshift generator; the crate takes no external test dependency.
    struct Rng(u64);

    impl Rng {
        fn new(seed: u64) -> Self {
            Self(seed)
        }

        fn next_u64(&mut self) -> u64 {
            let mut state = self.0;
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            self.0 = state;
            state
        }

        fn below(&mut self, bound: usize) -> usize {
            (self.next_u64() % bound as u64) as usize
        }

        fn in_range(&mut self, low: i32, high_exclusive: i32) -> i32 {
            let span = (high_exclusive - low) as u64;
            low + (self.next_u64() % span) as i32
        }
    }

    /// Counts every policy call so run splices can be proven comparison-free.
    struct CountingComparer {
        calls: Arc<AtomicUsize>,
    }

    impl EqualityComparer<i64> for CountingComparer {
        fn equals(&self, left: &i64, right: &i64) -> bool {
            self.calls.fetch_add(1, AtomicOrdering::Relaxed);
            left == right
        }
    }

    /// Panics on one armed call, standing in for the C# throwing comparer.
    struct PanicOnceComparer {
        state: Mutex<(usize, usize)>,
    }

    impl PanicOnceComparer {
        fn new() -> Self {
            Self {
                state: Mutex::new((0, 0)),
            }
        }

        fn arm(&self, call: usize) {
            *self.state.lock().expect("test mutex is not poisoned") = (0, call);
        }
    }

    impl EqualityComparer<i32> for PanicOnceComparer {
        fn equals(&self, left: &i32, right: &i32) -> bool {
            let mut guard = self.state.lock().expect("test mutex is not poisoned");
            guard.0 += 1;
            if guard.0 == guard.1 {
                guard.1 = 0;
                drop(guard);
                panic!("value comparer failure");
            }
            drop(guard);
            left == right
        }
    }

    fn natural_vector(values: &[i32]) -> PersistentRunDeltaVector<i32> {
        PersistentRunDeltaVector::from_values(values.iter().copied())
    }

    fn expected_runs(current: &[i32], checkpoint: &[i32]) -> Vec<PersistentDirtyRun> {
        let mut runs = Vec::new();
        let mut index = 0;
        while index < current.len() {
            if current[index] == checkpoint[index] {
                index += 1;
                continue;
            }
            let start = index;
            index += 1;
            while index < current.len() && current[index] != checkpoint[index] {
                index += 1;
            }
            runs.push(PersistentDirtyRun::new(start, index - start));
        }
        runs
    }

    fn assert_runs(vector: &PersistentRunDeltaVector<i32>, expected: &[PersistentDirtyRun]) {
        assert_eq!(
            vector.dirty_count(),
            expected.iter().map(|run| run.length).sum::<usize>()
        );
        assert_eq!(vector.dirty_run_count(), expected.len());
        assert_eq!(vector.dirty_runs().collect::<Vec<_>>(), expected);
        for (rank, run) in expected.iter().enumerate() {
            assert_eq!(vector.dirty_run_at(rank), Some(*run));
            for index in run.start..run.end_exclusive() {
                assert_eq!(vector.is_dirty(index), Some(true));
                assert_eq!(vector.dirty_run_containing(index), Some(*run));
            }
        }
        vector
            .validate_structure()
            .expect("the version satisfies every invariant");
    }

    fn assert_matches_model(
        vector: &PersistentRunDeltaVector<i32>,
        current: &[i32],
        checkpoint: &[i32],
    ) {
        assert_eq!(vector.len(), current.len());
        assert_eq!(vector.to_vec(), current);
        let runs = expected_runs(current, checkpoint);
        for index in 0..current.len() {
            assert_eq!(vector.get(index), Some(&current[index]));
            assert_eq!(vector.get_checkpoint(index), Some(&checkpoint[index]));
            let expected_run = runs.iter().find(|run| run.contains(index)).copied();
            assert_eq!(vector.is_dirty(index), Some(expected_run.is_some()));
            assert_eq!(vector.dirty_run_containing(index), expected_run);
        }
        assert_eq!(
            vector.dirty_count(),
            runs.iter().map(|run| run.length).sum::<usize>()
        );
        assert_eq!(vector.dirty_run_count(), runs.len());
        assert_eq!(vector.dirty_runs().collect::<Vec<_>>(), runs);
        for (rank, run) in runs.iter().enumerate() {
            assert_eq!(vector.dirty_run_at(rank), Some(*run));
        }
        assert_eq!(vector.dirty_run_at(runs.len()), None);
        vector
            .validate_structure()
            .expect("the version satisfies every invariant");
    }

    #[test]
    fn empty_factories_have_exact_contracts() {
        let empty = PersistentRunDeltaVector::<i32>::new();
        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
        assert!(!empty.has_changes());
        assert_eq!(empty.dirty_count(), 0);
        assert_eq!(empty.dirty_run_count(), 0);
        assert_eq!(empty.iter().count(), 0);
        assert_eq!(empty.dirty_runs().count(), 0);
        assert!(empty.checkpoint().is_empty());
        assert!(empty.rollback().is_empty());
        assert_eq!(empty.get(0), None);
        assert_eq!(empty.get_checkpoint(0), None);
        assert_eq!(empty.is_dirty(0), None);
        assert_eq!(empty.dirty_run_containing(0), None);
        assert_eq!(empty.dirty_run_at(0), None);
        assert!(empty.accept_dirty_run_at(0).is_none());
        assert!(empty.revert_dirty_run_at(0).is_none());
        assert!(empty.set_item(0, 1).is_none());
        assert!(empty.reset_item(0).is_none());
        assert!(empty.value_policy().is_natural());
        empty
            .validate_structure()
            .expect("the empty version is valid");

        let default = PersistentRunDeltaVector::<i32>::default();
        assert_eq!(default.len(), 0);
        let collected: PersistentRunDeltaVector<i32> = (0..4).collect();
        assert_eq!(collected.to_vec(), vec![0, 1, 2, 3]);
        assert!(!collected.has_changes());
        assert_eq!(collected[2], 2);
        assert_eq!(
            (&collected).into_iter().copied().collect::<Vec<_>>(),
            vec![0, 1, 2, 3]
        );
        assert_eq!(collected.iter().len(), 4);
        assert!(format!("{collected:?}").contains("PersistentRunDeltaVector"));
        assert_eq!(PersistentDirtyRun::new(3, 2).to_string(), "[3, 5)");
        assert_eq!(PersistentDirtyRun::new(3, 2).end_exclusive(), 5);
    }

    #[test]
    fn custom_policy_defines_no_ops_and_cancellation() {
        let policy = EqualityPolicy::custom(|left: &String, right: &String| {
            left.eq_ignore_ascii_case(right.as_str())
        });
        let source = PersistentRunDeltaVector::from_values_with_policy(
            ["Alpha".to_owned(), "Beta".to_owned()],
            policy.clone(),
        );
        assert!(source.value_policy().is_compatible_with(&policy));

        // A policy-equal write is a semantic no-op that keeps the current representative.
        let no_op = source
            .set_item(0, "ALPHA".to_owned())
            .expect("position is in range");
        assert!(!no_op.has_changes());
        assert_eq!(no_op.get(0), Some(&"Alpha".to_owned()));
        assert!(no_op.shares_current_root_with(&source));

        let dirty = source
            .set_item(0, "Gamma".to_owned())
            .expect("position is in range");
        assert_eq!(dirty.get(0), Some(&"Gamma".to_owned()));
        assert_eq!(dirty.get_checkpoint(0), Some(&"Alpha".to_owned()));
        assert_eq!(
            dirty.dirty_runs().collect::<Vec<_>>(),
            vec![PersistentDirtyRun::new(0, 1)]
        );

        // Returning to the checkpoint's equality class cancels the point.
        let cancelled = dirty
            .set_item(0, "alpha".to_owned())
            .expect("position is in range");
        assert!(!cancelled.has_changes());
        assert_eq!(cancelled.get(0), Some(&"Alpha".to_owned()));
        assert!(cancelled.shares_current_root_with(&cancelled.checkpoint()));
        assert!(cancelled.shares_checkpoint_root_with(&source));
        cancelled
            .validate_structure()
            .expect("a cancelled version is valid");
        // The retained intermediate version is untouched.
        assert_eq!(dirty.get(0), Some(&"Gamma".to_owned()));
    }

    #[test]
    fn reflexive_float_policy_keeps_a_nan_round_trip_a_no_op() {
        // Raw `f64` cannot reach the natural policy at all, because `NaN != NaN` would mark a
        // rewrite dirty and leave a phantom run that no invariant check can see.
        let source = PersistentRunDeltaVector::from_values_with_policy(
            [f64::NAN, 1.0, 0.0],
            EqualityPolicy::<f64>::reflexive_ieee(),
        );
        assert!(source.value_policy().is_natural());

        let rewritten = source.set_item(0, f64::NAN).expect("position is in range");
        assert!(!rewritten.has_changes());
        assert_eq!(rewritten.dirty_count(), 0);
        assert_eq!(rewritten.dirty_run_count(), 0);
        assert!(rewritten.shares_current_root_with(&source));
        assert!(rewritten.get(0).expect("current value").is_nan());
        rewritten
            .validate_structure()
            .expect("a NaN rewrite leaves the version clean and canonical");

        // Signed zeros are one class too, matching the C# baseline's default comparer.
        let signed_zero = source.set_item(2, -0.0).expect("position is in range");
        assert!(!signed_zero.has_changes());
        assert!(signed_zero.shares_current_root_with(&source));

        // A genuine change is still recorded, and returning NaN cancels it.
        let dirty = source.set_item(0, 2.0).expect("position is in range");
        assert_eq!(
            dirty.dirty_runs().collect::<Vec<_>>(),
            vec![PersistentDirtyRun::new(0, 1)]
        );
        let cancelled = dirty.set_item(0, f64::NAN).expect("position is in range");
        assert!(!cancelled.has_changes());
        assert!(cancelled.get(0).expect("current value").is_nan());
        cancelled
            .validate_structure()
            .expect("a cancelled NaN version is valid");
    }

    #[test]
    fn cancellation_restores_the_exact_checkpoint_representative() {
        // A pointer-identity policy proves restoration is by identity, not merely by value.
        let policy =
            EqualityPolicy::custom(|left: &Arc<u8>, right: &Arc<u8>| Arc::ptr_eq(left, right));
        let checkpoint_representative = Arc::new(7u8);
        let replacement_representative = Arc::new(7u8);
        assert_eq!(checkpoint_representative, replacement_representative);
        assert!(!Arc::ptr_eq(
            &checkpoint_representative,
            &replacement_representative
        ));

        let source = PersistentRunDeltaVector::from_values_with_policy(
            [Arc::clone(&checkpoint_representative)],
            policy,
        );
        let dirty = source
            .set_item(0, Arc::clone(&replacement_representative))
            .expect("position is in range");
        assert_eq!(dirty.is_dirty(0), Some(true));
        assert!(Arc::ptr_eq(
            dirty.get(0).expect("current value"),
            &replacement_representative
        ));
        assert!(Arc::ptr_eq(
            dirty.get_checkpoint(0).expect("checkpoint value"),
            &checkpoint_representative
        ));

        let reset = dirty.reset_item(0).expect("position is in range");
        assert!(!reset.has_changes());
        assert!(Arc::ptr_eq(
            reset.get(0).expect("current value"),
            &checkpoint_representative
        ));
        reset
            .validate_structure()
            .expect("a reset version is valid");
    }

    #[test]
    fn point_edits_maintain_exact_maximal_dirty_runs() {
        let mut vector = natural_vector(&[0; 12]);
        vector = vector
            .set_item(2, 20)
            .and_then(|value| value.set_item(4, 40))
            .expect("positions are in range");
        assert_runs(
            &vector,
            &[PersistentDirtyRun::new(2, 1), PersistentDirtyRun::new(4, 1)],
        );
        assert_eq!(vector.dirty_run_at(vector.dirty_run_count()), None);

        // A new position between two singleton runs merges both.
        vector = vector.set_item(3, 30).expect("position is in range");
        assert_runs(&vector, &[PersistentDirtyRun::new(2, 3)]);

        // A detached position, then the gap that joins it to the run on its left.
        vector = vector
            .set_item(6, 60)
            .and_then(|value| value.set_item(5, 50))
            .expect("positions are in range");
        assert_runs(&vector, &[PersistentDirtyRun::new(2, 5)]);

        // Interior clearing splits one run in two.
        vector = vector.reset_item(4).expect("position is in range");
        assert_runs(
            &vector,
            &[PersistentDirtyRun::new(2, 2), PersistentDirtyRun::new(5, 2)],
        );
        // Left-edge clearing shrinks from the start.
        vector = vector.reset_item(2).expect("position is in range");
        assert_runs(
            &vector,
            &[PersistentDirtyRun::new(3, 1), PersistentDirtyRun::new(5, 2)],
        );
        // Right-edge clearing shrinks from the end.
        vector = vector.reset_item(6).expect("position is in range");
        assert_runs(
            &vector,
            &[PersistentDirtyRun::new(3, 1), PersistentDirtyRun::new(5, 1)],
        );
        // Clearing the last dirty position snaps the current root back to the checkpoint root.
        vector = vector
            .reset_item(3)
            .and_then(|value| value.reset_item(5))
            .expect("positions are in range");
        assert!(!vector.has_changes());
        assert_eq!(vector.dirty_runs().count(), 0);
        assert_eq!(vector.to_vec(), vec![0; 12]);
        assert!(vector.shares_current_root_with(&vector.rollback()));
        vector
            .validate_structure()
            .expect("a fully cleared version is valid");

        // Resetting an already clean position returns the receiver's contents.
        let unchanged = vector.reset_item(3).expect("position is in range");
        assert!(unchanged.shares_current_root_with(&vector));
    }

    #[test]
    fn accept_and_revert_dirty_runs_change_only_selected_hunks() {
        let calls = Arc::new(AtomicUsize::new(0));
        let policy = EqualityPolicy::custom(CountingComparer {
            calls: Arc::clone(&calls),
        });
        let original = PersistentRunDeltaVector::from_values_with_policy(0..20i64, policy);
        let mut edited = original.clone();
        for index in (2..6).chain(9..14).chain(17..18) {
            edited = edited
                .set_item(index, 1000 + index as i64)
                .expect("position is in range");
        }
        let expected = vec![
            PersistentDirtyRun::new(2, 4),
            PersistentDirtyRun::new(9, 5),
            PersistentDirtyRun::new(17, 1),
        ];
        assert_eq!(edited.dirty_runs().collect::<Vec<_>>(), expected);
        assert_eq!(edited.dirty_count(), 10);

        // Splicing a whole run must not consult the value policy at all.
        let before = calls.load(AtomicOrdering::Relaxed);
        let accepted_middle = edited.accept_dirty_run_at(1).expect("rank is in range");
        assert_eq!(calls.load(AtomicOrdering::Relaxed), before);
        assert_eq!(
            accepted_middle.dirty_runs().collect::<Vec<_>>(),
            vec![
                PersistentDirtyRun::new(2, 4),
                PersistentDirtyRun::new(17, 1)
            ]
        );
        for index in 9..14usize {
            assert_eq!(accepted_middle.get(index), Some(&(1000 + index as i64)));
            assert_eq!(
                accepted_middle.get_checkpoint(index),
                Some(&(1000 + index as i64))
            );
        }

        let before = calls.load(AtomicOrdering::Relaxed);
        let reverted_first = accepted_middle
            .revert_dirty_run_at(0)
            .expect("rank is in range");
        assert_eq!(calls.load(AtomicOrdering::Relaxed), before);
        assert_eq!(
            reverted_first.dirty_runs().collect::<Vec<_>>(),
            vec![PersistentDirtyRun::new(17, 1)]
        );
        for index in 2..6usize {
            assert_eq!(reverted_first.get(index), Some(&(index as i64)));
        }

        let before = calls.load(AtomicOrdering::Relaxed);
        let clean = reverted_first
            .accept_dirty_run_at(0)
            .expect("rank is in range");
        assert_eq!(calls.load(AtomicOrdering::Relaxed), before);
        assert!(!clean.has_changes());
        assert_eq!(clean.get_checkpoint(17), Some(&1017));

        // Every retained source version is unchanged.
        assert_eq!(edited.dirty_runs().collect::<Vec<_>>(), expected);
        assert!(!original.has_changes());
        assert_eq!(original.to_vec(), (0..20i64).collect::<Vec<_>>());
        for version in [&edited, &accepted_middle, &reverted_first, &clean] {
            version
                .validate_structure()
                .expect("every retained version is valid");
        }
        assert!(edited.accept_dirty_run_at(3).is_none());
        assert!(edited.revert_dirty_run_at(3).is_none());
    }

    #[test]
    fn position_addressed_run_operations_agree_with_rank_addressed_ones() {
        let calls = Arc::new(AtomicUsize::new(0));
        let policy = EqualityPolicy::custom(CountingComparer {
            calls: Arc::clone(&calls),
        });
        let original = PersistentRunDeltaVector::from_values_with_policy(0..16i64, policy);
        let mut edited = original.clone();
        for index in (2..6).chain(9..12) {
            edited = edited
                .set_item(index, 1000 + index as i64)
                .expect("position is in range");
        }
        let expected = vec![PersistentDirtyRun::new(2, 4), PersistentDirtyRun::new(9, 3)];
        assert_eq!(edited.dirty_runs().collect::<Vec<_>>(), expected);

        // A position outside the vector is out of range, matching the other position-addressed
        // accessors.
        assert!(edited.accept_dirty_run_containing(16).is_none());
        assert!(edited.revert_dirty_run_containing(16).is_none());
        let empty = PersistentRunDeltaVector::<i32>::new();
        assert!(empty.accept_dirty_run_containing(0).is_none());
        assert!(empty.revert_dirty_run_containing(0).is_none());

        // A clean position is vacuous, not an error: the receiver's contents come back sharing
        // every root, and consult no comparer.
        for index in [0usize, 6, 8, 15] {
            assert_eq!(edited.is_dirty(index), Some(false));
            let before = calls.load(AtomicOrdering::Relaxed);
            for unchanged in [
                edited
                    .accept_dirty_run_containing(index)
                    .expect("position is in range"),
                edited
                    .revert_dirty_run_containing(index)
                    .expect("position is in range"),
            ] {
                assert!(unchanged.shares_current_root_with(&edited));
                assert!(unchanged.shares_checkpoint_root_with(&edited));
                assert_eq!(unchanged.dirty_count(), edited.dirty_count());
                assert_eq!(unchanged.dirty_runs().collect::<Vec<_>>(), expected);
            }
            assert_eq!(calls.load(AtomicOrdering::Relaxed), before);
        }

        // Every position inside a run acts exactly as the rank-addressed operation on that run.
        for (rank, run) in expected.iter().enumerate() {
            for index in run.start..run.end_exclusive() {
                assert_eq!(edited.dirty_run_containing(index), Some(*run));
                let before = calls.load(AtomicOrdering::Relaxed);
                let accepted_by_position = edited
                    .accept_dirty_run_containing(index)
                    .expect("position is in range");
                let reverted_by_position = edited
                    .revert_dirty_run_containing(index)
                    .expect("position is in range");
                assert_eq!(calls.load(AtomicOrdering::Relaxed), before);
                let accepted_by_rank = edited.accept_dirty_run_at(rank).expect("rank is in range");
                let reverted_by_rank = edited.revert_dirty_run_at(rank).expect("rank is in range");
                for (by_position, by_rank) in [
                    (&accepted_by_position, &accepted_by_rank),
                    (&reverted_by_position, &reverted_by_rank),
                ] {
                    assert_eq!(by_position.to_vec(), by_rank.to_vec());
                    assert_eq!(by_position.dirty_count(), by_rank.dirty_count());
                    assert_eq!(
                        by_position.dirty_runs().collect::<Vec<_>>(),
                        by_rank.dirty_runs().collect::<Vec<_>>()
                    );
                    for position in 0..edited.len() {
                        assert_eq!(
                            by_position.get_checkpoint(position),
                            by_rank.get_checkpoint(position)
                        );
                    }
                    by_position
                        .validate_structure()
                        .expect("a position-addressed successor is valid");
                }
            }
        }

        // The sole-run case still collapses onto the whole-checkpoint and whole-rollback paths.
        let single = original
            .set_item(4, 4_000)
            .expect("position is in range")
            .set_item(5, 5_000)
            .expect("position is in range");
        let accepted = single
            .accept_dirty_run_containing(5)
            .expect("position is in range");
        assert!(!accepted.has_changes());
        assert_eq!(accepted.get_checkpoint(4), Some(&4_000));
        let reverted = single
            .revert_dirty_run_containing(4)
            .expect("position is in range");
        assert!(!reverted.has_changes());
        assert_eq!(reverted.to_vec(), (0..16i64).collect::<Vec<_>>());

        // Every retained source version is untouched.
        assert_eq!(edited.dirty_runs().collect::<Vec<_>>(), expected);
        assert!(!original.has_changes());
        for version in [&original, &edited, &single, &accepted, &reverted] {
            version
                .validate_structure()
                .expect("every retained version is valid");
        }
    }

    #[test]
    fn randomized_position_addressed_run_operations_hold_across_retained_versions() {
        const LENGTH: usize = 36;
        let mut rng = Rng::new(0x0B5E_2026);
        let initial: Vec<i32> = (0..LENGTH).map(|index| (index % 5) as i32).collect();
        let mut versions = vec![(natural_vector(&initial), initial.clone(), initial)];

        for _ in 0..600 {
            let parent_index = rng.below(versions.len());
            let (parent, parent_current, parent_checkpoint) = versions[parent_index].clone();
            let mut current = parent_current.clone();
            let mut checkpoint = parent_checkpoint.clone();
            let mut result = parent.clone();
            let index = rng.below(LENGTH);

            match rng.below(4) {
                // Seed dirtiness so the run set keeps churning between run operations.
                0 | 1 => {
                    let value = rng.in_range(-6, 7);
                    result = result.set_item(index, value).expect("position is in range");
                    current[index] = value;
                }
                2 => {
                    result = result
                        .accept_dirty_run_containing(index)
                        .expect("position is in range");
                    if let Some(run) = expected_runs(&current, &checkpoint)
                        .into_iter()
                        .find(|run| run.contains(index))
                    {
                        let range = run.start..run.end_exclusive();
                        let slice = current[range.clone()].to_vec();
                        checkpoint[range].copy_from_slice(&slice);
                    }
                }
                _ => {
                    result = result
                        .revert_dirty_run_containing(index)
                        .expect("position is in range");
                    if let Some(run) = expected_runs(&current, &checkpoint)
                        .into_iter()
                        .find(|run| run.contains(index))
                    {
                        let range = run.start..run.end_exclusive();
                        let slice = checkpoint[range.clone()].to_vec();
                        current[range].copy_from_slice(&slice);
                    }
                }
            }

            versions.push((result, current, checkpoint));
            if versions.len() > 12 {
                let victim = 1 + rng.below(versions.len() - 2);
                versions.remove(victim);
            }
            // Every retained branching version still reports its own values, dirty count, run set,
            // and structural invariants after every step.
            for (version, version_current, version_checkpoint) in &versions {
                assert_matches_model(version, version_current, version_checkpoint);
            }
        }
    }

    #[test]
    fn randomized_retained_versions_match_independent_model() {
        const LENGTH: usize = 48;
        let mut rng = Rng::new(0xD17E_2026);
        let initial: Vec<i32> = (0..LENGTH).map(|index| (index % 7) as i32).collect();
        let mut versions = vec![(natural_vector(&initial), initial.clone(), initial)];

        for _ in 0..5_000 {
            let parent_index = rng.below(versions.len());
            let (parent, parent_current, parent_checkpoint) = versions[parent_index].clone();
            let mut current = parent_current.clone();
            let mut checkpoint = parent_checkpoint.clone();
            let mut result = parent.clone();

            match rng.below(6) {
                0 | 1 => {
                    let index = rng.below(LENGTH);
                    let value = rng.in_range(-8, 9);
                    result = result.set_item(index, value).expect("position is in range");
                    current[index] = value;
                }
                2 => {
                    let index = rng.below(LENGTH);
                    result = result.reset_item(index).expect("position is in range");
                    current[index] = checkpoint[index];
                }
                3 => {
                    result = result.checkpoint();
                    checkpoint = current.clone();
                }
                4 => {
                    result = result.rollback();
                    current = checkpoint.clone();
                }
                _ => {
                    let runs = expected_runs(&current, &checkpoint);
                    if !runs.is_empty() {
                        let rank = rng.below(runs.len());
                        let run = runs[rank];
                        let range = run.start..run.end_exclusive();
                        if rng.below(2) == 0 {
                            result = result.accept_dirty_run_at(rank).expect("rank is in range");
                            let slice = current[range.clone()].to_vec();
                            checkpoint[range].copy_from_slice(&slice);
                        } else {
                            result = result.revert_dirty_run_at(rank).expect("rank is in range");
                            let slice = checkpoint[range.clone()].to_vec();
                            current[range].copy_from_slice(&slice);
                        }
                    }
                }
            }

            assert_matches_model(&result, &current, &checkpoint);
            // Deriving a branch leaves the parent version exactly as it was.
            assert_matches_model(&parent, &parent_current, &parent_checkpoint);
            versions.push((result, current, checkpoint));
            if versions.len() > 512 {
                let victim = 1 + rng.below(versions.len() - 2);
                versions.remove(victim);
            }
        }
    }

    #[test]
    fn clustered_delta_uses_two_descriptors_for_thousands_of_dirty_positions() {
        // The k-versus-r witness: thousands of dirty positions, exactly two descriptors.
        const LENGTH: usize = 8_192;
        let source = natural_vector(&[0; LENGTH]);
        let mut edited = source.clone();
        for index in 0..LENGTH - 2 {
            edited = edited.set_item(index, 1).expect("position is in range");
        }
        edited = edited
            .set_item(LENGTH - 1, 1)
            .expect("position is in range");

        assert_eq!(edited.dirty_count(), LENGTH - 1);
        assert_eq!(edited.dirty_run_count(), 2);
        assert_eq!(
            edited.dirty_runs().collect::<Vec<_>>(),
            vec![
                PersistentDirtyRun::new(0, LENGTH - 2),
                PersistentDirtyRun::new(LENGTH - 1, 1),
            ]
        );

        // Accepting the huge run is a splice, not a walk over its positions.
        let accepted = edited.accept_dirty_run_at(0).expect("rank is in range");
        assert_eq!(
            accepted.dirty_runs().collect::<Vec<_>>(),
            vec![PersistentDirtyRun::new(LENGTH - 1, 1)]
        );
        assert_eq!(accepted.get_checkpoint(0), Some(&1));
        assert_eq!(accepted.get_checkpoint(LENGTH - 1), Some(&0));
        assert_eq!(accepted.dirty_count(), 1);

        assert_eq!(source.to_vec(), vec![0; LENGTH]);
        edited
            .validate_structure()
            .expect("the clustered version is valid");
        accepted
            .validate_structure()
            .expect("the spliced version is valid");
    }

    #[test]
    fn comparer_failure_publishes_no_partial_version() {
        let comparer = Arc::new(PanicOnceComparer::new());
        let source = PersistentRunDeltaVector::from_values_with_policy(
            [1, 2, 3],
            EqualityPolicy::shared(Arc::clone(&comparer) as Arc<dyn EqualityComparer<i32>>),
        );

        // The second policy call is the checkpoint comparison, immediately before a successor would
        // be constructed.
        comparer.arm(2);
        let outcome = catch_unwind(AssertUnwindSafe(|| source.set_item(1, 99)));
        assert!(outcome.is_err(), "the armed comparer must panic");

        assert_eq!(source.to_vec(), vec![1, 2, 3]);
        assert!(!source.has_changes());
        source
            .validate_structure()
            .expect("the source version survived the failure intact");

        let edited = source.set_item(1, 99).expect("position is in range");
        assert_eq!(edited.to_vec(), vec![1, 99, 3]);
        assert_eq!(
            edited.dirty_runs().collect::<Vec<_>>(),
            vec![PersistentDirtyRun::new(1, 1)]
        );
        assert_eq!(source.to_vec(), vec![1, 2, 3]);
    }

    #[test]
    fn concurrent_readers_and_independent_writers_observe_stable_snapshots() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<PersistentRunDeltaVector<i32>>();
        assert_send_sync::<PersistentDirtyRun>();

        let initial: Vec<i32> = (0..2_048).map(|index| index % 17).collect();
        let mut source = natural_vector(&initial);
        for index in 100..300usize {
            source = source
                .set_item(index, -(index as i32))
                .expect("position is in range");
        }
        let expected_current = source.to_vec();
        let expected_checkpoint = initial;

        thread::scope(|scope| {
            for worker in 0..8usize {
                let source = &source;
                let expected_current = &expected_current;
                scope.spawn(move || {
                    let mut branch = source.clone();
                    let mut current = expected_current.clone();
                    for iteration in 0..200usize {
                        let index = (worker * 97 + iteration * 31) % current.len();
                        // Readers see one stable snapshot while writers derive private branches.
                        assert_eq!(source.get(index), Some(&expected_current[index]));
                        let value = (worker * 10_000 + iteration) as i32;
                        branch = branch.set_item(index, value).expect("position is in range");
                        current[index] = value;
                        if iteration % 64 == 63 {
                            branch = branch.checkpoint();
                        }
                    }
                    assert_eq!(branch.to_vec(), current);
                    branch
                        .validate_structure()
                        .expect("each private branch stays valid");
                });
            }
        });

        assert_matches_model(&source, &expected_current, &expected_checkpoint);
    }

    #[test]
    fn validate_structure_reports_broken_run_indexes() {
        let vector = natural_vector(&[0, 1, 2, 3]);
        let dirty = vector.set_item(1, 9).expect("position is in range");
        assert_eq!(
            dirty.validate_structure(),
            Ok(RunDeltaVectorStatistics {
                len: 4,
                dirty_count: 1,
                dirty_run_count: 1,
            })
        );

        // A run index claiming more than the cardinality records is rejected.
        let broken = PersistentRunDeltaVector {
            dirty_runs: dirty.dirty_runs.set_item(1, 3),
            ..dirty.clone()
        };
        assert_eq!(
            broken.validate_structure(),
            Err(RunDeltaVectorInvariantError::DirtyCountMismatch)
        );

        // A version left uncanonicalized after its last change cleared is rejected.
        let uncanonical = PersistentRunDeltaVector {
            dirty_runs: SortedMap::new(),
            dirty_count: 0,
            ..dirty.clone()
        };
        assert_eq!(
            uncanonical.validate_structure(),
            Err(RunDeltaVectorInvariantError::CleanVersionNotCanonical)
        );

        // A genuinely dirty position left outside every run is rejected.
        let uncovered = PersistentRunDeltaVector {
            dirty_runs: SortedMap::new().set_item(3, 4),
            ..dirty
        };
        assert_eq!(
            uncovered.validate_structure(),
            Err(RunDeltaVectorInvariantError::CleanPositionNotShared)
        );

        // A run over positions that are policy-equal to their checkpoint is rejected.
        let untruthful = PersistentRunDeltaVector {
            dirty_runs: SortedMap::new().set_item(3, 4),
            dirty_count: 1,
            ..vector
        };
        assert_eq!(
            untruthful.validate_structure(),
            Err(RunDeltaVectorInvariantError::DirtyPositionEqualsCheckpoint)
        );
        assert_eq!(
            RunDeltaVectorInvariantError::CleanVersionNotCanonical.to_string(),
            "a clean version is not canonicalized to its checkpoint root"
        );
    }
}
