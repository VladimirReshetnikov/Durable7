use crate::measured::{FingerTree, MeasurePolicy};
use std::fmt;
use std::marker::PhantomData;
use std::sync::Arc;

const MIN_CHUNK_SIZE: usize = 256;
const MAX_CHUNK_SIZE: usize = 2048;

struct RopeChunk<T> {
    data: Arc<[T]>,
    start: usize,
    len: usize,
}

impl<T> Clone for RopeChunk<T> {
    fn clone(&self) -> Self {
        Self {
            data: Arc::clone(&self.data),
            start: self.start,
            len: self.len,
        }
    }
}

impl<T> RopeChunk<T> {
    fn new(items: Vec<T>) -> Self {
        let len = items.len();
        Self {
            data: Arc::from(items),
            start: 0,
            len,
        }
    }

    fn len(&self) -> usize {
        self.len
    }

    fn as_slice(&self) -> &[T] {
        &self.data[self.start..self.start + self.len]
    }

    fn first(&self) -> Option<&T> {
        self.as_slice().first()
    }

    fn last(&self) -> Option<&T> {
        self.as_slice().last()
    }

    fn get(&self, offset: usize) -> Option<&T> {
        self.as_slice().get(offset)
    }

    fn slice(&self, offset: usize, len: usize) -> Self {
        debug_assert!(offset <= self.len && len <= self.len - offset);
        Self {
            data: Arc::clone(&self.data),
            start: self.start + offset,
            len,
        }
    }
}

impl<T> RopeChunk<T>
where
    T: Clone,
{
    fn set_at(&self, offset: usize, item: T) -> Self {
        let mut items = self.as_slice().to_vec();
        items[offset] = item;
        Self::new(items)
    }

    fn insert_at(&self, offset: usize, item: T) -> Self {
        let mut items = Vec::with_capacity(self.len + 1);
        items.extend_from_slice(&self.as_slice()[..offset]);
        items.push(item);
        items.extend_from_slice(&self.as_slice()[offset..]);
        Self::new(items)
    }

    fn remove_at(&self, offset: usize) -> Self {
        let mut items = Vec::with_capacity(self.len - 1);
        items.extend_from_slice(&self.as_slice()[..offset]);
        items.extend_from_slice(&self.as_slice()[offset + 1..]);
        Self::new(items)
    }

    fn concat(left: &Self, right: &Self) -> Self {
        let mut items = Vec::with_capacity(left.len + right.len);
        items.extend_from_slice(left.as_slice());
        items.extend_from_slice(right.as_slice());
        Self::new(items)
    }
}

impl<T> fmt::Debug for RopeChunk<T>
where
    T: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.as_slice()).finish()
    }
}

impl<T> PartialEq for RopeChunk<T>
where
    T: PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.as_slice() == other.as_slice()
    }
}

impl<T> Eq for RopeChunk<T> where T: Eq {}

struct ChunkLengthMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<RopeChunk<T>> for ChunkLengthMeasure<T> {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(element: &RopeChunk<T>) -> Self::Measure {
        element.len()
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left.checked_add(*right).expect("rope length overflow")
    }
}

type RopeTree<T> = FingerTree<RopeChunk<T>, ChunkLengthMeasure<T>>;

/// A persistent chunked sequence with cached length measures.
///
/// Cached lengths use `usize`. Construction or growth that would make the total length
/// unrepresentable panics before publishing a result; every input rope remains valid and reusable.
#[derive(Debug, PartialEq, Eq)]
pub struct Rope<T> {
    chunks: RopeTree<T>,
}

/// An immutable positional editing cursor over one [`Rope`] snapshot.
///
/// A cursor denotes a gap in `0..=len`: elements before `position` precede the gap and elements at
/// or after it follow the gap. Movement and edits return new cursor values, so retained cursors can
/// branch independently. This Rust checkpoint stores a cheap root-sharing rope value plus the gap;
/// it is not the focused zipper used by the C# implementation and makes no amortized-locality claim.
///
/// Creating, cloning, moving, seeking, and taking a snapshot are O(1). Peeks and point edits are
/// O(log n) plus bounded chunk work. Inserting `m` elements is O(m + log n). Navigation, peeking,
/// seeking, and snapshotting do not require `T: Clone`; edits carry that bound because the current
/// rope substrate copies the affected chunk. An edit whose resulting length cannot fit in `usize`
/// panics before returning and leaves the receiver reusable. The cursor intentionally has no
/// `Default`; it is `Send + Sync` whenever `T` is `Send + Sync`.
#[derive(Debug, PartialEq, Eq)]
pub struct RopeCursor<T> {
    rope: Rope<T>,
    position: usize,
}

pub struct RopeIter<'a, T> {
    chunks: crate::measured::Iter<'a, RopeChunk<T>, usize>,
    current: Option<std::slice::Iter<'a, T>>,
}

impl<T> Clone for Rope<T> {
    fn clone(&self) -> Self {
        Self {
            chunks: self.chunks.clone(),
        }
    }
}

impl<T> Clone for RopeCursor<T> {
    fn clone(&self) -> Self {
        Self {
            rope: self.rope.clone(),
            position: self.position,
        }
    }
}

impl<T> Rope<T> {
    #[must_use]
    pub fn new() -> Self {
        Self::from_tree(RopeTree::new())
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_tree(tree_from_items(items))
    }

    fn from_tree(chunks: RopeTree<T>) -> Self {
        Self { chunks }
    }

    /// Creates a cursor at the gap before the first element. O(1).
    #[must_use]
    pub fn cursor(&self) -> RopeCursor<T> {
        RopeCursor {
            rope: self.clone(),
            position: 0,
        }
    }

    /// Creates a cursor at `position`, returning `None` when it is outside `0..=len`. O(1).
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<RopeCursor<T>> {
        (position <= self.len()).then(|| RopeCursor {
            rope: self.clone(),
            position,
        })
    }

    #[must_use]
    pub fn len(&self) -> usize {
        *self.chunks.measure()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.chunks.is_empty()
    }

    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.chunks.front().and_then(RopeChunk::first)
    }

    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.chunks.back().and_then(RopeChunk::last)
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len() {
            return None;
        }

        let located = self.chunks.try_locate(|count| *count > index);
        let offset = index - located.measure_before;
        self.chunks.get(located.index)?.get(offset)
    }

    pub fn iter(&self) -> RopeIter<'_, T> {
        RopeIter {
            chunks: self.chunks.iter(),
            current: None,
        }
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.chunks.shares_storage_with(&other.chunks)
    }

    #[cfg(test)]
    fn chunk_count(&self) -> usize {
        self.chunks.iter().count()
    }

    #[cfg(test)]
    fn validate_chunk_invariants(&self) {
        let mut total = 0;
        for chunk in self.chunks.iter() {
            assert!(chunk.len() > 0);
            assert!(chunk.len() <= MAX_CHUNK_SIZE);
            total += chunk.len();
        }

        assert_eq!(total, self.len());
        self.chunks.validate_invariants();
    }
}

impl<T> RopeCursor<T> {
    /// Returns the number of elements in this cursor's immutable rope snapshot. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.rope.len()
    }

    /// Returns whether this cursor's immutable rope snapshot is empty. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.rope.is_empty()
    }

    /// Returns the gap position in `0..=len`. O(1).
    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    /// Returns whether the gap is before the first element. O(1).
    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    /// Returns whether the gap is after the last element. O(1).
    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    /// Borrows the element immediately before the gap, if one exists. O(log n).
    #[must_use]
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.rope.get(index))
    }

    /// Borrows the element immediately after the gap, if one exists. O(log n).
    #[must_use]
    pub fn peek_next(&self) -> Option<&T> {
        self.rope.get(self.position)
    }

    /// Moves the gap one element toward the start, or returns `None` at the start. O(1).
    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            rope: self.rope.clone(),
            position: self.position.checked_sub(1)?,
        })
    }

    /// Moves the gap one element toward the end, or returns `None` at the end. O(1).
    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }

        Some(Self {
            rope: self.rope.clone(),
            position: self.position + 1,
        })
    }

    /// Moves the gap to `position`, returning `None` outside `0..=len`. O(1).
    ///
    /// Seeking to the current position returns an unchanged root-sharing cursor value.
    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        if position > self.len() {
            return None;
        }

        if position == self.position {
            return Some(self.clone());
        }

        Some(Self {
            rope: self.rope.clone(),
            position,
        })
    }

    /// Returns this cursor's immutable rope snapshot by O(1) root sharing.
    #[must_use]
    pub fn snapshot(&self) -> Rope<T> {
        self.rope.clone()
    }
}

impl<T> RopeCursor<T>
where
    T: Clone,
{
    /// Inserts `item` at the gap and returns a cursor immediately after it.
    ///
    /// The edit is O(log n) plus bounded chunk copying. The receiver remains valid.
    ///
    /// # Panics
    ///
    /// Panics if the resulting rope length cannot be represented by `usize`.
    #[must_use]
    pub fn insert(&self, item: T) -> Self {
        let position = self
            .position
            .checked_add(1)
            .expect("rope cursor position overflow");
        let rope = self
            .rope
            .insert_at(self.position, item)
            .expect("a cursor gap is always a valid insertion position");
        Self { rope, position }
    }

    /// Inserts `items` at the gap in iteration order and returns a cursor after the range.
    ///
    /// Inserting `m` elements is O(m + log n). An empty input returns an unchanged root-sharing
    /// cursor value.
    ///
    /// # Panics
    ///
    /// Panics if the resulting rope length cannot be represented by `usize`.
    #[must_use]
    pub fn insert_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let items: Vec<T> = items.into_iter().collect();
        if items.is_empty() {
            return self.clone();
        }

        let position = self
            .position
            .checked_add(items.len())
            .expect("rope cursor position overflow");
        let rope = self
            .rope
            .insert_range(self.position, items)
            .expect("a cursor gap is always a valid range insertion position");
        Self { rope, position }
    }

    /// Deletes the element before the gap and moves the gap left, or returns `None` at the start.
    ///
    /// The edit is O(log n) plus bounded chunk copying. The receiver remains valid.
    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let rope = self
            .rope
            .remove_at(position)
            .expect("a non-start cursor always has a previous element");
        Some(Self { rope, position })
    }

    /// Deletes the element after the gap without moving it, or returns `None` at the end.
    ///
    /// The edit is O(log n) plus bounded chunk copying. The receiver remains valid.
    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }

        let rope = self
            .rope
            .remove_at(self.position)
            .expect("a non-end cursor always has a next element");
        Some(Self {
            rope,
            position: self.position,
        })
    }

    /// Unconditionally replaces the element after the gap, or returns `None` at the end.
    ///
    /// Replacement invokes no equality comparison and creates an edited snapshot even when the
    /// supplied value compares equal to the stored value. The gap does not move. The edit is
    /// O(log n) plus bounded chunk copying.
    #[must_use]
    pub fn replace_next(&self, item: T) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }

        let rope = self
            .rope
            .set_item(self.position, item)
            .expect("a non-end cursor always has a next element");
        Some(Self {
            rope,
            position: self.position,
        })
    }
}

impl<'a, T> Iterator for RopeIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Some(current) = &mut self.current
                && let Some(item) = current.next()
            {
                return Some(item);
            }

            let chunk = self.chunks.next()?;
            self.current = Some(chunk.as_slice().iter());
        }
    }
}

