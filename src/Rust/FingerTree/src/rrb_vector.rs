use std::fmt;
use std::iter::FusedIterator;
use std::ops::Index;
use std::sync::Arc;

const RADIX_BITS: u32 = 5;
const BRANCH_FACTOR: usize = 1 << RADIX_BITS;
// The first term is the greatest minimum height required anywhere in the count domain;
// boundary-only concatenation may legally retain one additional level of slack.
const MAXIMUM_HEIGHT: u8 = ((usize::BITS - 1) / RADIX_BITS) as u8 + 1;

/// An immutable relaxed radix-balanced vector with 32-way branching.
///
/// Regular branches use radix arithmetic and allocate no cumulative size table. Splits and
/// concatenations introduce relaxed branches only where child spans are irregular. All updates
/// preserve old snapshots through immutable [`Arc`] nodes. Concatenation redistributes only the
/// boundary seam and does not impose a global minimum occupancy on unrelated nodes.
pub struct RrbVector<T> {
    root: Option<Arc<Node<T>>>,
}

/// The two persistent vectors produced by [`RrbVector::split_at`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RrbVectorSplit<T> {
    pub left: RrbVector<T>,
    pub right: RrbVector<T>,
}

/// An endpoint value and the vector that remains after removing it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RrbVectorPop<T> {
    pub value: T,
    pub rest: RrbVector<T>,
}

/// Representation statistics returned by [`RrbVector::validate_structure`].
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct RrbVectorStatistics {
    pub count: usize,
    pub height: u8,
    pub leaf_count: usize,
    pub branch_count: usize,
    pub regular_branch_count: usize,
    pub relaxed_branch_count: usize,
    pub minimum_leaf_len: usize,
    pub maximum_leaf_len: usize,
    pub minimum_branching_factor: usize,
    pub maximum_branching_factor: usize,
}

/// An invariant failure detected by [`RrbVector::validate_structure`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RrbVectorInvariantError {
    message: &'static str,
}

impl fmt::Display for RrbVectorInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl std::error::Error for RrbVectorInvariantError {}

enum Node<T> {
    Leaf {
        data: Arc<[T]>,
        start: usize,
        len: usize,
    },
    Branch {
        children: Arc<[Arc<Node<T>>]>,
        cumulative_sizes: Option<Arc<[usize]>>,
        count: usize,
        height: u8,
    },
}

type NodeSplit<T> = (Option<Arc<Node<T>>>, Option<Arc<Node<T>>>);

impl<T> Node<T> {
    fn count(&self) -> usize {
        match self {
            Self::Leaf { len, .. } => *len,
            Self::Branch { count, .. } => *count,
        }
    }

    fn height(&self) -> u8 {
        match self {
            Self::Leaf { .. } => 0,
            Self::Branch { height, .. } => *height,
        }
    }

    fn find_child(&self, index: usize) -> (usize, usize) {
        let Self::Branch {
            cumulative_sizes,
            height,
            ..
        } = self
        else {
            unreachable!("child lookup requires an RRB branch")
        };

        if let Some(sizes) = cumulative_sizes {
            let child = sizes.partition_point(|&end| index >= end);
            let before = child.checked_sub(1).map_or(0, |previous| sizes[previous]);
            return (child, before);
        }

        let span = child_capacity(*height);
        let child = index / span;
        (child, child * span)
    }
}

impl<T> Clone for RrbVector<T> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
        }
    }
}

impl<T> RrbVector<T> {
    /// Creates an empty vector.
    #[must_use]
    pub const fn new() -> Self {
        Self { root: None }
    }

    /// Creates an empty mutable append builder.
    #[must_use]
    pub fn builder() -> RrbVectorBuilder<T> {
        RrbVectorBuilder::new()
    }

    /// Creates an append builder with this vector as an O(1) frozen prefix.
    #[must_use]
    pub fn to_builder(&self) -> RrbVectorBuilder<T> {
        RrbVectorBuilder::from_vector(self)
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.root.as_deref().map_or(0, Node::count)
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns the tree height, where leaves have height zero.
    #[must_use]
    pub fn height(&self) -> u8 {
        self.root.as_deref().map_or(0, Node::height)
    }

    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        (index < self.len()).then(|| get_node(self.root.as_deref().expect("non-empty root"), index))
    }

    #[must_use]
    pub fn first(&self) -> Option<&T> {
        self.get(0)
    }

    #[must_use]
    pub fn last(&self) -> Option<&T> {
        self.len().checked_sub(1).and_then(|index| self.get(index))
    }

