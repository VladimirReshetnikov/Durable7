//! The measured 2-3 finger tree that most of this crate is built on, plus the standard measures.
//!
//! [`FingerTree`] is a persistent Hinze–Paterson finger tree: 1–4-item digits at both ends of
//! every level, 2-3 nodes below the surface, and each level's middle subtree held behind a
//! *memoized suspension* — the paper's lazy tree realized in a strict language, exactly as the
//! C#, C++, and C workspaces realize it. Because the measure of a subtree is cached and available
//! without descending into it, a single generic split can answer any question the measure can
//! express — "the 100th element", "the first prefix whose sum exceeds 40", "the leftmost element
//! whose key is at least `k`" — in O(log n).
//!
//! # Bounds
//!
//! With O(1) policy operations: [`front`](FingerTree::front) and [`back`](FingerTree::back) are
//! O(1) worst-case digit reads; [`prepend`](FingerTree::prepend), [`append`](FingerTree::append),
//! and the endpoint views are O(1) amortized and O(log n) worst-case per call;
//! [`concat`](FingerTree::concat) is O(log(min(m, n))) amortized; splitting, indexing, and
//! locating are O(log n); iteration is O(n). The amortized bounds hold under fully persistent
//! branching histories, not merely ephemeral linear use, because a forced suspension memoizes its
//! result in a cell shared by every version that references it: work deferred by one version and
//! forced by another is never repeated.
//!
//! # Laziness mechanics
//!
//! Laziness is load-bearing in exactly two places and eager everywhere else. A digit overflow on
//! `prepend`/`append` pushes a 2-3 node into the middle *inside* a suspension, and `concat`
//! recurses into the two middles *inside* a suspension; every size (and every 2-3 node's measure)
//! is computed strictly at construction, so index arithmetic never forces structure it does not
//! enter. The suspensions are *defunctionalized*: the deferred operations are stored as data and
//! interpreted by an explicit-stack force loop, so the Θ(n)-deep pending chains that organic
//! append loops build force without recursion, and the suspension's `Drop` unlinks such chains
//! iteratively for the same reason. Forcing publishes through a [`std::sync::OnceLock`], so
//! concurrent forces race benignly — a losing thread discards its duplicate result — and a
//! deferred computation that panics publishes nothing and may be retried, so policy failures keep
//! every retained version valid. A deep node's total measure is memoized separately and computing
//! it may force the middle spine, which is why [`measure`](FingerTree::measure) is O(1) amortized
//! rather than worst-case.
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
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};

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
/// The overall maximum is the root measure, so peeking is O(1) amortized and extraction is
/// O(log n) — with no requirement that the elements be stored in sorted order.
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
/// The mirror image of [`MaxMeasure`]: peeking the minimum is O(1) amortized and extraction is
/// O(log n).
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
/// Counts and tracks the maximum at once: positional indexing plus O(1) amortized maximum access.
pub type SizeAndMaxMeasure<T> = ProductMeasure<T, SizeMeasure, MaxMeasure>;
/// Counts and tracks the minimum at once: positional indexing plus O(1) amortized minimum access.
pub type SizeAndMinMeasure<T> = ProductMeasure<T, SizeMeasure, MinMeasure>;

/// A persistent sequence that caches the measure defined by `P` at every node.
///
/// Every operation returns a new tree and leaves the receiver valid; versions share their unchanged
/// nodes through [`Arc`], so cloning is O(1) and an update copies only one root-to-leaf path.
/// Because each node's measure is available without descending into it, the single
/// [`split`](Self::split) operation answers any monotone question the measure can express in
/// O(log n), while access at either end is O(1) worst-case for reads and O(1) amortized for
/// updates — bounds that survive persistent branching because deferred spine work is memoized in
/// suspension cells shared by every version.
///
/// A tree is `Send + Sync` whenever `T` is, so one snapshot can be read concurrently without
/// locking.
pub struct FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    root: Arc<Tree<T, P::Measure>>,
    measure: OnceLock<P::Measure>,
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

// --- internal representation -------------------------------------------------------------------
//
// An `Item` is a user element at the surface level and a 2-3 `Node23` below it. Both carry strict
// sizes and strict measures, so no size or item-measure read ever forces structure; the only lazy
// measure is a deep node's memoized total. The middle of every `Deep` sits behind a `Susp`, a
// defunctionalized memoized suspension.

/// A stored element together with its eagerly computed measure.
struct MeasuredElement<T, M> {
    value: T,
    measure: M,
}

/// One digit slot: a user element at level zero, a 2-3 node below.
enum Item<T, M> {
    Leaf(Arc<MeasuredElement<T, M>>),
    Node(Arc<Node23<T, M>>),
}

impl<T, M> Clone for Item<T, M> {
    fn clone(&self) -> Self {
        match self {
            Self::Leaf(element) => Self::Leaf(Arc::clone(element)),
            Self::Node(node) => Self::Node(Arc::clone(node)),
        }
    }
}

/// A 2-3 node: strict size and measure, plus cached first and last descendant elements.
struct Node23<T, M> {
    children: Vec<Item<T, M>>,
    size: usize,
    measure: M,
    first: Arc<MeasuredElement<T, M>>,
    last: Arc<MeasuredElement<T, M>>,
}

/// A deferred spine operation, stored as data so forcing can interpret arbitrarily long chains
/// with an explicit stack instead of recursing once per link.
enum Pending<T, M> {
    /// Push an overflow node onto the front of the suspended middle.
    PushFront(Item<T, M>, Arc<Susp<T, M>>),
    /// Push an overflow node onto the back of the suspended middle.
    PushBack(Arc<Susp<T, M>>, Item<T, M>),
    /// Concatenate two suspended middles around already-regrouped 2-3 nodes.
    Concat(Arc<Susp<T, M>>, Vec<Item<T, M>>, Arc<Susp<T, M>>),
}

impl<T, M> Clone for Pending<T, M> {
    fn clone(&self) -> Self {
        match self {
            Self::PushFront(item, inner) => Self::PushFront(item.clone(), Arc::clone(inner)),
            Self::PushBack(inner, item) => Self::PushBack(Arc::clone(inner), item.clone()),
            Self::Concat(left, between, right) => {
                Self::Concat(Arc::clone(left), between.clone(), Arc::clone(right))
            }
        }
    }
}

/// A memoized suspension of a subtree.
///
/// Forcing snapshots the pending operation, computes outside the lock, publishes through the
/// `OnceLock` (a lost race discards the duplicate result), and only then drops the pending
/// payload — so a panicking policy leaves every unforced cell pending and retryable, and a cell
/// whose pending is gone always has a published value.
struct Susp<T, M> {
    forced: OnceLock<Arc<Tree<T, M>>>,
    pending: Mutex<Option<Pending<T, M>>>,
}

/// The finger tree at one level: items are elements at the surface, 2-3 nodes below.
enum Tree<T, M> {
    Empty,
    Single(Item<T, M>),
    Deep(Deep<T, M>),
}

/// Digits at both ends around a suspended middle of one-level-deeper nodes.
///
/// `size` and `middle_size` are strict — every construction site knows them arithmetically — so no
/// size read ever forces the middle. The node's total measure is memoized on first read.
struct Deep<T, M> {
    size: usize,
    prefix: Vec<Item<T, M>>,
    middle: Arc<Susp<T, M>>,
    middle_size: usize,
    suffix: Vec<Item<T, M>>,
    measure: OnceLock<M>,
}

impl<T, M> Susp<T, M> {
    /// Wraps an already-built subtree; nothing is deferred.
    fn ready(tree: Arc<Tree<T, M>>) -> Arc<Self> {
        let forced = OnceLock::new();
        let _ = forced.set(tree);
        Arc::new(Self {
            forced,
            pending: Mutex::new(None),
        })
    }

    /// Defers `pending` until the suspension is first forced.
    fn deferred(pending: Pending<T, M>) -> Arc<Self> {
        Arc::new(Self {
            forced: OnceLock::new(),
            pending: Mutex::new(Some(pending)),
        })
    }

    fn is_forced(&self) -> bool {
        self.forced.get().is_some()
    }

    fn lock_pending(&self) -> MutexGuard<'_, Option<Pending<T, M>>> {
        match self.pending.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        }
    }
}

