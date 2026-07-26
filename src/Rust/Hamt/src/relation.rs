//! Persistent many-to-many relation with both directions materialized.
//!
//! [`PersistentRelation`] keeps two mutually inverse [`PersistentHashMultimap`]s — left-to-right
//! and right-to-left — updated together and published together. Materializing the reverse direction
//! is the point: asking "which left values relate to this right value?" is then as cheap as the
//! forward question, whereas a single forward multimap would have to scan every pair. Inversion is
//! consequently O(1), since it exchanges the two multimaps.
//!
//! The cost is roughly two multimap entries per logical pair, and the obligation that both
//! directions stay in agreement — which [`PersistentRelation::validate`] rechecks, returning
//! [`RelationStatistics`] or the first [`RelationInvariantError`].

use crate::{HashMultimapIter, PersistentHashMultimap, PersistentHashSet};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

/// Immutable many-to-many relation backed by mutually inverse persistent hash multimaps.
pub struct PersistentRelation<L, R, SL = RandomState, SR = RandomState> {
    forward: PersistentHashMultimap<L, R, SL, SR>,
    reverse: PersistentHashMultimap<R, L, SR, SL>,
}

impl<L, R, SL, SR> Clone for PersistentRelation<L, R, SL, SR>
where
    SL: Clone,
    SR: Clone,
{
    fn clone(&self) -> Self {
        Self {
            forward: self.forward.clone(),
            reverse: self.reverse.clone(),
        }
    }
}

impl<L, R> PersistentRelation<L, R, RandomState, RandomState> {
    /// Creates an empty relation using fresh default hash policies for both domains.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hashers(RandomState::new(), RandomState::new())
    }
}

impl<L, R, SL, SR> PersistentRelation<L, R, SL, SR> {
    /// Creates an empty relation with an independently chosen hash policy for each domain.
    #[must_use]
    pub fn with_hashers(left_hasher: SL, right_hasher: SR) -> Self
    where
        SL: Clone,
        SR: Clone,
    {
        Self {
            forward: PersistentHashMultimap::with_hashers(
                left_hasher.clone(),
                right_hasher.clone(),
            ),
            reverse: PersistentHashMultimap::with_hashers(right_hasher, left_hasher),
        }
    }

    /// Returns the number of distinct left values that participate in at least one pair. O(1).
    #[must_use]
    pub fn left_count(&self) -> usize {
        self.forward.key_count()
    }

    /// Returns the number of distinct right values that participate in at least one pair. O(1).
    #[must_use]
    pub fn right_count(&self) -> usize {
        self.reverse.key_count()
    }

    /// Returns the number of `(left, right)` pairs. O(1).
    #[must_use]
    pub fn pair_count(&self) -> usize {
        self.forward.pair_count()
    }

    /// Returns `true` when the relation holds no pairs.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.forward.is_empty()
    }

    /// Borrows the hash policy defining left-value equivalence.
    #[must_use]
    pub fn left_hasher(&self) -> &SL {
        self.forward.key_hasher()
    }

    /// Borrows the hash policy defining right-value equivalence.
    #[must_use]
    pub fn right_hasher(&self) -> &SR {
        self.reverse.key_hasher()
    }

    /// Iterates the distinct left values, in unspecified order.
    pub fn lefts(&self) -> impl Iterator<Item = &L> {
        self.forward.keys()
    }

    /// Iterates the distinct right values, in unspecified order.
    pub fn rights(&self) -> impl Iterator<Item = &R> {
        self.reverse.keys()
    }

    /// Produces the inverse facade by cloning and swapping the two already-built indexes.
    ///
    /// Rust value types do not assign facade identity, so this is O(1) root cloning rather than a
    /// cyclic cached object. Calling `inverse` twice shares the original pair of roots.
    #[must_use]
    pub fn inverse(&self) -> PersistentRelation<R, L, SR, SL>
    where
        SL: Clone,
        SR: Clone,
    {
        PersistentRelation {
            forward: self.reverse.clone(),
            reverse: self.forward.clone(),
        }
    }

    /// Reports whether two relations share *both* adjacency indexes, so neither can observe an edit
    /// made to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_indexes_with(&self, other: &Self) -> bool {
        self.forward.shares_groups_root_with(&other.forward)
            && self.reverse.shares_groups_root_with(&other.reverse)
    }
}

