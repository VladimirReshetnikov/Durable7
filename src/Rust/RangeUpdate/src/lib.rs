#![forbid(unsafe_code)]
#![doc = "Persistent measured sequences with algebraic lazy range updates for Rust."]

use std::collections::HashSet;
use std::fmt;
use std::iter::FusedIterator;
use std::marker::PhantomData;
use std::sync::Arc;

pub use tools_data_structures_fingertree::MeasurePolicy;

/// The largest sequence length, matching the cross-port signed-32-bit count contract.
pub const MAXIMUM_COUNT: usize = i32::MAX as usize;

/// Extends an ordered measure policy with a monoid of lazy range-update tags.
///
/// `compose(newer, older)` is directional: it represents applying `older` first and `newer`
/// second. Tags must form a monoid under that composition, every tag recognized by
/// [`is_identity`](Self::is_identity) must act as identity, and the action on cached measures must
/// agree with the action on elements and distribute over ordered measure combination.
pub trait RangeUpdateAlgebra<T>: MeasurePolicy<T> {
    /// The lazy action carried by a covered subtree root.
    type Tag: Clone;

    /// Returns a distinguished tag-monoid identity.
    fn identity_tag() -> Self::Tag;

    /// Recognizes every tag value with full algebraic identity behavior.
    fn is_identity(tag: &Self::Tag) -> bool;

    /// Returns the action equivalent to applying `older` and then `newer`.
    fn compose(newer: &Self::Tag, older: &Self::Tag) -> Self::Tag;

    /// Applies a tag to one current logical element.
    fn apply_element(tag: &Self::Tag, element: &T) -> T;

    /// Applies a tag to the cached measure of `count` current logical elements.
    fn apply_measure(tag: &Self::Tag, measure: &Self::Measure, count: usize) -> Self::Measure;
}

/// A validated public-operation failure.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RangeUpdateError {
    /// An element operation received an index outside `[0, len)`.
    ElementIndexOutOfRange { index: usize, len: usize },
    /// A boundary operation received an index outside `[0, len]`.
    BoundaryIndexOutOfRange { index: usize, len: usize },
    /// An index/count pair does not describe a range within the sequence.
    RangeOutOfRange {
        index: usize,
        count: usize,
        len: usize,
    },
    /// The requested result would exceed [`MAXIMUM_COUNT`].
    CapacityOverflow,
}

impl fmt::Display for RangeUpdateError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ElementIndexOutOfRange { index, len } => write!(
                formatter,
                "element index {index} is outside the valid range for a sequence of length {len}"
            ),
            Self::BoundaryIndexOutOfRange { index, len } => write!(
                formatter,
                "boundary index {index} is outside the valid range for a sequence of length {len}"
            ),
            Self::RangeOutOfRange { index, count, len } => write!(
                formatter,
                "range ({index}, {count}) is outside a sequence of length {len}"
            ),
            Self::CapacityOverflow => formatter
                .write_str("the operation would create a sequence longer than i32::MAX elements"),
        }
    }
}

impl std::error::Error for RangeUpdateError {}

/// The two persistent sequences produced by [`RangeUpdateSequence::split_at`].
pub struct RangeUpdateSplit<T, A>
where
    A: RangeUpdateAlgebra<T>,
{
    pub left: RangeUpdateSequence<T, A>,
    pub right: RangeUpdateSequence<T, A>,
}

impl<T, A> Clone for RangeUpdateSplit<T, A>
where
    A: RangeUpdateAlgebra<T>,
{
    fn clone(&self) -> Self {
        Self {
            left: self.left.clone(),
            right: self.right.clone(),
        }
    }
}

/// Representation statistics returned by [`RangeUpdateSequence::validate_structure`].
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RangeUpdateSequenceStatistics {
    pub count: usize,
    pub node_count: usize,
    pub height: u32,
    pub maximum_absolute_balance_factor: u32,
    pub pending_tag_node_count: usize,
    pub maximum_pending_tag_depth: usize,
}

/// An invariant failure found by [`RangeUpdateSequence::validate_structure`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RangeUpdateInvariantError {
    BalanceFactor,
    CachedHeight,
    CachedCount,
    IdentityPendingTag,
    CachedMeasure,
    ReachableNodeCount,
}

impl fmt::Display for RangeUpdateInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::BalanceFactor => "an AVL balance factor is outside [-1, 1]",
            Self::CachedHeight => "a cached AVL height disagrees with its children",
            Self::CachedCount => "a cached subtree count disagrees with its children",
            Self::IdentityPendingTag => "a node retains a tag recognized as identity",
            Self::CachedMeasure => "a cached logical measure disagrees with its subtree",
            Self::ReachableNodeCount => "the reachable-node count disagrees with the root count",
        };
        formatter.write_str(message)
    }
}