    pub fn iter(&self) -> RrbVectorIter<'_, T> {
        RrbVectorIter::new(self)
    }

    /// Returns whether both vectors have exactly the same root node.
    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (None, None) => true,
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            _ => false,
        }
    }

    /// Replaces one item by path copying. Equal replacement preserves the root.
    #[must_use]
    pub fn set_item(&self, index: usize, value: T) -> Option<Self>
    where
        T: Clone + PartialEq,
    {
        let current = self.get(index)?;
        if current == &value {
            return Some(self.clone());
        }

        Some(Self {
            root: Some(set_node(
                self.root.as_ref().expect("validated index has a root"),
                index,
                value,
            )),
        })
    }

    /// Appends one item through boundary-spine concatenation.
    #[must_use]
    pub fn push_back(&self, value: T) -> Self
    where
        T: Clone,
    {
        self.concat(&Self::singleton(value))
    }

    /// Prepends one item through boundary-spine concatenation.
    #[must_use]
    pub fn push_front(&self, value: T) -> Self
    where
        T: Clone,
    {
        Self::singleton(value).concat(self)
    }

    /// Removes the first item without cloning it.
    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        (!self.is_empty()).then(|| {
            self.split_at(1)
                .expect("one is a valid non-empty split")
                .right
        })
    }

    /// Removes the last item without cloning it.
    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        let boundary = self.len().checked_sub(1)?;
        Some(
            self.split_at(boundary)
                .expect("len - 1 is a valid split")
                .left,
        )
    }

    /// Concatenates two vectors by rebalancing only their boundary spines.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self
    where
        T: Clone,
    {
        let Some(left) = &self.root else {
            return other.clone();
        };
        let Some(right) = &other.root else {
            return self.clone();
        };
        self.len()
            .checked_add(other.len())
            .expect("RRB vector length overflow");

        let roots = concat_nodes(left, right);
        let root = if roots.len() == 1 {
            Arc::clone(&roots[0])
        } else {
            branch(roots)
        };
        Self::from_root(Some(root))
    }

    /// Splits at a boundary, returning `None` when the boundary exceeds the length.
    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<RrbVectorSplit<T>> {
        if index > self.len() {
            return None;
        }
        if index == 0 {
            return Some(RrbVectorSplit {
                left: Self::new(),
                right: self.clone(),
            });
        }
        if index == self.len() {
            return Some(RrbVectorSplit {
                left: self.clone(),
                right: Self::new(),
            });
        }

        let (left, right) = split_node(
            self.root.as_ref().expect("non-boundary split has a root"),
            index,
        );
        Some(RrbVectorSplit {
            left: Self::from_root(left),
            right: Self::from_root(right),
        })
    }

    /// Inserts an owned sequence at a boundary.
    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
        T: Clone,
    {
        if index > self.len() {
            return None;
        }
        let inserted: Self = items.into_iter().collect();
        if inserted.is_empty() {
            return Some(self.clone());
        }
        let split = self.split_at(index).expect("validated boundary must split");
        Some(split.left.concat(&inserted).concat(&split.right))
    }

    /// Removes a contiguous range, returning `None` for an invalid range.
    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self>
    where
        T: Clone,
    {
        let end = index.checked_add(count)?;
        if end > self.len() {
            return None;
        }
        if count == 0 {
            return Some(self.clone());
        }

        let first = self.split_at(index).expect("validated boundary must split");
        let second = first
            .right
            .split_at(count)
            .expect("validated range length must split");
        Some(first.left.concat(&second.right))
    }

    /// Returns a structurally shared slice, or `None` for an invalid range.
    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        let end = index.checked_add(count)?;
        if end > self.len() {
            return None;
        }
        let tail = self.split_at(index)?.right;
        Some(tail.split_at(count)?.left)
    }

    /// Checks every cached count, height, branching, and relaxed-size invariant.
    pub fn validate_structure(&self) -> Result<RrbVectorStatistics, RrbVectorInvariantError> {
        let Some(root) = self.root.as_deref() else {
            return Ok(RrbVectorStatistics::default());
        };
        let mut accumulator = ValidationAccumulator::default();
        let (count, height) = validate_node(root, true, &mut accumulator)?;
        if count != self.len() || height != self.height() {
            return Err(invariant_error(
                "root count or height disagrees with validation",
            ));
        }
        // The height cap is enforced for every branch (root included) inside validate_node, which
        // returns above via `?` before reaching here, so a redundant root check would be dead code.
        Ok(accumulator.finish(count, height))
    }

    fn singleton(value: T) -> Self {
        Self {
            root: Some(leaf(vec![value])),
        }
    }

    fn from_root(mut root: Option<Arc<Node<T>>>) -> Self {
        while let Some(Node::Branch { children, .. }) = root.as_deref() {
            if children.len() != 1 {
                break;
            }
            root = Some(Arc::clone(&children[0]));
        }
        Self { root }
    }

    #[cfg(test)]
    fn leaf_identities(&self) -> Vec<*const Node<T>> {
        let mut result = Vec::new();
        let mut stack = self.root.iter().map(Arc::as_ref).collect::<Vec<_>>();
        while let Some(node) = stack.pop() {
            match node {
                Node::Leaf { .. } => result.push(node as *const Node<T>),
                Node::Branch { children, .. } => {
                    stack.extend(children.iter().rev().map(Arc::as_ref));
                }
            }
        }
        result
    }
}

impl<T> RrbVector<T>
where
    T: Clone,
{
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.iter().cloned().collect()
    }

    #[must_use]
    pub fn pop_front(&self) -> Option<RrbVectorPop<T>> {
        Some(RrbVectorPop {
            value: self.first()?.clone(),
            rest: self.remove_first().expect("non-empty vector has a tail"),
        })
    }

    #[must_use]
    pub fn pop_back(&self) -> Option<RrbVectorPop<T>> {
        Some(RrbVectorPop {
            value: self.last()?.clone(),
            rest: self.remove_last().expect("non-empty vector has an init"),
        })
    }
}

impl<T> Default for RrbVector<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> Index<usize> for RrbVector<T> {
    type Output = T;

    fn index(&self, index: usize) -> &Self::Output {
        self.get(index).expect("RRB vector index out of bounds")
    }
}

