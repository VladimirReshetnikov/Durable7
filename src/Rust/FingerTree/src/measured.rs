//! The measured 2-3 finger tree that most of this crate is built on, plus the standard measures.
//!
//! [`FingerTree`] is a persistent sequence that caches a monoidal *measure* at every node. Because
//! the measure of a subtree is available without descending into it, a single generic split can
//! answer any question the measure can express — "the 100th element", "the first prefix whose sum
//! exceeds 40", "the leftmost element whose key is at least `k`" — in O(log n). Amortized access at
//! either end is O(1).
//!
//! The measure is chosen by a [`MeasurePolicy`], and the module ships the policies the rest of the
//! crate uses: [`SizeMeasure`] for order statistics, [`SumMeasure`], [`MaxMeasure`],
//! [`MinMeasure`], [`KeyMeasure`] and [`OrderStatisticMeasure`] for ordered structures, and
//! [`ProductMeasure`] for pairing two policies (with [`SizeAndSumMeasure`], [`SizeAndMaxMeasure`],
//! and [`SizeAndMinMeasure`] as ready-made combinations).
//!
//! Splitting produces a [`MeasuredSplit`]; locating produces a [`LocateResult`]. Positional
//! traversal uses [`FingerTreeCursor`], whose measure-directed seeks report a
//! [`FingerTreeCursorSearch`].

use std::fmt;
use std::marker::PhantomData;
use std::ops::Add;
use std::sync::Arc;

/// Defines the monoid that a [`FingerTree`] caches at every node.
///
/// Implementations must form a monoid: [`combine`](Self::combine) must be associative and
/// [`empty`](Self::empty) must be a two-sided identity for it. Splitting relies on those laws, so a
/// policy that breaks them yields unspecified — not merely suboptimal — search results. The
/// operation need not be commutative; measures are always combined left to right in sequence order.
pub trait MeasurePolicy<T> {
    /// The cached summary type.
    type Measure: Clone;

    /// Returns the identity measure, that is, the measure of the empty sequence.
    fn empty() -> Self::Measure;

    /// Returns the measure of a single element.
    fn measure(element: &T) -> Self::Measure;

    /// Combines two measures in sequence order, `left` preceding `right`.
    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure;
}

/// The measure computed by a [`ProductMeasure`]: two independent measures carried together.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MeasurePair<TFirst, TSecond> {
    /// The first component's measure.
    pub first: TFirst,
    /// The second component's measure.
    pub second: TSecond,
}

/// Counts elements, making the tree an order-statistic sequence indexable by position.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SizeMeasure;

impl<T> MeasurePolicy<T> for SizeMeasure {
    type Measure = usize;

    fn empty() -> Self::Measure {
        0
    }

    fn measure(_element: &T) -> Self::Measure {
        1
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left + right
    }
}

/// Runs two measure policies side by side over the same elements.
///
/// The product of two monoids is a monoid, so a tree measured this way can be searched by either
/// component — for example counting elements *and* summing weights, so the same tree answers both
/// "the element at index 10" and "where the running total passes 500".
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ProductMeasure<T, PFirst, PSecond>(PhantomData<(T, PFirst, PSecond)>);

impl<T, PFirst, PSecond> MeasurePolicy<T> for ProductMeasure<T, PFirst, PSecond>
where
    PFirst: MeasurePolicy<T>,
    PSecond: MeasurePolicy<T>,
{
    type Measure = MeasurePair<PFirst::Measure, PSecond::Measure>;

    fn empty() -> Self::Measure {
        MeasurePair {
            first: PFirst::empty(),
            second: PSecond::empty(),
        }
    }

    fn measure(element: &T) -> Self::Measure {
        MeasurePair {
            first: PFirst::measure(element),
            second: PSecond::measure(element),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        MeasurePair {
            first: PFirst::combine(&left.first, &right.first),
            second: PSecond::combine(&left.second, &right.second),
        }
    }
}

/// The measure computed by an [`OrderStatisticMeasure`]: a subtree's element count and its last key.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RankedKey<T> {
    /// The number of elements in the subtree.
    pub count: usize,
    /// The last element in the subtree, or `None` when it is empty. For an ascending sequence this
    /// is the subtree's largest key, which is what upper- and lower-bound searches compare against.
    pub key: Option<T>,
}

/// Caches both the element count and the last key, so one tree supports rank, select, and ordered
/// search at once.
///
/// This is the measure behind [`SortedBag`](crate::SortedBag), [`SortedSet`](crate::SortedSet), and
/// [`SortedMap`](crate::SortedMap): counting alone would give positional access without ordered
/// lookup, and keys alone would give ordered lookup without rank.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct OrderStatisticMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<T> for OrderStatisticMeasure<T>
where
    T: Clone,
{
    type Measure = RankedKey<T>;

    fn empty() -> Self::Measure {
        RankedKey {
            count: 0,
            key: None,
        }
    }

    fn measure(element: &T) -> Self::Measure {
        RankedKey {
            count: 1,
            key: Some(element.clone()),
        }
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        RankedKey {
            count: left.count + right.count,
            key: right.key.clone().or_else(|| left.key.clone()),
        }
    }
}

/// Caches each subtree's last element, supporting ordered lower- and upper-bound searches.
///
/// Use this when only ordered lookup is needed; [`OrderStatisticMeasure`] additionally caches
/// counts and so also supports rank and select.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct KeyMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<T> for KeyMeasure<T>
where
    T: Clone,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        right.clone().or_else(|| left.clone())
    }
}

/// Adds elements together, so the cached measure of a subtree is the total of its elements.
///
/// Turns the tree into a cumulative-weight structure: prefix sums, threshold splits, and weighted
/// selection all become O(log n).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SumMeasure<T>(PhantomData<T>);

impl<T> MeasurePolicy<T> for SumMeasure<T>
where
    T: Add<Output = T> + Clone + Default,
{
    type Measure = T;

    fn empty() -> Self::Measure {
        T::default()
    }

    fn measure(element: &T) -> Self::Measure {
        element.clone()
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        left.clone() + right.clone()
    }
}

/// Caches each subtree's maximum element, making the tree a priority queue.
///
/// The overall maximum is the root measure, so peeking is O(1) and extraction is O(log n) — with
/// no requirement that the elements be stored in sorted order.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MaxMeasure;