impl std::error::Error for RangeUpdateInvariantError {}

struct Node<T, M, U> {
    value: T,
    left: Link<T, M, U>,
    right: Link<T, M, U>,
    height: u32,
    count: usize,
    measure: M,
    pending_tag: Option<U>,
}

type Link<T, M, U> = Option<Arc<Node<T, M, U>>>;
type LinkSplit<T, M, U> = (Link<T, M, U>, Link<T, M, U>);

/// Immutable indexed sequence with lazily composed contiguous range updates.
///
/// The representation is a path-copied implicit AVL tree. A node's value and cached measure
/// already include its pending tag while its children do not. Structural descent immutably pushes
/// pending tags; reads carry inherited tags without installing a mutable cache.
pub struct RangeUpdateSequence<T, A>
where
    A: RangeUpdateAlgebra<T>,
{
    root: Link<T, A::Measure, A::Tag>,
    empty_measure: Arc<A::Measure>,
    algebra: PhantomData<A>,
}

impl<T, A> Clone for RangeUpdateSequence<T, A>
where
    A: RangeUpdateAlgebra<T>,
{
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            empty_measure: self.empty_measure.clone(),
            algebra: PhantomData,
        }
    }
}

impl<T, A> RangeUpdateSequence<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    /// Creates an empty sequence and captures the algebra's empty measure once.
    #[must_use]
    pub fn new() -> Self {
        Self {
            root: None,
            empty_measure: Arc::new(A::empty()),
            algebra: PhantomData,
        }
    }

    /// Eagerly materializes `items` once and builds a deterministic minimum-height AVL tree.
    ///
    /// Source enumeration and count validation finish before the empty-measure or element-measure
    /// callbacks run.
    pub fn from_items<I>(items: I) -> Result<Self, RangeUpdateError>
    where
        I: IntoIterator<Item = T>,
    {
        let mut values = Vec::new();
        for item in items {
            if values.len() == MAXIMUM_COUNT {
                return Err(RangeUpdateError::CapacityOverflow);
            }
            values.push(item);
        }

        let empty_measure = Arc::new(A::empty());
        let root = build_balanced::<T, A>(&values, 0, values.len());
        Ok(Self {
            root,
            empty_measure,
            algebra: PhantomData,
        })
    }

    /// Builds from a slice after cloning all input elements.
    pub fn from_slice(items: &[T]) -> Result<Self, RangeUpdateError> {
        Self::from_items(items.iter().cloned())
    }

    #[must_use]
    pub fn len(&self) -> usize {
        count_of(&self.root)
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns the cached ordered measure of the complete logical sequence.
    #[must_use]
    pub fn measure(&self) -> &A::Measure {
        self.root
            .as_ref()
            .map_or(self.empty_measure.as_ref(), |root| &root.measure)
    }

    /// Returns the logical element at `index`, including every inherited lazy action.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<T> {
        (index < self.len()).then(|| {
            get_element::<T, A>(
                self.root
                    .as_ref()
                    .expect("a validated element index requires a root"),
                index,
            )
        })
    }

    /// Prepends one current logical value.
    pub fn prepend(&self, item: T) -> Result<Self, RangeUpdateError> {
        self.insert(0, item)
    }

    /// Appends one current logical value.
    pub fn append(&self, item: T) -> Result<Self, RangeUpdateError> {
        self.insert(self.len(), item)
    }

    /// Inserts a current logical value at a boundary in `[0, len]`.
    pub fn insert(&self, index: usize, item: T) -> Result<Self, RangeUpdateError> {
        self.check_boundary_index(index)?;
        self.check_room_for_one_more()?;
        Ok(self.wrap(Some(insert_core::<T, A>(&self.root, index, item))))
    }

    /// Replaces one logical element without an equality-based identity shortcut.
    pub fn set_item(&self, index: usize, item: T) -> Result<Self, RangeUpdateError> {
        self.check_element_index(index)?;
        Ok(self.wrap(Some(set_core::<T, A>(
            self.root
                .as_ref()
                .expect("a validated element index requires a root"),
            index,
            item,
        ))))
    }

    /// Removes one logical element.
    pub fn remove_at(&self, index: usize) -> Result<Self, RangeUpdateError> {
        self.check_element_index(index)?;
        Ok(self.wrap(remove_core::<T, A>(
            self.root
                .as_ref()
                .expect("a validated element index requires a root"),
            index,
        )))
    }

    /// Concatenates `other` after this sequence.
    ///
    /// The signed-32-bit count boundary is checked before any policy callback. An empty operand
    /// returns a root-sharing clone of the nonempty operand.
    pub fn concat(&self, other: &Self) -> Result<Self, RangeUpdateError> {
        let combined = self
            .len()
            .checked_add(other.len())
            .filter(|&count| count <= MAXIMUM_COUNT)
            .ok_or(RangeUpdateError::CapacityOverflow)?;
        debug_assert_eq!(combined, self.len() + other.len());
        if self.is_empty() {
            return Ok(other.clone());
        }
        if other.is_empty() {
            return Ok(self.clone());
        }

        let (pivot, remainder) = extract_minimum::<T, A>(
            other
                .root
                .as_ref()
                .expect("a nonempty sequence requires a root"),
        );
        Ok(self.wrap(Some(join::<T, A>(self.root.clone(), pivot, remainder))))
    }

    /// Splits at a boundary in `[0, len]`.
    pub fn split_at(&self, index: usize) -> Result<RangeUpdateSplit<T, A>, RangeUpdateError> {
        self.check_boundary_index(index)?;
        if index == 0 {
            return Ok(RangeUpdateSplit {
                left: self.empty_like(),
                right: self.clone(),
            });
        }
        if index == self.len() {
            return Ok(RangeUpdateSplit {
                left: self.clone(),
                right: self.empty_like(),
            });
        }

        let (left, right) = split_nodes::<T, A>(&self.root, index);
        Ok(RangeUpdateSplit {
            left: self.wrap(left),
            right: self.wrap(right),
        })
    }

    /// Returns the contiguous range beginning at `index` with length `count`.
    pub fn get_range(&self, index: usize, count: usize) -> Result<Self, RangeUpdateError> {
        self.check_range(index, count)?;
        if count == 0 {
            return Ok(self.empty_like());
        }
        if count == self.len() {
            return Ok(self.clone());
        }

        let (_, tail) = split_nodes::<T, A>(&self.root, index);
        let (middle, _) = split_nodes::<T, A>(&tail, count);
        Ok(self.wrap(middle))
    }

    /// Applies `tag` after every action already represented by the selected range.
    ///
    /// An empty range is a root-sharing no-op and does not call `A::is_identity`. A recognized
    /// identity is also a no-op. A nonidentity whole-sequence update replaces only the root.
    pub fn apply_range(
        &self,
        index: usize,
        count: usize,
        tag: A::Tag,
    ) -> Result<Self, RangeUpdateError> {
        self.check_range(index, count)?;
        if count == 0 {
            return Ok(self.clone());
        }
        if A::is_identity(&tag) {
            return Ok(self.clone());
        }
        if count == self.len() {
            return Ok(self.wrap(Some(apply_subtree::<T, A>(
                self.root
                    .as_ref()
                    .expect("a nonempty range requires a root"),
                &tag,
            ))));
        }

        let (left, tail) = split_nodes::<T, A>(&self.root, index);
        let (middle, right) = split_nodes::<T, A>(&tail, count);
        let updated_middle = apply_subtree::<T, A>(
            middle
                .as_ref()
                .expect("a nonempty isolated range requires a root"),
            &tag,
        );
        let joined = join_concatenated::<T, A>(left, Some(updated_middle));
        Ok(self.wrap(join_concatenated::<T, A>(joined, right)))
    }

    /// Returns the ordered measure of a contiguous logical range.
    pub fn measure_range(
        &self,
        index: usize,
        count: usize,
    ) -> Result<A::Measure, RangeUpdateError> {
        self.check_range(index, count)?;
        if count == 0 {
            return Ok(self.empty_measure.as_ref().clone());
        }
        let root = self
            .root
            .as_ref()
            .expect("a nonempty range requires a root");
        if count == self.len() {
            return Ok(root.measure.clone());
        }
        Ok(measure_range_core::<T, A>(root, index, count, None))
    }

    /// Creates an independent, snapshot-owning iterator over logical values.
    #[must_use]
    pub fn iter(&self) -> RangeUpdateIter<T, A> {
        RangeUpdateIter::new(self.root.clone(), self.len())
    }

    /// Copies every logical element in sequence order.
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.iter().collect()
    }

    /// Reports whether both facades retain the exact same root object.
    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            (None, None) => true,
            _ => false,
        }
    }

    /// Counts node occurrences in `other` whose node object is also reachable from this sequence.
    #[must_use]
    pub fn shared_node_count(&self, other: &Self) -> usize {
        let Some(root) = &self.root else {
            return 0;
        };
        let Some(other_root) = &other.root else {
            return 0;
        };

        let mut mine = HashSet::new();
        let mut stack = vec![root.clone()];
        while let Some(node) = stack.pop() {
            mine.insert(Arc::as_ptr(&node));
            if let Some(left) = &node.left {
                stack.push(left.clone());
            }
            if let Some(right) = &node.right {
                stack.push(right.clone());
            }
        }

        let mut shared = 0;
        let mut probe = vec![other_root.clone()];
        while let Some(node) = probe.pop() {
            if mine.contains(&Arc::as_ptr(&node)) {
                shared += 1;
            }
            if let Some(left) = &node.left {
                probe.push(left.clone());
            }
            if let Some(right) = &node.right {
                probe.push(right.clone());
            }
        }
        shared
    }

    /// Returns structural statistics without invoking algebra callbacks.
    #[must_use]
    pub fn structure_statistics(&self) -> RangeUpdateSequenceStatistics {
        structure_statistics(&self.root)
    }

    /// Recomputes structural and cached-measure invariants with a caller-supplied measure equality.
    pub fn validate_structure_by<F>(
        &self,
        mut measure_equals: F,
    ) -> Result<RangeUpdateSequenceStatistics, RangeUpdateInvariantError>
    where
        F: FnMut(&A::Measure, &A::Measure) -> bool,
    {
        if let Some(root) = &self.root {
            validate_node::<T, A, F>(root, &mut measure_equals)?;
        }
        let statistics = self.structure_statistics();
        if statistics.node_count != statistics.count {
            return Err(RangeUpdateInvariantError::ReachableNodeCount);
        }
        Ok(statistics)
    }

    fn wrap(&self, root: Link<T, A::Measure, A::Tag>) -> Self {
        Self {
            root,
            empty_measure: self.empty_measure.clone(),
            algebra: PhantomData,
        }
    }

    fn empty_like(&self) -> Self {
        self.wrap(None)
    }

    fn check_element_index(&self, index: usize) -> Result<(), RangeUpdateError> {
        (index < self.len())
            .then_some(())
            .ok_or(RangeUpdateError::ElementIndexOutOfRange {
                index,
                len: self.len(),
            })
    }

    fn check_boundary_index(&self, index: usize) -> Result<(), RangeUpdateError> {
        (index <= self.len())
            .then_some(())
            .ok_or(RangeUpdateError::BoundaryIndexOutOfRange {
                index,
                len: self.len(),
            })
    }

    fn check_range(&self, index: usize, count: usize) -> Result<(), RangeUpdateError> {
        if index > self.len() || count > self.len() - index {
            return Err(RangeUpdateError::RangeOutOfRange {
                index,
                count,
                len: self.len(),
            });
        }
        Ok(())
    }

    fn check_room_for_one_more(&self) -> Result<(), RangeUpdateError> {
        (self.len() < MAXIMUM_COUNT)
            .then_some(())
            .ok_or(RangeUpdateError::CapacityOverflow)
    }
}

