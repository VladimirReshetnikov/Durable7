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
        debug_assert!(offset + len <= self.len);
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
        left + right
    }
}

type RopeTree<T> = FingerTree<RopeChunk<T>, ChunkLengthMeasure<T>>;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Rope<T> {
    chunks: RopeTree<T>,
}

pub struct RopeIter<'a, T> {
    chunks: crate::measured::Iter<'a, RopeChunk<T>, usize>,
    current: Option<std::slice::Iter<'a, T>>,
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
    pub fn to_vec(&self) -> Vec<T> {
        let mut items = Vec::with_capacity(self.len());
        for chunk in self.chunks.iter() {
            items.extend_from_slice(chunk.as_slice());
        }
        items
    }

    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        self.insert_at(0, item)
            .expect("front insertion index is always valid")
    }

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
        (left.0 + right.0, P::combine(&left.1, &right.1))
    }
}

type MeasuredRopeTree<T, P> = FingerTree<MeasuredRopeChunk<T, P>, MeasuredChunkMeasure<T, P>>;

pub struct MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    tree: MeasuredRopeTree<T, P>,
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

impl<T, P> MeasuredRope<T, P>
where
    P: MeasurePolicy<T>,
{
    #[must_use]
    pub fn new() -> Self {
        Self::from_tree(MeasuredRopeTree::new())
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_tree(measured_tree_from_items::<T, P>(items))
    }

    fn from_tree(tree: MeasuredRopeTree<T, P>) -> Self {
        Self { tree }
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
        let located = self.tree.try_locate(|measure| predicate(&measure.1));
        let Some(chunk) = located.item else {
            return MeasuredRopeSplit {
                left: self.clone(),
                right: Self::new(),
            };
        };

        let mut prefix = located.measure_before.1.clone();
        let mut offset = 0;
        for item in chunk.as_slice() {
            let next = P::combine(&prefix, &P::measure(item));
            if predicate(&next) {
                break;
            }

            prefix = next;
            offset += 1;
        }

        let split = self
            .tree
            .split_at_index(located.index)
            .expect("located chunk index is valid");
        MeasuredRopeSplit {
            left: Self::from_tree(append_measured_prefix(split.left, &chunk, offset)),
            right: Self::from_tree(prepend_measured_suffix(split.right, &chunk, offset)),
        }
    }

    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        Self::from_tree(self.tree.concat(&other.tree))
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

impl<T, P> Default for MeasuredRope<T, P>
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
    pub fn to_vec(&self) -> Vec<T> {
        let mut items = Vec::with_capacity(self.len());
        for chunk in self.tree.iter() {
            items.extend_from_slice(chunk.as_slice());
        }
        items
    }

    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
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
        left + right
    }
}

pub struct TextRope {
    chars: MeasuredRope<char, NewlineMeasure>,
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
        self.chars.to_vec().into_iter().collect()
    }

    #[must_use]
    pub fn line_count(&self) -> usize {
        *self.chars.measure() + 1
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
        Some(
            self.chars
                .to_vec()
                .into_iter()
                .skip(start)
                .take(end - start)
                .collect(),
        )
    }

    #[must_use]
    pub fn lines(&self) -> Vec<String> {
        (0..self.line_count())
            .filter_map(|line| self.get_line(line))
            .collect()
    }

    #[must_use]
    pub fn to_char_rope(&self) -> Rope<char> {
        self.chars.to_vec().into_iter().collect()
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
        self.chars.to_vec() == other.chars.to_vec()
    }
}

impl Eq for TextRope {}

impl Default for TextRope {
    fn default() -> Self {
        Self::new()
    }
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RopeBuilder {
    text: String,
}

impl RopeBuilder {
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    pub fn append(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self
    }

    pub fn append_line(&mut self, text: &str) -> &mut Self {
        self.text.push_str(text);
        self.text.push('\n');
        self
    }

    pub fn clear(&mut self) -> &mut Self {
        self.text.clear();
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::measured::SumMeasure;

    #[test]
    fn rope_edits_preserve_snapshots() {
        let rope: Rope<_> = [1, 2, 3].into_iter().collect();
        let edited = rope.insert_at(1, 9).unwrap().remove_at(3).unwrap();

        assert_eq!(rope.to_vec(), vec![1, 2, 3]);
        assert_eq!(edited.to_vec(), vec![1, 9, 2]);
        assert_eq!(rope.slice(1, 2).unwrap().to_vec(), vec![2, 3]);
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

        assert_eq!(rope.measure(), &14);
        assert_eq!(rope.prefix_measure(2), Some(6));
        assert_eq!(located.index, 1);
        assert_eq!(located.measure_before, 2);
        assert_eq!(located.value, Some(4));
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
    fn text_rope_uses_newline_measured_navigation() {
        let text = TextRope::from_text("alpha\nbeta\n");

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
    }

    #[test]
    fn builder_creates_char_and_text_ropes() {
        let mut builder = RopeBuilder::new();
        builder.append("hello").append_line(" world");

        assert_eq!(builder.to_rope().to_vec().len(), 12);
        assert_eq!(builder.to_text_rope().as_string(), "hello world\n");
    }
}