impl<T, M> Susp<T, M>
where
    M: Clone,
{
    /// Forces the suspension, interpreting the pending chain iteratively with an explicit stack.
    ///
    /// Organic endpoint loops build Θ(n)-deep pending chains; a recursive force would take one
    /// call frame per link and overflow the stack, which is why the deferred operations are data.
    /// Each link's result is published before its pending payload is dropped, so every version
    /// sharing a cell reads the same forced subtree and deferred work is never repeated.
    fn force<'a, C>(this: &'a Arc<Self>, combine: &C) -> &'a Arc<Tree<T, M>>
    where
        C: Fn(&M, &M) -> M,
    {
        if let Some(tree) = this.forced.get() {
            return tree;
        }

        let mut stack: Vec<Arc<Self>> = vec![Arc::clone(this)];
        while let Some(top) = stack.last().cloned() {
            if top.forced.get().is_some() {
                stack.pop();
                // Publication happened first, so dropping the payload cannot lose work. The
                // chain link it holds is the previous suspension, already forced and cleared.
                *top.lock_pending() = None;
                continue;
            }

            // Snapshot rather than take: a panicking policy below must leave the cell pending.
            let Some(pending) = top.lock_pending().clone() else {
                // Another force published and cleared between the two checks; re-examine.
                continue;
            };
            let unforced_inner = match &pending {
                Pending::PushFront(_, inner) | Pending::PushBack(inner, _) => {
                    (!inner.is_forced()).then(|| Arc::clone(inner))
                }
                Pending::Concat(left, _, right) => {
                    if !left.is_forced() {
                        Some(Arc::clone(left))
                    } else if !right.is_forced() {
                        Some(Arc::clone(right))
                    } else {
                        None
                    }
                }
            };
            if let Some(inner) = unforced_inner {
                stack.push(inner);
                continue;
            }

            let result = match pending {
                Pending::PushFront(item, inner) => {
                    cons(combine, item, forced_value(&inner))
                }
                Pending::PushBack(inner, item) => snoc(combine, forced_value(&inner), item),
                Pending::Concat(left, between, right) => {
                    app3(combine, forced_value(&left), between, forced_value(&right))
                }
            };
            // A lost race discards the duplicate result; the published tree wins everywhere.
            let _ = top.forced.set(result);
        }

        this.forced
            .get()
            .expect("a drained force stack has published its root suspension")
    }
}

fn forced_value<T, M>(susp: &Arc<Susp<T, M>>) -> &Arc<Tree<T, M>> {
    susp.forced
        .get()
        .expect("an interpreted suspension's inputs were forced first")
}

impl<T, M> Drop for Susp<T, M> {
    /// Unlinks the pending chain iteratively.
    ///
    /// An unforced append-built spine is a Θ(n)-long chain of suspensions each owning the next
    /// through its pending payload; the default recursive drop would take one call frame per link
    /// and overflow the stack.
    fn drop(&mut self) {
        let first = match self.pending.get_mut() {
            Ok(pending) => pending.take(),
            Err(poisoned) => poisoned.into_inner().take(),
        };
        let mut stack: Vec<Arc<Susp<T, M>>> = Vec::new();
        push_pending_links(first, &mut stack);
        while let Some(link) = stack.pop() {
            if let Some(mut sole) = Arc::into_inner(link) {
                let pending = match sole.pending.get_mut() {
                    Ok(pending) => pending.take(),
                    Err(poisoned) => poisoned.into_inner().take(),
                };
                push_pending_links(pending, &mut stack);
                // `sole` drops here with its pending already taken, so its own drop is shallow.
            }
        }
    }
}

fn push_pending_links<T, M>(pending: Option<Pending<T, M>>, stack: &mut Vec<Arc<Susp<T, M>>>) {
    match pending {
        None => {}
        Some(Pending::PushFront(_, inner)) | Some(Pending::PushBack(inner, _)) => {
            stack.push(inner);
        }
        Some(Pending::Concat(left, _, right)) => {
            stack.push(left);
            stack.push(right);
        }
    }
}

// --- item and digit helpers --------------------------------------------------------------------

fn item_size<T, M>(item: &Item<T, M>) -> usize {
    match item {
        Item::Leaf(_) => 1,
        Item::Node(node) => node.size,
    }
}

fn item_measure<T, M>(item: &Item<T, M>) -> &M {
    match item {
        Item::Leaf(element) => &element.measure,
        Item::Node(node) => &node.measure,
    }
}

fn item_first<T, M>(item: &Item<T, M>) -> &Arc<MeasuredElement<T, M>> {
    match item {
        Item::Leaf(element) => element,
        Item::Node(node) => &node.first,
    }
}

fn item_last<T, M>(item: &Item<T, M>) -> &Arc<MeasuredElement<T, M>> {
    match item {
        Item::Leaf(element) => element,
        Item::Node(node) => &node.last,
    }
}

fn item_at<T, M>(item: &Item<T, M>, mut index: usize) -> &T {
    let mut current = item;
    'descend: loop {
        match current {
            Item::Leaf(element) => {
                debug_assert_eq!(index, 0);
                return &element.value;
            }
            Item::Node(node) => {
                for child in &node.children {
                    let child_size = item_size(child);
                    if index < child_size {
                        current = child;
                        continue 'descend;
                    }
                    index -= child_size;
                }
                unreachable!("an item index stays within the item's size");
            }
        }
    }
}

/// The digit an item pulled up from a middle contributes: node children, or the item alone.
fn item_digit<T, M>(item: Item<T, M>) -> Vec<Item<T, M>> {
    match item {
        Item::Node(node) => node.children.clone(),
        leaf => vec![leaf],
    }
}

fn digit_size<T, M>(digit: &[Item<T, M>]) -> usize {
    digit.iter().map(item_size).sum()
}

fn digit_measure<T, M, C>(combine: &C, digit: &[Item<T, M>]) -> M
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    let mut measure = item_measure(&digit[0]).clone();
    for item in &digit[1..] {
        measure = combine(&measure, item_measure(item));
    }
    measure
}

/// Builds a 2-3 node, computing its size and measure strictly and caching its end elements.
fn make_node<T, M, C>(combine: &C, children: Vec<Item<T, M>>) -> Item<T, M>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    debug_assert!((2..=3).contains(&children.len()));
    let size = digit_size(&children);
    let measure = digit_measure(combine, &children);
    let first = Arc::clone(item_first(&children[0]));
    let last = Arc::clone(item_last(
        children.last().expect("a 2-3 node has children"),
    ));
    Item::Node(Arc::new(Node23 {
        children,
        size,
        measure,
        first,
        last,
    }))
}

// --- tree helpers ------------------------------------------------------------------------------

fn tree_size<T, M>(tree: &Tree<T, M>) -> usize {
    match tree {
        Tree::Empty => 0,
        Tree::Single(item) => item_size(item),
        Tree::Deep(deep) => deep.size,
    }
}

/// The tree's total measure, memoized at each deep node. Forces the middles it enters.
fn tree_measure<'a, T, M, C>(combine: &C, tree: &'a Tree<T, M>) -> Option<&'a M>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match tree {
        Tree::Empty => None,
        Tree::Single(item) => Some(item_measure(item)),
        Tree::Deep(deep) => Some(deep.measure.get_or_init(|| {
            let mut measure = digit_measure(combine, &deep.prefix);
            let middle = Susp::force(&deep.middle, combine);
            if let Some(middle_measure) = tree_measure(combine, middle) {
                measure = combine(&measure, middle_measure);
            }
            combine(&measure, &digit_measure(combine, &deep.suffix))
        })),
    }
}

fn deep_tree<T, M>(
    prefix: Vec<Item<T, M>>,
    middle: Arc<Susp<T, M>>,
    middle_size: usize,
    suffix: Vec<Item<T, M>>,
) -> Arc<Tree<T, M>> {
    Arc::new(Tree::Deep(Deep {
        size: digit_size(&prefix) + middle_size + digit_size(&suffix),
        prefix,
        middle,
        middle_size,
        suffix,
        measure: OnceLock::new(),
    }))
}