impl<T, A> RangeUpdateSequence<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
    A::Measure: PartialEq,
{
    /// Recomputes all invariants using `PartialEq` for cached measures.
    pub fn validate_structure(
        &self,
    ) -> Result<RangeUpdateSequenceStatistics, RangeUpdateInvariantError> {
        self.validate_structure_by(|left, right| left == right)
    }
}

impl<T, A> Default for RangeUpdateSequence<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<T, A> FromIterator<T> for RangeUpdateSequence<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_items(iter).expect("range-update sequence capacity overflow")
    }
}

impl<T, A> fmt::Debug for RangeUpdateSequence<T, A>
where
    T: Clone + fmt::Debug,
    A: RangeUpdateAlgebra<T>,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

fn height_of<T, M, U>(node: &Link<T, M, U>) -> u32 {
    node.as_ref().map_or(0, |node| node.height)
}

fn count_of<T, M, U>(node: &Link<T, M, U>) -> usize {
    node.as_ref().map_or(0, |node| node.count)
}

fn checked_node_count(left: usize, right: usize) -> usize {
    left.checked_add(1)
        .and_then(|count| count.checked_add(right))
        .filter(|&count| count <= MAXIMUM_COUNT)
        .expect("a prevalidated range-update operation exceeded its structural count bound")
}