impl<T> MeasurePolicy<T> for MaxMeasure
where
    T: Clone + Ord,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.max(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

/// Caches each subtree's minimum element, making the tree a min-priority queue.
///
/// The mirror image of [`MaxMeasure`]: peeking the minimum is O(1) and extraction is O(log n).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct MinMeasure;

impl<T> MeasurePolicy<T> for MinMeasure
where
    T: Clone + Ord,
{
    type Measure = Option<T>;

    fn empty() -> Self::Measure {
        None
    }

    fn measure(element: &T) -> Self::Measure {
        Some(element.clone())
    }

    fn combine(left: &Self::Measure, right: &Self::Measure) -> Self::Measure {
        match (left, right) {
            (Some(left), Some(right)) => Some(left.min(right).clone()),
            (Some(value), None) | (None, Some(value)) => Some(value.clone()),
            (None, None) => None,
        }
    }
}

/// Counts and sums at once: positional indexing plus cumulative-weight search and selection.
pub type SizeAndSumMeasure<T> = ProductMeasure<T, SizeMeasure, SumMeasure<T>>;
/// Counts and tracks the maximum at once: positional indexing plus O(1) maximum access.
pub type SizeAndMaxMeasure<T> = ProductMeasure<T, SizeMeasure, MaxMeasure>;
/// Counts and tracks the minimum at once: positional indexing plus O(1) minimum access.
pub type SizeAndMinMeasure<T> = ProductMeasure<T, SizeMeasure, MinMeasure>;

/// A persistent sequence that caches the measure defined by `P` at every node.
///
/// Every operation returns a new tree and leaves the receiver valid; versions share their unchanged
/// nodes through [`Arc`], so cloning is O(1) and an update copies only one root-to-leaf path.
/// Because each node's measure is available without descending into it, the single
/// [`split`](Self::split) operation answers any monotone question the measure can express in
/// O(log n), while access at either end is amortized O(1).
///
/// A tree is `Send + Sync` whenever `T` is, so one snapshot can be read concurrently without
/// locking.
pub struct FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    root: Arc<MeasuredNode<T, P::Measure>>,
    measure: P::Measure,
    _policy: PhantomData<P>,
}

/// Immutable measure-aware gap cursor over one exact general finger-tree snapshot.
///
/// The boundary index is representation state, not a public element-count contract. Arbitrary
/// monoids are navigated through ordered measures and neighboring elements.
pub struct FingerTreeCursor<T, P>
where
    P: MeasurePolicy<T>,
{
    tree: FingerTree<T, P>,
    position: usize,
}

/// Result of locating a finger-tree cursor with an inclusive-prefix measure predicate.
pub struct FingerTreeCursorSearch<T, P>
where
    P: MeasurePolicy<T>,
{
    /// The located cursor. On a miss this is still a usable cursor positioned at the end.
    pub cursor: FingerTreeCursor<T, P>,
    /// Whether some prefix actually satisfied the predicate.
    pub found: bool,
}

/// The two halves produced by splitting a [`FingerTree`].
///
/// Both halves are complete trees sharing structure with the original, which itself remains valid.
#[derive(Clone)]
pub struct MeasuredSplit<T, P>
where
    P: MeasurePolicy<T>,
{
    /// The elements before the split point.
    pub left: FingerTree<T, P>,
    /// The elements at and after the split point.
    pub right: FingerTree<T, P>,
}

/// Where a measure-directed search landed, reported without splitting the tree.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct LocateResult<T, M> {
    /// The index of the located element, or the tree's length on a miss.
    pub index: usize,
    /// The combined measure of every element before `index`.
    pub measure_before: M,
    /// The located element, or `None` on a miss.
    pub item: Option<T>,
}

enum MeasuredNode<T, M> {
    Empty,
    Leaf {
        item: T,
        measure: M,
    },
    Node {
        left: Arc<MeasuredNode<T, M>>,
        right: Arc<MeasuredNode<T, M>>,
        len: usize,
        height: u8,
        measure: M,
    },
}