fn empty_tree<T, M>() -> Arc<Tree<T, M>> {
    Arc::new(Tree::Empty)
}

fn empty_middle<T, M>() -> Arc<Susp<T, M>> {
    Susp::ready(empty_tree())
}

fn digit_to_tree<T, M>(digit: Vec<Item<T, M>>) -> Arc<Tree<T, M>> {
    debug_assert!(!digit.is_empty());
    if digit.len() == 1 {
        let mut digit = digit;
        return Arc::new(Tree::Single(digit.pop().expect("a one-item digit")));
    }

    let mut prefix = digit;
    let suffix = prefix.split_off(prefix.len() / 2);
    deep_tree(prefix, empty_middle(), 0, suffix)
}

// --- endpoint operations -----------------------------------------------------------------------

fn cons<T, M, C>(combine: &C, item: Item<T, M>, tree: &Arc<Tree<T, M>>) -> Arc<Tree<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match tree.as_ref() {
        Tree::Empty => Arc::new(Tree::Single(item)),
        Tree::Single(existing) => deep_tree(
            vec![item],
            empty_middle(),
            0,
            vec![existing.clone()],
        ),
        Tree::Deep(deep) if deep.prefix.len() < 4 => {
            let mut prefix = Vec::with_capacity(deep.prefix.len() + 1);
            prefix.push(item);
            prefix.extend(deep.prefix.iter().cloned());
            deep_tree(
                prefix,
                Arc::clone(&deep.middle),
                deep.middle_size,
                deep.suffix.clone(),
            )
        }
        Tree::Deep(deep) => {
            let overflow = make_node(combine, deep.prefix[1..].to_vec());
            let overflow_size = item_size(&overflow);
            deep_tree(
                vec![item, deep.prefix[0].clone()],
                // The push into the middle is the suspended step Hinze–Paterson amortization
                // needs.
                Susp::deferred(Pending::PushFront(overflow, Arc::clone(&deep.middle))),
                deep.middle_size + overflow_size,
                deep.suffix.clone(),
            )
        }
    }
}

fn snoc<T, M, C>(combine: &C, tree: &Arc<Tree<T, M>>, item: Item<T, M>) -> Arc<Tree<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match tree.as_ref() {
        Tree::Empty => Arc::new(Tree::Single(item)),
        Tree::Single(existing) => deep_tree(
            vec![existing.clone()],
            empty_middle(),
            0,
            vec![item],
        ),
        Tree::Deep(deep) if deep.suffix.len() < 4 => {
            let mut suffix = Vec::with_capacity(deep.suffix.len() + 1);
            suffix.extend(deep.suffix.iter().cloned());
            suffix.push(item);
            deep_tree(
                deep.prefix.clone(),
                Arc::clone(&deep.middle),
                deep.middle_size,
                suffix,
            )
        }
        Tree::Deep(deep) => {
            let overflow = make_node(combine, deep.suffix[..3].to_vec());
            let overflow_size = item_size(&overflow);
            deep_tree(
                deep.prefix.clone(),
                Susp::deferred(Pending::PushBack(Arc::clone(&deep.middle), overflow)),
                deep.middle_size + overflow_size,
                vec![deep.suffix[3].clone(), item],
            )
        }
    }
}

/// A detached leftmost item together with the remainder of its tree, or `None` when empty.
type LeftView<T, M> = Option<(Item<T, M>, Arc<Tree<T, M>>)>;

/// A detached rightmost item together with the remainder of its tree, or `None` when empty.
type RightView<T, M> = Option<(Arc<Tree<T, M>>, Item<T, M>)>;

/// Splits off the leftmost item. O(1) amortized: pulling a node up from the middle forces one
/// memoized suspension.
fn view_left<T, M, C>(combine: &C, tree: &Arc<Tree<T, M>>) -> LeftView<T, M>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match tree.as_ref() {
        Tree::Empty => None,
        Tree::Single(item) => Some((item.clone(), empty_tree())),
        Tree::Deep(deep) => {
            let head = deep.prefix[0].clone();
            if deep.prefix.len() > 1 {
                return Some((
                    head,
                    deep_tree(
                        deep.prefix[1..].to_vec(),
                        Arc::clone(&deep.middle),
                        deep.middle_size,
                        deep.suffix.clone(),
                    ),
                ));
            }

            let middle = Susp::force(&deep.middle, combine);
            if matches!(middle.as_ref(), Tree::Empty) {
                return Some((head, digit_to_tree(deep.suffix.clone())));
            }

            let (node, rest) =
                view_left(combine, middle).expect("a non-empty middle has a first node");
            let rest_size = deep.middle_size - item_size(&node);
            Some((
                head,
                deep_tree(item_digit(node), Susp::ready(rest), rest_size, deep.suffix.clone()),
            ))
        }
    }
}

/// Splits off the rightmost item; the mirror image of [`view_left`].
fn view_right<T, M, C>(combine: &C, tree: &Arc<Tree<T, M>>) -> RightView<T, M>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match tree.as_ref() {
        Tree::Empty => None,
        Tree::Single(item) => Some((empty_tree(), item.clone())),
        Tree::Deep(deep) => {
            let last = deep
                .suffix
                .last()
                .expect("a digit holds at least one item")
                .clone();
            if deep.suffix.len() > 1 {
                return Some((
                    deep_tree(
                        deep.prefix.clone(),
                        Arc::clone(&deep.middle),
                        deep.middle_size,
                        deep.suffix[..deep.suffix.len() - 1].to_vec(),
                    ),
                    last,
                ));
            }

            let middle = Susp::force(&deep.middle, combine);
            if matches!(middle.as_ref(), Tree::Empty) {
                return Some((digit_to_tree(deep.prefix.clone()), last));
            }

            let (rest, node) =
                view_right(combine, middle).expect("a non-empty middle has a last node");
            let rest_size = deep.middle_size - item_size(&node);
            Some((
                deep_tree(deep.prefix.clone(), Susp::ready(rest), rest_size, item_digit(node)),
                last,
            ))
        }
    }
}

// --- concatenation -----------------------------------------------------------------------------

/// Groups 2..=12 items into 2-3 nodes, the standard Hinze–Paterson regrouping.
fn group_nodes<T, M, C>(combine: &C, items: Vec<Item<T, M>>) -> Vec<Item<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    debug_assert!(items.len() >= 2);
    let mut nodes = Vec::with_capacity(items.len() / 2 + 1);
    let mut index = 0;
    let mut remaining = items.len();
    while remaining > 0 {
        let take = if remaining == 2 || remaining == 4 { 2 } else { 3 };
        nodes.push(make_node(combine, items[index..index + take].to_vec()));
        index += take;
        remaining -= take;
    }

    nodes
}

fn app3<T, M, C>(
    combine: &C,
    left: &Arc<Tree<T, M>>,
    between: Vec<Item<T, M>>,
    right: &Arc<Tree<T, M>>,
) -> Arc<Tree<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match (left.as_ref(), right.as_ref()) {
        (Tree::Empty, _) => {
            let mut result = Arc::clone(right);
            for item in between.into_iter().rev() {
                result = cons(combine, item, &result);
            }
            result
        }
        (_, Tree::Empty) => {
            let mut result = Arc::clone(left);
            for item in between {
                result = snoc(combine, &result, item);
            }
            result
        }
        (Tree::Single(item), _) => {
            let rest = app3(combine, &empty_tree(), between, right);
            cons(combine, item.clone(), &rest)
        }
        (_, Tree::Single(item)) => {
            let rest = app3(combine, left, between, &empty_tree());
            snoc(combine, &rest, item.clone())
        }
        (Tree::Deep(left_deep), Tree::Deep(right_deep)) => {
            let mut middle_items = left_deep.suffix.clone();
            middle_items.extend(between);
            middle_items.extend(right_deep.prefix.iter().cloned());
            let nodes = group_nodes(combine, middle_items);
            let nodes_size = digit_size(&nodes);
            deep_tree(
                left_deep.prefix.clone(),
                // The recursion into both middles is suspended, which is what makes concat
                // O(log(min(n, m))) amortized instead of costing its whole recursion up front.
                Susp::deferred(Pending::Concat(
                    Arc::clone(&left_deep.middle),
                    nodes,
                    Arc::clone(&right_deep.middle),
                )),
                left_deep.middle_size + nodes_size + right_deep.middle_size,
                right_deep.suffix.clone(),
            )
        }
    }
}

