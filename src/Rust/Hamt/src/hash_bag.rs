//! Persistent multiset (bag) with explicit per-class multiplicities.
//!
//! [`PersistentHashBag`] stores each equivalence class once, in a CHAMP map from the retained
//! *first representative* to a positive occurrence count. Counting rather than storing duplicates
//! keeps memory proportional to the number of distinct classes, and makes "add 1000 copies" O(1)
//! work instead of a thousand insertions.
//!
//! Two counting domains are distinguished on purpose: a per-class count is a positive `i32`, while
//! the bag's total is a widened `i64` that cannot overflow by summing in-range classes. Operations
//! that would leave those domains — a negative copy count, or a class exceeding [`i32::MAX`] —
//! fail with [`HashBagError`] rather than wrapping.
//!
//! Multiset algebra (sum, difference, intersection, union) follows the *receiver's* hash policy,
//! and the receiver's stored representative is retained wherever a class survives, so which
//! concrete value you get back is well defined rather than incidental.

use super::{Iter, PersistentHashMap};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};
use std::iter::FusedIterator;

/// A checked hash-bag operation failure.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HashBagError {
    /// A copy count supplied to an update was negative.
    NegativeCopies(i32),
    /// One equivalence class would exceed `i32::MAX` occurrences.
    MultiplicityOverflow,
    /// The expanded occurrence count would exceed `i64::MAX`.
    TotalCountOverflow,
    /// The expanded bag cannot be represented by one Rust `Vec` on this target.
    ExpandedCollectionTooLarge(i64),
}

impl fmt::Display for HashBagError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NegativeCopies(copies) => {
                write!(
                    formatter,
                    "copy count must be non-negative, but was {copies}"
                )
            }
            Self::MultiplicityOverflow => {
                formatter.write_str("hash-bag multiplicity exceeds i32::MAX")
            }
            Self::TotalCountOverflow => {
                formatter.write_str("hash-bag total count exceeds i64::MAX")
            }
            Self::ExpandedCollectionTooLarge(total_count) => write!(
                formatter,
                "expanded hash bag with {total_count} occurrences does not fit in one Vec",
            ),
        }
    }
}

impl std::error::Error for HashBagError {}

/// One stored item representative and its positive multiplicity.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HashBagEntry<T> {
    pub item: T,
    pub count: i32,
}

/// An immutable unordered multiset backed by a persistent CHAMP map.
///
/// Each `Eq` equivalence class retains its first stored representative and one positive `i32`
/// multiplicity. `total_count` is the expanded `i64` occurrence count. Default iteration is
/// expanded and repeats every representative contiguously in stable-for-one-version, otherwise
/// unspecified trie order.
pub struct PersistentHashBag<T, S = RandomState> {
    counts: PersistentHashMap<T, i32, S>,
    total_count: i64,
}

impl<T, S: Clone> Clone for PersistentHashBag<T, S> {
    fn clone(&self) -> Self {
        Self {
            counts: self.counts.clone(),
            total_count: self.total_count,
        }
    }
}

impl<T> PersistentHashBag<T, RandomState> {
    /// Creates an empty bag with a fresh default hash policy.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }

    /// Aggregates occurrences in input order with a fresh default hash policy.
    pub fn try_from_items<I>(items: I) -> Result<Self, HashBagError>
    where
        T: Eq + Hash + Clone,
        I: IntoIterator<Item = T>,
    {
        Self::try_from_items_with_hasher(items, RandomState::new())
    }
}

impl<T, S> PersistentHashBag<T, S> {
    /// Creates an empty bag retaining `hasher` as its hash policy.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            counts: PersistentHashMap::with_hasher(hasher),
            total_count: 0,
        }
    }

    /// Returns the number of stored equivalence classes.
    #[must_use]
    pub fn distinct_count(&self) -> usize {
        self.counts.len()
    }

    /// Returns the expanded number of occurrences.
    #[must_use]
    pub fn total_count(&self) -> i64 {
        self.total_count
    }

    /// Returns `true` when the bag holds no occurrences at all.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.counts.is_empty()
    }

    /// Borrows the retained hash policy that defines this bag's equivalence classes.
    #[must_use]
    pub fn hasher(&self) -> &S {
        self.counts.hasher()
    }

    /// Reports whether both bags share the exact CHAMP root.
    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        self.counts.shares_root_with(&other.counts)
    }

    /// Iterates expanded occurrences.
    #[must_use]
    pub fn iter(&self) -> BagIter<'_, T> {
        BagIter {
            entries: self.counts.iter(),
            current_item: None,
            copies_left: 0,
            remaining: self.total_count,
        }
    }

    /// Iterates each stored representative once.
    pub fn distinct_items(&self) -> impl Iterator<Item = &T> {
        self.counts.keys()
    }

    /// Iterates stored representatives paired with their positive multiplicities.
    pub fn entries(&self) -> impl Iterator<Item = HashBagEntry<&T>> {
        self.counts.iter().map(|(item, count)| HashBagEntry {
            item,
            count: *count,
        })
    }
}