impl<T, M> MeasuredNode<T, M>
where
    M: Clone,
{
    fn empty() -> Arc<Self> {
        Arc::new(Self::Empty)
    }

    fn leaf(item: T, measure: M) -> Arc<Self> {
        Arc::new(Self::Leaf { item, measure })
    }

    fn from_vec<F, C>(mut items: Vec<T>, measure_element: &F, combine: &C) -> Arc<Self>
    where
        F: Fn(&T) -> M,
        C: Fn(&M, &M) -> M,
    {
        match items.len() {
            0 => Self::empty(),
            1 => {
                let item = items.pop().expect("single-item vector has an item");
                let measure = measure_element(&item);
                Self::leaf(item, measure)
            }
            len => {
                let right = items.split_off(len / 2);
                Self::make_node(
                    Self::from_vec(items, measure_element, combine),
                    Self::from_vec(right, measure_element, combine),
                    combine,
                )
            }
        }
    }

    fn len(&self) -> usize {
        match self {
            Self::Empty => 0,
            Self::Leaf { .. } => 1,
            Self::Node { len, .. } => *len,
        }
    }

    fn height(&self) -> u8 {
        match self {
            Self::Empty => 0,
            Self::Leaf { .. } => 1,
            Self::Node { height, .. } => *height,
        }
    }

    fn measure(&self) -> Option<&M> {
        match self {
            Self::Empty => None,
            Self::Leaf { measure, .. } | Self::Node { measure, .. } => Some(measure),
        }
    }

    fn measure_or(&self, empty: &M) -> M {
        self.measure().cloned().unwrap_or_else(|| empty.clone())
    }

    fn make_node<C>(left: Arc<Self>, right: Arc<Self>, combine: &C) -> Arc<Self>
    where
        C: Fn(&M, &M) -> M,
    {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let len = left.len() + right.len();
        let height = left.height().max(right.height()) + 1;
        let left_measure = left
            .measure()
            .expect("non-empty left measured node has a measure");
        let right_measure = right
            .measure()
            .expect("non-empty right measured node has a measure");
        let measure = combine(left_measure, right_measure);
        Arc::new(Self::Node {
            left,
            right,
            len,
            height,
            measure,
        })
    }

    fn concat<C>(left: Arc<Self>, right: Arc<Self>, combine: &C) -> Arc<Self>
    where
        C: Fn(&M, &M) -> M,
    {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let left_height = left.height();
        let right_height = right.height();
        if left_height > right_height + 1
            && let Self::Node {
                left: left_left,
                right: left_right,
                ..
            } = left.as_ref()
        {
            let joined = Self::concat(Arc::clone(left_right), right, combine);
            return Self::balance(Arc::clone(left_left), joined, combine);
        }

        if right_height > left_height + 1
            && let Self::Node {
                left: right_left,
                right: right_right,
                ..
            } = right.as_ref()
        {
            let joined = Self::concat(left, Arc::clone(right_left), combine);
            return Self::balance(joined, Arc::clone(right_right), combine);
        }

        Self::balance(left, right, combine)
    }

    fn balance<C>(left: Arc<Self>, right: Arc<Self>, combine: &C) -> Arc<Self>
    where
        C: Fn(&M, &M) -> M,
    {
        if left.len() == 0 || right.len() == 0 {
            return Self::make_node(left, right, combine);
        }

        let left_height = left.height();
        let right_height = right.height();
        if left_height > right_height + 1
            && let Self::Node {
                left: left_left,
                right: left_right,
                ..
            } = left.as_ref()
        {
            if left_left.height() >= left_right.height() {
                return Self::make_node(
                    Arc::clone(left_left),
                    Self::make_node(Arc::clone(left_right), right, combine),
                    combine,
                );
            }

            if let Self::Node {
                left: middle_left,
                right: middle_right,
                ..
            } = left_right.as_ref()
            {
                return Self::make_node(
                    Self::make_node(Arc::clone(left_left), Arc::clone(middle_left), combine),
                    Self::make_node(Arc::clone(middle_right), right, combine),
                    combine,
                );
            }
        }

        if right_height > left_height + 1
            && let Self::Node {
                left: right_left,
                right: right_right,
                ..
            } = right.as_ref()
        {
            if right_right.height() >= right_left.height() {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(right_left), combine),
                    Arc::clone(right_right),
                    combine,
                );
            }

            if let Self::Node {
                left: middle_left,
                right: middle_right,
                ..
            } = right_left.as_ref()
            {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(middle_left), combine),
                    Self::make_node(Arc::clone(middle_right), Arc::clone(right_right), combine),
                    combine,
                );
            }
        }

        Self::make_node(left, right, combine)
    }

    fn split_at<C>(tree: &Arc<Self>, index: usize, combine: &C) -> (Arc<Self>, Arc<Self>)
    where
        C: Fn(&M, &M) -> M,
    {
        if index == 0 {
            return (Self::empty(), Arc::clone(tree));
        }

        if index == tree.len() {
            return (Arc::clone(tree), Self::empty());
        }

        match tree.as_ref() {
            Self::Empty => (Self::empty(), Self::empty()),
            Self::Leaf { .. } => {
                debug_assert_eq!(index, 0);
                (Self::empty(), Arc::clone(tree))
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                match index.cmp(&left_len) {
                    std::cmp::Ordering::Less => {
                        let (before, after) = Self::split_at(left, index, combine);
                        (before, Self::concat(after, Arc::clone(right), combine))
                    }
                    std::cmp::Ordering::Equal => (Arc::clone(left), Arc::clone(right)),
                    std::cmp::Ordering::Greater => {
                        let (before, after) = Self::split_at(right, index - left_len, combine);
                        (Self::concat(Arc::clone(left), before, combine), after)
                    }
                }
            }
        }
    }

    fn boundary_index<F, C>(
        tree: &Arc<Self>,
        prefix: &M,
        predicate: &mut F,
        combine: &C,
    ) -> Option<usize>
    where
        F: FnMut(&M) -> bool,
        C: Fn(&M, &M) -> M,
    {
        match tree.as_ref() {
            Self::Empty => None,
            Self::Leaf { measure, .. } => {
                let next = combine(prefix, measure);
                predicate(&next).then_some(0)
            }
            Self::Node { left, right, .. } => {
                let left_measure = left
                    .measure()
                    .expect("non-empty left measured node has a measure");
                let left_total = combine(prefix, left_measure);
                if predicate(&left_total) {
                    Self::boundary_index(left, prefix, predicate, combine)
                } else {
                    Self::boundary_index(right, &left_total, predicate, combine)
                        .map(|index| left.len() + index)
                }
            }
        }
    }

    fn prefix_measure<C>(tree: &Arc<Self>, count: usize, empty: &M, combine: &C) -> M
    where
        C: Fn(&M, &M) -> M,
    {
        if count == 0 {
            return empty.clone();
        }

        if count >= tree.len() {
            return tree.measure_or(empty);
        }

        match tree.as_ref() {
            Self::Empty => empty.clone(),
            Self::Leaf { measure, .. } => {
                debug_assert_eq!(count, 1);
                measure.clone()
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                if count <= left_len {
                    Self::prefix_measure(left, count, empty, combine)
                } else {
                    let left_measure = left.measure_or(empty);
                    let right_measure =
                        Self::prefix_measure(right, count - left_len, empty, combine);
                    combine(&left_measure, &right_measure)
                }
            }
        }
    }

    fn first(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf { item, .. } => Some(item),
            Self::Node { left, .. } => left.first(),
        }
    }

    fn last(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf { item, .. } => Some(item),
            Self::Node { right, .. } => right.last(),
        }
    }

    fn get(&self, index: usize) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf { item, .. } => (index == 0).then_some(item),
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                if index < left_len {
                    left.get(index)
                } else {
                    right.get(index - left_len)
                }
            }
        }
    }

    fn copy_to_vec(&self, destination: &mut Vec<T>)
    where
        T: Clone,
    {
        match self {
            Self::Empty => {}
            Self::Leaf { item, .. } => destination.push(item.clone()),
            Self::Node { left, right, .. } => {
                left.copy_to_vec(destination);
                right.copy_to_vec(destination);
            }
        }
    }

    #[cfg(test)]
    fn validate<F, C>(
        &self,
        measure_element: &F,
        empty: &M,
        combine: &C,
    ) -> Result<(usize, M), String>
    where
        F: Fn(&T) -> M,
        C: Fn(&M, &M) -> M,
        M: PartialEq + std::fmt::Debug,
    {
        match self {
            Self::Empty => Ok((0, empty.clone())),
            Self::Leaf { item, measure } => {
                let expected = measure_element(item);
                if expected != *measure {
                    return Err(format!(
                        "leaf cached measure {measure:?} disagrees with expected {expected:?}"
                    ));
                }

                Ok((1, expected))
            }
            Self::Node {
                left,
                right,
                len,
                height,
                measure,
            } => {
                let (left_len, left_measure) = left.validate(measure_element, empty, combine)?;
                let (right_len, right_measure) = right.validate(measure_element, empty, combine)?;
                let expected_len = left_len + right_len;
                if expected_len != *len {
                    return Err(format!(
                        "node cached length {len} disagrees with child total {expected_len}"
                    ));
                }

                let expected_height = left.height().max(right.height()) + 1;
                if expected_height != *height {
                    return Err(format!(
                        "node cached height {height} disagrees with child height {expected_height}"
                    ));
                }

                if left.height().abs_diff(right.height()) > 2 {
                    return Err(format!(
                        "node is too imbalanced: left height {}, right height {}",
                        left.height(),
                        right.height()
                    ));
                }

                let expected_measure = combine(&left_measure, &right_measure);
                if expected_measure != *measure {
                    return Err(format!(
                        "node cached measure {measure:?} disagrees with child total {expected_measure:?}"
                    ));
                }

                Ok((expected_len, expected_measure))
            }
        }
    }
}

impl<T, P> Clone for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            root: Arc::clone(&self.root),
            measure: self.measure.clone(),
            _policy: PhantomData,
        }
    }
}

impl<T, P> Clone for FingerTreeCursor<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        Self {
            tree: self.tree.clone(),
            position: self.position,
        }
    }
}