impl<T> Default for Rope<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for Rope<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> Rope<T>
where
    T: Clone,
{
    #[must_use]
    pub fn from_chunks<I, C>(chunks: I) -> Self
    where
        I: IntoIterator<Item = C>,
        C: AsRef<[T]>,
    {
        Self::from_tree(tree_from_chunks(chunks))
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut items = Vec::with_capacity(self.len());
        for chunk in self.chunks.iter() {
            items.extend_from_slice(chunk.as_slice());
        }
        items
    }

    pub fn copy_to(&self, index: usize, destination: &mut [T]) -> Option<()> {
        if index > self.len() || destination.len() > self.len() - index {
            return None;
        }

        let mut chunk_start = 0;
        let mut written = 0;
        for chunk in self.chunks.iter() {
            let chunk_end = chunk_start + chunk.len();
            if chunk_end <= index {
                chunk_start = chunk_end;
                continue;
            }

            let start_in_chunk = index.saturating_sub(chunk_start);
            let available = chunk.len() - start_in_chunk;
            let take = available.min(destination.len() - written);
            destination[written..written + take]
                .clone_from_slice(&chunk.as_slice()[start_in_chunk..start_in_chunk + take]);
            written += take;
            if written == destination.len() {
                return Some(());
            }

            chunk_start = chunk_end;
        }

        Some(())
    }

    /// Returns a rope with `item` prepended.
    ///
    /// # Panics
    ///
    /// Panics if the resulting rope length cannot be represented by `usize`.
    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        self.insert_at(0, item)
            .expect("front insertion index is always valid")
    }

    /// Returns a rope with `item` appended.
    ///
    /// # Panics
    ///
    /// Panics if the resulting rope length cannot be represented by `usize`.
    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        self.insert_at(self.len(), item)
            .expect("back insertion index is always valid")
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let (left, chunk, right) = self
            .chunks
            .try_split_find(|count| *count > index)
            .expect("validated index locates a rope chunk");
        let offset = index - *left.measure();
        Some(Self::from_tree(
            left.append(chunk.set_at(offset, item)).concat(&right),
        ))
    }

    /// Returns a rope with `item` inserted at `index`, or `None` when `index > len()`.
    ///
    /// # Panics
    ///
    /// Panics if the resulting rope length cannot be represented by `usize`.
    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        if self.is_empty() {
            return Some(Self::from_tree(
                RopeTree::new().append(RopeChunk::new(vec![item])),
            ));
        }

        if index == self.len() {
            let (last, rest) = self
                .chunks
                .try_view_right()
                .expect("non-empty rope has a last chunk");
            if last.len() < MAX_CHUNK_SIZE {
                return Some(Self::from_tree(
                    rest.append(last.insert_at(last.len(), item)),
                ));
            }

            return Some(Self::from_tree(
                self.chunks
                    .concat(&RopeTree::new().append(RopeChunk::new(vec![item]))),
            ));
        }

        let (left, chunk, right) = self
            .chunks
            .try_split_find(|count| *count > index)
            .expect("validated index locates a rope chunk");
        let offset = index - *left.measure();
        Some(Self::from_tree(join_grown(
            left,
            chunk.insert_at(offset, item),
            right,
        )))
    }

    /// Returns a rope with `items` inserted at `index`, or `None` when `index > len()`.
    ///
    /// # Panics
    ///
    /// Panics if the resulting rope length cannot be represented by `usize`.
    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        if index > self.len() {
            return None;
        }

        let middle = Self::from_vec(items.into_iter().collect());
        if middle.is_empty() {
            return Some(self.clone());
        }

        let (left, right) = self.split_at(index)?;
        Some(left.concat(&middle).concat(&right))
    }

    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let (left, chunk, right) = self
            .chunks
            .try_split_find(|count| *count > index)
            .expect("validated index locates a rope chunk");
        if chunk.len() == 1 {
            return Some(Self::from_tree(left.concat(&right)));
        }

        let offset = index - *left.measure();
        Some(Self::from_tree(join_shrunk(
            left,
            chunk.remove_at(offset),
            right,
        )))
    }

    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        if index > self.len() || count > self.len() - index {
            return None;
        }

        if count == 0 {
            return Some(self.clone());
        }

        let (left, rest) = self.split_at(index)?;
        let (_, right) = rest.split_at(count)?;
        Some(left.concat(&right))
    }

    #[must_use]
    pub fn slice(&self, index: usize, count: usize) -> Option<Self> {
        if index > self.len() || count > self.len() - index {
            return None;
        }

        if count == 0 {
            return Some(Self::new());
        }

        if index == 0 && count == self.len() {
            return Some(self.clone());
        }

        let (_, rest) = self.split_at(index)?;
        let (range, _) = rest.split_at(count)?;
        Some(range)
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<(Self, Self)> {
        if index > self.len() {
            return None;
        }

        if index == 0 {
            return Some((Self::new(), self.clone()));
        }

        if index == self.len() {
            return Some((self.clone(), Self::new()));
        }

        let (left, chunk, right) = self
            .chunks
            .try_split_find(|count| *count > index)
            .expect("validated index locates a rope chunk");
        let offset = index - *left.measure();
        let left_tree = if offset == 0 {
            left
        } else {
            left.append(chunk.slice(0, offset))
        };
        let right_tree = if offset == chunk.len() {
            right
        } else {
            right.prepend(chunk.slice(offset, chunk.len() - offset))
        };
        Some((Self::from_tree(left_tree), Self::from_tree(right_tree)))
    }

    /// Concatenates two persistent ropes.
    ///
    /// # Panics
    ///
    /// Panics before returning if the combined length cannot fit in `usize`. Both inputs remain
    /// valid because concatenation does not mutate them.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        let (last_left, left_rest) = self
            .chunks
            .try_view_right()
            .expect("non-empty rope has a last chunk");
        let (first_right, right_rest) = other
            .chunks
            .try_view_left()
            .expect("non-empty rope has a first chunk");
        if last_left.len() + first_right.len() <= MAX_CHUNK_SIZE {
            return Self::from_tree(
                left_rest
                    .append(RopeChunk::concat(&last_left, &first_right))
                    .concat(&right_rest),
            );
        }

        Self::from_tree(self.chunks.concat(&other.chunks))
    }

    #[must_use]
    pub fn compact(&self) -> Self {
        Self::from_vec(self.to_vec())
    }
}

fn tree_from_items<T>(items: Vec<T>) -> RopeTree<T> {
    if items.is_empty() {
        return RopeTree::new();
    }

    let mut chunks = Vec::new();
    let mut current = Vec::with_capacity(MAX_CHUNK_SIZE);
    for item in items {
        current.push(item);
        if current.len() == MAX_CHUNK_SIZE {
            chunks.push(RopeChunk::new(current));
            current = Vec::with_capacity(MAX_CHUNK_SIZE);
        }
    }

    if !current.is_empty() {
        chunks.push(RopeChunk::new(current));
    }

    chunks.into_iter().collect()
}

fn tree_from_chunks<T, I, C>(chunks: I) -> RopeTree<T>
where
    T: Clone,
    I: IntoIterator<Item = C>,
    C: AsRef<[T]>,
{
    let mut tree = RopeTree::new();
    for chunk in chunks {
        for block in chunk.as_ref().chunks(MAX_CHUNK_SIZE) {
            if !block.is_empty() {
                tree = tree.append(RopeChunk::new(block.to_vec()));
            }
        }
    }

    tree
}

fn join_grown<T>(left: RopeTree<T>, grown: RopeChunk<T>, right: RopeTree<T>) -> RopeTree<T>
where
    T: Clone,
{
    if grown.len() <= MAX_CHUNK_SIZE {
        return left.append(grown).concat(&right);
    }

    let half = grown.len() / 2;
    left.append(grown.slice(0, half))
        .append(grown.slice(half, grown.len() - half))
        .concat(&right)
}

fn join_shrunk<T>(left: RopeTree<T>, shrunk: RopeChunk<T>, right: RopeTree<T>) -> RopeTree<T>
where
    T: Clone,
{
    if shrunk.len() >= MIN_CHUNK_SIZE {
        return left.append(shrunk).concat(&right);
    }

    if let Some((last_left, left_rest)) = left.try_view_right()
        && last_left.len() + shrunk.len() <= MAX_CHUNK_SIZE
    {
        return left_rest
            .append(RopeChunk::concat(&last_left, &shrunk))
            .concat(&right);
    }

    if let Some((first_right, right_rest)) = right.try_view_left()
        && shrunk.len() + first_right.len() <= MAX_CHUNK_SIZE
    {
        return left
            .append(RopeChunk::concat(&shrunk, &first_right))
            .concat(&right_rest);
    }

    left.append(shrunk).concat(&right)
}

struct MeasuredRopeChunk<T, P>
where
    P: MeasurePolicy<T>,
{
    chunk: RopeChunk<T>,
    measure: P::Measure,
    _policy: PhantomData<P>,
}

impl<T, P> Clone for MeasuredRopeChunk<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            chunk: self.chunk.clone(),
            measure: self.measure.clone(),
            _policy: PhantomData,
        }
    }
}

impl<T, P> MeasuredRopeChunk<T, P>
where
    P: MeasurePolicy<T>,
{
    fn new(items: Vec<T>) -> Self {
        Self::from_chunk(RopeChunk::new(items))
    }

    fn from_chunk(chunk: RopeChunk<T>) -> Self {
        let measure = measure_slice::<T, P>(chunk.as_slice());
        Self {
            chunk,
            measure,
            _policy: PhantomData,
        }
    }

    fn len(&self) -> usize {
        self.chunk.len()
    }

    fn as_slice(&self) -> &[T] {
        self.chunk.as_slice()
    }

    fn get(&self, offset: usize) -> Option<&T> {
        self.chunk.get(offset)
    }

    fn slice(&self, offset: usize, len: usize) -> Self {
        Self::from_chunk(self.chunk.slice(offset, len))
    }

    fn prefix_measure(&self, count: usize) -> P::Measure {
        measure_slice::<T, P>(&self.as_slice()[..count])
    }
}

impl<T, P> MeasuredRopeChunk<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    fn set_at(&self, offset: usize, item: T) -> Self {
        Self::from_chunk(self.chunk.set_at(offset, item))
    }

    fn insert_at(&self, offset: usize, item: T) -> Self {
        Self::from_chunk(self.chunk.insert_at(offset, item))
    }

    fn remove_at(&self, offset: usize) -> Self {
        Self::from_chunk(self.chunk.remove_at(offset))
    }

    fn concat(left: &Self, right: &Self) -> Self {
        Self::from_chunk(RopeChunk::concat(&left.chunk, &right.chunk))
    }
}

struct MeasuredChunkMeasure<T, P>(PhantomData<(T, P)>);

impl<T, P> MeasurePolicy<MeasuredRopeChunk<T, P>> for MeasuredChunkMeasure<T, P>
where
    P: MeasurePolicy<T>,
{
    type Measure = (usize, P::Measure);

    fn empty() -> Self::Measure {
        (0, P::empty())
    }

    fn measure(element: &MeasuredRopeChunk<T, P>) -> Self::Measure {
        (element.len(), element.measure.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        (
            left.0
                .checked_add(right.0)
                .expect("measured rope length overflow"),
            P::combine(&left.1, &right.1),
        )
    }
}

type MeasuredRopeTree<T, P> = FingerTree<MeasuredRopeChunk<T, P>, MeasuredChunkMeasure<T, P>>;

pub struct MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    tree: MeasuredRopeTree<T, P>,
}

/// An immutable positional editing cursor over one [`MeasuredRope`] snapshot.
///
/// The cursor retains the exact measured rope version and a gap in `0..=len`. Operations return
/// new values, so retained cursors branch independently. This Rust port deliberately uses the
/// same root-sharing snapshot-plus-gap representation as [`RopeCursor`]; it does not claim the
/// focused zipper or amortized local-edit bounds of the C# implementation.
pub struct MeasuredRopeCursor<T, P>
where
    P: MeasurePolicy<T>,
{
    rope: MeasuredRope<T, P>,
    position: usize,
}