// --- index-directed splitting ------------------------------------------------------------------

/// A digit split apart at an element index: (before, item, after, index into item).
type DigitSplit<T, M> = (Vec<Item<T, M>>, Item<T, M>, Vec<Item<T, M>>, usize);

/// Splits a digit at an element index.
fn split_digit<T, M>(digit: &[Item<T, M>], mut index: usize) -> DigitSplit<T, M> {
    for (position, item) in digit.iter().enumerate() {
        let size = item_size(item);
        if index < size {
            return (
                digit[..position].to_vec(),
                item.clone(),
                digit[position + 1..].to_vec(),
                index,
            );
        }
        index -= size;
    }

    unreachable!("a digit split index stays within the digit's size");
}

/// Splits an item at an element index, collapsing its path into flat item lists.
///
/// The returned lists deliberately mix levels; every consumer treats items as opaque size and
/// measure carriers, so mixed levels rebuild correctly through [`cons`] and [`snoc`].
#[allow(clippy::type_complexity)]
fn split_item<T, M>(
    item: Item<T, M>,
    mut index: usize,
) -> (
    Vec<Item<T, M>>,
    Arc<MeasuredElement<T, M>>,
    Vec<Item<T, M>>,
) {
    let mut before = Vec::new();
    let mut after: Vec<Item<T, M>> = Vec::new();
    let mut current = item;
    loop {
        match current {
            Item::Leaf(element) => {
                debug_assert_eq!(index, 0);
                return (before, element, after);
            }
            Item::Node(node) => {
                let mut found = None;
                let mut trailing = Vec::new();
                for child in &node.children {
                    if found.is_none() {
                        let child_size = item_size(child);
                        if index < child_size {
                            found = Some(child.clone());
                        } else {
                            index -= child_size;
                            before.push(child.clone());
                        }
                    } else {
                        trailing.push(child.clone());
                    }
                }
                trailing.extend(after);
                after = trailing;
                current = found.expect("a node split index stays within the node's size");
            }
        }
    }
}

/// Rebuilds a tree whose prefix digit may be empty by pulling a node up from the middle.
fn deep_left<T, M, C>(
    combine: &C,
    before: Vec<Item<T, M>>,
    middle: Arc<Susp<T, M>>,
    middle_size: usize,
    suffix: Vec<Item<T, M>>,
) -> Arc<Tree<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    if !before.is_empty() {
        return deep_tree(before, middle, middle_size, suffix);
    }

    let forced = Susp::force(&middle, combine);
    if matches!(forced.as_ref(), Tree::Empty) {
        return digit_to_tree(suffix);
    }

    let (node, rest) = view_left(combine, forced).expect("a non-empty middle has a first node");
    let rest_size = middle_size - item_size(&node);
    deep_tree(item_digit(node), Susp::ready(rest), rest_size, suffix)
}

/// Rebuilds a tree whose suffix digit may be empty; the mirror image of [`deep_left`].
fn deep_right<T, M, C>(
    combine: &C,
    prefix: Vec<Item<T, M>>,
    middle: Arc<Susp<T, M>>,
    middle_size: usize,
    after: Vec<Item<T, M>>,
) -> Arc<Tree<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    if !after.is_empty() {
        return deep_tree(prefix, middle, middle_size, after);
    }

    let forced = Susp::force(&middle, combine);
    if matches!(forced.as_ref(), Tree::Empty) {
        return digit_to_tree(prefix);
    }

    let (rest, node) = view_right(combine, forced).expect("a non-empty middle has a last node");
    let rest_size = middle_size - item_size(&node);
    deep_tree(prefix, Susp::ready(rest), rest_size, item_digit(node))
}

/// Splits at an element index: (elements before, the element, elements after).
#[allow(clippy::type_complexity)]
fn split_tree<T, M, C>(
    combine: &C,
    tree: &Arc<Tree<T, M>>,
    index: usize,
) -> (
    Arc<Tree<T, M>>,
    Arc<MeasuredElement<T, M>>,
    Arc<Tree<T, M>>,
)
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match tree.as_ref() {
        Tree::Empty => unreachable!("a split index stays within a non-empty tree"),
        Tree::Single(item) => {
            let (item_before, element, item_after) = split_item(item.clone(), index);
            let mut left = empty_tree();
            for piece in item_before {
                left = snoc(combine, &left, piece);
            }
            let mut right = empty_tree();
            for piece in item_after.into_iter().rev() {
                right = cons(combine, piece, &right);
            }
            (left, element, right)
        }
        Tree::Deep(deep) => {
            let prefix_size = digit_size(&deep.prefix);
            if index < prefix_size {
                let (before, item, after, inner) = split_digit(&deep.prefix, index);
                let (item_before, element, item_after) = split_item(item, inner);
                let mut left = empty_tree();
                for piece in before.into_iter().chain(item_before) {
                    left = snoc(combine, &left, piece);
                }
                let mut right = deep_left(
                    combine,
                    after,
                    Arc::clone(&deep.middle),
                    deep.middle_size,
                    deep.suffix.clone(),
                );
                for piece in item_after.into_iter().rev() {
                    right = cons(combine, piece, &right);
                }
                return (left, element, right);
            }

            let mut index = index - prefix_size;
            if index < deep.middle_size {
                // The recursion already collapses down to the element level, so its halves are
                // complete trees of nodes.
                let middle = Arc::clone(Susp::force(&deep.middle, combine));
                let (middle_left, element, middle_right) = split_tree(combine, &middle, index);
                let left_size = tree_size(&middle_left);
                let right_size = tree_size(&middle_right);
                let left = deep_right(
                    combine,
                    deep.prefix.clone(),
                    Susp::ready(middle_left),
                    left_size,
                    Vec::new(),
                );
                let right = deep_left(
                    combine,
                    Vec::new(),
                    Susp::ready(middle_right),
                    right_size,
                    deep.suffix.clone(),
                );
                return (left, element, right);
            }

            index -= deep.middle_size;
            let (before, item, after, inner) = split_digit(&deep.suffix, index);
            let (item_before, element, item_after) = split_item(item, inner);
            let mut left = deep_right(
                combine,
                deep.prefix.clone(),
                Arc::clone(&deep.middle),
                deep.middle_size,
                before,
            );
            for piece in item_before {
                left = snoc(combine, &left, piece);
            }
            let mut right = empty_tree();
            for piece in item_after.into_iter().chain(after).rev() {
                right = cons(combine, piece, &right);
            }
            (left, element, right)
        }
    }
}

// --- measure-directed location and prefix accumulation -----------------------------------------

/// Descends into `item` toward the first boundary satisfying `predicate`, advancing the running
/// accumulators past skipped children. Monotone predicates always trip on some child; the last
/// child is the fallback that keeps a non-monotone predicate's result unspecified but terminating.
fn locate_descend_item<T, M, F, C>(
    item: &Item<T, M>,
    before: &mut M,
    index: &mut usize,
    predicate: &mut F,
    combine: &C,
) where
    M: Clone,
    F: FnMut(&M) -> bool,
    C: Fn(&M, &M) -> M,
{
    let mut current = item.clone();
    while let Item::Node(node) = current {
        let count = node.children.len();
        let mut chosen = None;
        for (position, child) in node.children.iter().enumerate() {
            let with_child = combine(before, item_measure(child));
            if predicate(&with_child) || position + 1 == count {
                chosen = Some(child.clone());
                break;
            }
            *before = with_child;
            *index += item_size(child);
        }
        current = chosen.expect("a 2-3 node has children");
    }
}

fn locate_scan_items<T, M, F, C>(
    items: &[Item<T, M>],
    before: &mut M,
    index: &mut usize,
    predicate: &mut F,
    combine: &C,
) -> bool
where
    M: Clone,
    F: FnMut(&M) -> bool,
    C: Fn(&M, &M) -> M,
{
    for item in items {
        let with_item = combine(before, item_measure(item));
        if predicate(&with_item) {
            locate_descend_item(item, before, index, predicate, combine);
            return true;
        }
        *before = with_item;
        *index += item_size(item);
    }

    false
}

