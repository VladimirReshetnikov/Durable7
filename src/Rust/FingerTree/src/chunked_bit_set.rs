//! Persistent sparse bit set stored as measured fixed-width chunks.
//!
//! [`PersistentChunkedBitSet`] keeps only the chunks that contain at least one set bit, ordered by
//! chunk index inside a [`FingerTree`](crate::FingerTree). The measure caches each subtree's chunk
//! span and population count, which is what makes point lookup, inclusive rank, and select
//! logarithmic rather than linear in the highest index ever set.
//!
//! Bit indices are restricted to the cross-language nonnegative signed-32-bit domain so that every
//! port accepts exactly the same inputs; a negative index is reported as [`NegativeBitIndex`]
//! instead of being coerced. [`PersistentChunkedBitSet::validate`] checks the stored chunks
//! against their cached measure and returns [`ChunkedBitSetStatistics`] when they agree.

use crate::{FingerTree, MeasurePolicy};
use std::collections::BTreeMap;
use std::fmt;
use std::iter;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct BitSetChunk {
    word_index: i32,
    bits: u64,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct BitSetSummary {
    chunk_count: usize,
    pop_count: u64,
    last_word_index: Option<i32>,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct BitSetMeasure;

impl MeasurePolicy<BitSetChunk> for BitSetMeasure {
    type Measure = BitSetSummary;

    fn empty() -> Self::Measure {
        BitSetSummary::default()
    }

    fn measure(element: &BitSetChunk) -> Self::Measure {
        BitSetSummary {
            chunk_count: 1,
            pop_count: u64::from(element.bits.count_ones()),
            last_word_index: Some(element.word_index),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        BitSetSummary {
            chunk_count: left
                .chunk_count
                .checked_add(right.chunk_count)
                .expect("chunked bit-set chunk count overflow"),
            pop_count: left
                .pop_count
                .checked_add(right.pop_count)
                .expect("chunked bit-set population count overflow"),
            last_word_index: right.last_word_index.or(left.last_word_index),
        }
    }
}

/// A bit index is outside the cross-language nonnegative signed-32-bit domain.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct NegativeBitIndex;

impl fmt::Display for NegativeBitIndex {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("bit indexes must be nonnegative")
    }
}

impl std::error::Error for NegativeBitIndex {}

/// Successful sparse bit-set invariant statistics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ChunkedBitSetStatistics {
    /// The number of stored chunks.
    pub chunk_count: usize,
    /// The number of set bits, recounted from the stored chunks.
    pub pop_count: u64,
}

/// A disagreement between stored chunks and their cached measure.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ChunkedBitSetInvariantError {
    /// Stored chunk indices are not strictly ascending, so lookups could not be trusted.
    WordIndexesNotStrictlyAscending,
    /// An all-zero chunk was retained, which the sparse representation forbids.
    EmptyChunk,
    /// The cached measure disagrees with the bits actually stored.
    MeasureMismatch,
}

impl fmt::Display for ChunkedBitSetInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::WordIndexesNotStrictlyAscending => {
                "chunked bit-set word indexes are not strictly ascending"
            }
            Self::EmptyChunk => "the chunked bit set stores an empty word",
            Self::MeasureMismatch => "the chunked bit-set measure disagrees with its chunks",
        })
    }
}

impl std::error::Error for ChunkedBitSetInvariantError {}

#[derive(Clone, Copy)]
enum SetOperation {
    Union,
    Intersect,
    Except,
    SymmetricExcept,
}

/// Immutable sparse bit set over the nonnegative `i32` domain.
///
/// Only nonzero 64-bit words are stored. Population count and the last word index are cached in a
/// measured persistent tree, providing logarithmic membership, point edits, inclusive rank, and
/// zero-based select in the number of represented words.
#[derive(Clone)]
pub struct PersistentChunkedBitSet {
    chunks: FingerTree<BitSetChunk, BitSetMeasure>,
}

impl PersistentChunkedBitSet {
    /// Creates an empty bit set.
    #[must_use]
    pub fn new() -> Self {
        Self {
            chunks: FingerTree::new(),
        }
    }