impl<T, P> fmt::Debug for FingerTree<T, P>
where
    T: fmt::Debug,
    P: MeasurePolicy<T>,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<T, P> PartialEq for FingerTree<T, P>
where
    T: PartialEq,
    P: MeasurePolicy<T>,
{
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len() && self.iter().eq(other.iter())
    }
}

impl<T, P> Eq for FingerTree<T, P>
where
    T: Eq,
    P: MeasurePolicy<T>,
{
}

impl<T, P> FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    /// Creates an empty tree whose measure is the policy identity.
    #[must_use]
    pub fn new() -> Self {
        Self {
            root: MeasuredNode::empty(),
            measure: P::empty(),
            _policy: PhantomData,
        }
    }

    /// Returns the number of elements. O(1); the count is cached at the root.
    #[must_use]
    pub fn len(&self) -> usize {
        self.root.len()
    }

    /// Returns `true` when the tree holds no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.len() == 0
    }

    /// Creates a measure-aware cursor before the first element.
    #[must_use]
    pub fn cursor_at_start(&self) -> FingerTreeCursor<T, P> {
        FingerTreeCursor {
            tree: self.clone(),
            position: 0,
        }
    }

    /// Creates a measure-aware cursor after the final element.
    #[must_use]
    pub fn cursor_at_end(&self) -> FingerTreeCursor<T, P> {
        FingerTreeCursor {
            tree: self.clone(),
            position: self.len(),
        }
    }

    /// Locates the gap before the first element whose inclusive prefix satisfies `predicate`.
    /// A miss returns a usable end cursor with `found == false`.
    #[must_use]
    pub fn cursor_by_measure<F>(&self, predicate: F) -> FingerTreeCursorSearch<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let position = self.boundary_index(predicate);
        FingerTreeCursorSearch {
            cursor: FingerTreeCursor {
                tree: self.clone(),
                position: position.unwrap_or(self.len()),
            },
            found: position.is_some(),
        }
    }

    /// Returns the combined measure of every element, in sequence order. O(1); it is cached.
    #[must_use]
    pub fn measure(&self) -> &P::Measure {
        &self.measure
    }

    /// Borrows the first element, or `None` when the tree is empty. O(1).
    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.root.first()
    }

    /// Borrows the last element, or `None` when the tree is empty. O(1).
    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.root.last()
    }

    /// Borrows the element at `index`, or `None` when `index` is out of range. O(log n).
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        (index < self.len()).then(|| self.root.get(index)).flatten()
    }

    /// Iterates the elements in sequence order.
    pub fn iter(&self) -> Iter<'_, T, P::Measure> {
        Iter::new(&self.root)
    }

    /// Returns `true` when `self` and `other` are the same root, so neither can observe a change
    /// made to the other.
    ///
    /// This is a *representation* test, not an equality test: it answers "did these two versions
    /// come from the same node?" in O(1) and is used to confirm that a no-op operation really did
    /// avoid copying. Two trees holding equal elements but built independently return `false`. All
    /// empty trees are treated as sharing.
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        (self.is_empty() && other.is_empty()) || Arc::ptr_eq(&self.root, &other.root)
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_root(MeasuredNode::from_vec(items, &P::measure, &P::combine))
    }

    fn from_root(root: Arc<MeasuredNode<T, P::Measure>>) -> Self {
        let measure = root.measure_or(&P::empty());
        Self {
            root,
            measure,
            _policy: PhantomData,
        }
    }

    /// Concatenates two trees, placing every element of `other` after every element of `self`.
    ///
    /// O(log(min(m, n))). Concatenating with an empty tree shares the non-empty operand's root
    /// instead of copying it.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        Self::from_root(MeasuredNode::concat(
            Arc::clone(&self.root),
            Arc::clone(&other.root),
            &P::combine,
        ))
    }

    /// Splits at the first position whose inclusive prefix measure satisfies `predicate`.
    ///
    /// This is the tree's central operation: because every node caches its measure, the search
    /// descends without visiting the elements it skips, so the split costs O(log n) rather than a
    /// scan. `predicate` is expected to be *monotone* — false for every prefix up to some boundary
    /// and true from there on — which is what makes "the first satisfying position" well defined;
    /// a non-monotone predicate yields an unspecified but still structurally valid split. When no
    /// prefix satisfies it, everything lands in the left half.
    #[must_use]
    pub fn split<F>(&self, predicate: F) -> MeasuredSplit<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate).unwrap_or(self.len());
        self.split_at_index_unchecked(index)
    }

    /// Splits at a positional boundary in `0..=len`, or returns `None` when `index` exceeds the
    /// length. O(log n).
    #[must_use]
    pub fn split_at_index(&self, index: usize) -> Option<MeasuredSplit<T, P>> {
        (index <= self.len()).then(|| self.split_at_index_unchecked(index))
    }

    /// Returns the combined measure of the first `count` elements, or `None` when `count` exceeds
    /// the length. O(log n) — the prefix is summed from cached node measures, not element by
    /// element.
    #[must_use]
    pub fn prefix_measure(&self, count: usize) -> Option<P::Measure> {
        (count <= self.len())
            .then(|| MeasuredNode::prefix_measure(&self.root, count, &P::empty(), &P::combine))
    }

    fn split_at_index_unchecked(&self, index: usize) -> MeasuredSplit<T, P> {
        let (left, right) = MeasuredNode::split_at(&self.root, index, &P::combine);
        MeasuredSplit {
            left: Self::from_root(left),
            right: Self::from_root(right),
        }
    }

    fn boundary_index<F>(&self, mut predicate: F) -> Option<usize>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        MeasuredNode::boundary_index(&self.root, &P::empty(), &mut predicate, &P::combine)
    }

    #[cfg(test)]
    pub(crate) fn tree_depth(&self) -> usize {
        self.root.height() as usize
    }

    #[cfg(test)]
    pub(crate) fn validate_invariants(&self)
    where
        P::Measure: PartialEq + std::fmt::Debug,
    {
        let (len, measure) = self
            .root
            .validate(&P::measure, &P::empty(), &P::combine)
            .expect("measured tree invariant failure");
        assert_eq!(len, self.len());
        assert_eq!(measure, self.measure);
    }

    #[cfg(test)]
    pub(crate) fn shared_node_count_with(&self, other: &Self) -> usize {
        use std::collections::HashSet;

        fn collect<T, M>(
            tree: &Arc<MeasuredNode<T, M>>,
            seen: &mut HashSet<*const MeasuredNode<T, M>>,
        ) {
            if !seen.insert(Arc::as_ptr(tree)) {
                return;
            }

            if let MeasuredNode::Node { left, right, .. } = tree.as_ref() {
                collect(left, seen);
                collect(right, seen);
            }
        }

        fn count<T, M>(
            tree: &Arc<MeasuredNode<T, M>>,
            seen: &HashSet<*const MeasuredNode<T, M>>,
        ) -> usize {
            let mut total = usize::from(seen.contains(&Arc::as_ptr(tree)));
            if let MeasuredNode::Node { left, right, .. } = tree.as_ref() {
                total += count(left, seen);
                total += count(right, seen);
            }

            total
        }

        let mut seen = HashSet::new();
        collect(&self.root, &mut seen);
        count(&other.root, &seen)
    }
}