/// The result of an absolute prefix-measure cursor search.
///
/// A miss carries a usable cursor at the end of the retained snapshot.
pub struct MeasuredRopeCursorSearch<T, P>
where
    P: MeasurePolicy<T>,
{
    pub cursor: MeasuredRopeCursor<T, P>,
    pub found: bool,
}

pub struct MeasuredRopeIter<'a, T, P>
where
    P: MeasurePolicy<T>,
{
    chunks: crate::measured::Iter<'a, MeasuredRopeChunk<T, P>, (usize, P::Measure)>,
    current: Option<std::slice::Iter<'a, T>>,
}

#[derive(Clone)]
pub struct MeasuredRopeSplit<T, P>
where
    P: MeasurePolicy<T>,
{
    pub left: MeasuredRope<T, P>,
    pub right: MeasuredRope<T, P>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MeasuredRopeLocate<T, M> {
    pub index: usize,
    pub measure_before: M,
    pub value: Option<T>,
}

/// Mutable append builder for [`MeasuredRope`].
///
/// The builder retains an immutable prefix and stages at most one chunk of new elements. Freezing
/// the builder publishes that tail as persistent measured-tree storage, so later mutations cannot
/// affect earlier snapshots and successive snapshots share their unchanged prefix.
pub struct MeasuredRopeBuilder<T, P>
where
    P: MeasurePolicy<T>,
{
    prefix: MeasuredRope<T, P>,
    tail: Vec<T>,
    tail_measure: P::Measure,
}

impl<T, P> Clone for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            tree: self.tree.clone(),
        }
    }
}

impl<T, P> Clone for MeasuredRopeCursor<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            rope: self.rope.clone(),
            position: self.position,
        }
    }
}

impl<T, P> Clone for MeasuredRopeCursorSearch<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            cursor: self.cursor.clone(),
            found: self.found,
        }
    }
}

impl<T, P> MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_tree(MeasuredRopeTree::new())
    }

    /// Creates an empty append builder.
    #[must_use]
    pub fn builder() -> MeasuredRopeBuilder<T, P> {
        MeasuredRopeBuilder::new()
    }

    /// Creates an append builder whose immutable prefix is this rope. This is O(1).
    #[must_use]
    pub fn to_builder(&self) -> MeasuredRopeBuilder<T, P> {
        MeasuredRopeBuilder::from_rope(self)
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_tree(measured_tree_from_items::<T, P>(items))
    }

    fn from_tree(tree: MeasuredRopeTree<T, P>) -> Self {
        Self { tree }
    }

    /// Creates a cursor at the gap before the first element. O(1).
    #[must_use]
    pub fn cursor(&self) -> MeasuredRopeCursor<T, P> {
        MeasuredRopeCursor {
            rope: self.clone(),
            position: 0,
        }
    }

    /// Creates a cursor at `position`, returning `None` outside `0..=len`. O(1).
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<MeasuredRopeCursor<T, P>> {
        (position <= self.len()).then(|| MeasuredRopeCursor {
            rope: self.clone(),
            position,
        })
    }

    /// Searches absolute prefixes and returns a cursor at the first matching element.
    ///
    /// The predicate must be monotone over successive prefixes. A miss returns `found == false`
    /// and an end cursor. Predicate failure cannot mutate the source rope.
    #[must_use]
    pub fn cursor_by_measure<F>(&self, predicate: F) -> MeasuredRopeCursorSearch<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        self.cursor().seek_by_measure(predicate)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.tree.measure().0
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.tree.is_empty()
    }

    #[must_use]
    pub fn measure(&self) -> &P::Measure {
        &self.tree.measure().1
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len() {
            return None;
        }

        let located = self.tree.try_locate(|measure| measure.0 > index);
        let offset = index - located.measure_before.0;
        self.tree.get(located.index)?.get(offset)
    }

    pub fn iter(&self) -> MeasuredRopeIter<'_, T, P> {
        MeasuredRopeIter {
            chunks: self.tree.iter(),
            current: None,
        }
    }

    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.tree.shares_storage_with(&other.tree)
    }

    #[must_use]
    pub fn prefix_measure(&self, count: usize) -> Option<P::Measure> {
        if count > self.len() {
            return None;
        }

        if count == 0 {
            return Some(P::empty());
        }

        if count == self.len() {
            return Some(self.measure().clone());
        }

        let located = self.tree.try_locate(|measure| measure.0 >= count);
        let chunk = self.tree.get(located.index)?;
        let count_in_chunk = count - located.measure_before.0;
        Some(P::combine(
            &located.measure_before.1,
            &chunk.prefix_measure(count_in_chunk),
        ))
    }

    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<MeasuredRopeSplit<T, P>> {
        self.split_at_count(index)
    }

    #[must_use]
    pub fn split_by_measure<F>(&self, mut predicate: F) -> MeasuredRopeSplit<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let Some((left, chunk, right)) = self.tree.try_split_find(|measure| predicate(&measure.1))
        else {
            return MeasuredRopeSplit {
                left: self.clone(),
                right: Self::new(),
            };
        };

        let mut prefix = left.measure().1.clone();
        let mut offset = 0;
        for item in chunk.as_slice() {
            let next = P::combine(&prefix, &P::measure(item));
            if predicate(&next) {
                break;
            }

            prefix = next;
            offset += 1;
        }

        MeasuredRopeSplit {
            left: Self::from_tree(append_measured_prefix(left, &chunk, offset)),
            right: Self::from_tree(prepend_measured_suffix(right, &chunk, offset)),
        }
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        self.len()
            .checked_add(other.len())
            .expect("measured rope length overflow");
        Self::from_tree(self.tree.concat(&other.tree))
    }

    fn locate_cursor_by_measure<F>(&self, mut predicate: F) -> (usize, bool)
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let located = self.tree.try_locate(|measure| predicate(&measure.1));
        let Some(chunk) = self.tree.get(located.index) else {
            return (self.len(), false);
        };

        let mut measure_before = located.measure_before.1;
        for (index, item) in (located.measure_before.0..).zip(chunk.as_slice()) {
            let next = P::combine(&measure_before, &P::measure(item));
            if predicate(&next) {
                return (index, true);
            }

            measure_before = next;
        }

        (self.len(), false)
    }

    fn split_at_count(&self, index: usize) -> Option<MeasuredRopeSplit<T, P>> {
        if index > self.len() {
            return None;
        }

        if index == 0 {
            return Some(MeasuredRopeSplit {
                left: Self::new(),
                right: self.clone(),
            });
        }

        if index == self.len() {
            return Some(MeasuredRopeSplit {
                left: self.clone(),
                right: Self::new(),
            });
        }

        let (left, chunk, right) = self
            .tree
            .try_split_find(|measure| measure.0 > index)
            .expect("validated index locates a measured rope chunk");
        let offset = index - left.measure().0;
        Some(MeasuredRopeSplit {
            left: Self::from_tree(append_measured_prefix(left, &chunk, offset)),
            right: Self::from_tree(prepend_measured_suffix(right, &chunk, offset)),
        })
    }

    #[cfg(test)]
    fn chunk_count(&self) -> usize {
        self.tree.iter().count()
    }

    #[cfg(test)]
    fn validate_chunk_invariants(&self)
    where
        P::Measure: PartialEq + std::fmt::Debug,
    {
        let mut total_len = 0;
        let mut total_measure = P::empty();
        for chunk in self.tree.iter() {
            assert!(chunk.len() > 0);
            assert!(chunk.len() <= MAX_CHUNK_SIZE);
            total_len += chunk.len();
            total_measure = P::combine(&total_measure, &chunk.measure);
        }

        assert_eq!(total_len, self.len());
        assert_eq!(&total_measure, self.measure());
        self.tree.validate_invariants();
    }
}

impl<T, P> MeasuredRopeCursor<T, P>
where
    P: MeasurePolicy<T>,
{
    /// Returns the number of elements in the retained snapshot. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.rope.len()
    }

    /// Returns whether the retained snapshot is empty. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.rope.is_empty()
    }

    /// Returns the gap position in `0..=len`. O(1).
    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    /// Returns the ordered measure of the prefix before the gap. O(log n).
    #[must_use]
    pub fn measure_before(&self) -> P::Measure {
        self.rope
            .prefix_measure(self.position)
            .expect("a cursor gap is always a valid measured-rope position")
    }

    /// Returns the ordered measure of the suffix after the gap. O(log n).
    #[must_use]
    pub fn measure_after(&self) -> P::Measure {
        self.rope
            .split_at_count(self.position)
            .expect("a cursor gap is always a valid measured-rope position")
            .right
            .measure()
            .clone()
    }

    #[must_use]
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.rope.get(index))
    }

    #[must_use]
    pub fn peek_next(&self) -> Option<&T> {
        self.rope.get(self.position)
    }

    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            rope: self.rope.clone(),
            position: self.position.checked_sub(1)?,
        })
    }

    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }

        Some(Self {
            rope: self.rope.clone(),
            position: self.position + 1,
        })
    }

    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        if position > self.len() {
            return None;
        }

        Some(Self {
            rope: self.rope.clone(),
            position,
        })
    }

    /// Searches absolute prefixes of the retained snapshot, independent of the current gap.
    ///
    /// The predicate must be monotone over successive prefixes. A miss returns a usable end
    /// cursor. The receiver remains valid if the predicate panics.
    #[must_use]
    pub fn seek_by_measure<F>(&self, predicate: F) -> MeasuredRopeCursorSearch<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let (position, found) = self.rope.locate_cursor_by_measure(predicate);
        MeasuredRopeCursorSearch {
            cursor: Self {
                rope: self.rope.clone(),
                position,
            },
            found,
        }
    }

    /// Returns the exact retained measured-rope snapshot by root sharing. O(1).
    #[must_use]
    pub fn snapshot(&self) -> MeasuredRope<T, P> {
        self.rope.clone()
    }
}

impl<T, P> MeasuredRopeCursor<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn insert(&self, item: T) -> Self {
        let position = self
            .position
            .checked_add(1)
            .expect("measured rope cursor position overflow");
        let rope = self
            .rope
            .insert_at(self.position, item)
            .expect("a cursor gap is always a valid insertion position");
        Self { rope, position }
    }

    #[must_use]
    pub fn insert_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let items: Vec<T> = items.into_iter().collect();
        if items.is_empty() {
            return self.clone();
        }

        let position = self
            .position
            .checked_add(items.len())
            .expect("measured rope cursor position overflow");
        let rope = self
            .rope
            .insert_range(self.position, items)
            .expect("a cursor gap is always a valid range insertion position");
        Self { rope, position }
    }

    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let rope = self
            .rope
            .remove_at(position)
            .expect("a non-start cursor always has a previous element");
        Some(Self { rope, position })
    }

    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }

        let rope = self
            .rope
            .remove_at(self.position)
            .expect("a non-end cursor always has a next element");
        Some(Self {
            rope,
            position: self.position,
        })
    }

    /// Unconditionally replaces the element after the gap without invoking equality.
    #[must_use]
    pub fn replace_next(&self, item: T) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }

        let rope = self
            .rope
            .set_item(self.position, item)
            .expect("a non-end cursor always has a next element");
        Some(Self {
            rope,
            position: self.position,
        })
    }
}