    /// Builds a bit set with every listed index set, failing on the first negative index.
    pub fn from_indices<I>(indices: I) -> Result<Self, NegativeBitIndex>
    where
        I: IntoIterator<Item = i32>,
    {
        let mut words = BTreeMap::<i32, u64>::new();
        for bit_index in indices {
            Self::validate_index(bit_index)?;
            let word_index = bit_index >> 6;
            let bit = 1_u64 << (bit_index & 63);
            *words.entry(word_index).or_default() |= bit;
        }
        Ok(Self::from_chunks(
            words
                .into_iter()
                .map(|(word_index, bits)| BitSetChunk { word_index, bits })
                .collect(),
        ))
    }

    /// Returns the number of set bits. O(1) — the population count is cached at the root.
    ///
    /// Widened to `u64` because the index domain spans the whole nonnegative signed-32-bit range.
    #[must_use]
    pub fn len(&self) -> u64 {
        self.chunks.measure().pop_count
    }

    /// Returns the number of stored chunks, that is, how many fixed-width blocks contain at least one
    /// set bit. A measure of representation size rather than of contents.
    #[must_use]
    pub fn chunk_count(&self) -> usize {
        self.chunks.measure().chunk_count
    }

    /// Returns `true` when no bit is set.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.chunks.is_empty()
    }

    /// Reports whether two sets are backed by the same chunk sequence, so neither can observe an edit
    /// made to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.chunks.shares_storage_with(&other.chunks)
    }

    /// Reports whether `bit_index` is set. A negative index is simply absent rather than an error.
    /// O(log c) in the number of stored chunks.
    #[must_use]
    pub fn contains(&self, bit_index: i32) -> bool {
        if bit_index < 0 {
            return false;
        }
        let word_index = bit_index >> 6;
        self.chunks
            .try_locate(|summary| {
                summary
                    .last_word_index
                    .is_some_and(|last| last >= word_index)
            })
            .item
            .is_some_and(|chunk| {
                chunk.word_index == word_index && chunk.bits & (1_u64 << (bit_index & 63)) != 0
            })
    }

    /// Returns a set with `bit_index` set, failing on a negative index.
    ///
    /// Setting an already set bit is a no-op that shares the receiver's storage.
    pub fn insert(&self, bit_index: i32) -> Result<Self, NegativeBitIndex> {
        Self::validate_index(bit_index)?;
        let word_index = bit_index >> 6;
        let bit = 1_u64 << (bit_index & 63);
        let located = self.chunks.try_locate(|summary| {
            summary
                .last_word_index
                .is_some_and(|last| last >= word_index)
        });
        if let Some(chunk) = located.item
            && chunk.word_index == word_index
        {
            let updated_bits = chunk.bits | bit;
            if updated_bits == chunk.bits {
                return Ok(self.clone());
            }
            return Ok(Self {
                chunks: self.replace_chunk(
                    located.index,
                    BitSetChunk {
                        word_index,
                        bits: updated_bits,
                    },
                ),
            });
        }
        let split = self
            .chunks
            .split_at_index(located.index)
            .expect("located insertion rank is valid");
        Ok(Self {
            chunks: split
                .left
                .append(BitSetChunk {
                    word_index,
                    bits: bit,
                })
                .concat(&split.right),
        })
    }

    /// Sets `bit_index` and reports whether it was previously clear, failing on a negative index.
    pub fn try_insert(&self, bit_index: i32) -> Result<(Self, bool), NegativeBitIndex> {
        let result = self.insert(bit_index)?;
        let changed = !result.shares_storage_with(self);
        Ok((result, changed))
    }

    /// Returns a set with `bit_index` clear. Clearing an unset or negative index is a no-op that
    /// shares the receiver's storage; a chunk emptied by the removal is dropped.
    #[must_use]
    pub fn remove(&self, bit_index: i32) -> Self {
        if bit_index < 0 {
            return self.clone();
        }
        let word_index = bit_index >> 6;
        let bit = 1_u64 << (bit_index & 63);
        let located = self.chunks.try_locate(|summary| {
            summary
                .last_word_index
                .is_some_and(|last| last >= word_index)
        });
        let Some(chunk) = located.item else {
            return self.clone();
        };
        if chunk.word_index != word_index || chunk.bits & bit == 0 {
            return self.clone();
        }
        let updated_bits = chunk.bits & !bit;
        if updated_bits == 0 {
            Self {
                chunks: self.remove_chunk(located.index),
            }
        } else {
            Self {
                chunks: self.replace_chunk(
                    located.index,
                    BitSetChunk {
                        word_index,
                        bits: updated_bits,
                    },
                ),
            }
        }
    }

    /// Returns the number of set bits less than or equal to `bit_index`.
    #[must_use]
    pub fn rank(&self, bit_index: i32) -> u64 {
        if bit_index < 0 {
            return 0;
        }
        let word_index = bit_index >> 6;
        let located = self.chunks.try_locate(|summary| {
            summary
                .last_word_index
                .is_some_and(|last| last >= word_index)
        });
        let Some(chunk) = located.item else {
            return located.measure_before.pop_count;
        };
        if chunk.word_index != word_index {
            return located.measure_before.pop_count;
        }
        let offset = bit_index & 63;
        let mask = if offset == 63 {
            u64::MAX
        } else {
            (1_u64 << (offset + 1)) - 1
        };
        located.measure_before.pop_count + u64::from((chunk.bits & mask).count_ones())
    }

    /// Selects the bit at a zero-based population rank.
    #[must_use]
    pub fn select(&self, rank: u64) -> Option<i32> {
        if rank >= self.len() {
            return None;
        }
        let located = self.chunks.try_locate(|summary| summary.pop_count > rank);
        let chunk = located.item?;
        let mut bits = chunk.bits;
        for _ in 0..(rank - located.measure_before.pop_count) {
            bits &= bits - 1;
        }
        let offset = bits.trailing_zeros() as i64;
        i32::try_from(i64::from(chunk.word_index) * 64 + offset).ok()
    }

    /// Returns the bits set in either operand.
    ///
    /// Merges chunk by chunk rather than bit by bit, so the cost is proportional to the number of
    /// stored chunks, not to the index range.
    #[must_use]
    pub fn union(&self, other: &Self) -> Self {
        if self.shares_storage_with(other) {
            self.clone()
        } else {
            self.combine(other, SetOperation::Union)
        }
    }

    /// Returns the bits set in both operands. Chunkwise, as for [`Self::union`].
    #[must_use]
    pub fn intersect(&self, other: &Self) -> Self {
        if self.shares_storage_with(other) {
            self.clone()
        } else {
            self.combine(other, SetOperation::Intersect)
        }
    }

    /// Returns this set's bits that are clear in `other`. Chunkwise, as for [`Self::union`].
    #[must_use]
    pub fn except(&self, other: &Self) -> Self {
        if self.shares_storage_with(other) {
            Self::new()
        } else {
            self.combine(other, SetOperation::Except)
        }
    }

    /// Returns the bits set in exactly one operand. Chunkwise, as for [`Self::union`].
    #[must_use]
    pub fn symmetric_except(&self, other: &Self) -> Self {
        if self.shares_storage_with(other) {
            Self::new()
        } else {
            self.combine(other, SetOperation::SymmetricExcept)
        }
    }

    /// Returns an empty set. Clearing an already empty set is a no-op that shares the receiver's
    /// storage.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::new()
        }
    }

    /// Iterates the set bit indices in ascending order.
    pub fn iter(&self) -> impl Iterator<Item = i32> + '_ {
        self.chunks.iter().flat_map(|chunk| {
            let word_index = chunk.word_index;
            let mut bits = chunk.bits;
            iter::from_fn(move || {
                if bits == 0 {
                    return None;
                }
                let offset = bits.trailing_zeros();
                bits &= bits - 1;
                i32::try_from(i64::from(word_index) * 64 + i64::from(offset)).ok()
            })
        })
    }

    /// Checks the stored chunks against their cached measure: ascending distinct chunk indices, no
    /// all-zero chunk retained, and a population count matching the bits actually set.
    ///
    /// A defensive audit over the whole set; ordinary operations maintain these invariants.
    pub fn validate(&self) -> Result<ChunkedBitSetStatistics, ChunkedBitSetInvariantError> {
        let mut previous = None;
        let mut chunk_count = 0usize;
        let mut pop_count = 0u64;
        for chunk in self.chunks.iter() {
            if previous.is_some_and(|word| word >= chunk.word_index) {
                return Err(ChunkedBitSetInvariantError::WordIndexesNotStrictlyAscending);
            }
            if chunk.bits == 0 {
                return Err(ChunkedBitSetInvariantError::EmptyChunk);
            }
            previous = Some(chunk.word_index);
            chunk_count = chunk_count
                .checked_add(1)
                .ok_or(ChunkedBitSetInvariantError::MeasureMismatch)?;
            pop_count = pop_count
                .checked_add(u64::from(chunk.bits.count_ones()))
                .ok_or(ChunkedBitSetInvariantError::MeasureMismatch)?;
        }
        if chunk_count != self.chunk_count()
            || pop_count != self.len()
            || previous != self.chunks.measure().last_word_index
        {
            return Err(ChunkedBitSetInvariantError::MeasureMismatch);
        }
        Ok(ChunkedBitSetStatistics {
            chunk_count,
            pop_count,
        })
    }

    fn validate_index(bit_index: i32) -> Result<(), NegativeBitIndex> {
        (bit_index >= 0).then_some(()).ok_or(NegativeBitIndex)
    }

    fn from_chunks(chunks: Vec<BitSetChunk>) -> Self {
        Self {
            chunks: FingerTree::from_vec(chunks),
        }
    }

    fn replace_chunk(
        &self,
        index: usize,
        replacement: BitSetChunk,
    ) -> FingerTree<BitSetChunk, BitSetMeasure> {
        let split = self
            .chunks
            .split_at_index(index)
            .expect("validated chunk index is valid");
        let after = split
            .right
            .split_at_index(1)
            .expect("validated chunk is present");
        split.left.append(replacement).concat(&after.right)
    }

    fn remove_chunk(&self, index: usize) -> FingerTree<BitSetChunk, BitSetMeasure> {
        let split = self
            .chunks
            .split_at_index(index)
            .expect("validated chunk index is valid");
        let after = split
            .right
            .split_at_index(1)
            .expect("validated chunk is present");
        split.left.concat(&after.right)
    }

    fn combine(&self, other: &Self, operation: SetOperation) -> Self {
        let mut left = self.chunks.iter().peekable();
        let mut right = other.chunks.iter().peekable();
        let mut chunks = Vec::new();
        while left.peek().is_some() || right.peek().is_some() {
            match (left.peek(), right.peek()) {
                (Some(left_chunk), Some(right_chunk))
                    if left_chunk.word_index == right_chunk.word_index =>
                {
                    let left_chunk = *left.next().expect("peeked left chunk");
                    let right_chunk = *right.next().expect("peeked right chunk");
                    let bits = match operation {
                        SetOperation::Union => left_chunk.bits | right_chunk.bits,
                        SetOperation::Intersect => left_chunk.bits & right_chunk.bits,
                        SetOperation::Except => left_chunk.bits & !right_chunk.bits,
                        SetOperation::SymmetricExcept => left_chunk.bits ^ right_chunk.bits,
                    };
                    if bits != 0 {
                        chunks.push(BitSetChunk {
                            word_index: left_chunk.word_index,
                            bits,
                        });
                    }
                }
                (Some(left_chunk), Some(right_chunk))
                    if left_chunk.word_index < right_chunk.word_index =>
                {
                    let chunk = *left.next().expect("peeked left chunk");
                    if matches!(
                        operation,
                        SetOperation::Union | SetOperation::Except | SetOperation::SymmetricExcept
                    ) {
                        chunks.push(chunk);
                    }
                }
                (Some(_), Some(_)) => {
                    let chunk = *right.next().expect("peeked right chunk");
                    if matches!(
                        operation,
                        SetOperation::Union | SetOperation::SymmetricExcept
                    ) {
                        chunks.push(chunk);
                    }
                }
                (Some(_), None) => {
                    let chunk = *left.next().expect("peeked left chunk");
                    if matches!(
                        operation,
                        SetOperation::Union | SetOperation::Except | SetOperation::SymmetricExcept
                    ) {
                        chunks.push(chunk);
                    }
                }
                (None, Some(_)) => {
                    let chunk = *right.next().expect("peeked right chunk");
                    if matches!(
                        operation,
                        SetOperation::Union | SetOperation::SymmetricExcept
                    ) {
                        chunks.push(chunk);
                    }
                }
                (None, None) => break,
            }
        }
        if chunks.is_empty() {
            return Self::new();
        }
        let result = Self::from_chunks(chunks);
        if result.chunks == self.chunks {
            self.clone()
        } else {
            result
        }
    }
}