impl<T, P> FingerTreeCursor<T, P>
where
    P: MeasurePolicy<T>,
{
    /// Returns `true` when the gap is before the first element.
    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    /// Returns `true` when the gap is after the last element.
    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.tree.len()
    }

    /// Returns the combined measure of every element before the gap. O(log n).
    #[must_use]
    pub fn measure_before(&self) -> P::Measure {
        self.tree
            .prefix_measure(self.position)
            .expect("a cursor boundary has a prefix measure")
    }

    /// Returns the combined measure of every element at or after the gap. O(log n).
    #[must_use]
    pub fn measure_after(&self) -> P::Measure {
        self.tree
            .split_at_index(self.position)
            .expect("a cursor boundary can be split")
            .right
            .measure()
            .clone()
    }

    /// Borrows the element immediately before the gap, or `None` at the start.
    #[must_use]
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.tree.get(index))
    }

    /// Borrows the element immediately after the gap, or `None` at the end.
    #[must_use]
    pub fn peek_next(&self) -> Option<&T> {
        self.tree.get(self.position)
    }

    /// Returns a cursor one position earlier, or `None` at the start.
    ///
    /// The receiver is unchanged; movement produces a new cursor over the same snapshot.
    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            tree: self.tree.clone(),
            position: self.position.checked_sub(1)?,
        })
    }

    /// Returns a cursor one position later, or `None` at the end.
    ///
    /// The receiver is unchanged; movement produces a new cursor over the same snapshot.
    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            tree: self.tree.clone(),
            position: self.position + 1,
        })
    }

    /// Seeks from the retained exact snapshot by an inclusive-prefix measure predicate.
    #[must_use]
    pub fn seek_by_measure<F>(&self, predicate: F) -> FingerTreeCursorSearch<T, P>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        self.tree.cursor_by_measure(predicate)
    }

    /// Inserts `item` at the gap and returns a cursor positioned after it.
    ///
    /// The receiver keeps its own snapshot, so cursors retained before this call continue to see
    /// the sequence without `item`. O(log n).
    #[must_use]
    pub fn insert(&self, item: T) -> Self {
        let split = self
            .tree
            .split_at_index(self.position)
            .expect("a cursor boundary can be split");
        let middle: FingerTree<T, P> = std::iter::once(item).collect();
        Self {
            tree: split.left.concat(&middle).concat(&split.right),
            position: self.position + 1,
        }
    }

    /// Removes the element before the gap and returns a cursor in its place, or `None` at the
    /// start. O(log n).
    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let first = self
            .tree
            .split_at_index(position)
            .expect("a cursor boundary can be split");
        let second = first
            .right
            .split_at_index(1)
            .expect("a non-start cursor has a previous element");
        Some(Self {
            tree: first.left.concat(&second.right),
            position,
        })
    }

    /// Removes the element after the gap and returns a cursor in its place, or `None` at the end.
    /// O(log n).
    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }
        let first = self
            .tree
            .split_at_index(self.position)
            .expect("a cursor boundary can be split");
        let second = first
            .right
            .split_at_index(1)
            .expect("a non-end cursor has a next element");
        Some(Self {
            tree: first.left.concat(&second.right),
            position: self.position,
        })
    }

    /// Replaces the element after the gap with `item`, keeping the gap where it is, or returns
    /// `None` at the end. O(log n).
    #[must_use]
    pub fn replace_next(&self, item: T) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }
        let first = self
            .tree
            .split_at_index(self.position)
            .expect("a cursor boundary can be split");
        let second = first
            .right
            .split_at_index(1)
            .expect("a non-end cursor has a next element");
        let middle: FingerTree<T, P> = std::iter::once(item).collect();
        Some(Self {
            tree: first.left.concat(&middle).concat(&second.right),
            position: self.position,
        })
    }

    /// Returns the tree version this cursor is positioned in. O(1); the root is shared.
    #[must_use]
    pub fn snapshot(&self) -> FingerTree<T, P> {
        self.tree.clone()
    }
}

impl<T, P> Default for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, P> FromIterator<T> for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T, P> FingerTree<T, P>
where
    T: Clone,
    P: MeasurePolicy<T>,
{
    /// Copies every element into a new vector, in sequence order. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut result = Vec::with_capacity(self.len());
        self.root.copy_to_vec(&mut result);
        result
    }

    /// Returns a tree with `item` added at the front. Amortized O(1).
    #[must_use]
    pub fn prepend(&self, item: T) -> Self {
        let measure = P::measure(&item);
        Self::from_root(MeasuredNode::concat(
            MeasuredNode::leaf(item, measure),
            Arc::clone(&self.root),
            &P::combine,
        ))
    }

    /// Returns a tree with `item` added at the back. Amortized O(1).
    #[must_use]
    pub fn append(&self, item: T) -> Self {
        let measure = P::measure(&item);
        Self::from_root(MeasuredNode::concat(
            Arc::clone(&self.root),
            MeasuredNode::leaf(item, measure),
            &P::combine,
        ))
    }

    /// Splits off the first element, returning it together with the remaining tree, or `None` when
    /// empty. The receiver is unchanged.
    #[must_use]
    pub fn try_view_left(&self) -> Option<(T, Self)> {
        let first = self.front()?.clone();
        Some((first, self.split_at_index_unchecked(1).right))
    }

    /// Splits off the last element, returning it together with the remaining tree, or `None` when
    /// empty. The receiver is unchanged.
    #[must_use]
    pub fn try_view_right(&self) -> Option<(T, Self)> {
        let last = self.back()?.clone();
        Some((last, self.split_at_index_unchecked(self.len() - 1).left))
    }

    /// Finds the first element whose inclusive prefix measure satisfies `predicate` and returns the
    /// elements before it, the element itself, and the elements after it.
    ///
    /// Returns `None` when no prefix satisfies `predicate`. Unlike [`Self::split`], which always
    /// produces two halves, this distinguishes "found here" from "not found" and hands back the
    /// located element. O(log n).
    #[must_use]
    pub fn try_split_find<F>(&self, predicate: F) -> Option<(Self, T, Self)>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let index = self.boundary_index(predicate)?;
        let item = self.root.get(index)?.clone();
        let left = self.split_at_index_unchecked(index).left;
        let right = self.split_at_index_unchecked(index + 1).right;
        Some((left, item, right))
    }

    /// Reports where the first element satisfying `predicate` sits, without splitting the tree.
    ///
    /// The returned [`LocateResult`] carries the element's index, the combined measure of
    /// everything before it, and the element itself. When no prefix satisfies `predicate`, the
    /// result describes the end position and its `item` is `None`. O(log n).
    #[must_use]
    pub fn try_locate<F>(&self, predicate: F) -> LocateResult<T, P::Measure>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let Some(index) = self.boundary_index(predicate) else {
            return LocateResult {
                index: self.len(),
                measure_before: self.measure.clone(),
                item: None,
            };
        };

        LocateResult {
            index,
            measure_before: MeasuredNode::prefix_measure(
                &self.root,
                index,
                &P::empty(),
                &P::combine,
            ),
            item: self.root.get(index).cloned(),
        }
    }
}