fn make_node<T, A>(
    value: T,
    left: Link<T, A::Measure, A::Tag>,
    right: Link<T, A::Measure, A::Tag>,
) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    // Structural impossibility wins over user callbacks.
    let height = 1 + height_of(&left).max(height_of(&right));
    let count = checked_node_count(count_of(&left), count_of(&right));

    let mut measure = A::measure(&value);
    if let Some(left) = &left {
        measure = A::combine(&left.measure, &measure);
    }
    if let Some(right) = &right {
        measure = A::combine(&measure, &right.measure);
    }

    Arc::new(Node {
        value,
        left,
        right,
        height,
        count,
        measure,
        pending_tag: None,
    })
}

fn apply_subtree<T, A>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
    newer_tag: &A::Tag,
) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let value = A::apply_element(newer_tag, &node.value);
    let measure = A::apply_measure(newer_tag, &node.measure, node.count);
    let pending_tag = match &node.pending_tag {
        Some(older_tag) => {
            let composed = A::compose(newer_tag, older_tag);
            (!A::is_identity(&composed)).then_some(composed)
        }
        None => Some(newer_tag.clone()),
    };

    Arc::new(Node {
        value,
        left: node.left.clone(),
        right: node.right.clone(),
        height: node.height,
        count: node.count,
        measure,
        pending_tag,
    })
}