impl<L, R, SL, SR> PersistentRelation<L, R, SL, SR>
where
    L: Eq + Hash,
    R: Eq + Hash,
    SL: BuildHasher,
    SR: BuildHasher,
{
    /// Reports whether the pair `(left, right)` is present. O(1) expected.
    #[must_use]
    pub fn contains(&self, left: &L, right: &R) -> bool {
        self.forward.contains(left, right)
    }

    /// Reports whether `left` participates in at least one pair. O(1) expected.
    #[must_use]
    pub fn contains_left(&self, left: &L) -> bool {
        self.forward.contains_key(left)
    }

    /// Reports whether `right` participates in at least one pair. O(1) expected — the reverse index
    /// answers this without scanning the pairs.
    #[must_use]
    pub fn contains_right(&self, right: &R) -> bool {
        self.reverse.contains_key(right)
    }

    /// Borrows the set of right values related to `left`, or `None` when `left` is absent. Never
    /// yields an empty set.
    #[must_use]
    pub fn get_rights(&self, left: &L) -> Option<&PersistentHashSet<R, SR>> {
        self.forward.get_values(left)
    }

    /// Borrows the set of left values related to `right`, or `None` when `right` is absent. Never
    /// yields an empty set.
    #[must_use]
    pub fn get_lefts(&self, right: &R) -> Option<&PersistentHashSet<L, SL>> {
        self.reverse.get_values(right)
    }

    /// Returns how many right values `left` is related to, or zero when it is absent.
    #[must_use]
    pub fn count_rights(&self, left: &L) -> usize {
        self.forward.count_values(left)
    }

    /// Returns how many left values `right` is related to, or zero when it is absent.
    #[must_use]
    pub fn count_lefts(&self, right: &R) -> usize {
        self.reverse.count_values(right)
    }

    /// Borrows the stored left representative equivalent to `equal_left`, or `None` when absent.
    #[must_use]
    pub fn get_left(&self, equal_left: &L) -> Option<&L> {
        self.forward.get_key(equal_left)
    }

    /// Borrows the stored right representative equivalent to `equal_right`, or `None` when absent.
    #[must_use]
    pub fn get_right(&self, equal_right: &R) -> Option<&R> {
        self.reverse.get_key(equal_right)
    }
}

impl<L, R, SL, SR> PersistentRelation<L, R, SL, SR>
where
    L: Eq + Hash + Clone,
    R: Eq + Hash + Clone,
    SL: BuildHasher + Clone,
    SR: BuildHasher + Clone,
{
    /// Builds a relation from pairs under the supplied hash policies, retaining the first
    /// representative of each equivalence class and collapsing duplicate pairs.
    #[must_use]
    pub fn from_pairs_with_hashers<I>(pairs: I, left_hasher: SL, right_hasher: SR) -> Self
    where
        I: IntoIterator<Item = (L, R)>,
    {
        let mut result = Self::with_hashers(left_hasher, right_hasher);
        for (left, right) in pairs {
            result = result.insert(left, right);
        }
        result
    }

    /// Adds a pair while reusing globally retained representatives in both indexes.
    #[must_use]
    pub fn insert(&self, left: L, right: R) -> Self {
        // A nested multimap retains values per adjacency set. Normalize through the opposite outer
        // keys so one representative remains authoritative across every group in the relation.
        let actual_left = self.forward.get_key(&left).cloned().unwrap_or(left);
        let actual_right = self.reverse.get_key(&right).cloned().unwrap_or(right);
        let forward = self
            .forward
            .insert(actual_left.clone(), actual_right.clone());
        if forward.shares_groups_root_with(&self.forward) {
            return self.clone();
        }
        let reverse = self.reverse.insert(actual_right, actual_left);
        Self { forward, reverse }
    }

    /// Adds the pair `(left, right)`, reporting whether it was new.
    ///
    /// Re-adding a present pair is a no-op that shares both indexes.
    #[must_use]
    pub fn try_insert(&self, left: L, right: R) -> (Self, bool) {
        let result = self.insert(left, right);
        let changed = !result.shares_indexes_with(self);
        (result, changed)
    }

    /// Removes the pair `(left, right)` from both indexes. Removing an absent pair is a no-op.
    ///
    /// A value that loses its last pair drops out of the relation entirely, keeping every group
    /// nonempty.
    #[must_use]
    pub fn remove(&self, left: &L, right: &R) -> Self {
        if !self.contains(left, right) {
            return self.clone();
        }
        Self {
            forward: self.forward.remove(left, right),
            reverse: self.reverse.remove(right, left),
        }
    }

    /// Removes the pair `(left, right)`, or returns `None` when it was absent.
    #[must_use]
    pub fn try_remove(&self, left: &L, right: &R) -> Option<Self> {
        self.contains(left, right).then(|| self.remove(left, right))
    }

    /// Removes a left class and returns its stored representative and persistent adjacency set.
    #[must_use]
    pub fn try_remove_left(&self, left: &L) -> Option<(Self, L, PersistentHashSet<R, SR>)> {
        let (forward, actual_left, rights) = self.forward.try_remove_key(left)?;
        let mut reverse = self.reverse.clone();
        for right in rights.iter() {
            reverse = reverse.remove(right, &actual_left);
        }
        Some((Self { forward, reverse }, actual_left, rights))
    }

    /// Removes every pair involving `left`, updating both indexes.
    #[must_use]
    pub fn remove_left(&self, left: &L) -> Self {
        self.try_remove_left(left)
            .map_or_else(|| self.clone(), |(result, _, _)| result)
    }

    /// Removes a right class and returns its stored representative and persistent adjacency set.
    #[must_use]
    pub fn try_remove_right(&self, right: &R) -> Option<(Self, R, PersistentHashSet<L, SL>)> {
        let (reverse, actual_right, lefts) = self.reverse.try_remove_key(right)?;
        let mut forward = self.forward.clone();
        for left in lefts.iter() {
            forward = forward.remove(left, &actual_right);
        }
        Some((Self { forward, reverse }, actual_right, lefts))
    }

    /// Removes every pair involving `right`, updating both indexes. Symmetric with
    /// [`Self::remove_left`] and equally cheap, because the reverse index is materialized.
    #[must_use]
    pub fn remove_right(&self, right: &R) -> Self {
        self.try_remove_right(right)
            .map_or_else(|| self.clone(), |(result, _, _)| result)
    }

    /// Returns an empty relation retaining both hash policies. Clearing an empty relation is a no-op
    /// that shares the receiver's representation.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self::with_hashers(self.left_hasher().clone(), self.right_hasher().clone())
    }

    /// Iterates every `(left, right)` pair, in unspecified order.
    #[must_use]
    pub fn iter(&self) -> HashMultimapIter<'_, L, R, SR> {
        self.forward.iter()
    }

    /// Cross-checks that the forward and reverse indexes describe exactly the same pair set and that
    /// no group is empty.
    ///
    /// A defensive audit over the whole relation; ordinary operations maintain these invariants.
    pub fn validate(&self) -> Result<RelationStatistics, RelationInvariantError> {
        self.forward
            .validate()
            .map_err(|_| RelationInvariantError::ForwardMultimap)?;
        self.reverse
            .validate()
            .map_err(|_| RelationInvariantError::ReverseMultimap)?;
        if self.forward.pair_count() != self.reverse.pair_count() {
            return Err(RelationInvariantError::PairCountMismatch);
        }
        for (left, right) in self.forward.iter() {
            if !self.reverse.contains(right, left) {
                return Err(RelationInvariantError::MissingReversePair);
            }
        }
        for (right, left) in self.reverse.iter() {
            if !self.forward.contains(left, right) {
                return Err(RelationInvariantError::MissingForwardPair);
            }
        }
        Ok(RelationStatistics {
            left_count: self.left_count(),
            right_count: self.right_count(),
            pair_count: self.pair_count(),
        })
    }
}