impl<T, PFirst, PSecond> FingerTree<T, ProductMeasure<T, PFirst, PSecond>>
where
    PFirst: MeasurePolicy<T>,
    PSecond: MeasurePolicy<T>,
{
    /// Splits by a predicate over the first component of the product measure only.
    ///
    /// A convenience over [`Self::split`] for the common case where a tree carries two measures —
    /// say size and sum — and the caller wants to search by just one of them.
    #[must_use]
    pub fn split_by_first<F>(
        &self,
        mut predicate: F,
    ) -> MeasuredSplit<T, ProductMeasure<T, PFirst, PSecond>>
    where
        F: FnMut(&PFirst::Measure) -> bool,
    {
        self.split(|measure| predicate(&measure.first))
    }

    /// Splits by a predicate over the second component of the product measure only.
    #[must_use]
    pub fn split_by_second<F>(
        &self,
        mut predicate: F,
    ) -> MeasuredSplit<T, ProductMeasure<T, PFirst, PSecond>>
    where
        F: FnMut(&PSecond::Measure) -> bool,
    {
        self.split(|measure| predicate(&measure.second))
    }
}

impl<T, PFirst, PSecond> FingerTree<T, ProductMeasure<T, PFirst, PSecond>>
where
    T: Clone,
    PFirst: MeasurePolicy<T>,
    PSecond: MeasurePolicy<T>,
{
    /// [`Self::try_split_find`] driven by the first component of the product measure only.
    #[must_use]
    pub fn try_split_find_by_first<F>(&self, mut predicate: F) -> Option<(Self, T, Self)>
    where
        F: FnMut(&PFirst::Measure) -> bool,
    {
        self.try_split_find(|measure| predicate(&measure.first))
    }

    /// [`Self::try_split_find`] driven by the second component of the product measure only.
    #[must_use]
    pub fn try_split_find_by_second<F>(&self, mut predicate: F) -> Option<(Self, T, Self)>
    where
        F: FnMut(&PSecond::Measure) -> bool,
    {
        self.try_split_find(|measure| predicate(&measure.second))
    }

    /// [`Self::try_locate`] driven by the first component of the product measure only.
    #[must_use]
    pub fn try_locate_by_first<F>(
        &self,
        mut predicate: F,
    ) -> LocateResult<T, MeasurePair<PFirst::Measure, PSecond::Measure>>
    where
        F: FnMut(&PFirst::Measure) -> bool,
    {
        self.try_locate(|measure| predicate(&measure.first))
    }

    /// [`Self::try_locate`] driven by the second component of the product measure only.
    #[must_use]
    pub fn try_locate_by_second<F>(
        &self,
        mut predicate: F,
    ) -> LocateResult<T, MeasurePair<PFirst::Measure, PSecond::Measure>>
    where
        F: FnMut(&PSecond::Measure) -> bool,
    {
        self.try_locate(|measure| predicate(&measure.second))
    }
}

impl<T> FingerTree<T, MaxMeasure>
where
    T: Clone + Ord,
{
    /// Borrows the maximum element, or `None` when empty.
    ///
    /// O(1): the maximum is the tree's cached root measure, so no search is needed.
    #[must_use]
    pub fn try_peek_max(&self) -> Option<&T> {
        self.measure().as_ref()
    }

    /// Removes one occurrence of the maximum element, returning it and the remaining tree.
    ///
    /// Returns `None` when empty. When several elements tie for the maximum, the leftmost is
    /// removed. O(log n).
    #[must_use]
    pub fn try_extract_max(&self) -> Option<(T, Self)> {
        let target = self.measure().as_ref()?;
        let (left, found, right) =
            self.try_split_find(|measure| measure.as_ref().is_some_and(|value| value >= target))?;
        Some((found, left.concat(&right)))
    }
}

impl<T> FingerTree<T, MinMeasure>
where
    T: Clone + Ord,
{
    /// Borrows the minimum element, or `None` when empty.
    ///
    /// O(1): the minimum is the tree's cached root measure, so no search is needed.
    #[must_use]
    pub fn try_peek_min(&self) -> Option<&T> {
        self.measure().as_ref()
    }

    /// Removes one occurrence of the minimum element, returning it and the remaining tree.
    ///
    /// Returns `None` when empty. When several elements tie for the minimum, the leftmost is
    /// removed. O(log n).
    #[must_use]
    pub fn try_extract_min(&self) -> Option<(T, Self)> {
        let target = self.measure().as_ref()?;
        let (left, found, right) =
            self.try_split_find(|measure| measure.as_ref().is_some_and(|value| value <= target))?;
        Some((found, left.concat(&right)))
    }
}

impl<T> FingerTree<T, KeyMeasure<T>>
where
    T: Clone + Ord,
{
    /// Splits an ascending tree so that the left half holds every element strictly below `key`.
    ///
    /// Elements equivalent to `key` land in the right half, so this is the position where `key`
    /// would be inserted before its equals. O(log n). Assumes the elements are in ascending order.
    #[must_use]
    pub fn split_by_lower_bound(&self, key: &T) -> MeasuredSplit<T, KeyMeasure<T>> {
        self.split(|measure| measure.as_ref().is_some_and(|last_key| last_key >= key))
    }

    /// Splits an ascending tree so that the left half holds every element up to and including
    /// those equivalent to `key`.
    ///
    /// Together with [`Self::split_by_lower_bound`], this brackets the run of elements equivalent
    /// to `key`. O(log n). Assumes the elements are in ascending order.
    #[must_use]
    pub fn split_by_upper_bound(&self, key: &T) -> MeasuredSplit<T, KeyMeasure<T>> {
        self.split(|measure| measure.as_ref().is_some_and(|last_key| last_key > key))
    }
}

