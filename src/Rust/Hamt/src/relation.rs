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
    #[must_use]
    pub fn new() -> Self {
        Self::with_hashers(RandomState::new(), RandomState::new())
    }
}

impl<L, R, SL, SR> PersistentRelation<L, R, SL, SR> {
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

    #[must_use]
    pub fn left_count(&self) -> usize {
        self.forward.key_count()
    }

    #[must_use]
    pub fn right_count(&self) -> usize {
        self.reverse.key_count()
    }

    #[must_use]
    pub fn pair_count(&self) -> usize {
        self.forward.pair_count()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.forward.is_empty()
    }

    #[must_use]
    pub fn left_hasher(&self) -> &SL {
        self.forward.key_hasher()
    }

    #[must_use]
    pub fn right_hasher(&self) -> &SR {
        self.reverse.key_hasher()
    }

    pub fn lefts(&self) -> impl Iterator<Item = &L> {
        self.forward.keys()
    }

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
    #[must_use]
    pub fn contains(&self, left: &L, right: &R) -> bool {
        self.forward.contains(left, right)
    }

    #[must_use]
    pub fn contains_left(&self, left: &L) -> bool {
        self.forward.contains_key(left)
    }

    #[must_use]
    pub fn contains_right(&self, right: &R) -> bool {
        self.reverse.contains_key(right)
    }

    #[must_use]
    pub fn get_rights(&self, left: &L) -> Option<&PersistentHashSet<R, SR>> {
        self.forward.get_values(left)
    }

    #[must_use]
    pub fn get_lefts(&self, right: &R) -> Option<&PersistentHashSet<L, SL>> {
        self.reverse.get_values(right)
    }

    #[must_use]
    pub fn count_rights(&self, left: &L) -> usize {
        self.forward.count_values(left)
    }

    #[must_use]
    pub fn count_lefts(&self, right: &R) -> usize {
        self.reverse.count_values(right)
    }

    #[must_use]
    pub fn get_left(&self, equal_left: &L) -> Option<&L> {
        self.forward.get_key(equal_left)
    }

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

    #[must_use]
    pub fn try_insert(&self, left: L, right: R) -> (Self, bool) {
        let result = self.insert(left, right);
        let changed = !result.shares_indexes_with(self);
        (result, changed)
    }

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

    #[must_use]
    pub fn remove_right(&self, right: &R) -> Self {
        self.try_remove_right(right)
            .map_or_else(|| self.clone(), |(result, _, _)| result)
    }

    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self::with_hashers(self.left_hasher().clone(), self.right_hasher().clone())
    }

    #[must_use]
    pub fn iter(&self) -> HashMultimapIter<'_, L, R, SR> {
        self.forward.iter()
    }

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RelationStatistics {
    pub left_count: usize,
    pub right_count: usize,
    pub pair_count: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RelationInvariantError {
    ForwardMultimap,
    ReverseMultimap,
    PairCountMismatch,
    MissingForwardPair,
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