fn locate_scan_tree<T, M, F, C>(
    tree: &Tree<T, M>,
    before: &mut M,
    index: &mut usize,
    predicate: &mut F,
    combine: &C,
) -> bool
where
    M: Clone,
    F: FnMut(&M) -> bool,
    C: Fn(&M, &M) -> M,
{
    match tree {
        Tree::Empty => false,
        Tree::Single(item) => {
            locate_scan_items(std::slice::from_ref(item), before, index, predicate, combine)
        }
        Tree::Deep(deep) => {
            let with_tree = combine(
                before,
                tree_measure(combine, tree).expect("a deep tree has a measure"),
            );
            if !predicate(&with_tree) {
                *before = with_tree;
                *index += deep.size;
                return false;
            }

            if locate_scan_items(&deep.prefix, before, index, predicate, combine) {
                return true;
            }
            let middle = Susp::force(&deep.middle, combine);
            if locate_scan_tree(middle, before, index, predicate, combine) {
                return true;
            }
            locate_scan_items(&deep.suffix, before, index, predicate, combine)
        }
    }
}

fn take_from_items<T, M, C>(
    items: &[Item<T, M>],
    remaining: &mut usize,
    result: &mut M,
    combine: &C,
) where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    for item in items {
        if *remaining == 0 {
            return;
        }

        let size = item_size(item);
        if size <= *remaining {
            *result = combine(result, item_measure(item));
            *remaining -= size;
        } else if let Item::Node(node) = item {
            take_from_items(&node.children, remaining, result, combine);
        } else {
            unreachable!("an element has size one");
        }
    }
}

fn take_from_tree<T, M, C>(tree: &Tree<T, M>, remaining: &mut usize, result: &mut M, combine: &C)
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    if *remaining == 0 {
        return;
    }

    match tree {
        Tree::Empty => {}
        Tree::Single(item) => {
            take_from_items(std::slice::from_ref(item), remaining, result, combine);
        }
        Tree::Deep(deep) => {
            if deep.size <= *remaining {
                *result = combine(
                    result,
                    tree_measure(combine, tree).expect("a deep tree has a measure"),
                );
                *remaining -= deep.size;
                return;
            }

            take_from_items(&deep.prefix, remaining, result, combine);
            if *remaining > 0 {
                take_from_tree(Susp::force(&deep.middle, combine), remaining, result, combine);
            }
            if *remaining > 0 {
                take_from_items(&deep.suffix, remaining, result, combine);
            }
        }
    }
}

// --- bulk construction -------------------------------------------------------------------------

/// Builds a tree bottom-up with ready middles: bulk construction defers nothing.
///
/// Deferral pays off when later operations may never force the work; a bulk build's caller almost
/// always consumes the result, so suspending every overflow would only add cells to force and then
/// discard. Grouping level by level costs the same O(n) with none of that churn.
fn build_eager<T, M, C>(combine: &C, mut items: Vec<Item<T, M>>) -> Arc<Tree<T, M>>
where
    M: Clone,
    C: Fn(&M, &M) -> M,
{
    match items.len() {
        0 => empty_tree(),
        1 => Arc::new(Tree::Single(items.pop().expect("a one-item build"))),
        count if count <= 8 => {
            let suffix = items.split_off(count / 2);
            deep_tree(items, empty_middle(), 0, suffix)
        }
        count => {
            let suffix = items.split_off(count - 3);
            let middle_items = items.split_off(3);
            let prefix = items;
            let middle = build_eager(combine, group_nodes(combine, middle_items));
            let middle_size = tree_size(&middle);
            deep_tree(prefix, Susp::ready(middle), middle_size, suffix)
        }
    }
}

impl<T, P> Clone for FingerTree<T, P>
where
    P: MeasurePolicy<T>,
{
    fn clone(&self) -> Self {
        let measure = OnceLock::new();
        if let Some(known) = self.measure.get() {
            let _ = measure.set(known.clone());
        }
        Self {
            root: Arc::clone(&self.root),
            measure,
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
        Self::from_root(empty_tree())
    }

    /// Returns the number of elements. O(1); sizes are strict at every node, so no count ever
    /// forces a suspension.
    #[must_use]
    pub fn len(&self) -> usize {
        tree_size(&self.root)
    }

    /// Returns `true` when the tree holds no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        matches!(self.root.as_ref(), Tree::Empty)
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

    /// Returns the combined measure of every element, in sequence order.
    ///
    /// O(1) amortized: the measure is memoized, and the first read of a freshly edited spine may
    /// force suspended middles before caching. The memoized spine measures live in nodes shared by
    /// every version that references them, so the amortization survives persistent branching.
    #[must_use]
    pub fn measure(&self) -> &P::Measure {
        self.measure.get_or_init(|| {
            tree_measure(&P::combine, &self.root).map_or_else(P::empty, Clone::clone)
        })
    }

    /// Borrows the first element, or `None` when the tree is empty. O(1) worst-case: the outer
    /// prefix digit holds it directly.
    #[must_use]
    pub fn front(&self) -> Option<&T> {
        match self.root.as_ref() {
            Tree::Empty => None,
            Tree::Single(item) => Some(&item_first(item).value),
            Tree::Deep(deep) => Some(&item_first(&deep.prefix[0]).value),
        }
    }

    /// Borrows the last element, or `None` when the tree is empty. O(1) worst-case: the outer
    /// suffix digit holds it directly.
    #[must_use]
    pub fn back(&self) -> Option<&T> {
        match self.root.as_ref() {
            Tree::Empty => None,
            Tree::Single(item) => Some(&item_last(item).value),
            Tree::Deep(deep) => Some(
                &item_last(deep.suffix.last().expect("a digit holds at least one item")).value,
            ),
        }
    }

    /// Borrows the element at `index`, or `None` when `index` is out of range. O(log n); the
    /// descent follows strict cached sizes, so it forces only the middles it actually enters.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len() {
            return None;
        }

        let mut tree: &Tree<T, P::Measure> = &self.root;
        let mut index = index;
        loop {
            match tree {
                Tree::Empty => return None,
                Tree::Single(item) => return Some(item_at(item, index)),
                Tree::Deep(deep) => {
                    let mut found = None;
                    for item in &deep.prefix {
                        let size = item_size(item);
                        if index < size {
                            found = Some(item);
                            break;
                        }
                        index -= size;
                    }
                    if found.is_none() && index < deep.middle_size {
                        tree = Susp::force(&deep.middle, &P::combine);
                        continue;
                    }
                    if found.is_none() {
                        index -= deep.middle_size;
                        for item in &deep.suffix {
                            let size = item_size(item);
                            if index < size {
                                found = Some(item);
                                break;
                            }
                            index -= size;
                        }
                    }
                    return found.map(|item| item_at(item, index));
                }
            }
        }
    }

    /// Iterates the elements in sequence order, forcing suspended middles as it passes them.
    pub fn iter(&self) -> Iter<'_, T, P::Measure> {
        Iter::new(&self.root, P::combine)
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
        let items = items
            .into_iter()
            .map(|value| {
                let measure = P::measure(&value);
                Item::Leaf(Arc::new(MeasuredElement { value, measure }))
            })
            .collect();
        Self::from_root(build_eager(&P::combine, items))
    }

    fn from_root(root: Arc<Tree<T, P::Measure>>) -> Self {
        Self {
            root,
            measure: OnceLock::new(),
            _policy: PhantomData,
        }
    }

    /// Concatenates two trees, placing every element of `other` after every element of `self`.
    ///
    /// O(log(min(m, n))) amortized: the recursion into the two middles is suspended, and the
    /// digits between them are regrouped into 2-3 nodes. Concatenating with an empty tree shares
    /// the non-empty operand's root instead of copying it.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        Self::from_root(app3(&P::combine, &self.root, Vec::new(), &other.root))
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
        if count > self.len() {
            return None;
        }

        if count == 0 {
            return Some(P::empty());
        }

        if count == self.len() {
            return Some(self.measure().clone());
        }

        let mut result = P::empty();
        let mut remaining = count;
        take_from_tree(&self.root, &mut remaining, &mut result, &P::combine);
        debug_assert_eq!(remaining, 0);
        Some(result)
    }

    fn split_at_index_unchecked(&self, index: usize) -> MeasuredSplit<T, P> {
        if index == 0 {
            return MeasuredSplit {
                left: Self::new(),
                right: self.clone(),
            };
        }

        if index == self.len() {
            return MeasuredSplit {
                left: self.clone(),
                right: Self::new(),
            };
        }

        let (left, element, right) = split_tree(&P::combine, &self.root, index);
        let right = cons(&P::combine, Item::Leaf(element), &right);
        MeasuredSplit {
            left: Self::from_root(left),
            right: Self::from_root(right),
        }
    }

    fn boundary_index<F>(&self, mut predicate: F) -> Option<usize>
    where
        F: FnMut(&P::Measure) -> bool,
    {
        let mut before = P::empty();
        let mut index = 0;
        locate_scan_tree(&self.root, &mut before, &mut index, &mut predicate, &P::combine)
            .then_some(index)
    }

    #[cfg(test)]
    pub(crate) fn tree_depth(&self) -> usize {
        let mut depth = 0;
        let mut tree: &Tree<T, P::Measure> = &self.root;
        loop {
            match tree {
                Tree::Empty => return depth,
                Tree::Single(_) => return depth + 1,
                Tree::Deep(deep) => {
                    depth += 1;
                    tree = Susp::force(&deep.middle, &P::combine);
                }
            }
        }
    }

    #[cfg(test)]
    pub(crate) fn validate_invariants(&self)
    where
        P::Measure: PartialEq + std::fmt::Debug,
    {
        let (len, measure) =
            validate_tree::<T, P>(&self.root).expect("measured tree invariant failure");
        assert_eq!(len, self.len());
        let expected = measure.unwrap_or_else(P::empty);
        assert_eq!(&expected, self.measure());
    }

    #[cfg(test)]
    pub(crate) fn shared_node_count_with(&self, other: &Self) -> usize {
        use std::collections::HashSet;

        let mut mine = HashSet::new();
        collect_identities::<T, P>(&self.root, &mut mine);
        let mut theirs = HashSet::new();
        collect_identities::<T, P>(&other.root, &mut theirs);
        mine.intersection(&theirs).count()
    }
}