impl<T: fmt::Debug> fmt::Debug for RrbVector<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<T: PartialEq> PartialEq for RrbVector<T> {
    fn eq(&self, other: &Self) -> bool {
        self.shares_root_with(other) || self.len() == other.len() && self.iter().eq(other.iter())
    }
}

impl<T: Eq> Eq for RrbVector<T> {}

impl<T> FromIterator<T> for RrbVector<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        let mut builder = RrbVectorBuilder::new();
        builder.extend(iter);
        builder.publish_staged()
    }
}

impl<'a, T> IntoIterator for &'a RrbVector<T> {
    type Item = &'a T;
    type IntoIter = RrbVectorIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T: Clone> IntoIterator for RrbVector<T> {
    type Item = T;
    type IntoIter = RrbVectorIntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        RrbVectorIntoIter::new(self)
    }
}

/// Borrowed, streaming RRB-vector iterator.
#[derive(Clone)]
pub struct RrbVectorIter<'a, T> {
    stack: Vec<&'a Node<T>>,
    current: Option<std::slice::Iter<'a, T>>,
    remaining: usize,
}

impl<'a, T> RrbVectorIter<'a, T> {
    fn new(vector: &'a RrbVector<T>) -> Self {
        Self {
            stack: vector.root.iter().map(Arc::as_ref).collect(),
            current: None,
            remaining: vector.len(),
        }
    }
}

impl<'a, T> Iterator for RrbVectorIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Some(current) = &mut self.current
                && let Some(item) = current.next()
            {
                self.remaining -= 1;
                return Some(item);
            }
            self.current = None;

            match self.stack.pop()? {
                Node::Leaf {
                    data, start, len, ..
                } => self.current = Some(data[*start..*start + *len].iter()),
                Node::Branch { children, .. } => {
                    self.stack.extend(children.iter().rev().map(Arc::as_ref));
                }
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T> ExactSizeIterator for RrbVectorIter<'_, T> {}
impl<T> FusedIterator for RrbVectorIter<'_, T> {}

/// Owned iterator. Values are cloned because persistent snapshots may share leaf backing arrays.
pub struct RrbVectorIntoIter<T> {
    stack: Vec<Arc<Node<T>>>,
    current: Option<(Arc<[T]>, usize, usize)>,
    remaining: usize,
}

impl<T> RrbVectorIntoIter<T> {
    fn new(vector: RrbVector<T>) -> Self {
        let remaining = vector.len();
        Self {
            stack: vector.root.into_iter().collect(),
            current: None,
            remaining,
        }
    }
}

impl<T: Clone> Iterator for RrbVectorIntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Some((data, index, end)) = &mut self.current
                && *index < *end
            {
                let item = &data[*index];
                *index += 1;
                self.remaining -= 1;
                return Some(item.clone());
            }
            self.current = None;

            match self.stack.pop()?.as_ref() {
                Node::Leaf {
                    data, start, len, ..
                } => self.current = Some((Arc::clone(data), *start, *start + *len)),
                Node::Branch { children, .. } => {
                    self.stack.extend(children.iter().rev().cloned());
                }
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T: Clone> ExactSizeIterator for RrbVectorIntoIter<T> {}
impl<T: Clone> FusedIterator for RrbVectorIntoIter<T> {}

/// Mutable append-only staging surface with cached immutable snapshots.
///
/// Full 32-slot leaves are moved into immutable nodes. Freezing a partial tail also moves its
/// slots, so later builder mutations cannot affect an earlier snapshot. A vector supplied through
/// [`RrbVector::to_builder`] remains an O(1) frozen prefix.
pub struct RrbVectorBuilder<T> {
    prefix: RrbVector<T>,
    leaves: Vec<Arc<Node<T>>>,
    tail: Vec<T>,
    staged_len: usize,
}

impl<T> RrbVectorBuilder<T> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            prefix: RrbVector::new(),
            leaves: Vec::new(),
            tail: Vec::with_capacity(BRANCH_FACTOR),
            staged_len: 0,
        }
    }

    #[must_use]
    pub fn from_vector(vector: &RrbVector<T>) -> Self {
        Self {
            prefix: vector.clone(),
            leaves: Vec::new(),
            tail: Vec::with_capacity(BRANCH_FACTOR),
            staged_len: 0,
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.prefix
            .len()
            .checked_add(self.staged_len)
            .expect("RRB vector builder length overflow")
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.prefix.is_empty() && self.staged_len == 0
    }

    /// Appends one element in amortized O(1).
    pub fn push(&mut self, value: T) -> &mut Self {
        self.len()
            .checked_add(1)
            .expect("RRB vector builder length overflow");
        self.tail.push(value);
        self.staged_len += 1;
        if self.tail.len() == BRANCH_FACTOR {
            self.freeze_full_tail();
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

    /// Clears the frozen prefix and all mutable staging.
    pub fn clear(&mut self) -> &mut Self {
        self.prefix = RrbVector::new();
        self.leaves.clear();
        self.tail.clear();
        self.staged_len = 0;
        self
    }

    /// Publishes staged leaves and returns an immutable snapshot.
    ///
    /// Repeated clean calls return vectors with the same root. Appending after publication shares
    /// the old snapshot as an immutable prefix.
    #[must_use]
    pub fn to_immutable(&mut self) -> RrbVector<T>
    where
        T: Clone,
    {
        if self.staged_len == 0 {
            return self.prefix.clone();
        }

        let staged = self.publish_staged();
        self.prefix = self.prefix.concat(&staged);
        self.prefix.clone()
    }

    /// Returns a stable iterator after publishing the currently staged tail.
    pub fn iter(&mut self) -> RrbVectorIter<'_, T>
    where
        T: Clone,
    {
        let _ = self.to_immutable();
        self.prefix.iter()
    }

    fn freeze_full_tail(&mut self) {
        debug_assert_eq!(self.tail.len(), BRANCH_FACTOR);
        let full = std::mem::replace(&mut self.tail, Vec::with_capacity(BRANCH_FACTOR));
        self.leaves.push(leaf(full));
    }

    fn publish_staged(&mut self) -> RrbVector<T> {
        if self.staged_len == 0 {
            return RrbVector::new();
        }
        if !self.tail.is_empty() {
            let tail = std::mem::replace(&mut self.tail, Vec::with_capacity(BRANCH_FACTOR));
            self.leaves.push(leaf(tail));
        }
        let leaves = std::mem::take(&mut self.leaves);
        self.staged_len = 0;
        RrbVector::from_root(Some(build_level(leaves)))
    }
}

impl<T> Default for RrbVectorBuilder<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> Extend<T> for RrbVectorBuilder<T> {
    fn extend<I: IntoIterator<Item = T>>(&mut self, iter: I) {
        RrbVectorBuilder::extend(self, iter);
    }
}

impl<T: fmt::Debug> fmt::Debug for RrbVectorBuilder<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("RrbVectorBuilder")
            .field("len", &self.len())
            .field("frozen_prefix_len", &self.prefix.len())
            .field("staged_len", &self.staged_len)
            .finish()
    }
}