fn push<T, A>(node: &Arc<Node<T, A::Measure, A::Tag>>) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let Some(tag) = &node.pending_tag else {
        return node.clone();
    };

    // Left-before-right callback order is deterministic and matches the reference algorithm.
    let left = node
        .left
        .as_ref()
        .map(|child| apply_subtree::<T, A>(child, tag));
    let right = node
        .right
        .as_ref()
        .map(|child| apply_subtree::<T, A>(child, tag));
    Arc::new(Node {
        value: node.value.clone(),
        left,
        right,
        height: node.height,
        count: node.count,
        measure: node.measure.clone(),
        pending_tag: None,
    })
}

fn rotate_left<T, A>(node: &Arc<Node<T, A::Measure, A::Tag>>) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let node = push::<T, A>(node);
    let pivot = push::<T, A>(
        node.right
            .as_ref()
            .expect("a left rotation requires a right child"),
    );
    let lower = make_node::<T, A>(node.value.clone(), node.left.clone(), pivot.left.clone());
    make_node::<T, A>(pivot.value.clone(), Some(lower), pivot.right.clone())
}

fn rotate_right<T, A>(node: &Arc<Node<T, A::Measure, A::Tag>>) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let node = push::<T, A>(node);
    let pivot = push::<T, A>(
        node.left
            .as_ref()
            .expect("a right rotation requires a left child"),
    );
    let lower = make_node::<T, A>(node.value.clone(), pivot.right.clone(), node.right.clone());
    make_node::<T, A>(pivot.value.clone(), pivot.left.clone(), Some(lower))
}

fn balance<T, A>(original: Arc<Node<T, A::Measure, A::Tag>>) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let mut node = original;
    let factor = i64::from(height_of(&node.left)) - i64::from(height_of(&node.right));
    if factor > 1 {
        let left = node
            .left
            .as_ref()
            .expect("a left-heavy node requires a left child");
        if height_of(&left.left) < height_of(&left.right) {
            node = push::<T, A>(&node);
            let rotated = rotate_left::<T, A>(
                node.left
                    .as_ref()
                    .expect("a left-heavy node requires a left child"),
            );
            node = make_node::<T, A>(node.value.clone(), Some(rotated), node.right.clone());
        }
        return rotate_right::<T, A>(&node);
    }

    if factor < -1 {
        let right = node
            .right
            .as_ref()
            .expect("a right-heavy node requires a right child");
        if height_of(&right.right) < height_of(&right.left) {
            node = push::<T, A>(&node);
            let rotated = rotate_right::<T, A>(
                node.right
                    .as_ref()
                    .expect("a right-heavy node requires a right child"),
            );
            node = make_node::<T, A>(node.value.clone(), node.left.clone(), Some(rotated));
        }
        return rotate_left::<T, A>(&node);
    }

    node
}

fn insert_core<T, A>(
    node: &Link<T, A::Measure, A::Tag>,
    index: usize,
    item: T,
) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let Some(node) = node else {
        return make_node::<T, A>(item, None, None);
    };

    let node = push::<T, A>(node);
    let left_count = count_of(&node.left);
    if index <= left_count {
        let left = insert_core::<T, A>(&node.left, index, item);
        return balance::<T, A>(make_node::<T, A>(
            node.value.clone(),
            Some(left),
            node.right.clone(),
        ));
    }

    let right = insert_core::<T, A>(&node.right, index - left_count - 1, item);
    balance::<T, A>(make_node::<T, A>(
        node.value.clone(),
        node.left.clone(),
        Some(right),
    ))
}

fn set_core<T, A>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
    index: usize,
    item: T,
) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let node = push::<T, A>(node);
    let left_count = count_of(&node.left);
    if index < left_count {
        let left = set_core::<T, A>(
            node.left
                .as_ref()
                .expect("an index in the left span requires a left child"),
            index,
            item,
        );
        return make_node::<T, A>(node.value.clone(), Some(left), node.right.clone());
    }
    if index > left_count {
        let right = set_core::<T, A>(
            node.right
                .as_ref()
                .expect("an index in the right span requires a right child"),
            index - left_count - 1,
            item,
        );
        return make_node::<T, A>(node.value.clone(), node.left.clone(), Some(right));
    }

    make_node::<T, A>(item, node.left.clone(), node.right.clone())
}

fn extract_minimum<T, A>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
) -> (T, Link<T, A::Measure, A::Tag>)
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let node = push::<T, A>(node);
    let Some(left) = &node.left else {
        return (node.value.clone(), node.right.clone());
    };

    let (minimum, remainder) = extract_minimum::<T, A>(left);
    let root = balance::<T, A>(make_node::<T, A>(
        node.value.clone(),
        remainder,
        node.right.clone(),
    ));
    (minimum, Some(root))
}