impl<T: Clone, S> PersistentHashBag<T, S> {
    /// Materializes expanded iteration after checking the target `Vec` length and capacity.
    pub fn to_vec(&self) -> Result<Vec<T>, HashBagError> {
        let length = usize::try_from(self.total_count)
            .map_err(|_| HashBagError::ExpandedCollectionTooLarge(self.total_count))?;
        let mut result = Vec::new();
        result
            .try_reserve_exact(length)
            .map_err(|_| HashBagError::ExpandedCollectionTooLarge(self.total_count))?;
        result.extend(self.iter().cloned());
        Ok(result)
    }
}

impl<T, S: Clone> PersistentHashBag<T, S> {
    /// Returns an empty bag preserving this bag's hash-policy identity.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self {
            counts: self.counts.clear(),
            total_count: 0,
        }
    }
}

impl<T, S> PersistentHashBag<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    /// Reports whether `item`'s equivalence class occurs at least once. O(1).
    #[must_use]
    pub fn contains(&self, item: &T) -> bool {
        self.counts.contains_key(item)
    }

    /// Returns the multiplicity of `item`, or zero when its class is absent.
    #[must_use]
    pub fn count_of(&self, item: &T) -> i32 {
        self.counts.get(item).copied().unwrap_or(0)
    }

    /// Returns the retained representative equal to `item`.
    #[must_use]
    pub fn get(&self, item: &T) -> Option<&T> {
        self.counts
            .get_key_value(item)
            .map(|(stored_item, _)| stored_item)
    }

    /// Explicitly named alias for [`PersistentHashBag::get`].
    #[must_use]
    pub fn get_stored(&self, item: &T) -> Option<&T> {
        self.get(item)
    }

    /// Returns the retained representative and its positive multiplicity.
    #[must_use]
    pub fn get_entry(&self, item: &T) -> Option<HashBagEntry<&T>> {
        self.counts
            .get_key_value(item)
            .map(|(stored_item, count)| HashBagEntry {
                item: stored_item,
                count: *count,
            })
    }
}