/// Recomputes an item's size and measure, checking every cache along the way.
///
/// Items are deliberately *not* required to sit at a uniform level: splitting collapses node
/// paths into flat item lists whose rebuilt digits mix elements with nodes, and every algorithm
/// treats items as opaque size and measure carriers, so the audit does too.
#[cfg(test)]
fn validate_item<T, P>(item: &Item<T, P::Measure>) -> Result<(usize, P::Measure), String>
where
    P: MeasurePolicy<T>,
    P::Measure: PartialEq + std::fmt::Debug,
{
    match item {
        Item::Leaf(element) => {
            let expected = P::measure(&element.value);
            if expected != element.measure {
                return Err(format!(
                    "element cached measure {:?} disagrees with expected {expected:?}",
                    element.measure
                ));
            }

            Ok((1, expected))
        }
        Item::Node(node) => {
            if !(2..=3).contains(&node.children.len()) {
                return Err(format!("a node holds {} children", node.children.len()));
            }

            let mut size = 0;
            let mut measure: Option<P::Measure> = None;
            for child in &node.children {
                let (child_size, child_measure) = validate_item::<T, P>(child)?;
                size += child_size;
                measure = Some(match measure {
                    None => child_measure,
                    Some(so_far) => P::combine(&so_far, &child_measure),
                });
            }

            if size != node.size {
                return Err(format!(
                    "node cached size {} disagrees with child total {size}",
                    node.size
                ));
            }

            let expected = measure.expect("a node has children");
            if expected != node.measure {
                return Err(format!(
                    "node cached measure {:?} disagrees with child total {expected:?}",
                    node.measure
                ));
            }

            if !Arc::ptr_eq(&node.first, item_first(&node.children[0])) {
                return Err("node cached first element disagrees with its first child".to_owned());
            }

            let last_child = node.children.last().expect("a node has children");
            if !Arc::ptr_eq(&node.last, item_last(last_child)) {
                return Err("node cached last element disagrees with its last child".to_owned());
            }

            Ok((node.size, node.measure.clone()))
        }
    }
}

#[cfg(test)]
#[allow(clippy::type_complexity)]
fn validate_tree<T, P>(tree: &Tree<T, P::Measure>) -> Result<(usize, Option<P::Measure>), String>
where
    P: MeasurePolicy<T>,
    P::Measure: PartialEq + std::fmt::Debug,
{
    match tree {
        Tree::Empty => Ok((0, None)),
        Tree::Single(item) => {
            let (size, measure) = validate_item::<T, P>(item)?;
            Ok((size, Some(measure)))
        }
        Tree::Deep(deep) => {
            if deep.prefix.is_empty() || deep.prefix.len() > 4 {
                return Err(format!("a prefix digit holds {} items", deep.prefix.len()));
            }

            if deep.suffix.is_empty() || deep.suffix.len() > 4 {
                return Err(format!("a suffix digit holds {} items", deep.suffix.len()));
            }

            let mut size = 0;
            let mut measure: Option<P::Measure> = None;
            let fold = |item: &Item<T, P::Measure>,
                            size: &mut usize,
                            measure: &mut Option<P::Measure>|
             -> Result<(), String> {
                let (item_size, item_measure) = validate_item::<T, P>(item)?;
                *size += item_size;
                *measure = Some(match measure.take() {
                    None => item_measure,
                    Some(so_far) => P::combine(&so_far, &item_measure),
                });
                Ok(())
            };
            for item in &deep.prefix {
                fold(item, &mut size, &mut measure)?;
            }

            let middle = Susp::force(&deep.middle, &P::combine);
            let (middle_size, middle_measure) = validate_tree::<T, P>(middle)?;
            if middle_size != deep.middle_size {
                return Err(format!(
                    "deep cached middle size {} disagrees with forced middle size {middle_size}",
                    deep.middle_size
                ));
            }
            size += middle_size;
            if let Some(middle_measure) = middle_measure {
                let so_far = measure.take().expect("a prefix digit contributed a measure");
                measure = Some(P::combine(&so_far, &middle_measure));
            }

            for item in &deep.suffix {
                fold(item, &mut size, &mut measure)?;
            }

            if size != deep.size {
                return Err(format!(
                    "deep cached size {} disagrees with recomputed total {size}",
                    deep.size
                ));
            }

            let expected = measure.expect("a deep node is non-empty");
            if let Some(memoized) = deep.measure.get()
                && *memoized != expected
            {
                return Err(format!(
                    "deep memoized measure {memoized:?} disagrees with recomputed {expected:?}"
                ));
            }

            Ok((deep.size, Some(expected)))
        }
    }
}