impl<'a, T, P> Iterator for MeasuredRopeIter<'a, T, P>
where
    P: MeasurePolicy<T>,
{
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Some(current) = &mut self.current
                && let Some(item) = current.next()
            {
                return Some(item);
            }

            let chunk = self.chunks.next()?;
            self.current = Some(chunk.as_slice().iter());
        }
    }
}

impl<T, P> Default for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> MeasuredRopeBuilder<T, P>
where
    P: MeasurePolicy<T>,
{
    /// Creates an empty builder.
    #[must_use]
    pub fn new() -> Self {
        Self {
            prefix: MeasuredRope::new(),
            tail: Vec::with_capacity(MAX_CHUNK_SIZE),
            tail_measure: P::empty(),
        }
    }

    /// Creates a builder that appends to `rope` without copying its existing elements.
    #[must_use]
    pub fn from_rope(rope: &MeasuredRope<T, P>) -> Self {
        Self {
            prefix: rope.clone(),
            tail: Vec::with_capacity(MAX_CHUNK_SIZE),
            tail_measure: P::empty(),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.prefix
            .len()
            .checked_add(self.tail.len())
            .expect("measured rope length overflow")
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.prefix.is_empty() && self.tail.is_empty()
    }

    /// Returns the combined measure of the frozen prefix and staged tail.
    #[must_use]
    pub fn measure(&self) -> P::Measure {
        P::combine(self.prefix.measure(), &self.tail_measure)
    }

    /// Appends one element, amortized O(1) between persistent-tree publications.
    pub fn push(&mut self, item: T) -> &mut Self {
        self.len()
            .checked_add(1)
            .expect("measured rope length overflow");
        let item_measure = P::measure(&item);
        let next_measure = P::combine(&self.tail_measure, &item_measure);
        self.tail.push(item);
        self.tail_measure = next_measure;
        if self.tail.len() == MAX_CHUNK_SIZE {
            self.flush_tail();
        }
        self
    }

    /// Appends every element from `items`, consuming the iterator once.
    pub fn extend<I>(&mut self, items: I) -> &mut Self
    where
        I: IntoIterator<Item = T>,
    {
        for item in items {
            self.push(item);
        }
        self
    }

    /// Removes every staged element and resets the builder to the empty rope.
    pub fn clear(&mut self) -> &mut Self {
        self.prefix = MeasuredRope::new();
        self.tail.clear();
        self.tail_measure = P::empty();
        self
    }

    /// Publishes the staged tail and returns an immutable snapshot.
    ///
    /// Repeated calls without intervening mutation return values backed by the same persistent
    /// tree. Appending after this call preserves the returned snapshot.
    #[must_use]
    pub fn to_immutable(&mut self) -> MeasuredRope<T, P> {
        self.flush_tail();
        self.prefix.clone()
    }

    fn flush_tail(&mut self) {
        if self.tail.is_empty() {
            return;
        }

        let tail = std::mem::replace(&mut self.tail, Vec::with_capacity(MAX_CHUNK_SIZE));
        let published = MeasuredRope::from_tree(measured_tree_from_items::<T, P>(tail));
        self.prefix = self.prefix.concat(&published);
        self.tail_measure = P::empty();
    }
}

impl<T, P> Default for MeasuredRopeBuilder<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> FromIterator<T> for MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T, P> MeasuredRope<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn from_chunks<I, C>(chunks: I) -> Self
    where
        I: IntoIterator<Item = C>,
        C: AsRef<[T]>,
    {
        Self::from_tree(measured_tree_from_chunks::<T, P, I, C>(chunks))
    }

    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut items = Vec::with_capacity(self.len());
        for chunk in self.tree.iter() {
            items.extend_from_slice(chunk.as_slice());
        }
        items
    }

    pub fn copy_to(&self, index: usize, destination: &mut [T]) -> Option<()> {
        if index > self.len() || destination.len() > self.len() - index {
            return None;
        }

        let mut chunk_start = 0;
        let mut written = 0;
        for chunk in self.tree.iter() {
            let chunk_end = chunk_start + chunk.len();
            if chunk_end <= index {
                chunk_start = chunk_end;
                continue;
            }

            let start_in_chunk = index.saturating_sub(chunk_start);
            let available = chunk.len() - start_in_chunk;
            let take = available.min(destination.len() - written);
            destination[written..written + take]
                .clone_from_slice(&chunk.as_slice()[start_in_chunk..start_in_chunk + take]);
            written += take;
            if written == destination.len() {
                return Some(());
            }

            chunk_start = chunk_end;
        }

        Some(())
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        self.len()
            .checked_add(1)
            .expect("measured rope length overflow");
        if self.is_empty() {
            return Self::from_tree(
                MeasuredRopeTree::new().append(MeasuredRopeChunk::new(vec![item])),
            );
        }

        let (last, rest) = self
            .tree
            .try_view_right()
            .expect("non-empty measured rope has a last chunk");
        if last.len() < MAX_CHUNK_SIZE {
            return Self::from_tree(rest.append(last.insert_at(last.len(), item)));
        }

        Self::from_tree(
            self.tree
                .concat(&MeasuredRopeTree::new().append(MeasuredRopeChunk::new(vec![item]))),
        )
    }

    /// Returns a rope with `item` inserted at `index`, or `None` when `index > len()`.
    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        self.len()
            .checked_add(1)
            .expect("measured rope length overflow");

        if self.is_empty() {
            return Some(Self::from_tree(
                MeasuredRopeTree::new().append(MeasuredRopeChunk::new(vec![item])),
            ));
        }

        if index == self.len() {
            return Some(self.push_back(item));
        }

        let (left, chunk, right) = self
            .tree
            .try_split_find(|measure| measure.0 > index)
            .expect("validated index locates a measured rope chunk");
        let offset = index - left.measure().0;
        Some(Self::from_tree(join_measured_grown(
            left,
            chunk.insert_at(offset, item),
            right,
        )))
    }

    /// Returns a rope with `items` spliced at `index`.
    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        if index > self.len() {
            return None;
        }

        let items: Vec<T> = items.into_iter().collect();
        self.len()
            .checked_add(items.len())
            .expect("measured rope length overflow");
        let middle = Self::from_vec(items);
        if middle.is_empty() {
            return Some(self.clone());
        }

        let split = self.split_at_count(index)?;
        Some(split.left.concat(&middle).concat(&split.right))
    }

    /// Returns a rope without the element at `index`.
    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let (left, chunk, right) = self
            .tree
            .try_split_find(|measure| measure.0 > index)
            .expect("validated index locates a measured rope chunk");
        if chunk.len() == 1 {
            return Some(Self::from_tree(left.concat(&right)));
        }

        let offset = index - left.measure().0;
        Some(Self::from_tree(join_measured_shrunk(
            left,
            chunk.remove_at(offset),
            right,
        )))
    }

    /// Returns a rope without the half-open range `[index, index + count)`.
    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        if index > self.len() || count > self.len() - index {
            return None;
        }

        if count == 0 {
            return Some(self.clone());
        }

        let left_and_rest = self.split_at_count(index)?;
        let removed_and_right = left_and_rest.right.split_at_count(count)?;
        Some(left_and_rest.left.concat(&removed_and_right.right))
    }

    /// Returns the half-open range `[index, index + count)`, sharing unchanged chunks and
    /// measured subtrees with this rope.
    #[must_use]
    pub fn slice(&self, index: usize, count: usize) -> Option<Self> {
        if index > self.len() || count > self.len() - index {
            return None;
        }

        if count == 0 {
            return Some(Self::new());
        }

        if index == 0 && count == self.len() {
            return Some(self.clone());
        }

        let left_and_rest = self.split_at_count(index)?;
        let range_and_right = left_and_rest.right.split_at_count(count)?;
        Some(range_and_right.left)
    }

    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let (left, chunk, right) = self
            .tree
            .try_split_find(|measure| measure.0 > index)
            .expect("validated index locates a measured rope chunk");
        let offset = index - left.measure().0;
        Some(Self::from_tree(
            left.append(chunk.set_at(offset, item)).concat(&right),
        ))
    }

    #[must_use]
    pub fn locate_by_measure<F>(&self, mut predicate: F) -> MeasuredRopeLocate<T, P::Measure>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let located = self.tree.try_locate(|measure| predicate(&measure.1));
        let Some(chunk) = self.tree.get(located.index) else {
            return MeasuredRopeLocate {
                index: self.len(),
                measure_before: located.measure_before.1,
                value: None,
            };
        };

        let mut measure_before = located.measure_before.1;
        for (index, item) in (located.measure_before.0..).zip(chunk.as_slice()) {
            let next = P::combine(&measure_before, &P::measure(item));
            if predicate(&next) {
                return MeasuredRopeLocate {
                    index,
                    measure_before,
                    value: Some(item.clone()),
                };
            }

            measure_before = next;
        }

        MeasuredRopeLocate {
            index: self.len(),
            measure_before,
            value: None,
        }
    }
}

fn join_measured_grown<T, P>(
    left: MeasuredRopeTree<T, P>,
    grown: MeasuredRopeChunk<T, P>,
    right: MeasuredRopeTree<T, P>,
) -> MeasuredRopeTree<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    if grown.len() <= MAX_CHUNK_SIZE {
        return left.append(grown).concat(&right);
    }

    let half = grown.len() / 2;
    left.append(grown.slice(0, half))
        .append(grown.slice(half, grown.len() - half))
        .concat(&right)
}

fn join_measured_shrunk<T, P>(
    left: MeasuredRopeTree<T, P>,
    shrunk: MeasuredRopeChunk<T, P>,
    right: MeasuredRopeTree<T, P>,
) -> MeasuredRopeTree<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    if shrunk.len() >= MIN_CHUNK_SIZE {
        return left.append(shrunk).concat(&right);
    }

    if let Some((last_left, left_rest)) = left.try_view_right()
        && last_left.len() + shrunk.len() <= MAX_CHUNK_SIZE
    {
        return left_rest
            .append(MeasuredRopeChunk::concat(&last_left, &shrunk))
            .concat(&right);
    }

    if let Some((first_right, right_rest)) = right.try_view_left()
        && shrunk.len() + first_right.len() <= MAX_CHUNK_SIZE
    {
        return left
            .append(MeasuredRopeChunk::concat(&shrunk, &first_right))
            .concat(&right_rest);
    }

    left.append(shrunk).concat(&right)
}

fn measure_slice<T, P>(items: &[T]) -> P::Measure
where
    P: MeasurePolicy<T>,
{
    let mut measure = P::empty();
    for item in items {
        measure = P::combine(&measure, &P::measure(item));
    }

    measure
}

fn measured_tree_from_items<T, P>(items: Vec<T>) -> MeasuredRopeTree<T, P>
where
    P: MeasurePolicy<T>,
{
    if items.is_empty() {
        return MeasuredRopeTree::new();
    }

    let mut chunks = Vec::new();
    let mut current = Vec::with_capacity(MAX_CHUNK_SIZE);
    for item in items {
        current.push(item);
        if current.len() == MAX_CHUNK_SIZE {
            chunks.push(MeasuredRopeChunk::new(current));
            current = Vec::with_capacity(MAX_CHUNK_SIZE);
        }
    }

    if !current.is_empty() {
        chunks.push(MeasuredRopeChunk::new(current));
    }

    chunks.into_iter().collect()
}

fn measured_tree_from_chunks<T, P, I, C>(chunks: I) -> MeasuredRopeTree<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
    I: IntoIterator<Item = C>,
    C: AsRef<[T]>,
{
    let mut tree = MeasuredRopeTree::new();
    for chunk in chunks {
        for block in chunk.as_ref().chunks(MAX_CHUNK_SIZE) {
            if !block.is_empty() {
                tree = tree.append(MeasuredRopeChunk::new(block.to_vec()));
            }
        }
    }

    tree
}