fn remove_core<T, A>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
    index: usize,
) -> Link<T, A::Measure, A::Tag>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let node = push::<T, A>(node);
    let left_count = count_of(&node.left);
    if index < left_count {
        let left = remove_core::<T, A>(
            node.left
                .as_ref()
                .expect("an index in the left span requires a left child"),
            index,
        );
        return Some(balance::<T, A>(make_node::<T, A>(
            node.value.clone(),
            left,
            node.right.clone(),
        )));
    }
    if index > left_count {
        let right = remove_core::<T, A>(
            node.right
                .as_ref()
                .expect("an index in the right span requires a right child"),
            index - left_count - 1,
        );
        return Some(balance::<T, A>(make_node::<T, A>(
            node.value.clone(),
            node.left.clone(),
            right,
        )));
    }

    match (&node.left, &node.right) {
        (None, _) => node.right.clone(),
        (_, None) => node.left.clone(),
        (Some(_), Some(right)) => {
            let (minimum, remainder) = extract_minimum::<T, A>(right);
            Some(balance::<T, A>(make_node::<T, A>(
                minimum,
                node.left.clone(),
                remainder,
            )))
        }
    }
}

fn join<T, A>(
    left: Link<T, A::Measure, A::Tag>,
    pivot: T,
    right: Link<T, A::Measure, A::Tag>,
) -> Arc<Node<T, A::Measure, A::Tag>>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let left_height = height_of(&left);
    let right_height = height_of(&right);
    if left_height <= right_height + 1 && right_height <= left_height + 1 {
        return make_node::<T, A>(pivot, left, right);
    }

    if left_height > right_height {
        let left = push::<T, A>(
            left.as_ref()
                .expect("a taller left side requires a left root"),
        );
        let boundary = join::<T, A>(left.right.clone(), pivot, right);
        return balance::<T, A>(make_node::<T, A>(
            left.value.clone(),
            left.left.clone(),
            Some(boundary),
        ));
    }

    let right = push::<T, A>(
        right
            .as_ref()
            .expect("a taller right side requires a right root"),
    );
    let boundary = join::<T, A>(left, pivot, right.left.clone());
    balance::<T, A>(make_node::<T, A>(
        right.value.clone(),
        Some(boundary),
        right.right.clone(),
    ))
}

fn join_concatenated<T, A>(
    left: Link<T, A::Measure, A::Tag>,
    right: Link<T, A::Measure, A::Tag>,
) -> Link<T, A::Measure, A::Tag>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    match (left, right) {
        (None, right) => right,
        (left, None) => left,
        (left @ Some(_), Some(right)) => {
            let (pivot, remainder) = extract_minimum::<T, A>(&right);
            Some(join::<T, A>(left, pivot, remainder))
        }
    }
}

fn split_nodes<T, A>(
    node: &Link<T, A::Measure, A::Tag>,
    index: usize,
) -> LinkSplit<T, A::Measure, A::Tag>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let Some(node) = node else {
        return (None, None);
    };
    if index == 0 {
        return (None, Some(node.clone()));
    }
    if index == node.count {
        return (Some(node.clone()), None);
    }

    let node = push::<T, A>(node);
    let left_count = count_of(&node.left);
    if index <= left_count {
        let (left, boundary) = split_nodes::<T, A>(&node.left, index);
        let right = join::<T, A>(boundary, node.value.clone(), node.right.clone());
        return (left, Some(right));
    }

    let (boundary, right) = split_nodes::<T, A>(&node.right, index - left_count - 1);
    let left = join::<T, A>(node.left.clone(), node.value.clone(), boundary);
    (Some(left), right)
}

fn child_inheritance<T, A>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
    inherited: &Option<A::Tag>,
) -> Option<A::Tag>
where
    A: RangeUpdateAlgebra<T>,
{
    match (&node.pending_tag, inherited) {
        (None, inherited) => inherited.clone(),
        (Some(local), None) => Some(local.clone()),
        (Some(local), Some(inherited)) => {
            // Ancestor inheritance is newer than the node-local pending tag.
            let composed = A::compose(inherited, local);
            (!A::is_identity(&composed)).then_some(composed)
        }
    }
}

fn get_element<T, A>(root: &Arc<Node<T, A::Measure, A::Tag>>, requested_index: usize) -> T
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    let mut node = root.clone();
    let mut index = requested_index;
    let mut inherited = None;
    loop {
        let left_count = count_of(&node.left);
        if index == left_count {
            return inherited.as_ref().map_or_else(
                || node.value.clone(),
                |tag| A::apply_element(tag, &node.value),
            );
        }

        inherited = child_inheritance::<T, A>(&node, &inherited);
        if index < left_count {
            node = node
                .left
                .as_ref()
                .expect("an index in the left span requires a left child")
                .clone();
        } else {
            index -= left_count + 1;
            node = node
                .right
                .as_ref()
                .expect("an index in the right span requires a right child")
                .clone();
        }
    }
}