impl<L, R> PersistentRelation<L, R, RandomState, RandomState>
where
    L: Eq + Hash + Clone,
    R: Eq + Hash + Clone,
{
    /// Builds a relation from pairs under fresh default hash policies.
    #[must_use]
    pub fn from_pairs<I>(pairs: I) -> Self
    where
        I: IntoIterator<Item = (L, R)>,
    {
        Self::from_pairs_with_hashers(pairs, RandomState::new(), RandomState::new())
    }
}

impl<L, R, SL, SR> Default for PersistentRelation<L, R, SL, SR>
where
    SL: Default + Clone,
    SR: Default + Clone,
{
    fn default() -> Self {
        Self::with_hashers(SL::default(), SR::default())
    }
}

impl<L, R, SL, SR> fmt::Debug for PersistentRelation<L, R, SL, SR>
where
    L: Eq + Hash + Clone + fmt::Debug,
    R: Eq + Hash + Clone + fmt::Debug,
    SL: BuildHasher + Clone,
    SR: BuildHasher + Clone,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_set().entries(self.iter()).finish()
    }
}

/// Cardinalities recomputed by a successful [`PersistentRelation::validate`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RelationStatistics {
    /// The number of distinct left values participating in at least one pair.
    pub left_count: usize,
    /// The number of distinct right values participating in at least one pair.
    pub right_count: usize,
    /// The number of pairs, which both indexes must agree on.
    pub pair_count: usize,
}

/// A structural invariant violation found by [`PersistentRelation::validate`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RelationInvariantError {
    /// The forward multimap failed its own validation.
    ForwardMultimap,
    /// The reverse multimap failed its own validation.
    ReverseMultimap,
    /// The two indexes report different pair counts.
    PairCountMismatch,
    /// A pair present in the reverse index is missing from the forward index.
    MissingForwardPair,
    /// A pair present in the forward index is missing from the reverse index.
    MissingReversePair,
}

impl fmt::Display for RelationInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::ForwardMultimap => "the forward multimap is invalid",
            Self::ReverseMultimap => "the reverse multimap is invalid",
            Self::PairCountMismatch => "the relation indexes disagree on pair count",
            Self::MissingForwardPair => "a reverse pair is absent from the forward index",
            Self::MissingReversePair => "a forward pair is absent from the reverse index",
        })
    }
}

impl std::error::Error for RelationInvariantError {}