impl<T> FingerTree<T, OrderStatisticMeasure<T>>
where
    T: Clone + Ord,
{
    /// Splits an ascending tree so that the left half holds every element strictly below `key`.
    ///
    /// The order-statistic measure also caches counts, so the left half's length is the rank of
    /// `key`. O(log n). Assumes the elements are in ascending order.
    #[must_use]
    pub fn split_by_lower_bound(&self, key: &T) -> MeasuredSplit<T, OrderStatisticMeasure<T>> {
        self.split(|measure| measure.key.as_ref().is_some_and(|last_key| last_key >= key))
    }

    /// Splits an ascending tree so that the left half holds every element up to and including
    /// those equivalent to `key`.
    ///
    /// The left half's length is therefore the number of elements at most `key`. O(log n). Assumes
    /// the elements are in ascending order.
    #[must_use]
    pub fn split_by_upper_bound(&self, key: &T) -> MeasuredSplit<T, OrderStatisticMeasure<T>> {
        self.split(|measure| measure.key.as_ref().is_some_and(|last_key| last_key > key))
    }
}

impl<T> FingerTree<T, SizeAndSumMeasure<T>>
where
    T: Add<Output = T> + Clone + Default + PartialOrd,
{
    /// Splits at the first position where the running total of elements exceeds `threshold`.
    ///
    /// The left half is the longest prefix whose sum is at most `threshold`. O(log n).
    #[must_use]
    pub fn split_by_cumulative_weight(
        &self,
        threshold: &T,
    ) -> MeasuredSplit<T, SizeAndSumMeasure<T>> {
        self.split_by_second(|sum| sum > threshold)
    }

    /// Selects the element that carries `threshold` in a weighted-choice scan, with its index.
    ///
    /// This is the standard weighted-sampling step: treating each element as a weight, it returns
    /// the first element whose inclusive running total exceeds `threshold`, so drawing `threshold`
    /// uniformly from `0..total` picks each element in proportion to its weight. Returns `None`
    /// when `threshold` is at least the total. O(log n) rather than the usual linear scan.
    #[must_use]
    pub fn try_select_by_cumulative_weight(&self, threshold: &T) -> Option<(T, usize)> {
        let located = self.try_locate_by_second(|sum| sum > threshold);
        located
            .item
            .map(|item| (item, located.measure_before.first))
    }
}

impl<T> FingerTree<T, SizeAndMaxMeasure<T>>
where
    T: Clone + Ord,
{
    /// Borrows the maximum element, or `None` when empty. O(1) from the cached root measure.
    #[must_use]
    pub fn try_peek_max(&self) -> Option<&T> {
        self.measure().second.as_ref()
    }

    /// Removes the leftmost occurrence of the maximum element, returning it and the remaining tree.
    /// Returns `None` when empty. O(log n).
    #[must_use]
    pub fn try_extract_max(&self) -> Option<(T, Self)> {
        let target = self.measure().second.as_ref()?;
        let (left, found, right) = self.try_split_find_by_second(|measure| {
            measure.as_ref().is_some_and(|value| value >= target)
        })?;
        Some((found, left.concat(&right)))
    }
}

impl<T> FingerTree<T, SizeAndMinMeasure<T>>
where
    T: Clone + Ord,
{
    /// Borrows the minimum element, or `None` when empty. O(1) from the cached root measure.
    #[must_use]
    pub fn try_peek_min(&self) -> Option<&T> {
        self.measure().second.as_ref()
    }

    /// Removes the leftmost occurrence of the minimum element, returning it and the remaining tree.
    /// Returns `None` when empty. O(log n).
    #[must_use]
    pub fn try_extract_min(&self) -> Option<(T, Self)> {
        let target = self.measure().second.as_ref()?;
        let (left, found, right) = self.try_split_find_by_second(|measure| {
            measure.as_ref().is_some_and(|value| value <= target)
        })?;
        Some((found, left.concat(&right)))
    }
}

/// Borrowing iterator over a [`FingerTree`] in sequence order.
pub struct Iter<'a, T, M> {
    stack: Vec<&'a MeasuredNode<T, M>>,
    remaining: usize,
}

impl<'a, T, M> Iter<'a, T, M> {
    fn new(root: &'a Arc<MeasuredNode<T, M>>) -> Self {
        let remaining = match root.as_ref() {
            MeasuredNode::Empty => 0,
            MeasuredNode::Leaf { .. } => 1,
            MeasuredNode::Node { len, .. } => *len,
        };
        Self {
            stack: vec![root.as_ref()],
            remaining,
        }
    }
}