fn measure_range_core<T, A>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
    index: usize,
    count: usize,
    inherited: Option<A::Tag>,
) -> A::Measure
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    if index == 0 && count == node.count {
        return inherited.as_ref().map_or_else(
            || node.measure.clone(),
            |tag| A::apply_measure(tag, &node.measure, node.count),
        );
    }

    let end = index + count;
    let left_count = count_of(&node.left);
    let mut result = None;
    let mut child_tag = None;

    if index < left_count {
        let tag = child_inheritance::<T, A>(node, &inherited);
        child_tag = Some(tag.clone());
        let child_count = count.min(left_count - index);
        result = Some(measure_range_core::<T, A>(
            node.left
                .as_ref()
                .expect("a range in the left span requires a left child"),
            index,
            child_count,
            tag,
        ));
    }

    if index <= left_count && end > left_count {
        let mut singleton = A::measure(&node.value);
        if let Some(tag) = &inherited {
            singleton = A::apply_measure(tag, &singleton, 1);
        }
        result = Some(result.map_or(singleton.clone(), |prefix| A::combine(&prefix, &singleton)));
    }

    let right_start = left_count + 1;
    if end > right_start {
        let tag = child_tag.unwrap_or_else(|| child_inheritance::<T, A>(node, &inherited));
        let local_start = index.saturating_sub(right_start);
        let local_end = end - right_start;
        let right_measure = measure_range_core::<T, A>(
            node.right
                .as_ref()
                .expect("a range in the right span requires a right child"),
            local_start,
            local_end - local_start,
            tag,
        );
        result = Some(result.map_or(right_measure.clone(), |prefix| {
            A::combine(&prefix, &right_measure)
        }));
    }

    result.expect("a nonempty valid range must produce a measure contribution")
}

fn build_balanced<T, A>(values: &[T], start: usize, count: usize) -> Link<T, A::Measure, A::Tag>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    if count == 0 {
        return None;
    }
    let left_count = count / 2;
    let middle = start + left_count;
    let left = build_balanced::<T, A>(values, start, left_count);
    let right = build_balanced::<T, A>(values, middle + 1, count - left_count - 1);
    Some(make_node::<T, A>(values[middle].clone(), left, right))
}

fn structure_statistics<T, M, U>(root: &Link<T, M, U>) -> RangeUpdateSequenceStatistics {
    let Some(root) = root else {
        return RangeUpdateSequenceStatistics::default();
    };

    let mut statistics = RangeUpdateSequenceStatistics {
        count: root.count,
        height: root.height,
        ..RangeUpdateSequenceStatistics::default()
    };
    let mut stack = vec![(root.clone(), 0_usize)];
    while let Some((node, inherited_pending_depth)) = stack.pop() {
        statistics.node_count += 1;
        let balance = height_of(&node.left).abs_diff(height_of(&node.right));
        statistics.maximum_absolute_balance_factor =
            statistics.maximum_absolute_balance_factor.max(balance);
        let pending_depth = if node.pending_tag.is_some() {
            statistics.pending_tag_node_count += 1;
            let depth = inherited_pending_depth + 1;
            statistics.maximum_pending_tag_depth = statistics.maximum_pending_tag_depth.max(depth);
            depth
        } else {
            inherited_pending_depth
        };
        if let Some(right) = &node.right {
            stack.push((right.clone(), pending_depth));
        }
        if let Some(left) = &node.left {
            stack.push((left.clone(), pending_depth));
        }
    }
    statistics
}