fn get_node<T>(mut node: &Node<T>, mut index: usize) -> &T {
    loop {
        match node {
            Node::Leaf {
                data, start, len, ..
            } => {
                debug_assert!(index < *len);
                return &data[*start + index];
            }
            Node::Branch { children, .. } => {
                let (child, before) = node.find_child(index);
                index -= before;
                node = children[child].as_ref();
            }
        }
    }
}

fn set_node<T: Clone>(node: &Arc<Node<T>>, index: usize, value: T) -> Arc<Node<T>> {
    match node.as_ref() {
        Node::Leaf {
            data, start, len, ..
        } => {
            let mut replaced = data[*start..*start + *len].to_vec();
            replaced[index] = value;
            leaf(replaced)
        }
        Node::Branch { children, .. } => {
            let (child_index, before) = node.find_child(index);
            let mut replaced = children.iter().cloned().collect::<Vec<_>>();
            replaced[child_index] = set_node(&children[child_index], index - before, value);
            branch(replaced)
        }
    }
}

fn concat_nodes<T: Clone>(left: &Arc<Node<T>>, right: &Arc<Node<T>>) -> Vec<Arc<Node<T>>> {
    match left.height().cmp(&right.height()) {
        std::cmp::Ordering::Equal => concat_same_height(left, right),
        std::cmp::Ordering::Greater => {
            let Node::Branch { children, .. } = left.as_ref() else {
                unreachable!("a taller RRB node must be a branch")
            };
            let mut joined = children[..children.len() - 1].to_vec();
            joined.extend(concat_nodes(
                children.last().expect("branch is non-empty"),
                right,
            ));
            partition(joined)
        }
        std::cmp::Ordering::Less => {
            let Node::Branch { children, .. } = right.as_ref() else {
                unreachable!("a taller RRB node must be a branch")
            };
            let mut joined = concat_nodes(left, &children[0]);
            joined.extend_from_slice(&children[1..]);
            partition(joined)
        }
    }
}

fn concat_same_height<T: Clone>(left: &Arc<Node<T>>, right: &Arc<Node<T>>) -> Vec<Arc<Node<T>>> {
    match (left.as_ref(), right.as_ref()) {
        (
            Node::Leaf {
                data: left_data,
                start: left_start,
                len: left_len,
            },
            Node::Leaf {
                data: right_data,
                start: right_start,
                len: right_len,
            },
        ) => {
            if *left_len == BRANCH_FACTOR && *right_len == BRANCH_FACTOR {
                return vec![Arc::clone(left), Arc::clone(right)];
            }

            let mut combined = Vec::with_capacity(*left_len + *right_len);
            combined.extend_from_slice(&left_data[*left_start..*left_start + *left_len]);
            combined.extend_from_slice(&right_data[*right_start..*right_start + *right_len]);
            if combined.len() <= BRANCH_FACTOR {
                vec![leaf(combined)]
            } else {
                let right_half = combined.split_off(combined.len() / 2);
                vec![leaf(combined), leaf(right_half)]
            }
        }
        (
            Node::Branch {
                children: left_children,
                ..
            },
            Node::Branch {
                children: right_children,
                ..
            },
        ) => {
            let mut joined = left_children[..left_children.len() - 1].to_vec();
            joined.extend(concat_same_height(
                left_children.last().expect("branch is non-empty"),
                &right_children[0],
            ));
            joined.extend_from_slice(&right_children[1..]);
            partition(joined)
        }
        _ => unreachable!("equal-height RRB nodes have the same node kind"),
    }
}

fn partition<T>(mut children: Vec<Arc<Node<T>>>) -> Vec<Arc<Node<T>>> {
    if children.len() <= BRANCH_FACTOR {
        return vec![branch(children)];
    }
    let right = children.split_off(children.len() / 2);
    vec![branch(children), branch(right)]
}