/// Presence-discriminated set-bit cursor search.
pub struct ChunkedBitSetCursorSearch {
    pub found: bool,
    pub cursor: PersistentChunkedBitSetCursor,
}

/// Immutable root-plus-population-rank cursor over present set bits.
#[derive(Clone)]
pub struct PersistentChunkedBitSetCursor {
    set: PersistentChunkedBitSet,
    position: u64,
}

impl PersistentChunkedBitSet {
    /// Creates a cursor at the gap `position` in `0..=len` of the ascending set-bit sequence, or
    /// `None` when it exceeds the population count.
    pub fn cursor_at(&self, position: u64) -> Option<PersistentChunkedBitSetCursor> {
        (position <= self.len()).then(|| PersistentChunkedBitSetCursor {
            set: self.clone(),
            position,
        })
    }
    /// Creates a cursor before the first set bit at or after `bit_index`. O(log c).
    pub fn cursor_at_or_after(&self, bit_index: i32) -> PersistentChunkedBitSetCursor {
        let position = if bit_index <= 0 {
            0
        } else {
            self.rank(bit_index - 1)
        };
        self.cursor_at(position).expect("population rank is valid")
    }
    /// Seeks to `bit_index` and reports whether it is set.
    ///
    /// On a miss the cursor sits before the next set bit, so it remains usable.
    pub fn find_cursor(&self, bit_index: i32) -> ChunkedBitSetCursorSearch {
        let cursor = self.cursor_at_or_after(bit_index);
        ChunkedBitSetCursorSearch {
            found: bit_index >= 0 && cursor.peek_next() == Some(bit_index),
            cursor,
        }
    }
}