impl<T, S> PersistentHashBag<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    /// Aggregates occurrences in input order under `hasher`.
    ///
    /// No mutable builder or transient bag is exposed: every intermediate value is immutable and
    /// is discarded if checked arithmetic, hashing, equality, or cloning fails.
    pub fn try_from_items_with_hasher<I>(items: I, hasher: S) -> Result<Self, HashBagError>
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = Self::with_hasher(hasher);
        for item in items {
            result = result.add(item)?;
        }
        Ok(result)
    }

    /// Adds one occurrence, retaining an existing representative.
    pub fn add(&self, item: T) -> Result<Self, HashBagError> {
        self.add_copies(item, 1)
    }

    /// Adds `copies` occurrences with checked per-class and total counts.
    ///
    /// Zero returns a root-sharing clone before hashing. A positive update hashes once, descends
    /// once, and invokes exactly the selected add/update closure. On failure the source remains
    /// unchanged and no successor is returned.
    pub fn add_copies(&self, item: T, copies: i32) -> Result<Self, HashBagError> {
        validate_copies(copies)?;
        if copies == 0 {
            return Ok(self.clone());
        }

        let total_count = self
            .total_count
            .checked_add(i64::from(copies))
            .ok_or(HashBagError::TotalCountOverflow)?;
        let mut multiplicity_overflow = false;
        let update = self.counts.add_or_update(
            item,
            |_| copies,
            |_, current| match current.checked_add(copies) {
                Some(count) => count,
                None => {
                    multiplicity_overflow = true;
                    *current
                }
            },
        );
        if multiplicity_overflow {
            return Err(HashBagError::MultiplicityOverflow);
        }
        Ok(self.with_counts(update.map, total_count))
    }

    /// Removes at most one occurrence.
    #[must_use]
    pub fn remove(&self, item: &T) -> Self {
        self.remove_copies_positive(item, 1)
    }

    /// Removes at most `copies` occurrences with saturated subtraction.
    ///
    /// Zero returns before hashing. A class whose remaining multiplicity is zero is removed from
    /// the underlying map.
    pub fn remove_copies(&self, item: &T, copies: i32) -> Result<Self, HashBagError> {
        validate_copies(copies)?;
        if copies == 0 {
            return Ok(self.clone());
        }

        Ok(self.remove_copies_positive(item, copies))
    }

    fn remove_copies_positive(&self, item: &T, copies: i32) -> Self {
        debug_assert!(copies > 0);
        let Some((stored_item, existing_count)) = self.counts.get_key_value(item) else {
            return self.clone();
        };
        if copies >= *existing_count {
            let (counts, removed_count) = self
                .counts
                .try_remove(item)
                .expect("a located hash-bag entry must remain removable");
            return self.with_counts(counts, self.total_count - i64::from(removed_count));
        }

        self.with_counts(
            self.counts
                .insert(stored_item.clone(), *existing_count - copies),
            self.total_count - i64::from(copies),
        )
    }

    /// Removes an entire equivalence class in one map traversal.
    #[must_use]
    pub fn remove_all(&self, item: &T) -> Self {
        let Some((counts, removed_count)) = self.counts.try_remove(item) else {
            return self.clone();
        };
        self.with_counts(counts, self.total_count - i64::from(removed_count))
    }

    /// Multiset union: each receiver-policy class receives the larger multiplicity.
    pub fn union(&self, other: &Self) -> Result<Self, HashBagError> {
        let normalized = self.normalize_argument(other)?;
        if self.same_version_as(&normalized) {
            return Ok(self.clone());
        }

        let mut counts = self.counts.clone();
        let mut total_count = self.total_count;
        for (argument_item, argument_count) in normalized.counts.iter() {
            let mut increment = *argument_count;
            let update = counts.add_or_update(
                argument_item.clone(),
                |_| *argument_count,
                |_, receiver_count| {
                    if *receiver_count >= *argument_count {
                        increment = 0;
                        *receiver_count
                    } else {
                        increment = *argument_count - *receiver_count;
                        *argument_count
                    }
                },
            );
            total_count = total_count
                .checked_add(i64::from(increment))
                .ok_or(HashBagError::TotalCountOverflow)?;
            counts = update.map;
        }
        Ok(self.with_counts(counts, total_count))
    }

    /// Multiset intersection: each receiver-policy class receives the smaller multiplicity.
    pub fn intersect(&self, other: &Self) -> Result<Self, HashBagError> {
        let normalized = self.normalize_argument(other)?;
        if self.same_version_as(&normalized) {
            return Ok(self.clone());
        }

        let mut counts = self.counts.clone();
        let mut total_count = self.total_count;
        for (receiver_item, receiver_count) in self.counts.iter() {
            let argument_count = normalized.counts.get(receiver_item).copied().unwrap_or(0);
            if argument_count == 0 {
                let (next, removed_count) = counts
                    .try_remove(receiver_item)
                    .expect("a source hash-bag entry must remain removable");
                counts = next;
                total_count -= i64::from(removed_count);
            } else if argument_count < *receiver_count {
                counts = counts.insert(receiver_item.clone(), argument_count);
                total_count -= i64::from(*receiver_count - argument_count);
            }
        }
        Ok(self.with_counts(counts, total_count))
    }

    /// Saturated multiset subtraction under the receiver's hash policy.
    pub fn except(&self, other: &Self) -> Result<Self, HashBagError> {
        let normalized = self.normalize_argument(other)?;
        if self.same_version_as(&normalized) {
            return Ok(self.clear());
        }

        let mut counts = self.counts.clone();
        let mut total_count = self.total_count;
        for (receiver_item, receiver_count) in self.counts.iter() {
            let argument_count = normalized.counts.get(receiver_item).copied().unwrap_or(0);
            if argument_count >= *receiver_count {
                let (next, removed_count) = counts
                    .try_remove(receiver_item)
                    .expect("a source hash-bag entry must remain removable");
                counts = next;
                total_count -= i64::from(removed_count);
            } else if argument_count > 0 {
                counts = counts.insert(receiver_item.clone(), *receiver_count - argument_count);
                total_count -= i64::from(argument_count);
            }
        }
        Ok(self.with_counts(counts, total_count))
    }

    /// Additive multiset sum with checked per-class and total counts.
    pub fn sum(&self, other: &Self) -> Result<Self, HashBagError> {
        let normalized = self.normalize_argument(other)?;
        let mut result = self.clone();
        for (argument_item, argument_count) in normalized.counts.iter() {
            result = result.add_copies(argument_item.clone(), *argument_count)?;
        }
        Ok(result)
    }

    fn normalize_argument(&self, other: &Self) -> Result<Self, HashBagError> {
        if self.counts.has_same_policy_identity(&other.counts) {
            return Ok(other.clone());
        }

        let mut normalized = Self {
            counts: self.counts.clear(),
            total_count: 0,
        };
        for (item, count) in other.counts.iter() {
            normalized = normalized.add_copies(item.clone(), *count)?;
        }
        Ok(normalized)
    }

    fn same_version_as(&self, other: &Self) -> bool {
        self.total_count == other.total_count
            && self.counts.has_same_policy_identity(&other.counts)
            && self.counts.shares_root_with(&other.counts)
    }

    fn with_counts(&self, counts: PersistentHashMap<T, i32, S>, total_count: i64) -> Self {
        if self.counts.shares_root_with(&counts) {
            debug_assert_eq!(self.total_count, total_count);
            return self.clone();
        }
        Self {
            counts,
            total_count,
        }
    }
}