fn split_node<T>(node: &Arc<Node<T>>, index: usize) -> NodeSplit<T> {
    if index == 0 {
        return (None, Some(Arc::clone(node)));
    }
    if index == node.count() {
        return (Some(Arc::clone(node)), None);
    }

    match node.as_ref() {
        Node::Leaf {
            data, start, len, ..
        } => (
            Some(leaf_view(Arc::clone(data), *start, index)),
            Some(leaf_view(Arc::clone(data), *start + index, *len - index)),
        ),
        Node::Branch { children, .. } => {
            let (child_index, before) = node.find_child(index);
            let (child_left, child_right) = split_node(&children[child_index], index - before);

            let mut left = children[..child_index].to_vec();
            left.extend(child_left);
            let mut right = child_right.into_iter().collect::<Vec<_>>();
            right.extend_from_slice(&children[child_index + 1..]);
            (build_same_height(left), build_same_height(right))
        }
    }
}

fn build_same_height<T>(nodes: Vec<Arc<Node<T>>>) -> Option<Arc<Node<T>>> {
    (!nodes.is_empty()).then(|| branch(nodes))
}

fn build_level<T>(mut nodes: Vec<Arc<Node<T>>>) -> Arc<Node<T>> {
    assert!(!nodes.is_empty(), "an RRB level requires at least one node");
    while nodes.len() > 1 {
        nodes = nodes
            .chunks(BRANCH_FACTOR)
            .map(|chunk| branch(chunk.to_vec()))
            .collect();
    }
    nodes.pop().expect("non-empty level has one root")
}

fn leaf<T>(items: Vec<T>) -> Arc<Node<T>> {
    debug_assert!(!items.is_empty() && items.len() <= BRANCH_FACTOR);
    let len = items.len();
    Arc::new(Node::Leaf {
        data: Arc::from(items),
        start: 0,
        len,
    })
}

fn leaf_view<T>(data: Arc<[T]>, start: usize, len: usize) -> Arc<Node<T>> {
    debug_assert!(len > 0 && len <= BRANCH_FACTOR && start + len <= data.len());
    Arc::new(Node::Leaf { data, start, len })
}

fn branch<T>(children: Vec<Arc<Node<T>>>) -> Arc<Node<T>> {
    debug_assert!(!children.is_empty() && children.len() <= BRANCH_FACTOR);
    let child_height = children[0].height();
    debug_assert!(children.iter().all(|child| child.height() == child_height));
    let height = child_height
        .checked_add(1)
        .expect("RRB vector height overflow");
    // Over-height branches are unreachable with constructible inputs (they need a count that
    // exhausts the usize domain), and if one were ever built it is handled correctly: the
    // has_regular_layout guard below routes it onto the relaxed size-table path (no shift UB),
    // and validate_node rejects it. Match the C# reference's failure model (silent build,
    // validate-only) and the C port's recoverable error rather than aborting the process on the
    // normal persistent-construction path; keep the invariant as a debug-time check only.
    debug_assert!(
        height <= MAXIMUM_HEIGHT,
        "RRB vector height exceeds the usize count domain"
    );
    let count = children
        .iter()
        .try_fold(0_usize, |total, child| total.checked_add(child.count()));
    let count = count.expect("RRB vector length overflow");
    let cumulative_sizes = if has_regular_layout(&children, height) {
        None
    } else {
        let mut total = 0_usize;
        Some(Arc::from(
            children
                .iter()
                .map(|child| {
                    total = total
                        .checked_add(child.count())
                        .expect("RRB vector length overflow");
                    total
                })
                .collect::<Vec<_>>(),
        ))
    };
    Arc::new(Node::Branch {
        children: Arc::from(children),
        cumulative_sizes,
        count,
        height,
    })
}

fn child_capacity(height: u8) -> usize {
    1_usize
        .checked_shl(u32::from(height) * RADIX_BITS)
        .unwrap_or(usize::MAX)
}

fn has_regular_layout<T>(children: &[Arc<Node<T>>], height: u8) -> bool {
    if children.is_empty() || height == 0 {
        return false;
    }
    let capacity = child_capacity(height);
    children[..children.len() - 1]
        .iter()
        .all(|child| child.count() == capacity)
        && children
            .last()
            .is_some_and(|child| child.count() <= capacity)
}

#[derive(Default)]
struct ValidationAccumulator {
    leaf_count: usize,
    branch_count: usize,
    regular_branch_count: usize,
    relaxed_branch_count: usize,
    minimum_leaf_len: usize,
    maximum_leaf_len: usize,
    minimum_branching_factor: usize,
    maximum_branching_factor: usize,
}

impl ValidationAccumulator {
    fn add_leaf(&mut self, len: usize) {
        self.leaf_count += 1;
        self.minimum_leaf_len = if self.minimum_leaf_len == 0 {
            len
        } else {
            self.minimum_leaf_len.min(len)
        };
        self.maximum_leaf_len = self.maximum_leaf_len.max(len);
    }

    fn add_branch(&mut self, factor: usize, regular: bool) {
        self.branch_count += 1;
        if regular {
            self.regular_branch_count += 1;
        } else {
            self.relaxed_branch_count += 1;
        }
        self.minimum_branching_factor = if self.minimum_branching_factor == 0 {
            factor
        } else {
            self.minimum_branching_factor.min(factor)
        };
        self.maximum_branching_factor = self.maximum_branching_factor.max(factor);
    }