/// Collects the identities of every allocation two versions could share: spine nodes, 2-3 nodes,
/// and element cells. The walk forces suspended middles — deferred structure captures shared
/// subtrees inside pending suspensions where no identity walk can see them, so a non-forcing probe
/// would report false negatives.
#[cfg(test)]
fn collect_identities<T, P>(
    root: &Arc<Tree<T, P::Measure>>,
    into: &mut std::collections::HashSet<usize>,
) where
    P: MeasurePolicy<T>,
{
    fn collect_item<T, M>(item: &Item<T, M>, into: &mut std::collections::HashSet<usize>) {
        match item {
            Item::Leaf(element) => {
                into.insert(Arc::as_ptr(element) as usize);
            }
            Item::Node(node) => {
                if into.insert(Arc::as_ptr(node) as usize) {
                    for child in &node.children {
                        collect_item(child, into);
                    }
                }
            }
        }
    }

    let mut stack: Vec<&Arc<Tree<T, P::Measure>>> = vec![root];
    while let Some(current) = stack.pop() {
        if !into.insert(Arc::as_ptr(current).cast::<()>() as usize) {
            continue;
        }

        match current.as_ref() {
            Tree::Empty => {}
            Tree::Single(item) => collect_item(item, into),
            Tree::Deep(deep) => {
                for item in deep.prefix.iter().chain(&deep.suffix) {
                    collect_item(item, into);
                }
                stack.push(Susp::force(&deep.middle, &P::combine));
            }
        }
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
        result.extend(self.iter().cloned());
        result
    }

    /// Returns a tree with `item` added at the front.
    ///
    /// Amortized O(1) and O(log n) worst-case: a full prefix digit pushes its overflow node into
    /// the middle *inside* a memoized suspension, so the deferred cascade is paid once no matter
    /// how many versions later force it.
    #[must_use]
    pub fn prepend(&self, item: T) -> Self {
        let measure = P::measure(&item);
        let element = Item::Leaf(Arc::new(MeasuredElement {
            value: item,
            measure,
        }));
        Self::from_root(cons(&P::combine, element, &self.root))
    }

    /// Returns a tree with `item` added at the back.
    ///
    /// Amortized O(1) and O(log n) worst-case, by the same suspended-overflow argument as
    /// [`prepend`](Self::prepend).
    #[must_use]
    pub fn append(&self, item: T) -> Self {
        let measure = P::measure(&item);
        let element = Item::Leaf(Arc::new(MeasuredElement {
            value: item,
            measure,
        }));
        Self::from_root(snoc(&P::combine, &self.root, element))
    }

    /// Splits off the first element, returning it together with the remaining tree, or `None` when
    /// empty. The receiver is unchanged. Amortized O(1) and O(log n) worst-case.
    #[must_use]
    pub fn try_view_left(&self) -> Option<(T, Self)> {
        let (item, rest) = view_left(&P::combine, &self.root)?;
        let value = item_first(&item).value.clone();
        Some((value, Self::from_root(rest)))
    }

    /// Splits off the last element, returning it together with the remaining tree, or `None` when
    /// empty. The receiver is unchanged. Amortized O(1) and O(log n) worst-case.
    #[must_use]
    pub fn try_view_right(&self) -> Option<(T, Self)> {
        let (rest, item) = view_right(&P::combine, &self.root)?;
        let value = item_last(&item).value.clone();
        Some((value, Self::from_root(rest)))
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
        let item = self.get(index)?.clone();
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
                measure_before: self.measure().clone(),
                item: None,
            };
        };

        LocateResult {
            index,
            measure_before: self
                .prefix_measure(index)
                .expect("a located boundary has a prefix measure"),
            item: self.get(index).cloned(),
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
    /// O(1) amortized: the maximum is the tree's memoized root measure, so no search is needed.
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
    /// O(1) amortized: the minimum is the tree's memoized root measure, so no search is needed.
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
    /// Borrows the maximum element, or `None` when empty. O(1) amortized from the memoized root
    /// measure.
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
    /// Borrows the minimum element, or `None` when empty. O(1) amortized from the memoized root
    /// measure.
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
///
/// Forces suspended middles as it reaches them; the forced results are memoized in cells shared by
/// every version, so re-iterating or iterating a sibling version never repeats spine work.
pub struct Iter<'a, T, M> {
    frames: Vec<IterFrame<'a, T, M>>,
    remaining: usize,
    combine: fn(&M, &M) -> M,
}