fn append_measured_prefix<T, P>(
    tree: MeasuredRopeTree<T, P>,
    chunk: &MeasuredRopeChunk<T, P>,
    count: usize,
) -> MeasuredRopeTree<T, P>
where
    P: MeasurePolicy<T>,
{
    if count == 0 {
        tree
    } else {
        tree.append(chunk.slice(0, count))
    }
}

fn prepend_measured_suffix<T, P>(
    tree: MeasuredRopeTree<T, P>,
    chunk: &MeasuredRopeChunk<T, P>,
    start: usize,
) -> MeasuredRopeTree<T, P>
where
    P: MeasurePolicy<T>,
{
    if start == chunk.len() {
        tree
    } else {
        tree.prepend(chunk.slice(start, chunk.len() - start))
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NewlineMeasure;

impl MeasurePolicy<char> for NewlineMeasure {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(element: &char) -> Self::Measure {
        usize::from(*element == '\n')
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left.checked_add(*right).expect("newline measure overflow")
    }
}

pub struct TextRope {
    chars: MeasuredRope<char, NewlineMeasure>,
}

/// An immutable character-gap cursor retaining one exact [`TextRope`] snapshot.
///
/// Offsets and columns count Unicode scalar values, matching [`TextRope`]. Newline measures count
/// `\n`; CRLF handling remains an opt-in text-extras concern. This is a thin nominal facade over
/// [`MeasuredRopeCursor`] and inherits its snapshot-plus-gap complexity contracts.
pub struct TextRopeCursor {
    measured: MeasuredRopeCursor<char, NewlineMeasure>,
}

/// The result of an absolute newline-prefix cursor search.
///
/// A miss carries a usable cursor at the end of the retained text snapshot.
pub struct TextRopeCursorSearch {
    pub cursor: TextRopeCursor,
    pub found: bool,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct LineColumn {
    pub line: usize,
    pub column: usize,
}

impl TextRope {
    #[must_use]
    pub fn new() -> Self {
        Self {
            chars: MeasuredRope::new(),
        }
    }

    #[must_use]
    pub fn from_text(text: &str) -> Self {
        Self {
            chars: text.chars().collect(),
        }
    }

    fn from_measured_rope(chars: MeasuredRope<char, NewlineMeasure>) -> Self {
        Self { chars }
    }

    /// Creates a cursor before the first character. O(1).
    #[must_use]
    pub fn cursor(&self) -> TextRopeCursor {
        TextRopeCursor {
            measured: self.chars.cursor(),
        }
    }

    /// Creates a cursor at a character offset, returning `None` outside `0..=len`. O(1).
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<TextRopeCursor> {
        Some(TextRopeCursor {
            measured: self.chars.cursor_at(position)?,
        })
    }

    /// Searches absolute newline-count prefixes and returns the first matching character gap.
    #[must_use]
    pub fn cursor_by_measure<F>(&self, predicate: F) -> TextRopeCursorSearch
    where
        F: FnMut(&usize) -> bool,
    {
        let result = self.chars.cursor_by_measure(predicate);
        TextRopeCursorSearch {
            cursor: TextRopeCursor {
                measured: result.cursor,
            },
            found: result.found,
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.chars.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.chars.is_empty()
    }

    #[must_use]
    pub fn as_string(&self) -> String {
        self.chars.iter().copied().collect()
    }

    /// Iterates over Unicode scalar values in character-offset order.
    pub fn iter(&self) -> MeasuredRopeIter<'_, char, NewlineMeasure> {
        self.chars.iter()
    }

    #[must_use]
    pub fn line_count(&self) -> usize {
        self.chars
            .measure()
            .checked_add(1)
            .expect("text rope line count overflow")
    }

    #[must_use]
    pub fn line_of_offset(&self, offset: usize) -> Option<usize> {
        self.chars.prefix_measure(offset)
    }

    #[must_use]
    pub fn line_start_offset(&self, line: usize) -> Option<usize> {
        if line > *self.chars.measure() {
            return None;
        }

        if line == 0 {
            return Some(0);
        }

        let located = self.chars.locate_by_measure(|newlines| *newlines >= line);
        located.value.map(|_| located.index + 1)
    }

    #[must_use]
    pub fn line_column_of(&self, offset: usize) -> Option<LineColumn> {
        let line = self.line_of_offset(offset)?;
        let start = self.line_start_offset(line)?;
        Some(LineColumn {
            line,
            column: offset - start,
        })
    }

    #[must_use]
    pub fn offset_of(&self, line: usize, column: usize) -> Option<usize> {
        let start = self.line_start_offset(line)?;
        let end = self.line_end_offset(line)?;
        let offset = start.checked_add(column)?;
        (offset <= end).then_some(offset)
    }

    #[must_use]
    pub fn get_line(&self, line: usize) -> Option<String> {
        let start = self.line_start_offset(line)?;
        let end = self.line_end_offset(line)?;
        // Slice shares tree structure, so extracting a line costs O(log n + length)
        // rather than the O(start) an iterator skip would pay.
        let slice = self.chars.slice(start, end - start)?;
        Some(slice.iter().copied().collect())
    }

    #[must_use]
    pub fn lines(&self) -> Vec<String> {
        // A single pass over the rope, matching the C# reference's Lines: the final
        // line (after the last newline) is always present, possibly empty.
        let mut lines = Vec::with_capacity(self.line_count());
        let mut current = String::new();
        for c in self.chars.iter() {
            if *c == '\n' {
                lines.push(core::mem::take(&mut current));
            } else {
                current.push(*c);
            }
        }

        lines.push(current);
        lines
    }

    #[must_use]
    pub fn to_char_rope(&self) -> Rope<char> {
        self.chars.iter().copied().collect()
    }

    #[must_use]
    pub fn to_measured_rope(&self) -> MeasuredRope<char, NewlineMeasure> {
        self.chars.clone()
    }

    fn line_end_offset(&self, line: usize) -> Option<usize> {
        if line > *self.chars.measure() {
            return None;
        }

        if line < *self.chars.measure() {
            return self
                .line_start_offset(line + 1)
                .and_then(|offset| offset.checked_sub(1));
        }

        Some(self.len())
    }
}

impl Clone for TextRopeCursor {
    fn clone(&self) -> Self {
        Self {
            measured: self.measured.clone(),
        }
    }
}

impl Clone for TextRopeCursorSearch {
    fn clone(&self) -> Self {
        Self {
            cursor: self.cursor.clone(),
            found: self.found,
        }
    }
}

impl TextRopeCursor {
    #[must_use]
    pub fn len(&self) -> usize {
        self.measured.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.measured.is_empty()
    }

    #[must_use]
    pub fn position(&self) -> usize {
        self.measured.position()
    }

    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.measured.is_at_start()
    }

    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.measured.is_at_end()
    }

    #[must_use]
    pub fn measure_before(&self) -> usize {
        self.measured.measure_before()
    }

    #[must_use]
    pub fn measure_after(&self) -> usize {
        self.measured.measure_after()
    }

    #[must_use]
    pub fn peek_previous(&self) -> Option<&char> {
        self.measured.peek_previous()
    }

    #[must_use]
    pub fn peek_next(&self) -> Option<&char> {
        self.measured.peek_next()
    }

    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            measured: self.measured.move_previous()?,
        })
    }

    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        Some(Self {
            measured: self.measured.move_next()?,
        })
    }

    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        Some(Self {
            measured: self.measured.seek(position)?,
        })
    }

    /// Searches absolute newline-count prefixes of this cursor's retained version.
    #[must_use]
    pub fn seek_by_measure<F>(&self, predicate: F) -> TextRopeCursorSearch
    where
        F: FnMut(&usize) -> bool,
    {
        let result = self.measured.seek_by_measure(predicate);
        TextRopeCursorSearch {
            cursor: Self {
                measured: result.cursor,
            },
            found: result.found,
        }
    }

    /// Returns the zero-based line and Unicode-scalar column at the gap.
    #[must_use]
    pub fn line_column(&self) -> LineColumn {
        let line = self.measure_before();
        let start = TextRope::from_measured_rope(self.measured.rope.clone())
            .line_start_offset(line)
            .expect("a cursor gap always has a valid line start");
        LineColumn {
            line,
            column: self.position() - start,
        }
    }

    #[must_use]
    pub fn insert(&self, value: char) -> Self {
        Self {
            measured: self.measured.insert(value),
        }
    }

    #[must_use]
    pub fn insert_range<I>(&self, values: I) -> Self
    where
        I: IntoIterator<Item = char>,
    {
        Self {
            measured: self.measured.insert_range(values),
        }
    }

    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        Some(Self {
            measured: self.measured.delete_previous()?,
        })
    }

    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        Some(Self {
            measured: self.measured.delete_next()?,
        })
    }

    #[must_use]
    pub fn replace_next(&self, value: char) -> Option<Self> {
        Some(Self {
            measured: self.measured.replace_next(value)?,
        })
    }

    /// Returns the exact retained text facade by root sharing. O(1).
    #[must_use]
    pub fn snapshot(&self) -> TextRope {
        TextRope::from_measured_rope(self.measured.snapshot())
    }
}

impl Clone for TextRope {
    fn clone(&self) -> Self {
        Self {
            chars: self.chars.clone(),
        }
    }
}

impl fmt::Debug for TextRope {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_tuple("TextRope")
            .field(&self.as_string())
            .finish()
    }
}

impl PartialEq for TextRope {
    fn eq(&self, other: &Self) -> bool {
        self.chars.iter().eq(other.chars.iter())
    }
}

impl Eq for TextRope {}

impl Default for TextRope {
    fn default() -> Self {
        Self::new()
    }
}

impl From<&str> for TextRope {
    fn from(value: &str) -> Self {
        Self::from_text(value)
    }
}

impl From<String> for TextRope {
    fn from(value: String) -> Self {
        Self::from_text(&value)
    }
}

impl From<&str> for Rope<char> {
    fn from(value: &str) -> Self {
        value.chars().collect()
    }
}

impl From<String> for Rope<char> {
    fn from(value: String) -> Self {
        value.chars().collect()
    }
}

impl fmt::Display for TextRope {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.as_string())
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RopeBuilder {
    text: String,
    // Running char count so len() is O(1); text.chars().count() would be O(n) per call.
    length: usize,
}

impl RopeBuilder {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.length
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.text.is_empty()
    }

    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.text
    }

    pub fn append(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self.length += text.chars().count();
        self
    }

    pub fn append_char(&mut self, value: char) -> &mut Self {
        self.text.push(value);
        self.length += 1;
        self
    }

    pub fn append_line(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self.text.push('\n');
        self.length += text.chars().count() + 1;
        self
    }

    pub fn clear(&mut self) -> &mut Self {
        self.text.clear();
        self.length = 0;
        self
    }

    #[must_use]
    pub fn to_rope(&self) -> Rope<char> {
        self.text.chars().collect()
    }

    #[must_use]
    pub fn to_text_rope(&self) -> TextRope {
        TextRope::from_text(&self.text)
    }
}

impl fmt::Display for RopeBuilder {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::measured::{SizeMeasure, SumMeasure};
    use std::sync::atomic::{AtomicUsize, Ordering};

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct TraceMeasure;

    impl MeasurePolicy<i32> for TraceMeasure {
        type Measure = Vec<i32>;

        fn empty() -> Self::Measure {
            Vec::new()
        }

        fn measure(element: &i32) -> Self::Measure {
            vec![*element]
        }

        fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
            let mut combined = Vec::with_capacity(left.len() + right.len());
            combined.extend_from_slice(left);
            combined.extend_from_slice(right);
            combined
        }
    }

    static COUNTING_MEASURE_CALLS: AtomicUsize = AtomicUsize::new(0);

    #[derive(Clone, Debug, PartialEq, Eq)]
    struct CountingSizeMeasure;

    impl MeasurePolicy<i32> for CountingSizeMeasure {
        type Measure = usize;

        fn empty() -> Self::Measure {
            0
        }

        fn measure(_element: &i32) -> Self::Measure {
            COUNTING_MEASURE_CALLS.fetch_add(1, Ordering::Relaxed);
            1
        }

        fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
            left.checked_add(*right).expect("counting measure overflow")
        }
    }

    #[test]
    fn rope_edits_preserve_snapshots() {
        let rope: Rope<_> = [1, 2, 3].into_iter().collect();
        let edited = rope.insert_at(1, 9).unwrap().remove_at(3).unwrap();

        assert_eq!(rope.to_vec(), vec![1, 2, 3]);
        assert_eq!(edited.to_vec(), vec![1, 9, 2]);
        assert_eq!(rope.slice(1, 2).unwrap().to_vec(), vec![2, 3]);
        assert!(rope.slice(1, usize::MAX).is_none());
        assert!(rope.remove_range(1, usize::MAX).is_none());
    }

    #[test]
    fn rope_cursor_navigation_and_snapshot_need_no_clone_bound() {
        #[derive(Debug, PartialEq, Eq)]
        struct NonClone(i32);

        let empty = Rope::<NonClone>::new().cursor();
        assert!(empty.is_empty());
        assert!(empty.is_at_start());
        assert!(empty.is_at_end());
        assert_eq!(empty.peek_previous(), None);
        assert_eq!(empty.peek_next(), None);
        assert!(empty.move_previous().is_none());
        assert!(empty.move_next().is_none());
        assert!(
            empty
                .seek(0)
                .unwrap()
                .snapshot()
                .shares_storage_with(&empty.snapshot())
        );

        let rope: Rope<_> = [NonClone(10), NonClone(20), NonClone(30)]
            .into_iter()
            .collect();
        let start = rope.cursor();

        assert_eq!(start.len(), 3);
        assert!(!start.is_empty());
        assert_eq!(start.position(), 0);
        assert!(start.is_at_start());
        assert!(!start.is_at_end());
        assert_eq!(start.peek_previous(), None);
        assert_eq!(start.peek_next().map(|value| value.0), Some(10));
        assert!(start.move_previous().is_none());
        assert!(rope.cursor_at(4).is_none());

        let middle = start.move_next().unwrap().move_next().unwrap();
        assert_eq!(middle.position(), 2);
        assert_eq!(middle.peek_previous().map(|value| value.0), Some(20));
        assert_eq!(middle.peek_next().map(|value| value.0), Some(30));

        let same = middle.seek(2).unwrap();
        assert_eq!(same.position(), middle.position());
        assert!(same.snapshot().shares_storage_with(&middle.snapshot()));
        assert!(middle.seek(4).is_none());

        let end = rope.cursor_at(rope.len()).unwrap();
        assert!(end.is_at_end());
        assert!(!end.is_at_start());
        assert_eq!(end.peek_previous().map(|value| value.0), Some(30));
        assert_eq!(end.peek_next(), None);
        assert!(end.move_next().is_none());
        assert!(end.snapshot().shares_storage_with(&rope));
    }

    #[test]
    fn rope_cursor_edits_cross_chunk_boundaries_and_preserve_no_ops() {
        let rope: Rope<_> = (0..(MAX_CHUNK_SIZE as i32 * 2 + 3)).collect();
        let edge = rope.cursor_at(MAX_CHUNK_SIZE).unwrap();

        assert_eq!(edge.peek_previous(), Some(&(MAX_CHUNK_SIZE as i32 - 1)));
        assert_eq!(edge.peek_next(), Some(&(MAX_CHUNK_SIZE as i32)));
        assert!(rope.cursor().delete_previous().is_none());
        assert!(rope.cursor_at(rope.len()).unwrap().delete_next().is_none());
        assert!(
            rope.cursor_at(rope.len())
                .unwrap()
                .replace_next(-1)
                .is_none()
        );

        let empty = edge.insert_range(std::iter::empty::<i32>());
        assert_eq!(empty.position(), edge.position());
        assert!(empty.snapshot().shares_storage_with(&rope));

        let equal_replacement = edge.replace_next(MAX_CHUNK_SIZE as i32).unwrap();
        assert_eq!(equal_replacement.position(), edge.position());
        assert_eq!(equal_replacement.peek_next(), edge.peek_next());
        assert!(!equal_replacement.snapshot().shares_storage_with(&rope));

        let ranged = edge.insert_range([-2, -3]);
        assert_eq!(ranged.position(), MAX_CHUNK_SIZE + 2);
        assert_eq!(ranged.peek_previous(), Some(&-3));
        assert_eq!(ranged.peek_next(), Some(&(MAX_CHUNK_SIZE as i32)));

        let inserted = ranged.insert(-1);
        assert_eq!(inserted.position(), MAX_CHUNK_SIZE + 3);
        assert_eq!(inserted.peek_previous(), Some(&-1));
        let restored = inserted.delete_previous().unwrap();
        assert_eq!(restored.snapshot().to_vec(), ranged.snapshot().to_vec());

        let replaced = restored.replace_next(99_999).unwrap();
        assert_eq!(replaced.position(), restored.position());
        assert_eq!(replaced.peek_next(), Some(&99_999));
        let deleted = replaced.delete_next().unwrap();
        assert_eq!(deleted.position(), replaced.position());
        assert_eq!(deleted.peek_previous(), Some(&-3));
        assert_eq!(deleted.peek_next(), Some(&(MAX_CHUNK_SIZE as i32 + 1)));

        ranged.snapshot().validate_chunk_invariants();
        inserted.snapshot().validate_chunk_invariants();
        replaced.snapshot().validate_chunk_invariants();
        deleted.snapshot().validate_chunk_invariants();
    }

    #[test]
    fn rope_cursor_retained_versions_branch_and_snapshot_by_root_sharing() {
        let rope: Rope<_> = (0..(MAX_CHUNK_SIZE as i32 * 4)).collect();
        let retained = rope.cursor_at(MAX_CHUNK_SIZE * 2).unwrap();
        let left = retained.insert(-10);
        let right = retained.insert(-20);

        let source_snapshot = retained.snapshot();
        let source_again = retained.snapshot();
        let left_snapshot = left.snapshot();
        let left_again = left.snapshot();
        let right_snapshot = right.snapshot();

        assert!(source_snapshot.shares_storage_with(&source_again));
        assert!(left_snapshot.shares_storage_with(&left_again));
        assert_eq!(
            source_snapshot.get(MAX_CHUNK_SIZE * 2),
            Some(&(MAX_CHUNK_SIZE as i32 * 2))
        );
        assert_eq!(left_snapshot.get(MAX_CHUNK_SIZE * 2), Some(&-10));
        assert_eq!(right_snapshot.get(MAX_CHUNK_SIZE * 2), Some(&-20));
        assert_eq!(retained.position(), MAX_CHUNK_SIZE * 2);
        assert_eq!(left.position(), MAX_CHUNK_SIZE * 2 + 1);
        assert_eq!(right.position(), MAX_CHUNK_SIZE * 2 + 1);
        assert!(rope.chunks.shared_node_count_with(&left_snapshot.chunks) > 0);
        assert!(rope.chunks.shared_node_count_with(&right_snapshot.chunks) > 0);
        assert_ne!(left_snapshot.to_vec(), right_snapshot.to_vec());
        source_snapshot.validate_chunk_invariants();
        left_snapshot.validate_chunk_invariants();
        right_snapshot.validate_chunk_invariants();
    }

    #[test]
    fn rope_cursor_deterministic_history_matches_gap_vector_model() {
        let mut cursor = Rope::<i32>::new().cursor();
        let mut model = Vec::new();
        let mut position = 0_usize;
        let mut state = 0x9e37_79b9_u32;

        for step in 0..750_i32 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            match state % 7 {
                0 => {
                    cursor = cursor.insert(step);
                    model.insert(position, step);
                    position += 1;
                }
                1 => {
                    let values = [step, -step];
                    cursor = cursor.insert_range(values);
                    model.splice(position..position, values);
                    position += values.len();
                }
                2 => {
                    let edited = cursor.delete_previous();
                    if position == 0 {
                        assert!(edited.is_none());
                    } else {
                        position -= 1;
                        model.remove(position);
                        cursor = edited.unwrap();
                    }
                }
                3 => {
                    let edited = cursor.delete_next();
                    if position == model.len() {
                        assert!(edited.is_none());
                    } else {
                        model.remove(position);
                        cursor = edited.unwrap();
                    }
                }
                4 => {
                    let edited = cursor.replace_next(step);
                    if position == model.len() {
                        assert!(edited.is_none());
                    } else {
                        model[position] = step;
                        cursor = edited.unwrap();
                    }
                }
                5 if (state & 0x1000) == 0 => {
                    let moved = cursor.move_previous();
                    if position == 0 {
                        assert!(moved.is_none());
                    } else {
                        position -= 1;
                        cursor = moved.unwrap();
                    }
                }
                5 => {
                    let moved = cursor.move_next();
                    if position == model.len() {
                        assert!(moved.is_none());
                    } else {
                        position += 1;
                        cursor = moved.unwrap();
                    }
                }
                _ => {
                    let target = ((state >> 8) as usize) % (model.len() + 2);
                    let sought = cursor.seek(target);
                    if target > model.len() {
                        assert!(sought.is_none());
                    } else {
                        position = target;
                        cursor = sought.unwrap();
                    }
                }
            }

            assert_eq!(cursor.position(), position);
            assert_eq!(cursor.len(), model.len());
            assert_eq!(
                cursor.peek_previous(),
                position.checked_sub(1).and_then(|i| model.get(i))
            );
            assert_eq!(cursor.peek_next(), model.get(position));
            let snapshot = cursor.snapshot();
            assert_eq!(snapshot.to_vec(), model);
            snapshot.validate_chunk_invariants();
        }
    }

    #[test]
    fn rope_length_overflow_panics_without_mutating_shared_sources() {
        let seed: Rope<_> = (0..MAX_CHUNK_SIZE).collect();
        let mut expanded = seed.clone();
        while expanded.len() <= usize::MAX / 2 {
            expanded = expanded.concat(&expanded);
        }

        let expanded_len = expanded.len();
        assert!(expanded_len > usize::MAX / 2);
        assert_eq!(expanded.front(), Some(&0));
        assert_eq!(expanded.back(), Some(&(MAX_CHUNK_SIZE - 1)));

        let failure =
            std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| expanded.concat(&expanded)));
        assert!(failure.is_err());

        assert_eq!(expanded.len(), expanded_len);
        assert_eq!(expanded.front(), Some(&0));
        assert_eq!(expanded.back(), Some(&(MAX_CHUNK_SIZE - 1)));
        assert_eq!(seed.len(), MAX_CHUNK_SIZE);
        assert_eq!(seed.front(), Some(&0));
        assert_eq!(seed.back(), Some(&(MAX_CHUNK_SIZE - 1)));
    }

    #[test]
    fn rope_from_chunks_and_copy_to_preserve_immutable_storage() {
        let first = vec![1, 2];
        let second = vec![3, 4, 5];
        let rope = Rope::from_chunks([first.as_slice(), &[], second.as_slice()]);
        let mut copied = vec![0; 3];

        assert_eq!(rope.to_vec(), vec![1, 2, 3, 4, 5]);
        assert_eq!(rope.copy_to(1, &mut copied), Some(()));
        assert_eq!(copied, vec![2, 3, 4]);
        assert_eq!(rope.copy_to(4, &mut copied), None);
        assert_eq!(rope.copy_to(5, &mut []), Some(()));
        assert_eq!(rope.chunk_count(), 2);
        rope.validate_chunk_invariants();
    }

    #[test]
    fn rope_edits_share_chunked_measured_tree() {
        let rope: Rope<_> = (0..8192).collect();
        let split = rope.split_at(3072).unwrap();
        let joined = split.0.concat(&split.1);
        let changed = rope.set_item(4096, -1).unwrap();
        let inserted = rope.insert_at(3000, -2).unwrap();
        let removed = rope.remove_range(3072, 512).unwrap();

        assert_eq!(joined.to_vec(), rope.to_vec());
        assert_eq!(rope.chunk_count(), 4);
        assert_eq!(rope.chunks.measure(), &8192);
        assert_eq!(changed.get(4096), Some(&-1));
        assert_eq!(inserted.get(3000), Some(&-2));
        assert_eq!(removed.len(), 7680);
        assert!(rope.chunks.shared_node_count_with(&split.0.chunks) > 0);
        assert!(rope.chunks.shared_node_count_with(&split.1.chunks) > 0);
        assert!(rope.chunks.shared_node_count_with(&changed.chunks) > 0);
        assert!(rope.chunks.shared_node_count_with(&inserted.chunks) > 0);
        assert!(rope.chunks.shared_node_count_with(&removed.chunks) > 0);
        assert!(rope.chunks.tree_depth() < 8);
        split.0.validate_chunk_invariants();
        split.1.validate_chunk_invariants();
        joined.validate_chunk_invariants();
        changed.validate_chunk_invariants();
        inserted.validate_chunk_invariants();
        removed.validate_chunk_invariants();
    }

    #[test]
    fn measured_rope_locates_by_user_measure() {
        let rope: MeasuredRope<_, SumMeasure<i32>> = [2, 4, 8].into_iter().collect();
        let located = rope.locate_by_measure(|sum| *sum > 5);
        let chunked = MeasuredRope::<_, SumMeasure<i32>>::from_chunks([&[2, 4][..], &[8][..]]);
        let mut copied = vec![0; 2];

        assert_eq!(rope.measure(), &14);
        assert_eq!(rope.prefix_measure(2), Some(6));
        assert_eq!(located.index, 1);
        assert_eq!(located.measure_before, 2);
        assert_eq!(located.value, Some(4));
        assert_eq!(chunked.measure(), &14);
        assert_eq!(chunked.copy_to(1, &mut copied), Some(()));
        assert_eq!(copied, vec![4, 8]);
        assert_eq!(chunked.copy_to(3, &mut [0]), None);
        let mut single = [0];
        assert_eq!(chunked.copy_to(usize::MAX, &mut single), None);
        chunked.validate_chunk_invariants();
    }

    #[test]
    fn measured_rope_uses_shared_measured_tree_storage() {
        let rope: MeasuredRope<_, SumMeasure<i32>> = (1..=8192).collect();
        let split = rope.split_at(3072).unwrap();
        let joined = split.left.concat(&split.right);
        let changed = rope.set_item(4096, 99_999).unwrap();
        let by_measure = rope.split_by_measure(|sum| *sum >= 1_000);

        let mut prefix = 0;
        let expected_measure_boundary = (1..=8192)
            .position(|value| {
                prefix += value;
                prefix >= 1_000
            })
            .unwrap();

        assert_eq!(rope.chunk_count(), 4);
        assert_eq!(rope.get(4096), Some(&4097));
        assert_eq!(rope.prefix_measure(10), Some(55));
        assert_eq!(rope.prefix_measure(4096), Some((4096 * 4097) / 2));
        assert_eq!(rope.prefix_measure(9000), None);
        assert_eq!(split.left.len(), 3072);
        assert_eq!(split.right.get(0), Some(&3073));
        assert_eq!(joined.to_vec(), rope.to_vec());
        assert_eq!(changed.get(4096), Some(&99_999));
        assert_eq!(changed.measure(), &(*rope.measure() - 4097 + 99_999));
        assert_eq!(by_measure.left.len(), expected_measure_boundary);
        assert_eq!(
            by_measure.left.len() + by_measure.right.len(),
            rope.len(),
            "split_by_measure halves must partition the rope"
        );
        let mut recombined = by_measure.left.to_vec();
        recombined.extend(by_measure.right.to_vec());
        assert_eq!(recombined, rope.to_vec());
        assert!(rope.tree.shared_node_count_with(&split.left.tree) > 0);
        assert!(rope.tree.shared_node_count_with(&split.right.tree) > 0);
        assert!(rope.tree.shared_node_count_with(&changed.tree) > 0);
        assert!(rope.tree.shared_node_count_with(&by_measure.right.tree) > 0);
        assert!(rope.tree.tree_depth() < 8);
        split.left.validate_chunk_invariants();
        split.right.validate_chunk_invariants();
        joined.validate_chunk_invariants();
        changed.validate_chunk_invariants();
        by_measure.left.validate_chunk_invariants();
        by_measure.right.validate_chunk_invariants();
    }

    #[test]
    fn measured_rope_positional_edits_match_a_vector_model_and_share_storage() {
        let rope: MeasuredRope<_, SumMeasure<i32>> = (1..=8192).collect();

        let inserted = rope.insert_at(3072, -1).unwrap();
        let inserted_range = inserted.insert_range(4096, [-2, -3, -4]).unwrap();
        let removed = inserted_range.remove_at(1024).unwrap();
        let removed_range = removed.remove_range(5000, 700).unwrap();
        let slice = removed_range.slice(2048, 3072).unwrap();

        let mut model = (1..=8192).collect::<Vec<i32>>();
        model.insert(3072, -1);
        model.splice(4096..4096, [-2, -3, -4]);
        model.remove(1024);
        model.drain(5000..5700);

        assert_eq!(rope.len(), 8192, "the source snapshot must not change");
        assert_eq!(removed_range.to_vec(), model);
        assert_eq!(removed_range.measure(), &model.iter().sum::<i32>());
        assert_eq!(slice.to_vec(), model[2048..5120]);
        assert_eq!(slice.measure(), &model[2048..5120].iter().sum::<i32>());
        assert!(rope.tree.shared_node_count_with(&inserted.tree) > 0);
        assert!(inserted.tree.shared_node_count_with(&inserted_range.tree) > 0);
        assert!(inserted_range.tree.shared_node_count_with(&removed.tree) > 0);
        assert!(removed.tree.shared_node_count_with(&removed_range.tree) > 0);
        assert!(removed_range.tree.shared_node_count_with(&slice.tree) > 0);

        assert!(rope.insert_at(8193, 0).is_none());
        assert!(rope.insert_range(8193, [1, 2]).is_none());
        assert!(rope.remove_at(8192).is_none());
        assert!(rope.remove_range(8190, 3).is_none());
        assert!(rope.slice(8190, 3).is_none());
        assert_eq!(rope.insert_range(100, []).unwrap().to_vec(), rope.to_vec());
        assert_eq!(rope.remove_range(100, 0).unwrap().to_vec(), rope.to_vec());
        assert!(rope.slice(100, 0).unwrap().is_empty());

        inserted.validate_chunk_invariants();
        inserted_range.validate_chunk_invariants();
        removed.validate_chunk_invariants();
        removed_range.validate_chunk_invariants();
        slice.validate_chunk_invariants();
    }

    #[test]
    fn measured_rope_positional_edits_survive_deterministic_model_replay() {
        let mut rope = MeasuredRope::<i32, SumMeasure<i32>>::new();
        let mut model = Vec::new();
        let mut state = 0x9e37_79b9_u32;

        for step in 0..750_i32 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            match state % 4 {
                0 | 1 => {
                    let index = (state as usize >> 8) % (model.len() + 1);
                    rope = rope.insert_at(index, step).unwrap();
                    model.insert(index, step);
                }
                2 if !model.is_empty() => {
                    let index = (state as usize >> 8) % model.len();
                    rope = rope.remove_at(index).unwrap();
                    model.remove(index);
                }
                _ => {
                    let index = (state as usize >> 8) % (model.len() + 1);
                    let values = [step, -step];
                    rope = rope.insert_range(index, values).unwrap();
                    model.splice(index..index, values);
                }
            }

            assert_eq!(rope.to_vec(), model);
            assert_eq!(rope.measure(), &model.iter().sum::<i32>());
            rope.validate_chunk_invariants();

            let start = (state as usize >> 16) % (model.len() + 1);
            let count = (state as usize >> 24) % (model.len() - start + 1);
            assert_eq!(
                rope.slice(start, count).unwrap().to_vec(),
                model[start..start + count]
            );
        }
    }

    #[test]
    fn measured_rope_builder_freezes_isolated_structurally_shared_snapshots() {
        let source: MeasuredRope<_, SumMeasure<i32>> = (1..=4096).collect();
        let mut builder = source.to_builder();

        assert_eq!(builder.len(), source.len());
        assert_eq!(builder.measure(), *source.measure());
        builder.push(4097).extend(4098..=6144);
        assert_eq!(builder.len(), 6144);
        assert_eq!(builder.measure(), (1..=6144).sum::<i32>());

        let first = builder.to_immutable();
        let cached = builder.to_immutable();
        assert_eq!(first.to_vec(), (1..=6144).collect::<Vec<_>>());
        assert!(source.tree.shared_node_count_with(&first.tree) > 0);
        assert!(first.shares_storage_with(&cached));

        builder.push(6145).extend(6146..=6200);
        let second = builder.to_immutable();
        assert_eq!(
            first.len(),
            6144,
            "later appends must not mutate an earlier snapshot"
        );
        assert_eq!(second.len(), 6200);
        assert_eq!(second.measure(), &(1..=6200).sum::<i32>());
        assert!(first.tree.shared_node_count_with(&second.tree) > 0);
        first.validate_chunk_invariants();
        second.validate_chunk_invariants();

        builder.clear();
        assert!(builder.is_empty());
        assert_eq!(builder.measure(), 0);
        assert!(builder.to_immutable().is_empty());
        assert_eq!(first.len(), 6144);
    }

    #[test]
    fn measured_rope_split_by_measure_partitions_the_rope() {
        let rope: MeasuredRope<i32, SumMeasure<i32>> = [2, 4, 8].into_iter().collect();
        let split = rope.split_by_measure(|sum| *sum > 5);
        assert_eq!(split.left.to_vec(), vec![2]);
        assert_eq!(split.right.to_vec(), vec![4, 8]);

        let never = rope.split_by_measure(|_| false);
        assert_eq!(never.left.to_vec(), vec![2, 4, 8]);
        assert!(never.right.is_empty());

        let always = rope.split_by_measure(|_| true);
        assert!(always.left.is_empty());
        assert_eq!(always.right.to_vec(), vec![2, 4, 8]);

        // Exercise boundaries interior to a chunk across several chunk shapes.
        for total in [1_usize, 5, 300, 700, 2049] {
            let rope: MeasuredRope<i32, SumMeasure<i32>> = (1..=total as i32).map(|_| 1).collect();
            for boundary in [0, 1, total / 2, total.saturating_sub(1), total] {
                let split = rope.split_by_measure(|sum| *sum > boundary as i32);
                assert_eq!(split.left.len(), boundary);
                assert_eq!(split.right.len(), total - boundary);
                split.left.validate_chunk_invariants();
                split.right.validate_chunk_invariants();
            }
        }
    }

    #[test]
    fn measured_rope_cursor_navigation_search_and_snapshot_need_no_clone_bound() {
        #[derive(Debug, PartialEq, Eq)]
        struct NonClone(i32);

        let empty = MeasuredRope::<NonClone, SizeMeasure>::new().cursor();
        assert!(empty.is_empty());
        assert!(empty.is_at_start());
        assert!(empty.is_at_end());
        assert_eq!(empty.measure_before(), 0);
        assert_eq!(empty.measure_after(), 0);
        assert!(empty.move_previous().is_none());
        assert!(empty.move_next().is_none());

        let empty_search = empty.seek_by_measure(|_| true);
        assert!(!empty_search.found);
        assert_eq!(empty_search.cursor.position(), 0);

        let rope: MeasuredRope<_, SizeMeasure> = [NonClone(10), NonClone(20), NonClone(30)]
            .into_iter()
            .collect();
        let cursor = rope.cursor_at(1).unwrap();
        assert_eq!(cursor.peek_previous(), Some(&NonClone(10)));
        assert_eq!(cursor.peek_next(), Some(&NonClone(20)));
        assert_eq!(cursor.measure_before(), 1);
        assert_eq!(cursor.measure_after(), 2);
        assert_eq!(cursor.move_next().unwrap().position(), 2);
        assert_eq!(cursor.move_previous().unwrap().position(), 0);
        assert!(cursor.seek(4).is_none());
        assert!(rope.shares_storage_with(&cursor.snapshot()));

        let found = cursor.seek_by_measure(|count| *count >= 2);
        assert!(found.found);
        assert_eq!(found.cursor.position(), 1);
        assert_eq!(found.cursor.peek_next(), Some(&NonClone(20)));

        let miss = cursor.seek_by_measure(|count| *count > 3);
        assert!(!miss.found);
        assert_eq!(miss.cursor.position(), 3);
        assert!(miss.cursor.is_at_end());
    }

    #[test]
    fn measured_rope_cursor_preserves_ordered_measures_and_retained_branches() {
        let source: MeasuredRope<i32, TraceMeasure> = (0..4096).collect();
        let cursor = source.cursor_at(2048).unwrap();

        assert_eq!(cursor.measure_before(), (0..2048).collect::<Vec<_>>());
        assert_eq!(cursor.measure_after(), (2048..4096).collect::<Vec<_>>());

        let inserted = cursor.insert(-1).insert_range([-2, -3]);
        let replaced = inserted.replace_next(99_999).unwrap();
        let previous_deleted = replaced.delete_previous().unwrap();
        let next_deleted = previous_deleted.delete_next().unwrap();

        let mut expected = (0..4096).collect::<Vec<_>>();
        expected.splice(2048..2048, [-1, -2, -3]);
        expected[2051] = 99_999;
        expected.remove(2050);
        expected.remove(2050);

        assert_eq!(next_deleted.snapshot().to_vec(), expected);
        assert_eq!(next_deleted.position(), 2050);
        assert_eq!(source.to_vec(), (0..4096).collect::<Vec<_>>());
        assert!(source.shares_storage_with(&cursor.snapshot()));
        assert!(
            source
                .tree
                .shared_node_count_with(&next_deleted.snapshot().tree)
                > 0
        );

        let nullable: MeasuredRope<Option<i32>, SizeMeasure> =
            [Some(1), None, Some(3)].into_iter().collect();
        let nullable_cursor = nullable.cursor_at(1).unwrap().replace_next(None).unwrap();
        assert_eq!(
            nullable_cursor.snapshot().to_vec(),
            vec![Some(1), None, Some(3)]
        );
    }

    #[test]
    fn measured_rope_cursor_measure_search_is_absolute_ordered_and_retryable() {
        let source: MeasuredRope<i32, SumMeasure<i32>> = [2, 4, 8].into_iter().collect();
        let receiver = source.cursor_at(2).unwrap();

        let found = source.cursor_by_measure(|sum| *sum > 5);
        assert!(found.found);
        assert_eq!(found.cursor.position(), 1);
        assert_eq!(found.cursor.measure_before(), 2);
        assert_eq!(found.cursor.peek_next(), Some(&4));

        let same_absolute_result = receiver.seek_by_measure(|sum| *sum > 5);
        assert!(same_absolute_result.found);
        assert_eq!(same_absolute_result.cursor.position(), 1);

        let first = receiver.seek_by_measure(|_| true);
        assert!(first.found);
        assert_eq!(first.cursor.position(), 0);

        let miss = receiver.seek_by_measure(|sum| *sum > 14);
        assert!(!miss.found);
        assert_eq!(miss.cursor.position(), source.len());

        let edited = receiver.insert(16);
        let edited_hit = edited.seek_by_measure(|sum| *sum >= 18);
        assert!(edited_hit.found);
        assert_eq!(edited_hit.cursor.position(), 2);
        assert_eq!(edited_hit.cursor.peek_next(), Some(&16));

        let failure = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            receiver.seek_by_measure(|_| panic!("cursor predicate failure"))
        }));
        assert!(failure.is_err());
        assert_eq!(receiver.position(), 2);
        assert_eq!(receiver.snapshot().to_vec(), vec![2, 4, 8]);
        assert_eq!(
            receiver.seek_by_measure(|sum| *sum >= 6).cursor.position(),
            1
        );

        let across_chunks: MeasuredRope<i32, SizeMeasure> = (0..4097).collect();
        let boundary = across_chunks.cursor_by_measure(|count| *count >= 2049);
        assert!(boundary.found);
        assert_eq!(boundary.cursor.position(), 2048);
        assert_eq!(boundary.cursor.peek_next(), Some(&2048));
    }

    #[test]
    fn measured_rope_cursor_edits_match_a_gap_vector_model() {
        let mut cursor = MeasuredRope::<i32, SumMeasure<i32>>::new().cursor();
        let mut model = Vec::new();
        let mut position = 0_usize;
        let mut state = 0xa341_316c_u32;

        for step in 0..750_i32 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            match state % 7 {
                0 | 1 => {
                    cursor = cursor.insert(step);
                    model.insert(position, step);
                    position += 1;
                }
                2 => {
                    let values = [step, -step];
                    cursor = cursor.insert_range(values);
                    model.splice(position..position, values);
                    position += values.len();
                }
                3 if position > 0 => {
                    cursor = cursor.delete_previous().unwrap();
                    position -= 1;
                    model.remove(position);
                }
                4 if position < model.len() => {
                    cursor = cursor.delete_next().unwrap();
                    model.remove(position);
                }
                5 if position < model.len() => {
                    cursor = cursor.replace_next(-step).unwrap();
                    model[position] = -step;
                }
                _ => {
                    position = (state as usize >> 8) % (model.len() + 1);
                    cursor = cursor.seek(position).unwrap();
                }
            }

            assert_eq!(cursor.position(), position);
            assert_eq!(cursor.snapshot().to_vec(), model);
            assert_eq!(
                cursor.peek_previous(),
                position.checked_sub(1).and_then(|i| model.get(i))
            );
            assert_eq!(cursor.peek_next(), model.get(position));
            assert_eq!(
                cursor.measure_before(),
                model[..position].iter().sum::<i32>()
            );
            assert_eq!(
                cursor.measure_after(),
                model[position..].iter().sum::<i32>()
            );
            cursor.snapshot().validate_chunk_invariants();
        }
    }

    #[test]
    fn measured_rope_length_overflow_precedes_cursor_measure_callbacks() {
        let seed: MeasuredRope<_, CountingSizeMeasure> = [0].into_iter().collect();
        let mut power = seed.clone();
        let mut expanded = MeasuredRope::new();
        for bit in 0..usize::BITS {
            expanded = expanded.concat(&power);
            if bit + 1 < usize::BITS {
                power = power.concat(&power);
            }
        }

        let expanded_len = expanded.len();
        assert_eq!(expanded_len, usize::MAX);
        COUNTING_MEASURE_CALLS.store(0, Ordering::Relaxed);
        let failure = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            expanded.cursor().insert(-1)
        }));

        assert!(failure.is_err());
        assert_eq!(COUNTING_MEASURE_CALLS.load(Ordering::Relaxed), 0);
        assert_eq!(expanded.len(), expanded_len);
        assert_eq!(expanded.get(0), Some(&0));
        assert_eq!(seed.len(), 1);
    }

    #[test]
    fn text_rope_cursor_retains_text_semantics_and_exact_facade() {
        let text = TextRope::from("a🙂\r\nβ");
        let start = text.cursor();
        assert_eq!(start.len(), 5);
        assert_eq!(start.line_column(), LineColumn { line: 0, column: 0 });

        let newline = text.cursor_by_measure(|count| *count >= 1);
        assert!(newline.found);
        assert_eq!(newline.cursor.position(), 3);
        assert_eq!(newline.cursor.peek_next(), Some(&'\n'));
        assert_eq!(newline.cursor.measure_before(), 0);
        assert_eq!(newline.cursor.measure_after(), 1);
        assert_eq!(
            newline.cursor.move_next().unwrap().line_column(),
            LineColumn { line: 1, column: 0 }
        );

        let edited = text
            .cursor_at(1)
            .unwrap()
            .insert_range("x\n".chars())
            .replace_next('Ω')
            .unwrap();
        assert_eq!(edited.position(), 3);
        assert_eq!(edited.snapshot().as_string(), "ax\nΩ\r\nβ");
        assert_eq!(edited.line_column(), LineColumn { line: 1, column: 0 });
        assert_eq!(text.as_string(), "a🙂\r\nβ");
        assert!(
            text.to_measured_rope()
                .shares_storage_with(&start.snapshot().to_measured_rope())
        );

        let miss = edited.seek_by_measure(|count| *count > 2);
        assert!(!miss.found);
        assert!(miss.cursor.is_at_end());
        assert_eq!(miss.cursor.snapshot().as_string(), "ax\nΩ\r\nβ");
    }

    #[test]
    fn text_rope_uses_newline_measured_navigation() {
        let text = TextRope::from("alpha\nbeta\n");
        let char_rope = Rope::<char>::from(String::from("alpha\nbeta\n"));

        assert_eq!(text.chars.measure(), &2);
        assert_eq!(text.line_count(), 3);
        assert_eq!(
            text.line_column_of(7),
            Some(LineColumn { line: 1, column: 1 })
        );
        assert_eq!(text.offset_of(1, 2), Some(8));
        assert_eq!(text.get_line(0).unwrap(), "alpha");
        assert_eq!(text.get_line(2).unwrap(), "");
        assert_eq!(text.lines(), vec!["alpha", "beta", ""]);
        assert_eq!(text.to_measured_rope().prefix_measure(10), Some(1));
        assert_eq!(
            text.to_char_rope().to_vec(),
            text.as_string().chars().collect::<Vec<_>>()
        );
        assert_eq!(char_rope.to_vec(), text.to_char_rope().to_vec());
        assert_eq!(text.to_string(), "alpha\nbeta\n");
    }

    #[test]
    fn builder_creates_char_and_text_ropes() {
        let mut builder = RopeBuilder::new();
        assert!(builder.is_empty());
        builder
            .append("hello")
            .append_char(' ')
            .append_line("world");

        assert_eq!(builder.len(), 12);
        assert_eq!(builder.as_str(), "hello world\n");
        assert_eq!(builder.to_string(), "hello world\n");
        assert_eq!(builder.to_rope().to_vec().len(), 12);
        assert_eq!(builder.to_text_rope().as_string(), "hello world\n");
        assert!(builder.clear().is_empty());
        assert_eq!(builder.as_str(), "");
    }
}