    fn finish(self, count: usize, height: u8) -> RrbVectorStatistics {
        RrbVectorStatistics {
            count,
            height,
            leaf_count: self.leaf_count,
            branch_count: self.branch_count,
            regular_branch_count: self.regular_branch_count,
            relaxed_branch_count: self.relaxed_branch_count,
            minimum_leaf_len: self.minimum_leaf_len,
            maximum_leaf_len: self.maximum_leaf_len,
            minimum_branching_factor: self.minimum_branching_factor,
            maximum_branching_factor: self.maximum_branching_factor,
        }
    }
}

fn invariant_error(message: &'static str) -> RrbVectorInvariantError {
    RrbVectorInvariantError { message }
}

fn validate_node<T>(
    node: &Node<T>,
    is_root: bool,
    accumulator: &mut ValidationAccumulator,
) -> Result<(usize, u8), RrbVectorInvariantError> {
    match node {
        Node::Leaf { data, start, len } => {
            let valid_range = start.checked_add(*len).is_some_and(|end| end <= data.len());
            if *len == 0 || *len > BRANCH_FACTOR || !valid_range {
                return Err(invariant_error("leaf length is outside 1..=32"));
            }
            accumulator.add_leaf(*len);
            Ok((*len, 0))
        }
        Node::Branch {
            children,
            cumulative_sizes,
            count,
            height,
        } => {
            if *height == 0 || *height > MAXIMUM_HEIGHT {
                return Err(invariant_error("branch height is outside the count domain"));
            }
            if children.is_empty() || children.len() > BRANCH_FACTOR {
                return Err(invariant_error("branching factor is outside 1..=32"));
            }
            if is_root && children.len() == 1 {
                return Err(invariant_error("root branch is not collapsed"));
            }
            let regular = cumulative_sizes.is_none();
            accumulator.add_branch(children.len(), regular);

            let mut actual_count = 0_usize;
            let mut child_height = None;
            for child in children.iter() {
                let (child_count, actual_height) = validate_node(child, false, accumulator)?;
                if child_height.is_some_and(|expected| expected != actual_height) {
                    return Err(invariant_error("branch children have unequal heights"));
                }
                child_height = Some(actual_height);
                actual_count = actual_count
                    .checked_add(child_count)
                    .ok_or_else(|| invariant_error("validated count overflow"))?;
            }
            let actual_height = child_height
                .expect("non-empty branch has a child height")
                .checked_add(1)
                .ok_or_else(|| invariant_error("validated height overflow"))?;
            if actual_count != *count || actual_height != *height {
                return Err(invariant_error("branch cached count or height is stale"));
            }

            let regular_layout = has_regular_layout(children, *height);
            match cumulative_sizes {
                None if !regular_layout => {
                    return Err(invariant_error("irregular branch omitted its size table"));
                }
                Some(_) if regular_layout => {
                    return Err(invariant_error("regular branch retained a size table"));
                }
                Some(sizes) => {
                    if sizes.len() != children.len() {
                        return Err(invariant_error("relaxed size-table length is stale"));
                    }
                    let mut cumulative = 0_usize;
                    for (child, size) in children.iter().zip(sizes.iter()) {
                        cumulative = cumulative
                            .checked_add(child.count())
                            .ok_or_else(|| invariant_error("relaxed size-table overflow"))?;
                        if cumulative != *size {
                            return Err(invariant_error("relaxed cumulative size is stale"));
                        }
                    }
                }
                None => {}
            }

            Ok((actual_count, actual_height))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashSet;
    use std::thread;

    #[test]
    fn construction_indexes_every_radix_boundary() {
        for count in [0, 1, 31, 32, 33, 1_023, 1_024, 1_025, 100_000] {
            let vector: RrbVector<_> = (0..count).collect();
            assert_eq!(vector.len(), count);
            assert_eq!(
                vector.iter().copied().collect::<Vec<_>>(),
                (0..count).collect::<Vec<_>>()
            );
            for index in (0..count).step_by((count / 101).max(1)) {
                assert_eq!(vector.get(index), Some(&index));
                assert_eq!(vector[index], index);
            }
            let statistics = vector.validate_structure().unwrap();
            assert_eq!(statistics.count, count);
            assert_eq!(statistics.relaxed_branch_count, 0);
        }
    }

    #[test]
    fn concat_rebalances_unequal_boundary_spines() {
        for (left_len, right_len) in [
            (1, 100_000),
            (100_000, 1),
            (31, 33),
            (1_023, 1_025),
            (50_000, 50_000),
        ] {
            let left: RrbVector<_> = (0..left_len).collect();
            let right: RrbVector<_> = (left_len..left_len + right_len).collect();
            let combined = left.concat(&right);
            assert_eq!(combined.len(), left_len + right_len);
            assert!(combined.iter().copied().eq(0..left_len + right_len));
            combined.validate_structure().unwrap();
            assert!(left.shares_root_with(&left.concat(&RrbVector::new())));
            assert!(right.shares_root_with(&RrbVector::new().concat(&right)));
        }
    }

    #[test]
    fn splits_round_trip_and_exact_leaf_boundaries_share_every_leaf() {
        let vector: RrbVector<_> = (0..10_000).collect();
        for index in [0, 1, 31, 32, 33, 999, 1_024, 5_000, 9_999, 10_000] {
            let split = vector.split_at(index).unwrap();
            assert!(split.left.iter().copied().eq(0..index));
            assert!(split.right.iter().copied().eq(index..10_000));
            assert_eq!(split.left.concat(&split.right), vector);
        }
        assert!(vector.shares_root_with(&vector.split_at(0).unwrap().right));
        assert!(vector.shares_root_with(&vector.split_at(vector.len()).unwrap().left));

        let packed: RrbVector<_> = (0..BRANCH_FACTOR * 128).collect();
        let original = packed.leaf_identities();
        for index in [32, 64, 32 * 31, 32 * 32, 32 * 33, 32 * 96, 32 * 127] {
            let split = packed.split_at(index).unwrap();
            let retained = split
                .left
                .leaf_identities()
                .into_iter()
                .chain(split.right.leaf_identities())
                .collect::<Vec<_>>();
            assert_eq!(retained, original);
            split.left.validate_structure().unwrap();
            split.right.validate_structure().unwrap();
        }
    }

    #[test]
    fn packed_nodes_omit_sizes_and_non_aligned_suffixes_are_relaxed() {
        let packed: RrbVector<_> = (0..BRANCH_FACTOR * 1_024).collect();
        let packed_statistics = packed.validate_structure().unwrap();
        assert!(packed_statistics.regular_branch_count > 0);
        assert_eq!(packed_statistics.relaxed_branch_count, 0);

        let relaxed = packed.split_at(1).unwrap().right;
        let relaxed_statistics = relaxed.validate_structure().unwrap();
        assert!(relaxed_statistics.relaxed_branch_count > 0);
        assert!(relaxed.iter().copied().eq(1..packed.len()));
        for index in (0..relaxed.len()).step_by(997) {
            assert_eq!(relaxed[index], index + 1);
        }
    }

    #[test]
    fn validator_admits_slack_cap_and_rejects_the_next_height() {
        let mut root = leaf(vec![0]);
        for _ in 0..MAXIMUM_HEIGHT {
            let child_count = root.count();
            let height = root.height() + 1;
            let children = Arc::<[Arc<Node<_>>]>::from([Arc::clone(&root), root]);
            root = Arc::new(Node::Branch {
                cumulative_sizes: Some(Arc::from([child_count, child_count * 2])),
                children,
                count: child_count * 2,
                height,
            });
        }
        let maximum_height_vector = RrbVector {
            root: Some(Arc::clone(&root)),
        };
        assert_eq!(
            maximum_height_vector.validate_structure().unwrap().height,
            MAXIMUM_HEIGHT
        );

        let child_count = root.count();
        let height = root.height() + 1;
        let children = Arc::<[Arc<Node<_>>]>::from([Arc::clone(&root), root]);
        let root = Arc::new(Node::Branch {
            cumulative_sizes: Some(Arc::from([child_count, child_count * 2])),
            children,
            count: child_count * 2,
            height,
        });
        let vector = RrbVector { root: Some(root) };
        assert_eq!(
            vector.validate_structure().unwrap_err().message,
            "branch height is outside the count domain"
        );
    }

    #[test]
    fn adversarial_split_concat_history_preserves_density_and_height() {
        let count = BRANCH_FACTOR * 1_024;
        let mut vector: RrbVector<_> = (0..count).collect();
        let mut state = 0x2026_0724_u32;
        for operation in 0..2_000 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            let boundary = match operation % 7 {
                0 => 1,
                1 => 31,
                2 => 32,
                3 => 1_023,
                4 => 1_024,
                5 => count - 1,
                _ => 1 + state as usize % (count - 1),
            };
            let split = vector.split_at(boundary).unwrap();
            vector = split.left.concat(&split.right);
            if operation % 127 == 0 {
                vector.validate_structure().unwrap();
            }
        }

        assert!(vector.iter().copied().eq(0..count));
        let statistics = vector.validate_structure().unwrap();
        assert!(statistics.height <= minimum_height(count) + 1);
        assert!(statistics.leaf_count <= count.div_ceil(16));
        assert!(statistics.branch_count < statistics.leaf_count);
    }

    #[test]
    fn adversarial_uneven_fragments_remain_dense() {
        let mut vector = RrbVector::new();
        let mut model = Vec::new();
        for fragment in 0..2_048 {
            let len = 1 + fragment * 17 % 63;
            let values = (model.len()..model.len() + len).collect::<Vec<_>>();
            vector = vector.concat(&values.iter().copied().collect());
            model.extend(values);
        }
        assert_eq!(vector.iter().copied().collect::<Vec<_>>(), model);
        let statistics = vector.validate_structure().unwrap();
        assert!(statistics.height <= minimum_height(vector.len()) + 1);
        assert!(statistics.leaf_count <= vector.len().div_ceil(16));
    }

    #[test]
    fn ten_thousand_randomized_edits_match_vec_and_retain_snapshots() {
        let mut state = 0x1357_9bdf_u32;
        let mut vector = RrbVector::new();
        let mut model = Vec::new();
        let mut snapshots = Vec::new();

        for step in 0..10_000_i32 {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            match state % 5 {
                0 => {
                    vector = vector.push_back(step);
                    model.push(step);
                }
                1 => {
                    vector = vector.push_front(step);
                    model.insert(0, step);
                }
                2 if !model.is_empty() => {
                    let index = (state as usize / 7) % model.len();
                    vector = vector.set_item(index, -step).unwrap();
                    model[index] = -step;
                }
                3 => {
                    let index = (state as usize / 11) % (model.len() + 1);
                    let values = [step, step + 1, step + 2];
                    vector = vector.insert_range(index, values).unwrap();
                    model.splice(index..index, values);
                }
                4 if !model.is_empty() => {
                    let index = (state as usize / 13) % model.len();
                    let count = (state as usize / 17) % (model.len() - index + 1);
                    vector = vector.remove_range(index, count).unwrap();
                    model.drain(index..index + count);
                }
                _ => {}
            }

            assert_eq!(vector.len(), model.len());
            if step % 701 == 0 {
                snapshots.push((vector.clone(), model.clone()));
            }
            if step % 997 == 0 {
                vector.validate_structure().unwrap();
            }
        }

        assert_eq!(vector.iter().copied().collect::<Vec<_>>(), model);
        for (snapshot, expected) in snapshots {
            assert_eq!(snapshot.iter().copied().collect::<Vec<_>>(), expected);
        }
    }

    #[test]
    fn no_ops_endpoints_and_ranges_preserve_contracts() {
        let empty = RrbVector::<String>::new();
        assert!(empty.pop_back().is_none());
        assert!(empty.pop_front().is_none());

        let vector: RrbVector<_> = ["a".to_owned(), "b".to_owned(), "c".to_owned()]
            .into_iter()
            .collect();
        let unchanged = vector.set_item(1, "b".to_owned()).unwrap();
        assert!(vector.shares_root_with(&unchanged));
        assert!(vector.shares_root_with(&vector.remove_range(1, 0).unwrap()));
        assert!(vector.shares_root_with(&vector.insert_range(2, []).unwrap()));
        assert!(vector.shares_root_with(&vector.get_range(0, vector.len()).unwrap()));

        let popped = vector.pop_back().unwrap();
        assert_eq!(popped.value, "c");
        assert_eq!(popped.rest.to_vec(), ["a", "b"]);
        assert_eq!(vector.to_vec(), ["a", "b", "c"]);
    }

    #[test]
    fn builder_caches_isolated_snapshots_and_adopts_prefixes() {
        let mut builder = RrbVector::builder();
        let mut source = (0..1_000).collect::<Vec<_>>();
        builder.extend(source.iter().copied());
        source[0] = -1;
        let first = builder.to_immutable();
        let cached = builder.to_immutable();
        assert!(first.shares_root_with(&cached));
        assert!(first.iter().copied().eq(0..1_000));
        assert_eq!(first.validate_structure().unwrap().relaxed_branch_count, 0);

        builder.push(1_000).extend(1_001..2_000);
        let second = builder.to_immutable();
        assert!(first.iter().copied().eq(0..1_000));
        assert!(second.iter().copied().eq(0..2_000));
        let first_leaves = first.leaf_identities().into_iter().collect::<HashSet<_>>();
        assert!(
            second
                .leaf_identities()
                .iter()
                .filter(|leaf| first_leaves.contains(leaf))
                .count()
                >= first_leaves.len().saturating_sub(1)
        );

        let mut adopted = second.to_builder();
        assert!(second.shares_root_with(&adopted.to_immutable()));
        adopted.push(2_000);
        let third = adopted.to_immutable();
        assert!(second.iter().copied().eq(0..2_000));
        assert!(third.iter().copied().eq(0..=2_000));
        assert_eq!(adopted.iter().copied().count(), 2_001);
        adopted.clear();
        assert!(adopted.is_empty());
        assert!(adopted.to_immutable().is_empty());
    }

    #[test]
    fn read_only_and_structural_apis_do_not_require_clone() {
        let vector: RrbVector<Box<dyn Fn() -> i32>> = [
            Box::new(|| 1_i32) as Box<dyn Fn() -> i32>,
            Box::new(|| 2_i32) as Box<dyn Fn() -> i32>,
        ]
        .into_iter()
        .collect();
        assert_eq!(vector.len(), 2);
        assert_eq!(vector.get(0).map(|function| function()), Some(1));
        assert_eq!(vector.get(1).map(|function| function()), Some(2));
        assert_eq!(vector.iter().count(), 2);
        assert_eq!(vector.split_at(1).unwrap().left.len(), 1);
        assert_eq!(vector.get_range(1, 1).unwrap().len(), 1);
        vector.validate_structure().unwrap();
    }

    #[test]
    fn snapshots_are_send_sync_and_safe_for_concurrent_readers() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<RrbVector<i32>>();

        let vector: RrbVector<_> = (0..10_000).collect();
        let handles = (0..8)
            .map(|_| {
                let snapshot = vector.clone();
                thread::spawn(move || {
                    for _ in 0..64 {
                        assert_eq!(snapshot.len(), 10_000);
                        assert_eq!(snapshot.get(4_321), Some(&4_321));
                        assert!(snapshot.iter().copied().eq(0..10_000));
                    }
                })
            })
            .collect::<Vec<_>>();
        for handle in handles {
            handle.join().expect("RRB reader failed");
        }
    }

    fn minimum_height(count: usize) -> u8 {
        let mut height = 0;
        let mut capacity = BRANCH_FACTOR;
        while capacity < count {
            capacity *= BRANCH_FACTOR;
            height += 1;
        }
        height
    }
}