enum IterFrame<'a, T, M> {
    Tree(&'a Tree<T, M>),
    Items(std::slice::Iter<'a, Item<T, M>>),
}

impl<'a, T, M> Iter<'a, T, M> {
    fn new(root: &'a Arc<Tree<T, M>>, combine: fn(&M, &M) -> M) -> Self {
        Self {
            frames: vec![IterFrame::Tree(root)],
            remaining: tree_size(root),
            combine,
        }
    }
}

impl<'a, T, M> Iterator for Iter<'a, T, M>
where
    M: Clone,
{
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let frame = self.frames.pop()?;
            match frame {
                IterFrame::Items(mut items) => {
                    if let Some(item) = items.next() {
                        self.frames.push(IterFrame::Items(items));
                        match item {
                            Item::Leaf(element) => {
                                self.remaining -= 1;
                                return Some(&element.value);
                            }
                            Item::Node(node) => {
                                self.frames.push(IterFrame::Items(node.children.iter()));
                            }
                        }
                    }
                }
                IterFrame::Tree(tree) => match tree {
                    Tree::Empty => {}
                    Tree::Single(item) => {
                        self.frames
                            .push(IterFrame::Items(std::slice::from_ref(item).iter()));
                    }
                    Tree::Deep(deep) => {
                        self.frames.push(IterFrame::Items(deep.suffix.iter()));
                        self.frames
                            .push(IterFrame::Tree(Susp::force(&deep.middle, &self.combine)));
                        self.frames.push(IterFrame::Items(deep.prefix.iter()));
                    }
                },
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T, M> ExactSizeIterator for Iter<'_, T, M> where M: Clone {}

#[cfg(test)]
mod tests {
    use super::*;
    use std::panic::{AssertUnwindSafe, catch_unwind};
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

    /// Declares a measure policy with dedicated call counters, so probes running in parallel test
    /// threads never share state.
    macro_rules! declare_counting_policy {
        ($policy:ident, $measures:ident, $combines:ident) => {
            static $measures: AtomicUsize = AtomicUsize::new(0);
            static $combines: AtomicUsize = AtomicUsize::new(0);

            #[derive(Clone, Debug, Default)]
            struct $policy;

            impl MeasurePolicy<i32> for $policy {
                type Measure = usize;

                fn empty() -> usize {
                    0
                }

                fn measure(_element: &i32) -> usize {
                    $measures.fetch_add(1, Ordering::Relaxed);
                    1
                }

                fn combine(left: &usize, right: &usize) -> usize {
                    $combines.fetch_add(1, Ordering::Relaxed);
                    left + right
                }
            }
        };
    }

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

    #[test]
    fn front_and_back_read_digits_without_policy_calls() {
        declare_counting_policy!(FrontBackPolicy, FRONT_BACK_MEASURES, FRONT_BACK_COMBINES);

        let tree: FingerTree<_, FrontBackPolicy> = (0..1024).collect();
        let grown = tree.append(1024).prepend(-1);
        FRONT_BACK_MEASURES.store(0, Ordering::Relaxed);
        FRONT_BACK_COMBINES.store(0, Ordering::Relaxed);

        assert_eq!(tree.front(), Some(&0));
        assert_eq!(tree.back(), Some(&1023));
        assert_eq!(grown.front(), Some(&-1));
        assert_eq!(grown.back(), Some(&1024));
        assert_eq!(tree.len(), 1024);
        assert_eq!(grown.len(), 1026);

        assert_eq!(
            FRONT_BACK_MEASURES.load(Ordering::Relaxed),
            0,
            "front/back/len must not measure any element"
        );
        assert_eq!(
            FRONT_BACK_COMBINES.load(Ordering::Relaxed),
            0,
            "front/back/len must not combine any measures"
        );
    }

    #[test]
    fn endpoint_update_cost_is_independent_of_size() {
        declare_counting_policy!(EndpointPolicy, ENDPOINT_MEASURES, ENDPOINT_COMBINES);

        let mut costs = Vec::new();
        for count in [1_024, 32_768] {
            let mut tree: FingerTree<_, EndpointPolicy> = (0..count).collect();
            ENDPOINT_COMBINES.store(0, Ordering::Relaxed);
            for value in 0..256 {
                tree = tree.append(value);
            }
            for value in 0..256 {
                tree = tree.prepend(value);
            }
            costs.push(ENDPOINT_COMBINES.load(Ordering::Relaxed));
            assert_eq!(tree.len(), count as usize + 512);
        }

        // The immediate work of an endpoint update touches only the outer digit; every deeper
        // cascade is suspended. The combine totals are therefore identical at both sizes, and
        // small: bounded by one 2-3 node build per overflow.
        assert_eq!(
            costs[0], costs[1],
            "endpoint update cost must not grow with tree size: {costs:?}"
        );
        assert!(
            costs[0] <= 3 * 512,
            "512 endpoint updates must stay within the amortized budget, cost {}",
            costs[0]
        );
    }

    #[test]
    fn concat_immediate_cost_ignores_operand_asymmetry() {
        declare_counting_policy!(ConcatPolicy, CONCAT_MEASURES, CONCAT_COMBINES);

        let big: FingerTree<_, ConcatPolicy> = (0..4_096).collect();
        let mut costs = Vec::new();
        for small_count in [1, 1_024] {
            let small: FingerTree<_, ConcatPolicy> = (0..small_count).collect();
            CONCAT_COMBINES.store(0, Ordering::Relaxed);
            let joined = big.concat(&small);
            costs.push(CONCAT_COMBINES.load(Ordering::Relaxed));
            assert_eq!(joined.len(), 4_096 + small_count as usize);
        }

        // The recursion into the two middles is suspended, so the immediate cost is one digit
        // regrouping regardless of how unequal the operands are.
        for cost in &costs {
            assert!(
                *cost <= 8,
                "an immediate concat must only regroup one digit boundary: {costs:?}"
            );
        }
    }

    #[test]
    fn forcing_memoizes_across_persistent_branches() {
        declare_counting_policy!(BranchPolicy, BRANCH_MEASURES, BRANCH_COMBINES);

        let mut base: FingerTree<_, BranchPolicy> = FingerTree::new();
        for value in 0..10_000 {
            base = base.append(value);
        }

        // Branch the unforced spine 49 ways before anything is forced.
        let branches: Vec<_> = (0..49).map(|value| base.append(value)).collect();

        BRANCH_COMBINES.store(0, Ordering::Relaxed);
        assert_eq!(*branches[0].measure(), 10_001);
        let first_force = BRANCH_COMBINES.load(Ordering::Relaxed);

        BRANCH_COMBINES.store(0, Ordering::Relaxed);
        for branch in &branches[1..] {
            assert_eq!(*branch.measure(), 10_001);
        }
        let other_forces = BRANCH_COMBINES.load(Ordering::Relaxed);

        // The first force replays the whole deferred spine; every sibling branch reuses the
        // memoized results and pays only for its own divergent nodes.
        assert!(
            first_force > 2_000,
            "the first force must replay the deferred spine, combines {first_force}"
        );
        assert!(
            other_forces < first_force / 3,
            "48 sibling forces must reuse memoized spine work: first {first_force}, rest {other_forces}"
        );
        assert!(
            other_forces / 48 < 200,
            "each sibling force must be cheap, rest total {other_forces}"
        );
    }

    #[test]
    fn deep_pending_chains_force_iteratively() {
        let mut appended: FingerTree<_, SizeMeasure> = FingerTree::new();
        for value in 0..200_000 {
            appended = appended.append(value);
        }

        // Forcing walks the Theta(n)-long pending chain with an explicit stack; a recursive force
        // would overflow the thread stack long before 200k links.
        assert_eq!(*appended.measure(), 200_000);
        assert_eq!(appended.front(), Some(&0));
        assert_eq!(appended.back(), Some(&199_999));
        assert_eq!(appended.get(123_456), Some(&123_456));
        assert_eq!(appended.iter().count(), 200_000);

        let mut prepended: FingerTree<_, SizeMeasure> = FingerTree::new();
        for value in 0..100_000 {
            prepended = prepended.prepend(value);
        }
        assert_eq!(*prepended.measure(), 100_000);
        assert_eq!(prepended.front(), Some(&99_999));
    }

    #[test]
    fn deep_pending_chains_drop_iteratively() {
        // Never forced: dropping the final version must unlink the Theta(n) suspension chain
        // iteratively instead of recursing once per link.
        let mut appended: FingerTree<_, SizeMeasure> = FingerTree::new();
        for value in 0..200_000 {
            appended = appended.append(value);
        }
        assert_eq!(appended.len(), 200_000);
        drop(appended);

        let mut prepended: FingerTree<_, SizeMeasure> = FingerTree::new();
        for value in 0..100_000 {
            prepended = prepended.prepend(value);
        }
        assert_eq!(prepended.len(), 100_000);
        drop(prepended);
    }

    #[test]
    fn failed_forces_publish_nothing_and_are_retryable() {
        static ARMED: AtomicBool = AtomicBool::new(false);

        #[derive(Clone, Debug, Default)]
        struct FailingPolicy;

        impl MeasurePolicy<i32> for FailingPolicy {
            type Measure = usize;

            fn empty() -> usize {
                0
            }

            fn measure(_element: &i32) -> usize {
                1
            }

            fn combine(left: &usize, right: &usize) -> usize {
                assert!(
                    !ARMED.load(Ordering::Relaxed),
                    "injected combine failure"
                );
                left + right
            }
        }

        let mut tree: FingerTree<_, FailingPolicy> = FingerTree::new();
        for value in 0..100 {
            tree = tree.append(value);
        }

        ARMED.store(true, Ordering::Relaxed);
        assert!(catch_unwind(AssertUnwindSafe(|| *tree.measure())).is_err());
        // The failed force published nothing, so a retry fails the same way instead of reading a
        // half-built spine.
        assert!(catch_unwind(AssertUnwindSafe(|| *tree.measure())).is_err());
        ARMED.store(false, Ordering::Relaxed);

        // With the failure cleared the same suspensions force successfully: the source survived.
        assert_eq!(*tree.measure(), 100);
        assert_eq!(tree.to_vec(), (0..100).collect::<Vec<_>>());
        tree.validate_invariants();
    }

    #[test]
    fn pending_suspensions_still_reveal_structural_sharing() {
        let base: FingerTree<_, SizeMeasure> = (0..256).collect();
        let appended = base.append(256);
        let prepended = base.prepend(-1);

        // Both edits defer their overflow work, so the shared structure hides behind pending
        // suspensions; the identity walk must force to see it.
        assert!(base.shared_node_count_with(&appended) > 100);
        assert!(base.shared_node_count_with(&prepended) > 100);
        assert!(appended.shared_node_count_with(&prepended) > 100);
        appended.validate_invariants();
        prepended.validate_invariants();
    }

    #[test]
    fn randomized_edit_sequences_match_a_vector_model() {
        let mut rng = 0xfeed_5eed_0451_u64;
        let mut step = move || {
            rng = rng.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            (rng >> 33) as usize
        };

        let mut tree: FingerTree<i64, SizeAndSumMeasure<i64>> = FingerTree::new();
        let mut model: Vec<i64> = Vec::new();

        for round in 0..600 {
            match step() % 8 {
                0 => {
                    let value = (step() % 1_000) as i64;
                    tree = tree.append(value);
                    model.push(value);
                }
                1 => {
                    let value = (step() % 1_000) as i64;
                    tree = tree.prepend(value);
                    model.insert(0, value);
                }
                2 => {
                    if let Some((value, rest)) = tree.try_view_left() {
                        assert_eq!(value, model.remove(0));
                        tree = rest;
                    } else {
                        assert!(model.is_empty());
                    }
                }
                3 => {
                    if let Some((value, rest)) = tree.try_view_right() {
                        assert_eq!(value, model.pop().unwrap());
                        tree = rest;
                    } else {
                        assert!(model.is_empty());
                    }
                }
                4 => {
                    let index = step() % (model.len() + 1);
                    let split = tree.split_at_index(index).unwrap();
                    assert_eq!(split.left.to_vec(), model[..index]);
                    assert_eq!(split.right.to_vec(), model[index..]);
                    tree = split.left.concat(&split.right);
                }
                5 => {
                    let batch: Vec<i64> = (0..step() % 40).map(|value| value as i64).collect();
                    let other: FingerTree<i64, SizeAndSumMeasure<i64>> =
                        batch.iter().copied().collect();
                    tree = tree.concat(&other);
                    model.extend(batch);
                }
                6 => {
                    if !model.is_empty() {
                        let index = step() % model.len();
                        assert_eq!(tree.get(index), Some(&model[index]));
                    }
                }
                _ => {
                    let count = step() % (model.len() + 1);
                    let expected: i64 = model[..count].iter().sum();
                    assert_eq!(tree.prefix_measure(count).unwrap().second, expected);
                }
            }

            assert_eq!(tree.len(), model.len());
            assert_eq!(tree.front(), model.first());
            assert_eq!(tree.back(), model.last());
            if round % 64 == 0 {
                assert_eq!(tree.to_vec(), model);
                tree.validate_invariants();
            }
        }

        assert_eq!(tree.to_vec(), model);
        tree.validate_invariants();
    }
}