impl<'a, T, M> Iterator for Iter<'a, T, M> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(tree) = self.stack.pop() {
            match tree {
                MeasuredNode::Empty => {}
                MeasuredNode::Leaf { item, .. } => {
                    self.remaining -= 1;
                    return Some(item);
                }
                MeasuredNode::Node { left, right, .. } => {
                    self.stack.push(right);
                    self.stack.push(left);
                }
            }
        }

        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T, M> ExactSizeIterator for Iter<'_, T, M> {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn size_measure_tracks_count() {
        let tree: FingerTree<_, SizeMeasure> = [10, 20, 30].into_iter().collect();
        let appended = tree.append(40);

        assert_eq!(*tree.measure(), 3);
        assert_eq!(*appended.measure(), 4);
        assert_eq!(tree.get(1), Some(&20));
        assert_eq!(tree.prefix_measure(2), Some(2));
        assert_eq!(tree.prefix_measure(4), None);
        assert_eq!(tree.to_vec(), vec![10, 20, 30]);
        tree.validate_invariants();
        appended.validate_invariants();
    }

    #[test]
    fn split_and_locate_use_prefix_measure() {
        let tree: FingerTree<_, SumMeasure<i32>> = [2, 3, 5, 7].into_iter().collect();
        let split = tree.split(|sum| *sum >= 6);
        let located = tree.try_locate(|sum| *sum >= 6);

        assert_eq!(split.left.to_vec(), vec![2, 3]);
        assert_eq!(split.right.to_vec(), vec![5, 7]);
        assert_eq!(tree.prefix_measure(3), Some(10));
        assert_eq!(located.index, 2);
        assert_eq!(located.measure_before, 5);
        assert_eq!(located.item, Some(5));
        split.left.validate_invariants();
        split.right.validate_invariants();
    }

    #[test]
    fn max_and_min_measures_are_monoids() {
        let max_tree: FingerTree<_, MaxMeasure> = [3, 1, 4, 2].into_iter().collect();
        let min_tree: FingerTree<_, MinMeasure> = [3, 1, 4, 2].into_iter().collect();

        assert_eq!(max_tree.measure(), &Some(4));
        assert_eq!(min_tree.measure(), &Some(1));
        max_tree.validate_invariants();
        min_tree.validate_invariants();
    }

    #[test]
    fn order_statistic_measure_tracks_count_and_last_key() {
        let tree: FingerTree<_, OrderStatisticMeasure<i32>> = [1, 3, 3, 7].into_iter().collect();
        let located = tree.try_locate(|measure| measure.key.as_ref().is_some_and(|key| *key >= 3));

        assert_eq!(
            tree.measure(),
            &RankedKey {
                count: 4,
                key: Some(7)
            }
        );
        assert_eq!(tree.prefix_measure(2).unwrap().count, 2);
        assert_eq!(located.index, 1);
        assert_eq!(located.measure_before.count, 1);
        assert_eq!(located.item, Some(3));
        tree.validate_invariants();
    }

    #[test]
    fn key_measure_splits_sorted_sequences_by_bound() {
        let tree: FingerTree<_, KeyMeasure<i32>> = [1, 2, 2, 4].into_iter().collect();
        let lower = tree.split_by_lower_bound(&2);
        let upper = tree.split_by_upper_bound(&2);

        assert_eq!(tree.measure(), &Some(4));
        assert_eq!(lower.left.to_vec(), vec![1]);
        assert_eq!(lower.right.to_vec(), vec![2, 2, 4]);
        assert_eq!(upper.left.to_vec(), vec![1, 2, 2]);
        assert_eq!(upper.right.to_vec(), vec![4]);
        lower.left.validate_invariants();
        lower.right.validate_invariants();
        upper.left.validate_invariants();
        upper.right.validate_invariants();
    }

    #[test]
    fn order_statistic_measure_exposes_bound_splits() {
        let tree: FingerTree<_, OrderStatisticMeasure<i32>> = [1, 2, 2, 4].into_iter().collect();
        let lower = tree.split_by_lower_bound(&2);
        let upper = tree.split_by_upper_bound(&2);

        assert_eq!(lower.left.measure().count, 1);
        assert_eq!(lower.left.to_vec(), vec![1]);
        assert_eq!(lower.right.to_vec(), vec![2, 2, 4]);
        assert_eq!(upper.left.measure().count, 3);
        assert_eq!(upper.left.to_vec(), vec![1, 2, 2]);
        assert_eq!(upper.right.to_vec(), vec![4]);
        lower.left.validate_invariants();
        lower.right.validate_invariants();
        upper.left.validate_invariants();
        upper.right.validate_invariants();
    }

    #[test]
    fn product_measure_composes_size_and_sum() {
        let tree: FingerTree<_, SizeAndSumMeasure<i32>> = [5, 1, 4, 2].into_iter().collect();
        let by_size = tree.split_by_first(|count| *count > 2);
        let by_weight = tree.split_by_cumulative_weight(&6);
        let selected = tree.try_select_by_cumulative_weight(&7);

        assert_eq!(
            tree.measure(),
            &MeasurePair {
                first: 4,
                second: 12
            }
        );
        assert_eq!(by_size.left.to_vec(), vec![5, 1]);
        assert_eq!(by_size.right.to_vec(), vec![4, 2]);
        assert_eq!(by_weight.left.to_vec(), vec![5, 1]);
        assert_eq!(by_weight.right.to_vec(), vec![4, 2]);
        assert_eq!(selected, Some((4, 2)));
        assert_eq!(tree.prefix_measure(3).unwrap().second, 10);
        by_size.left.validate_invariants();
        by_size.right.validate_invariants();
        by_weight.left.validate_invariants();
        by_weight.right.validate_invariants();
    }

    #[test]
    fn max_min_helpers_extract_first_extremum() {
        let max_tree: FingerTree<_, MaxMeasure> = [3, 9, 1, 9, 4].into_iter().collect();
        let min_tree: FingerTree<_, MinMeasure> = [3, 1, 4, 1, 5].into_iter().collect();

        let (max, max_rest) = max_tree.try_extract_max().unwrap();
        let (min, min_rest) = min_tree.try_extract_min().unwrap();

        assert_eq!(max_tree.try_peek_max(), Some(&9));
        assert_eq!(max, 9);
        assert_eq!(max_rest.to_vec(), vec![3, 1, 9, 4]);
        assert_eq!(max_rest.measure(), &Some(9));
        assert_eq!(min_tree.try_peek_min(), Some(&1));
        assert_eq!(min, 1);
        assert_eq!(min_rest.to_vec(), vec![3, 4, 1, 5]);
        assert_eq!(min_rest.measure(), &Some(1));
        max_rest.validate_invariants();
        min_rest.validate_invariants();
    }

    #[test]
    fn product_priority_helpers_keep_positional_measure() {
        let max_tree: FingerTree<_, SizeAndMaxMeasure<i32>> = [3, 9, 1, 9, 4].into_iter().collect();
        let min_tree: FingerTree<_, SizeAndMinMeasure<i32>> = [3, 1, 4, 1, 5].into_iter().collect();

        let (max, max_rest) = max_tree.try_extract_max().unwrap();
        let (min, min_rest) = min_tree.try_extract_min().unwrap();

        assert_eq!(max_tree.try_peek_max(), Some(&9));
        assert_eq!(
            max_tree.measure(),
            &MeasurePair {
                first: 5,
                second: Some(9)
            }
        );
        assert_eq!(max, 9);
        assert_eq!(max_rest.to_vec(), vec![3, 1, 9, 4]);
        assert_eq!(max_rest.measure().first, 4);
        assert_eq!(max_rest.measure().second, Some(9));
        assert_eq!(min_tree.try_peek_min(), Some(&1));
        assert_eq!(min, 1);
        assert_eq!(min_rest.to_vec(), vec![3, 4, 1, 5]);
        assert_eq!(min_rest.measure().first, 4);
        assert_eq!(min_rest.measure().second, Some(1));
        max_rest.validate_invariants();
        min_rest.validate_invariants();
    }

    #[test]
    fn measured_tree_splits_share_subtrees_and_cache_measures() {
        let tree: FingerTree<_, SizeMeasure> = (0..256).collect();
        let split = tree.split_at_index(96).unwrap();
        let joined = split.left.concat(&split.right);

        assert_eq!(split.left.len(), 96);
        assert_eq!(split.right.len(), 160);
        assert_eq!(joined.to_vec(), tree.to_vec());
        assert!(tree.shared_node_count_with(&split.left) > 64);
        assert!(tree.shared_node_count_with(&split.right) > 100);
        assert!(tree.tree_depth() < 24);
        split.left.validate_invariants();
        split.right.validate_invariants();
        joined.validate_invariants();
    }

    #[test]
    fn randomized_sum_measure_splits_match_vector_prefixes() {
        let mut rng = 0xdecaf_bad5eed_u64;
        let mut values = Vec::new();
        for _ in 0..300 {
            rng = rng.wrapping_mul(6364136223846793005).wrapping_add(1);
            values.push(((rng >> 32) % 9 + 1) as i32);
        }

        let tree: FingerTree<_, SumMeasure<i32>> = values.iter().copied().collect();
        for threshold in [1, 7, 37, 111, 777, *tree.measure() + 1] {
            let located = tree.try_locate(|sum| *sum >= threshold);
            let mut prefix = 0;
            let expected = values.iter().position(|value| {
                prefix += *value;
                prefix >= threshold
            });

            assert_eq!(located.index, expected.unwrap_or(values.len()));
            assert_eq!(located.item, expected.map(|index| values[index]));
        }

        tree.validate_invariants();
    }
}