impl<T, S: Default> Default for PersistentHashBag<T, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<T, S> FromIterator<T> for PersistentHashBag<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone + Default,
{
    fn from_iter<I: IntoIterator<Item = T>>(items: I) -> Self {
        Self::try_from_items_with_hasher(items, S::default())
            .unwrap_or_else(|error| panic!("cannot collect an overflowing hash bag: {error}"))
    }
}

impl<T, S> fmt::Debug for PersistentHashBag<T, S> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PersistentHashBag")
            .field("distinct_count", &self.distinct_count())
            .field("total_count", &self.total_count)
            .finish_non_exhaustive()
    }
}

impl<T, S> PartialEq for PersistentHashBag<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    fn eq(&self, other: &Self) -> bool {
        self.total_count == other.total_count && self.counts == other.counts
    }
}

impl<T, S> Eq for PersistentHashBag<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
}

impl<'a, T, S> IntoIterator for &'a PersistentHashBag<T, S> {
    type Item = &'a T;
    type IntoIter = BagIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

/// Expanded persistent-hash-bag iterator.
pub struct BagIter<'a, T> {
    entries: Iter<'a, T, i32>,
    current_item: Option<&'a T>,
    copies_left: i32,
    remaining: i64,
}

impl<T> Clone for BagIter<'_, T> {
    fn clone(&self) -> Self {
        Self {
            entries: self.entries.clone(),
            current_item: self.current_item,
            copies_left: self.copies_left,
            remaining: self.remaining,
        }
    }
}

impl<'a, T> Iterator for BagIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        if self.copies_left > 0 {
            self.copies_left -= 1;
            self.remaining -= 1;
            return self.current_item;
        }

        let (item, count) = self.entries.next()?;
        debug_assert!(*count > 0, "stored hash-bag multiplicities are positive");
        self.current_item = Some(item);
        self.copies_left = *count - 1;
        self.remaining -= 1;
        Some(item)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        match usize::try_from(self.remaining) {
            Ok(remaining) => (remaining, Some(remaining)),
            Err(_) => (usize::MAX, None),
        }
    }
}

impl<T> FusedIterator for BagIter<'_, T> {}

fn validate_copies(copies: i32) -> Result<(), HashBagError> {
    if copies < 0 {
        Err(HashBagError::NegativeCopies(copies))
    } else {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalization_adopts_the_receiver_policy_identity_and_preserves_counts() {
        let receiver = PersistentHashBag::new()
            .add_copies(1, 2)
            .expect("receiver copies");
        let argument = PersistentHashBag::new()
            .add_copies(1, 3)
            .expect("overlapping copies")
            .add_copies(2, 4)
            .expect("argument-only copies");
        assert!(!receiver.counts.has_same_policy_identity(&argument.counts));

        let normalized = receiver
            .normalize_argument(&argument)
            .expect("normalization succeeds");
        assert!(receiver.counts.has_same_policy_identity(&normalized.counts));
        assert_eq!(normalized.total_count, 7);
        assert_eq!(normalized.count_of(&1), 3);
        assert_eq!(normalized.count_of(&2), 4);
    }

    #[test]
    fn public_edits_preserve_positive_payload_and_cached_total_invariants() {
        let bag = PersistentHashBag::new()
            .add_copies(1, 5)
            .expect("first class")
            .add_copies(2, 3)
            .expect("second class")
            .remove_copies(&1, 2)
            .expect("partial removal")
            .remove_all(&2);

        assert!(bag.counts.iter().all(|(_, count)| *count > 0));
        assert_eq!(
            bag.total_count,
            bag.counts
                .values()
                .map(|count| i64::from(*count))
                .sum::<i64>(),
        );
    }
}