impl PersistentChunkedBitSetCursor {
    /// Returns the population count of the set version this cursor is positioned in.
    pub fn len(&self) -> u64 {
        self.set.len()
    }
    /// Returns `true` when that set version has no bits set.
    pub fn is_empty(&self) -> bool {
        self.set.is_empty()
    }
    /// Returns the cursor's gap index within the ascending set-bit sequence, which is also the rank
    /// of the next set bit.
    pub fn position(&self) -> u64 {
        self.position
    }
    /// Returns `true` when the gap precedes the lowest set bit.
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }
    /// Returns `true` when the gap follows the highest set bit.
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }
    /// Returns the set bit immediately before the gap, or `None` at the start.
    pub fn peek_previous(&self) -> Option<i32> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.set.select(rank))
    }
    /// Returns the set bit immediately after the gap, or `None` at the end.
    pub fn peek_next(&self) -> Option<i32> {
        self.set.select(self.position)
    }
    /// Returns a cursor one set bit earlier, or `None` at the start. The receiver is unchanged.
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.set.cursor_at(position))
    }
    /// Returns a cursor one set bit later, or `None` at the end. The receiver is unchanged.
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            set: self.set.clone(),
            position: self.position + 1,
        })
    }
    /// Jumps to the gap at `position` within the same set version, or `None` when `position` exceeds
    /// the population count.
    pub fn seek_rank(&self, position: u64) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.set.cursor_at(position)
        }
    }
    /// Sets `bit_index` and returns a cursor positioned just after it, failing on a negative index.
    ///
    /// The gap moves to the bit's ascending position rather than staying where the receiver was,
    /// since a bit set's order is decided by index.
    pub fn insert(&self, bit_index: i32) -> Result<Self, NegativeBitIndex> {
        let set = self.set.insert(bit_index)?;
        if self.set.contains(bit_index) {
            return Ok(self.clone());
        }
        let position = if bit_index == 0 {
            0
        } else {
            self.set.rank(bit_index - 1)
        };
        Ok(Self {
            set,
            position: position + 1,
        })
    }
    /// Clears the set bit before the gap and returns a cursor in its place, or `None` at the start.
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let bit = self.set.select(position)?;
        Some(Self {
            set: self.set.remove(bit),
            position,
        })
    }
    /// Clears the set bit after the gap and returns a cursor in its place, or `None` at the end.
    pub fn delete_next(&self) -> Option<Self> {
        let bit = self.set.select(self.position)?;
        Some(Self {
            set: self.set.remove(bit),
            position: self.position,
        })
    }
    /// Borrows the set version this cursor is positioned in.
    pub fn snapshot(&self) -> &PersistentChunkedBitSet {
        &self.set
    }
}

impl Default for PersistentChunkedBitSet {
    fn default() -> Self {
        Self::new()
    }
}

impl fmt::Debug for PersistentChunkedBitSet {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_set().entries(self.iter()).finish()
    }
}