fn validate_node<T, A, F>(
    node: &Arc<Node<T, A::Measure, A::Tag>>,
    measure_equals: &mut F,
) -> Result<(usize, u32), RangeUpdateInvariantError>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
    F: FnMut(&A::Measure, &A::Measure) -> bool,
{
    let (left_count, left_height) = match &node.left {
        Some(left) => validate_node::<T, A, F>(left, measure_equals)?,
        None => (0, 0),
    };
    let (right_count, right_height) = match &node.right {
        Some(right) => validate_node::<T, A, F>(right, measure_equals)?,
        None => (0, 0),
    };

    let expected_height = 1 + left_height.max(right_height);
    let expected_count = left_count
        .checked_add(1)
        .and_then(|count| count.checked_add(right_count))
        .filter(|&count| count <= MAXIMUM_COUNT)
        .ok_or(RangeUpdateInvariantError::CachedCount)?;
    if left_height.abs_diff(right_height) > 1 {
        return Err(RangeUpdateInvariantError::BalanceFactor);
    }
    if node.height != expected_height {
        return Err(RangeUpdateInvariantError::CachedHeight);
    }
    if node.count != expected_count {
        return Err(RangeUpdateInvariantError::CachedCount);
    }
    if node.pending_tag.as_ref().is_some_and(A::is_identity) {
        return Err(RangeUpdateInvariantError::IdentityPendingTag);
    }

    let mut expected_measure = A::measure(&node.value);
    if let Some(left) = &node.left {
        let left_measure = node.pending_tag.as_ref().map_or_else(
            || left.measure.clone(),
            |tag| A::apply_measure(tag, &left.measure, left.count),
        );
        expected_measure = A::combine(&left_measure, &expected_measure);
    }
    if let Some(right) = &node.right {
        let right_measure = node.pending_tag.as_ref().map_or_else(
            || right.measure.clone(),
            |tag| A::apply_measure(tag, &right.measure, right.count),
        );
        expected_measure = A::combine(&expected_measure, &right_measure);
    }
    if !measure_equals(&node.measure, &expected_measure) {
        return Err(RangeUpdateInvariantError::CachedMeasure);
    }

    Ok((expected_count, expected_height))
}

struct IterFrame<T, M, U> {
    node: Arc<Node<T, M, U>>,
    inherited: Option<U>,
    child_inherited: Option<Option<U>>,
    stage: u8,
}

enum IterAction<T, M, U> {
    Continue,
    Push(Arc<Node<T, M, U>>, Option<U>),
    Yield(T),
    Pop,
}

/// Independent snapshot iterator over a [`RangeUpdateSequence`].
pub struct RangeUpdateIter<T, A>
where
    A: RangeUpdateAlgebra<T>,
{
    stack: Vec<IterFrame<T, A::Measure, A::Tag>>,
    remaining: usize,
    algebra: PhantomData<A>,
}

impl<T, A> RangeUpdateIter<T, A>
where
    A: RangeUpdateAlgebra<T>,
{
    fn new(root: Link<T, A::Measure, A::Tag>, remaining: usize) -> Self {
        Self {
            stack: root
                .into_iter()
                .map(|node| IterFrame {
                    node,
                    inherited: None,
                    child_inherited: None,
                    stage: 0,
                })
                .collect(),
            remaining,
            algebra: PhantomData,
        }
    }
}

impl<T, A> Iterator for RangeUpdateIter<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let frame = self.stack.last_mut()?;
            let action = match frame.stage {
                0 => {
                    if let Some(left) = frame.node.left.clone() {
                        let inherited = match &frame.child_inherited {
                            Some(tag) => tag.clone(),
                            None => {
                                let tag = child_inheritance::<T, A>(&frame.node, &frame.inherited);
                                frame.child_inherited = Some(tag.clone());
                                tag
                            }
                        };
                        frame.stage = 1;
                        IterAction::Push(left, inherited)
                    } else {
                        frame.stage = 1;
                        IterAction::Continue
                    }
                }
                1 => {
                    let value = frame.inherited.as_ref().map_or_else(
                        || frame.node.value.clone(),
                        |tag| A::apply_element(tag, &frame.node.value),
                    );
                    frame.stage = 2;
                    IterAction::Yield(value)
                }
                2 => {
                    if let Some(right) = frame.node.right.clone() {
                        let inherited = match &frame.child_inherited {
                            Some(tag) => tag.clone(),
                            None => {
                                let tag = child_inheritance::<T, A>(&frame.node, &frame.inherited);
                                frame.child_inherited = Some(tag.clone());
                                tag
                            }
                        };
                        frame.stage = 3;
                        IterAction::Push(right, inherited)
                    } else {
                        frame.stage = 3;
                        IterAction::Continue
                    }
                }
                _ => IterAction::Pop,
            };

            match action {
                IterAction::Continue => {}
                IterAction::Push(node, inherited) => self.stack.push(IterFrame {
                    node,
                    inherited,
                    child_inherited: None,
                    stage: 0,
                }),
                IterAction::Yield(value) => {
                    self.remaining -= 1;
                    return Some(value);
                }
                IterAction::Pop => {
                    self.stack.pop();
                }
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T, A> ExactSizeIterator for RangeUpdateIter<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
}

impl<T, A> FusedIterator for RangeUpdateIter<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
}

impl<T, A> IntoIterator for &RangeUpdateSequence<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    type Item = T;
    type IntoIter = RangeUpdateIter<T, A>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T, A> IntoIterator for RangeUpdateSequence<T, A>
where
    T: Clone,
    A: RangeUpdateAlgebra<T>,
{
    type Item = T;
    type IntoIter = RangeUpdateIter<T, A>;

    fn into_iter(self) -> Self::IntoIter {
        let remaining = count_of(&self.root);
        RangeUpdateIter::new(self.root, remaining)
    }
}
