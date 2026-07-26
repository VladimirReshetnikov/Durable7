//! Persistent catenable deques, with and without O(1) reversal.
//!
//! [`PersistentDeque`] is a size-annotated binary tree behind an [`Arc`](std::sync::Arc): pushes and
//! pops at either end, indexing, splitting, and concatenation all share structure with the version
//! they were derived from, and every prior version stays valid.
//!
//! [`ReversibleDeque`] wraps that same representation and adds a reversal that is O(1) because it
//! flips an orientation flag instead of rebuilding the tree; all sequence operations then read
//! through the current orientation.
//!
//! Both types come with immutable gap cursors ([`PersistentDequeCursor`],
//! [`ReversibleDequeCursor`]) denoting a position in `0..=len`, and with result types that report
//! the pieces of a structural operation: [`DequePop`], [`DequeSplit`], [`DequeItemSplit`],
//! [`DequeRangeSplit`], [`ReversibleDequePop`], and [`ReversibleDequeSplit`].

use std::cmp::Ordering;
use std::fmt;
use std::sync::Arc;

/// A persistent double-ended queue with structural sharing between versions.
///
/// Cloning is O(1) and shares the whole representation; every operation returns a new deque and
/// leaves the receiver untouched.
pub struct PersistentDeque<T> {
    root: Arc<DequeTree<T>>,
}

/// Immutable snapshot-plus-position gap cursor over a persistent deque.
#[derive(Debug, PartialEq, Eq)]
pub struct PersistentDequeCursor<T> {
    deque: PersistentDeque<T>,
    position: usize,
}

impl<T> Clone for PersistentDeque<T> {
    fn clone(&self) -> Self {
        Self {
            root: Arc::clone(&self.root),
        }
    }
}

/// The two deques produced by a positional split.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeSplit<T> {
    pub left: PersistentDeque<T>,
    pub right: PersistentDeque<T>,
}

/// The pieces produced by splitting around one element: what precedes it, the element, and what
/// follows it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeItemSplit<T> {
    pub left: PersistentDeque<T>,
    pub item: T,
    pub right: PersistentDeque<T>,
}

/// The three deques produced by splitting out a range: before it, the range itself, and after it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequeRangeSplit<T> {
    pub before: PersistentDeque<T>,
    pub range: PersistentDeque<T>,
    pub after: PersistentDeque<T>,
}

/// An endpoint element and the deque that remains after removing it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DequePop<T> {
    pub value: T,
    pub rest: PersistentDeque<T>,
}

/// The two reversible deques produced by a positional split, each keeping the source orientation.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReversibleDequeSplit<T> {
    pub left: ReversibleDeque<T>,
    pub right: ReversibleDeque<T>,
}

/// An endpoint element and the reversible deque that remains after removing it.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReversibleDequePop<T> {
    pub value: T,
    pub rest: ReversibleDeque<T>,
}

/// Immutable logical-order gap cursor over a reversible deque.
#[derive(Debug, PartialEq, Eq)]
pub struct ReversibleDequeCursor<T> {
    deque: ReversibleDeque<T>,
    position: usize,
}

impl<T> Clone for PersistentDequeCursor<T> {
    fn clone(&self) -> Self {
        Self {
            deque: self.deque.clone(),
            position: self.position,
        }
    }
}

impl<T> Clone for ReversibleDequeCursor<T> {
    fn clone(&self) -> Self {
        Self {
            deque: self.deque.clone(),
            position: self.position,
        }
    }
}

enum DequeTree<T> {
    Empty,
    Leaf(T),
    Node {
        left: Arc<DequeTree<T>>,
        right: Arc<DequeTree<T>>,
        first: Arc<DequeTree<T>>,
        last: Arc<DequeTree<T>>,
        len: usize,
        height: u8,
    },
    Reversed {
        inner: Arc<DequeTree<T>>,
        first: Arc<DequeTree<T>>,
        last: Arc<DequeTree<T>>,
        len: usize,
        height: u8,
    },
}

impl<T> DequeTree<T> {
    fn empty() -> Arc<Self> {
        Arc::new(Self::Empty)
    }

    fn leaf(item: T) -> Arc<Self> {
        Arc::new(Self::Leaf(item))
    }

    fn from_vec(mut items: Vec<T>) -> Arc<Self> {
        match items.len() {
            0 => Self::empty(),
            1 => Self::leaf(items.pop().expect("single-item vector has an item")),
            len => {
                let right = items.split_off(len / 2);
                Self::make_node(Self::from_vec(items), Self::from_vec(right))
            }
        }
    }

    fn len(&self) -> usize {
        match self {
            Self::Empty => 0,
            Self::Leaf(_) => 1,
            Self::Node { len, .. } | Self::Reversed { len, .. } => *len,
        }
    }

    fn height(&self) -> u8 {
        match self {
            Self::Empty => 0,
            Self::Leaf(_) => 1,
            Self::Node { height, .. } | Self::Reversed { height, .. } => *height,
        }
    }

    fn mirror(tree: &Arc<Self>) -> Arc<Self> {
        match tree.as_ref() {
            Self::Empty | Self::Leaf(_) => Arc::clone(tree),
            Self::Reversed { inner, .. } => Arc::clone(inner),
            _ => Arc::new(Self::Reversed {
                inner: Arc::clone(tree),
                first: Self::last_leaf(tree),
                last: Self::first_leaf(tree),
                len: tree.len(),
                height: tree.height(),
            }),
        }
    }

    fn logical_children(tree: &Arc<Self>) -> Option<(Arc<Self>, Arc<Self>)> {
        match tree.as_ref() {
            Self::Node { left, right, .. } => Some((Arc::clone(left), Arc::clone(right))),
            Self::Reversed { inner, .. } => match inner.as_ref() {
                Self::Node { left, right, .. } => Some((Self::mirror(right), Self::mirror(left))),
                _ => None,
            },
            _ => None,
        }
    }

    fn make_node(left: Arc<Self>, right: Arc<Self>) -> Arc<Self> {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let len = left.len() + right.len();
        let height = left.height().max(right.height()) + 1;
        let first = Self::first_leaf(&left);
        let last = Self::last_leaf(&right);
        Arc::new(Self::Node {
            left,
            right,
            first,
            last,
            len,
            height,
        })
    }

    fn first_leaf(tree: &Arc<Self>) -> Arc<Self> {
        match tree.as_ref() {
            Self::Leaf(_) => Arc::clone(tree),
            Self::Node { first, .. } | Self::Reversed { first, .. } => Arc::clone(first),
            Self::Empty => unreachable!("empty trees do not have endpoint signposts"),
        }
    }

    fn last_leaf(tree: &Arc<Self>) -> Arc<Self> {
        match tree.as_ref() {
            Self::Leaf(_) => Arc::clone(tree),
            Self::Node { last, .. } | Self::Reversed { last, .. } => Arc::clone(last),
            Self::Empty => unreachable!("empty trees do not have endpoint signposts"),
        }
    }

    fn concat(left: Arc<Self>, right: Arc<Self>) -> Arc<Self> {
        if left.len() == 0 {
            return right;
        }

        if right.len() == 0 {
            return left;
        }

        let left_height = left.height();
        let right_height = right.height();
        if left_height > right_height + 1
            && let Some((left_left, left_right)) = Self::logical_children(&left)
        {
            let joined = Self::concat(Arc::clone(&left_right), right);
            return Self::balance(Arc::clone(&left_left), joined);
        }

        if right_height > left_height + 1
            && let Some((right_left, right_right)) = Self::logical_children(&right)
        {
            let joined = Self::concat(left, Arc::clone(&right_left));
            return Self::balance(joined, Arc::clone(&right_right));
        }

        Self::balance(left, right)
    }

    fn balance(left: Arc<Self>, right: Arc<Self>) -> Arc<Self> {
        if left.len() == 0 || right.len() == 0 {
            return Self::make_node(left, right);
        }

        let left_height = left.height();
        let right_height = right.height();
        if left_height > right_height + 1
            && let Some((left_left, left_right)) = Self::logical_children(&left)
        {
            if left_left.height() >= left_right.height() {
                return Self::make_node(
                    Arc::clone(&left_left),
                    Self::make_node(Arc::clone(&left_right), right),
                );
            }

            if let Some((middle_left, middle_right)) = Self::logical_children(&left_right) {
                return Self::make_node(
                    Self::make_node(Arc::clone(&left_left), Arc::clone(&middle_left)),
                    Self::make_node(Arc::clone(&middle_right), right),
                );
            }
        }

        if right_height > left_height + 1
            && let Some((right_left, right_right)) = Self::logical_children(&right)
        {
            if right_right.height() >= right_left.height() {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(&right_left)),
                    Arc::clone(&right_right),
                );
            }

            if let Some((middle_left, middle_right)) = Self::logical_children(&right_left) {
                return Self::make_node(
                    Self::make_node(left, Arc::clone(&middle_left)),
                    Self::make_node(Arc::clone(&middle_right), Arc::clone(&right_right)),
                );
            }
        }

        Self::make_node(left, right)
    }

    fn split_at(tree: &Arc<Self>, index: usize) -> (Arc<Self>, Arc<Self>) {
        if index == 0 {
            return (Self::empty(), Arc::clone(tree));
        }

        if index == tree.len() {
            return (Arc::clone(tree), Self::empty());
        }

        match tree.as_ref() {
            Self::Empty => (Self::empty(), Self::empty()),
            Self::Leaf(_) => {
                debug_assert_eq!(index, 0);
                (Self::empty(), Arc::clone(tree))
            }
            Self::Reversed { inner, len, .. } => {
                let (before, after) = Self::split_at(inner, len - index);
                (Self::mirror(&after), Self::mirror(&before))
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                match index.cmp(&left_len) {
                    Ordering::Less => {
                        let (before, after) = Self::split_at(left, index);
                        (before, Self::concat(after, Arc::clone(right)))
                    }
                    Ordering::Equal => (Arc::clone(left), Arc::clone(right)),
                    Ordering::Greater => {
                        let (before, after) = Self::split_at(right, index - left_len);
                        (Self::concat(Arc::clone(left), before), after)
                    }
                }
            }
        }
    }

    fn first(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf(item) => Some(item),
            Self::Node { first, .. } | Self::Reversed { first, .. } => match first.as_ref() {
                Self::Leaf(item) => Some(item),
                _ => unreachable!("endpoint signposts always reference leaves"),
            },
        }
    }

    fn last(&self) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf(item) => Some(item),
            Self::Node { last, .. } | Self::Reversed { last, .. } => match last.as_ref() {
                Self::Leaf(item) => Some(item),
                _ => unreachable!("endpoint signposts always reference leaves"),
            },
        }
    }

    fn get(&self, index: usize) -> Option<&T> {
        match self {
            Self::Empty => None,
            Self::Leaf(item) => (index == 0).then_some(item),
            Self::Reversed { inner, len, .. } => inner.get(len - 1 - index),
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

    fn set(tree: &Arc<Self>, index: usize, item: T) -> Option<Arc<Self>> {
        match tree.as_ref() {
            Self::Empty => None,
            Self::Leaf(_) => (index == 0).then(|| Self::leaf(item)),
            Self::Reversed { inner, len, .. } => {
                Some(Self::mirror(&Self::set(inner, len - 1 - index, item)?))
            }
            Self::Node { left, right, .. } => {
                let left_len = left.len();
                if index < left_len {
                    Some(Self::balance(
                        Self::set(left, index, item)?,
                        Arc::clone(right),
                    ))
                } else {
                    Some(Self::balance(
                        Arc::clone(left),
                        Self::set(right, index - left_len, item)?,
                    ))
                }
            }
        }
    }

    fn bound_index<F>(&self, predicate: &mut F) -> usize
    where
        F: FnMut(&T) -> bool,
    {
        self.bound_index_oriented(false, predicate)
    }

    fn bound_index_oriented<F>(&self, reversed: bool, predicate: &mut F) -> usize
    where
        F: FnMut(&T) -> bool,
    {
        match self {
            Self::Empty => 0,
            Self::Leaf(item) => usize::from(!predicate(item)),
            Self::Reversed { inner, .. } => inner.bound_index_oriented(!reversed, predicate),
            Self::Node { left, right, .. } => {
                if reversed {
                    let left_last = right
                        .first()
                        .expect("non-empty reversed-left child has a last leaf");
                    if predicate(left_last) {
                        right.bound_index_oriented(true, predicate)
                    } else {
                        right.len() + left.bound_index_oriented(true, predicate)
                    }
                } else {
                    let left_last = left.last().expect("non-empty left child has a last leaf");
                    if predicate(left_last) {
                        left.bound_index_oriented(false, predicate)
                    } else {
                        left.len() + right.bound_index_oriented(false, predicate)
                    }
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
            Self::Leaf(item) => destination.push(item.clone()),
            Self::Reversed { inner, .. } => inner.copy_to_vec_reversed(destination),
            Self::Node { left, right, .. } => {
                left.copy_to_vec(destination);
                right.copy_to_vec(destination);
            }
        }
    }

    fn copy_to_vec_reversed(&self, destination: &mut Vec<T>)
    where
        T: Clone,
    {
        match self {
            Self::Empty => {}
            Self::Leaf(item) => destination.push(item.clone()),
            Self::Reversed { inner, .. } => inner.copy_to_vec(destination),
            Self::Node { left, right, .. } => {
                right.copy_to_vec_reversed(destination);
                left.copy_to_vec_reversed(destination);
            }
        }
    }

    #[cfg(test)]
    fn validate(&self) -> Result<usize, String> {
        match self {
            Self::Empty => Ok(0),
            Self::Leaf(_) => Ok(1),
            Self::Reversed {
                inner,
                first,
                last,
                len,
                height,
            } => {
                let inner_len = inner.validate()?;
                if inner_len != *len {
                    return Err(format!(
                        "reversed node cached length {len} disagrees with inner total {inner_len}"
                    ));
                }

                if inner.height() != *height {
                    return Err(format!(
                        "reversed node cached height {height} disagrees with inner height {}",
                        inner.height()
                    ));
                }

                if !Arc::ptr_eq(first, &Self::last_leaf(inner))
                    || !Arc::ptr_eq(last, &Self::first_leaf(inner))
                {
                    return Err(
                        "reversed node endpoint signposts disagree with its inner tree".into(),
                    );
                }

                Ok(inner_len)
            }
            Self::Node {
                left,
                right,
                first,
                last,
                len,
                height,
            } => {
                let left_len = left.validate()?;
                let right_len = right.validate()?;
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

                if !Arc::ptr_eq(first, &Self::first_leaf(left))
                    || !Arc::ptr_eq(last, &Self::last_leaf(right))
                {
                    return Err("node endpoint signposts disagree with its children".into());
                }

                Ok(expected_len)
            }
        }
    }
}

impl<T> PersistentDeque<T> {
    /// Creates an empty deque.
    #[must_use]
    pub fn new() -> Self {
        Self {
            root: DequeTree::empty(),
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self {
            root: DequeTree::from_vec(items),
        }
    }

    /// Returns the number of elements. O(1); the count is cached at the root.
    #[must_use]
    pub fn len(&self) -> usize {
        self.root.len()
    }

    /// Returns `true` when the deque holds no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.len() == 0
    }

    /// Creates a cursor before the first element.
    #[must_use]
    pub fn cursor(&self) -> PersistentDequeCursor<T> {
        PersistentDequeCursor {
            deque: self.clone(),
            position: 0,
        }
    }

    /// Creates a cursor at a boundary in `0..=len`.
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<PersistentDequeCursor<T>> {
        (position <= self.len()).then(|| PersistentDequeCursor {
            deque: self.clone(),
            position,
        })
    }

    /// Borrows the first element, or `None` when empty. O(log n).
    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.root.first()
    }

    /// Borrows the last element, or `None` when empty. O(log n).
    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.root.last()
    }

    /// Borrows the element at `index`, or `None` when out of range. O(log n).
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        (index < self.len()).then(|| self.root.get(index)).flatten()
    }

    /// Iterates the elements in sequence order.
    pub fn iter(&self) -> Iter<'_, T> {
        Iter::new(&self.root)
    }

    /// Reports whether two deques are backed by the same tree, so neither can observe an edit made to
    /// the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        fn shares_root<T>(left: &Arc<DequeTree<T>>, right: &Arc<DequeTree<T>>) -> bool {
            if Arc::ptr_eq(left, right) {
                return true;
            }

            match (left.as_ref(), right.as_ref()) {
                (DequeTree::Reversed { inner, .. }, _) => shares_root(inner, right),
                (_, DequeTree::Reversed { inner, .. }) => shares_root(left, inner),
                _ => false,
            }
        }

        (self.is_empty() && other.is_empty()) || shares_root(&self.root, &other.root)
    }

    #[must_use]
    fn reversed_view(&self) -> Self {
        Self {
            root: DequeTree::mirror(&self.root),
        }
    }

    /// Returns a deque with `item` added at the front. The receiver is unchanged and shares almost
    /// all of the result's structure.
    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        Self {
            root: DequeTree::concat(DequeTree::leaf(item), Arc::clone(&self.root)),
        }
    }

    /// Returns a deque with `item` added at the back. The receiver is unchanged.
    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        Self {
            root: DequeTree::concat(Arc::clone(&self.root), DequeTree::leaf(item)),
        }
    }

    /// Concatenates two deques, placing every element of `other` after every element of `self`.
    ///
    /// O(log n) rather than proportional to either operand: the two trees are joined, not copied.
    /// Concatenating with an empty deque shares the non-empty operand.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        if self.is_empty() {
            return other.clone();
        }

        if other.is_empty() {
            return self.clone();
        }

        Self {
            root: DequeTree::concat(Arc::clone(&self.root), Arc::clone(&other.root)),
        }
    }

    /// Returns a deque without its first element, or `None` when empty. Use [`Self::pop_first`] to
    /// recover the removed element as well.
    #[must_use]
    pub fn remove_first(&self) -> Option<Self> {
        (!self.is_empty()).then(|| self.split_at(1).expect("one is a valid split").right)
    }

    /// Returns a deque without its last element, or `None` when empty. Use [`Self::pop_last`] to
    /// recover the removed element as well.
    #[must_use]
    pub fn remove_last(&self) -> Option<Self> {
        let split_index = self.len().checked_sub(1)?;
        Some(
            self.split_at(split_index)
                .expect("len - 1 is a valid split")
                .left,
        )
    }

    /// Replaces the element at `index`, or returns `None` when out of range. O(log n).
    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        (index < self.len()).then(|| Self {
            root: DequeTree::set(&self.root, index, item).expect("validated index must update"),
        })
    }

    /// Replaces the element at `index` with the result of applying `updater` to it, or returns `None`
    /// when out of range.
    ///
    /// Avoids the caller having to read the element and write it back as two separate O(log n)
    /// descents.
    #[must_use]
    pub fn update_at<F>(&self, index: usize, updater: F) -> Option<Self>
    where
        F: FnOnce(&T) -> T,
    {
        let current = self.get(index)?;
        self.set_item(index, updater(current))
    }

    /// Inserts `item` so that it ends up at `index`, or returns `None` when `index` exceeds the
    /// length. O(log n) — a split and two joins, not a shift of the tail.
    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        if index > self.len() {
            return None;
        }

        let split = self.split_at(index).expect("validated index must split");
        Some(split.left.push_back(item).concat(&split.right))
    }

    /// Inserts every element of `items` at `index`, in order, or returns `None` when `index` exceeds
    /// the length.
    ///
    /// Splits once and joins once regardless of how many elements are inserted.
    #[must_use]
    pub fn insert_range<I>(&self, index: usize, items: I) -> Option<Self>
    where
        I: IntoIterator<Item = T>,
    {
        if index > self.len() {
            return None;
        }

        let inserted = Self::from_vec(items.into_iter().collect());
        if inserted.is_empty() {
            return Some(self.clone());
        }

        let split = self.split_at(index).expect("validated index must split");
        Some(split.left.concat(&inserted).concat(&split.right))
    }

    /// Removes the element at `index`, or returns `None` when out of range. O(log n).
    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        if index >= self.len() {
            return None;
        }

        let split = self.split_range(index, 1)?;
        Some(split.before.concat(&split.after))
    }

    /// Removes `count` elements starting at `index`, or returns `None` when the range falls outside
    /// the deque. O(log n) regardless of `count`.
    #[must_use]
    pub fn remove_range(&self, index: usize, count: usize) -> Option<Self> {
        let split = self.split_range(index, count)?;
        Some(split.before.concat(&split.after))
    }

    /// Returns the `count` elements starting at `index` as a new deque, or `None` when the range
    /// falls outside the deque. O(log n); the result shares structure with the receiver.
    #[must_use]
    pub fn get_range(&self, index: usize, count: usize) -> Option<Self> {
        Some(self.split_range(index, count)?.range)
    }

    /// Splits into the elements before `index` and those from `index` on, or returns `None` when
    /// `index` exceeds the length. O(log n); both halves share structure with the receiver.
    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<DequeSplit<T>> {
        if index > self.len() {
            return None;
        }

        let (left, right) = DequeTree::split_at(&self.root, index);
        Some(DequeSplit {
            left: Self { root: left },
            right: Self { root: right },
        })
    }

    /// Splits into the elements before `index`, the `count` elements from `index`, and the rest, or
    /// returns `None` when the range falls outside the deque.
    #[must_use]
    pub fn split_range(&self, index: usize, count: usize) -> Option<DequeRangeSplit<T>> {
        let end = checked_range_end(self.len(), index, count)?;
        let (before, rest) = DequeTree::split_at(&self.root, index);
        let (range, after) = DequeTree::split_at(&rest, end - index);
        Some(DequeRangeSplit {
            before: Self { root: before },
            range: Self { root: range },
            after: Self { root: after },
        })
    }

    /// Appends every element of `items`, in order, publishing only the final deque.
    #[must_use]
    pub fn add_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        self.concat(&Self::from_vec(items.into_iter().collect()))
    }

    #[cfg(test)]
    pub(crate) fn tree_depth(&self) -> usize {
        self.root.height() as usize
    }

    #[cfg(test)]
    pub(crate) fn validate_invariants(&self) {
        let len = self
            .root
            .validate()
            .expect("persistent deque tree invariant failure");
        assert_eq!(len, self.len());
    }

    #[cfg(test)]
    pub(crate) fn shared_node_count_with(&self, other: &Self) -> usize {
        use std::collections::HashSet;

        fn collect<T>(tree: &Arc<DequeTree<T>>, seen: &mut HashSet<*const DequeTree<T>>) {
            if !seen.insert(Arc::as_ptr(tree)) {
                return;
            }

            if let DequeTree::Node { left, right, .. } = tree.as_ref() {
                collect(left, seen);
                collect(right, seen);
            } else if let DequeTree::Reversed { inner, .. } = tree.as_ref() {
                collect(inner, seen);
            }
        }

        fn count<T>(tree: &Arc<DequeTree<T>>, seen: &HashSet<*const DequeTree<T>>) -> usize {
            let mut total = usize::from(seen.contains(&Arc::as_ptr(tree)));
            if let DequeTree::Node { left, right, .. } = tree.as_ref() {
                total += count(left, seen);
                total += count(right, seen);
            } else if let DequeTree::Reversed { inner, .. } = tree.as_ref() {
                total += count(inner, seen);
            }

            total
        }

        let mut seen = HashSet::new();
        collect(&self.root, &mut seen);
        count(&other.root, &seen)
    }
}

impl<T> PersistentDequeCursor<T> {
    /// Returns the element count of the deque version this cursor is positioned in.
    #[must_use]
    pub fn len(&self) -> usize {
        self.deque.len()
    }

    /// Returns `true` when that deque version holds no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.deque.is_empty()
    }

    /// Returns the cursor's gap index in `0..=len`.
    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    /// Returns `true` when the gap precedes the first element.
    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    /// Returns `true` when the gap follows the last element.
    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    /// Borrows the element immediately before the gap, or `None` at the start.
    #[must_use]
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.deque.get(index))
    }

    /// Borrows the element immediately after the gap, or `None` at the end.
    #[must_use]
    pub fn peek_next(&self) -> Option<&T> {
        self.deque.get(self.position)
    }

    /// Returns a cursor one position earlier, or `None` at the start. The receiver is unchanged.
    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            deque: self.deque.clone(),
            position: self.position.checked_sub(1)?,
        })
    }

    /// Returns a cursor one position later, or `None` at the end. The receiver is unchanged.
    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            deque: self.deque.clone(),
            position: self.position + 1,
        })
    }

    /// Jumps to the gap at `position` within the same deque version, or `None` when `position`
    /// exceeds the element count.
    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        (position <= self.len()).then(|| Self {
            deque: self.deque.clone(),
            position,
        })
    }

    /// Inserts `item` at the gap and returns a cursor positioned after it. The receiver keeps its own
    /// version, so cursors retained beforehand never see `item`.
    #[must_use]
    pub fn insert(&self, item: T) -> Self {
        Self {
            deque: self
                .deque
                .insert_at(self.position, item)
                .expect("a cursor gap is a valid insertion boundary"),
            position: self.position + 1,
        }
    }

    /// Inserts every element of `items` at the gap, in order, and returns a cursor positioned after
    /// the last one.
    #[must_use]
    pub fn insert_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let inserted = PersistentDeque::from_vec(items.into_iter().collect());
        if inserted.is_empty() {
            return self.clone();
        }
        let split = self
            .deque
            .split_at(self.position)
            .expect("a cursor gap is a valid split boundary");
        Self {
            position: self
                .position
                .checked_add(inserted.len())
                .expect("deque cursor position overflow"),
            deque: split.left.concat(&inserted).concat(&split.right),
        }
    }

    /// Removes the element before the gap and returns a cursor in its place, or `None` at the start.
    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        Some(Self {
            deque: self
                .deque
                .remove_at(position)
                .expect("a non-start cursor has a previous element"),
            position,
        })
    }

    /// Removes the element after the gap and returns a cursor in its place, or `None` at the end.
    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            deque: self
                .deque
                .remove_at(self.position)
                .expect("a non-end cursor has a next element"),
            position: self.position,
        })
    }

    /// Replaces the element after the gap with `item`, keeping the gap where it is, or returns `None`
    /// at the end.
    #[must_use]
    pub fn replace_next(&self, item: T) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            deque: self
                .deque
                .set_item(self.position, item)
                .expect("a non-end cursor has a next element"),
            position: self.position,
        })
    }

    /// Returns the deque version this cursor is positioned in. O(1); the root is shared.
    #[must_use]
    pub fn snapshot(&self) -> PersistentDeque<T> {
        self.deque.clone()
    }
}

impl<T> PersistentDeque<T>
where
    T: Clone,
{
    /// Copies the elements into a vector in sequence order. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        let mut result = Vec::with_capacity(self.len());
        self.root.copy_to_vec(&mut result);
        result
    }

    /// Removes the first element, returning it together with the remaining deque, or `None` when
    /// empty. The receiver is unchanged.
    #[must_use]
    pub fn pop_first(&self) -> Option<DequePop<T>> {
        let value = self.front()?.clone();
        let rest = self.remove_first().expect("non-empty deque has a tail");
        Some(DequePop { value, rest })
    }

    /// Removes the last element, returning it together with the remaining deque, or `None` when
    /// empty. The receiver is unchanged.
    #[must_use]
    pub fn pop_last(&self) -> Option<DequePop<T>> {
        let value = self.back()?.clone();
        let rest = self.remove_last().expect("non-empty deque has an init");
        Some(DequePop { value, rest })
    }

    /// Splits around the element at `index`, returning the elements before it, that element, and the
    /// elements after it, or `None` when `index` is out of range.
    #[must_use]
    pub fn split_item_at(&self, index: usize) -> Option<DequeItemSplit<T>> {
        if index >= self.len() {
            return None;
        }

        let split = self.split_range(index, 1)?;
        Some(DequeItemSplit {
            left: split.before,
            item: self
                .get(index)
                .expect("validated index has an item")
                .clone(),
            right: split.after,
        })
    }
}

impl<T> Default for PersistentDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> fmt::Debug for PersistentDeque<T>
where
    T: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<T> PartialEq for PersistentDeque<T>
where
    T: PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.len() == other.len() && self.iter().eq(other.iter())
    }
}

impl<T> Eq for PersistentDeque<T> where T: Eq {}

impl<T> FromIterator<T> for PersistentDeque<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> IntoIterator for PersistentDeque<T>
where
    T: Clone,
{
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        IntoIter::new(self.root)
    }
}

impl<'a, T> IntoIterator for &'a PersistentDeque<T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

/// Owning iterator over a [`PersistentDeque`], in sequence order.
pub struct IntoIter<T> {
    stack: Vec<(Arc<DequeTree<T>>, bool)>,
    remaining: usize,
}

impl<T> IntoIter<T> {
    fn new(root: Arc<DequeTree<T>>) -> Self {
        let remaining = root.len();
        Self {
            stack: vec![(root, false)],
            remaining,
        }
    }
}

impl<T> Iterator for IntoIter<T>
where
    T: Clone,
{
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some((tree, reversed)) = self.stack.pop() {
            match tree.as_ref() {
                DequeTree::Empty => {}
                DequeTree::Leaf(item) => {
                    self.remaining -= 1;
                    return Some(item.clone());
                }
                DequeTree::Reversed { inner, .. } => {
                    self.stack.push((inner.clone(), !reversed));
                }
                DequeTree::Node { left, right, .. } => {
                    if reversed {
                        self.stack.push((left.clone(), true));
                        self.stack.push((right.clone(), true));
                    } else {
                        self.stack.push((right.clone(), false));
                        self.stack.push((left.clone(), false));
                    }
                }
            }
        }

        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T> ExactSizeIterator for IntoIter<T> where T: Clone {}

impl<T> PersistentDeque<T>
where
    T: Ord,
{
    /// Returns the index where `item` would be inserted before its equals, assuming the deque is
    /// already sorted ascending.
    ///
    /// The sorted family treats an ordinary deque as a sorted sequence; none of them checks or
    /// establishes that ordering, so results on an unsorted deque are unspecified. O(log n).
    #[must_use]
    pub fn sorted_lower_bound(&self, item: &T) -> usize {
        self.sorted_lower_bound_by(item, T::cmp)
    }

    /// Returns the index just past any elements equal to `item`, assuming ascending order. O(log n).
    #[must_use]
    pub fn sorted_upper_bound(&self, item: &T) -> usize {
        self.sorted_upper_bound_by(item, T::cmp)
    }

    /// Searches an ascending deque for `item`, returning `Ok(index)` when found and `Err(index)` with
    /// the insertion point otherwise. O(log n).
    pub fn sorted_binary_search(&self, item: &T) -> Result<usize, usize> {
        let index = self.sorted_lower_bound(item);
        if index < self.len()
            && self
                .get(index)
                .is_some_and(|value| value.cmp(item) == Ordering::Equal)
        {
            Ok(index)
        } else {
            Err(index)
        }
    }

    /// Reports whether an ascending deque contains `item`. O(log n).
    #[must_use]
    pub fn sorted_contains(&self, item: &T) -> bool {
        self.sorted_binary_search(item).is_ok()
    }

    /// Splits an ascending deque so that the left half holds every element strictly below `item`.
    #[must_use]
    pub fn split_at_sorted_lower_bound(&self, item: &T) -> DequeSplit<T> {
        self.split_at_sorted_lower_bound_by(item, T::cmp)
    }

    /// Splits an ascending deque so that the left half also holds the elements equal to `item`.
    #[must_use]
    pub fn split_at_sorted_upper_bound(&self, item: &T) -> DequeSplit<T> {
        self.split_at_sorted_upper_bound_by(item, T::cmp)
    }

    /// Splits an ascending deque into the elements below `item`, those equal to it, and those above.
    #[must_use]
    pub fn split_at_sorted_equal_range(&self, item: &T) -> DequeRangeSplit<T> {
        self.split_at_sorted_equal_range_by(item, T::cmp)
    }

    /// Inserts `item` at its sorted position, before any existing equals, keeping ascending order.
    #[must_use]
    pub fn insert_sorted(&self, item: T) -> Self {
        self.insert_sorted_by(item, T::cmp)
    }

    /// Removes every element equal to `item` from an ascending deque, in one split-and-join.
    #[must_use]
    pub fn remove_all_sorted(&self, item: &T) -> Self {
        self.remove_all_sorted_by(item, T::cmp)
    }
}

impl<T> PersistentDeque<T> {
    /// [`Self::sorted_lower_bound`] under a caller-supplied comparison.
    ///
    /// The `_by` variants let the deque be ordered by a rule other than `Ord`; the deque must already
    /// be sorted by that same rule.
    #[must_use]
    pub fn sorted_lower_bound_by<F>(&self, item: &T, mut compare: F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        self.sorted_lower_bound_with(item, &mut compare)
    }

    /// [`Self::sorted_upper_bound`] under a caller-supplied comparison.
    #[must_use]
    pub fn sorted_upper_bound_by<F>(&self, item: &T, mut compare: F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        self.sorted_upper_bound_with(item, &mut compare)
    }

    /// [`Self::split_at_sorted_lower_bound`] under a caller-supplied comparison.
    #[must_use]
    pub fn split_at_sorted_lower_bound_by<F>(&self, item: &T, mut compare: F) -> DequeSplit<T>
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let index = self.sorted_lower_bound_with(item, &mut compare);
        self.split_at(index)
            .expect("a computed lower bound is always a valid split index")
    }

    /// [`Self::split_at_sorted_upper_bound`] under a caller-supplied comparison.
    #[must_use]
    pub fn split_at_sorted_upper_bound_by<F>(&self, item: &T, mut compare: F) -> DequeSplit<T>
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let index = self.sorted_upper_bound_with(item, &mut compare);
        self.split_at(index)
            .expect("a computed upper bound is always a valid split index")
    }

    /// [`Self::split_at_sorted_equal_range`] under a caller-supplied comparison.
    #[must_use]
    pub fn split_at_sorted_equal_range_by<F>(&self, item: &T, mut compare: F) -> DequeRangeSplit<T>
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let lower = self.sorted_lower_bound_with(item, &mut compare);
        let upper = self.sorted_upper_bound_with(item, &mut compare);
        self.split_range(lower, upper - lower)
            .expect("computed sorted bounds always describe a valid range")
    }

    /// [`Self::insert_sorted`] under a caller-supplied comparison.
    #[must_use]
    pub fn insert_sorted_by<F>(&self, item: T, mut compare: F) -> Self
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let index = self.sorted_upper_bound_with(&item, &mut compare);
        self.insert_at(index, item)
            .expect("a computed upper bound is always a valid insertion index")
    }

    /// [`Self::remove_all_sorted`] under a caller-supplied comparison.
    #[must_use]
    pub fn remove_all_sorted_by<F>(&self, item: &T, mut compare: F) -> Self
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let lower = self.sorted_lower_bound_with(item, &mut compare);
        let upper = self.sorted_upper_bound_with(item, &mut compare);
        if lower == upper {
            self.clone()
        } else {
            self.remove_range(lower, upper - lower)
                .expect("computed sorted bounds always describe a valid range")
        }
    }

    fn sorted_lower_bound_with<F>(&self, item: &T, compare: &mut F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let mut predicate = |value: &T| compare(value, item) != Ordering::Less;
        self.root.bound_index(&mut predicate)
    }

    fn sorted_upper_bound_with<F>(&self, item: &T, compare: &mut F) -> usize
    where
        F: FnMut(&T, &T) -> Ordering,
    {
        let mut predicate = |value: &T| compare(value, item) == Ordering::Greater;
        self.root.bound_index(&mut predicate)
    }
}

/// Borrowing iterator over a deque, in the sequence's current order.
pub struct Iter<'a, T> {
    stack: Vec<(&'a DequeTree<T>, bool)>,
    remaining: usize,
}

impl<'a, T> Iter<'a, T> {
    fn new(root: &'a Arc<DequeTree<T>>) -> Self {
        Self {
            stack: vec![(root.as_ref(), false)],
            remaining: root.len(),
        }
    }
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some((tree, reversed)) = self.stack.pop() {
            match tree {
                DequeTree::Empty => {}
                DequeTree::Leaf(item) => {
                    self.remaining -= 1;
                    return Some(item);
                }
                DequeTree::Reversed { inner, .. } => {
                    self.stack.push((inner.as_ref(), !reversed));
                }
                DequeTree::Node { left, right, .. } => {
                    if reversed {
                        self.stack.push((left, true));
                        self.stack.push((right, true));
                    } else {
                        self.stack.push((right, false));
                        self.stack.push((left, false));
                    }
                }
            }
        }

        None
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T> ExactSizeIterator for Iter<'_, T> {}

/// A persistent deque whose element order can be reversed in O(1).
///
/// Reversal flips a stored orientation and shares the underlying tree rather than rebuilding the
/// sequence; every other operation then reads and writes through that orientation. Otherwise this
/// behaves exactly like [`PersistentDeque`].
#[derive(Debug, PartialEq, Eq)]
pub struct ReversibleDeque<T> {
    items: PersistentDeque<T>,
}

impl<T> Clone for ReversibleDeque<T> {
    fn clone(&self) -> Self {
        Self {
            items: self.items.clone(),
        }
    }
}

impl<T> ReversibleDeque<T> {
    /// Creates an empty reversible deque.
    #[must_use]
    pub fn new() -> Self {
        Self {
            items: PersistentDeque::new(),
        }
    }

    #[must_use]
    pub(crate) fn from_vec(items: Vec<T>) -> Self {
        Self::from_deque(PersistentDeque::from_vec(items))
    }

    fn from_deque(items: PersistentDeque<T>) -> Self {
        Self { items }
    }

    /// Returns the number of elements. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.items.len()
    }

    /// Returns `true` when the deque holds no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    /// Creates a logical-order cursor before the first element.
    #[must_use]
    pub fn cursor(&self) -> ReversibleDequeCursor<T> {
        ReversibleDequeCursor {
            deque: self.clone(),
            position: 0,
        }
    }

    /// Creates a logical-order cursor at a boundary in `0..=len`.
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<ReversibleDequeCursor<T>> {
        (position <= self.len()).then(|| ReversibleDequeCursor {
            deque: self.clone(),
            position,
        })
    }

    /// Returns this deque with its element order reversed.
    ///
    /// O(1): the orientation is flipped and the underlying tree is shared, rather than the sequence
    /// being rebuilt. This is the whole reason for the type.
    #[must_use]
    pub fn reverse(&self) -> Self {
        Self {
            items: self.items.reversed_view(),
        }
    }

    /// Reports whether two deques are backed by the same underlying tree, so neither can observe an
    /// edit made to the other. Two deques that differ only in orientation still share storage.
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.items.shares_storage_with(&other.items)
    }

    /// Borrows the first element in the current orientation, or `None` when empty.
    #[must_use]
    pub fn front(&self) -> Option<&T> {
        self.get(0)
    }

    /// Borrows the last element in the current orientation, or `None` when empty.
    #[must_use]
    pub fn back(&self) -> Option<&T> {
        self.len().checked_sub(1).and_then(|index| self.get(index))
    }

    /// Borrows the element at `index` in the current orientation, or `None` when out of range.
    #[must_use]
    pub fn get(&self, index: usize) -> Option<&T> {
        if index >= self.len() {
            return None;
        }

        self.items.get(index)
    }

    /// Iterates the elements in the current orientation.
    pub fn iter(&self) -> Iter<'_, T> {
        self.items.iter()
    }

    /// Returns a deque with `item` added at the current front.
    #[must_use]
    pub fn push_front(&self, item: T) -> Self {
        Self::from_deque(self.items.push_front(item))
    }

    /// Returns a deque with `item` added at the current back.
    #[must_use]
    pub fn push_back(&self, item: T) -> Self {
        Self::from_deque(self.items.push_back(item))
    }

    /// Replaces the element at `index` in the current orientation, or returns `None` when out of
    /// range.
    #[must_use]
    pub fn set_item(&self, index: usize, item: T) -> Option<Self> {
        Some(Self::from_deque(self.items.set_item(index, item)?))
    }

    /// Inserts `item` so that it ends up at `index` in the current orientation, or returns `None`
    /// when `index` exceeds the length.
    #[must_use]
    pub fn insert_at(&self, index: usize, item: T) -> Option<Self> {
        Some(Self::from_deque(self.items.insert_at(index, item)?))
    }

    /// Removes the element at `index` in the current orientation, or returns `None` when out of
    /// range.
    #[must_use]
    pub fn remove_at(&self, index: usize) -> Option<Self> {
        Some(Self::from_deque(self.items.remove_at(index)?))
    }

    /// Splits at `index` in the current orientation, or returns `None` when `index` exceeds the
    /// length. Both halves keep this deque's orientation.
    #[must_use]
    pub fn split_at(&self, index: usize) -> Option<ReversibleDequeSplit<T>> {
        let split = self.items.split_at(index)?;
        Some(ReversibleDequeSplit {
            left: Self::from_deque(split.left),
            right: Self::from_deque(split.right),
        })
    }

    /// Concatenates two deques in their current orientations, placing `other`'s elements after this
    /// deque's.
    #[must_use]
    pub fn concat(&self, other: &Self) -> Self {
        Self::from_deque(self.items.concat(&other.items))
    }
}

impl<T> ReversibleDequeCursor<T> {
    /// Returns the element count of the deque version this cursor is positioned in.
    #[must_use]
    pub fn len(&self) -> usize {
        self.deque.len()
    }

    /// Returns `true` when that deque version holds no elements.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.deque.is_empty()
    }

    /// Returns the cursor's gap index in `0..=len`, in the deque's current orientation.
    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    /// Returns `true` when the gap precedes the first element in the current orientation.
    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    /// Returns `true` when the gap follows the last element in the current orientation.
    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    /// Borrows the element immediately before the gap, or `None` at the start.
    #[must_use]
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|index| self.deque.get(index))
    }

    /// Borrows the element immediately after the gap, or `None` at the end.
    #[must_use]
    pub fn peek_next(&self) -> Option<&T> {
        self.deque.get(self.position)
    }

    /// Returns a cursor one position earlier, or `None` at the start. The receiver is unchanged.
    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            deque: self.deque.clone(),
            position: self.position.checked_sub(1)?,
        })
    }

    /// Returns a cursor one position later, or `None` at the end. The receiver is unchanged.
    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            deque: self.deque.clone(),
            position: self.position + 1,
        })
    }

    /// Jumps to the gap at `position` within the same deque version, or `None` when `position`
    /// exceeds the element count.
    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        (position <= self.len()).then(|| Self {
            deque: self.deque.clone(),
            position,
        })
    }

    /// Inserts `item` at the gap and returns a cursor positioned after it. The receiver keeps its own
    /// version.
    #[must_use]
    pub fn insert(&self, item: T) -> Self {
        Self {
            deque: self
                .deque
                .insert_at(self.position, item)
                .expect("a cursor gap is a valid insertion boundary"),
            position: self.position + 1,
        }
    }

    /// Inserts every element of `items` at the gap, in order, and returns a cursor positioned after
    /// the last one.
    #[must_use]
    pub fn insert_range<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let inserted = ReversibleDeque::from_vec(items.into_iter().collect());
        if inserted.is_empty() {
            return self.clone();
        }
        let split = self
            .deque
            .split_at(self.position)
            .expect("a cursor gap is a valid split boundary");
        Self {
            position: self
                .position
                .checked_add(inserted.len())
                .expect("reversible-deque cursor position overflow"),
            deque: split.left.concat(&inserted).concat(&split.right),
        }
    }

    /// Removes the element before the gap and returns a cursor in its place, or `None` at the start.
    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        Some(Self {
            deque: self
                .deque
                .remove_at(position)
                .expect("a non-start cursor has a previous element"),
            position,
        })
    }

    /// Removes the element after the gap and returns a cursor in its place, or `None` at the end.
    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            deque: self
                .deque
                .remove_at(self.position)
                .expect("a non-end cursor has a next element"),
            position: self.position,
        })
    }

    /// Replaces the element after the gap with `item`, keeping the gap where it is, or returns `None`
    /// at the end.
    #[must_use]
    pub fn replace_next(&self, item: T) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            deque: self
                .deque
                .set_item(self.position, item)
                .expect("a non-end cursor has a next element"),
            position: self.position,
        })
    }

    /// Reverses the logical snapshot and maps the gap to `len - position`.
    #[must_use]
    pub fn reverse(&self) -> Self {
        Self {
            deque: self.deque.reverse(),
            position: self.len() - self.position,
        }
    }

    /// Returns the deque version this cursor is positioned in. O(1); storage is shared.
    #[must_use]
    pub fn snapshot(&self) -> ReversibleDeque<T> {
        self.deque.clone()
    }
}

impl<T> Default for ReversibleDeque<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> FromIterator<T> for ReversibleDeque<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_vec(iter.into_iter().collect())
    }
}

impl<T> IntoIterator for ReversibleDeque<T>
where
    T: Clone,
{
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        self.items.into_iter()
    }
}

impl<'a, T> IntoIterator for &'a ReversibleDeque<T> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T> ReversibleDeque<T>
where
    T: Clone,
{
    /// Copies the elements into a vector in the current orientation. O(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<T> {
        self.items.to_vec()
    }

    /// Removes the first element in the current orientation, returning it with the remaining deque,
    /// or `None` when empty.
    #[must_use]
    pub fn pop_front(&self) -> Option<ReversibleDequePop<T>> {
        let popped = self.items.pop_first()?;
        Some(ReversibleDequePop {
            value: popped.value,
            rest: Self::from_deque(popped.rest),
        })
    }

    /// Removes the last element in the current orientation, returning it with the remaining deque,
    /// or `None` when empty.
    #[must_use]
    pub fn pop_back(&self) -> Option<ReversibleDequePop<T>> {
        let popped = self.items.pop_last()?;
        Some(ReversibleDequePop {
            value: popped.value,
            rest: Self::from_deque(popped.rest),
        })
    }
}

fn checked_range_end(len: usize, index: usize, count: usize) -> Option<usize> {
    let end = index.checked_add(count)?;
    (index <= len && end <= len).then_some(end)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn deque_updates_preserve_snapshots() {
        let deque: PersistentDeque<_> = [1, 2, 3].into_iter().collect();
        let pushed = deque.push_front(0).push_back(4);
        let removed = pushed.remove_at(2).unwrap();

        assert_eq!(deque.to_vec(), vec![1, 2, 3]);
        assert_eq!(pushed.to_vec(), vec![0, 1, 2, 3, 4]);
        assert_eq!(removed.to_vec(), vec![0, 1, 3, 4]);
        deque.validate_invariants();
        pushed.validate_invariants();
        removed.validate_invariants();
    }

    #[test]
    fn deque_split_and_concat_round_trip() {
        let deque: PersistentDeque<_> = (0..6).collect();
        let split = deque.split_range(2, 3).unwrap();

        assert_eq!(split.before.to_vec(), vec![0, 1]);
        assert_eq!(split.range.to_vec(), vec![2, 3, 4]);
        assert_eq!(split.after.to_vec(), vec![5]);
        assert_eq!(
            split
                .before
                .concat(&split.range)
                .concat(&split.after)
                .to_vec(),
            deque.to_vec()
        );
        split.before.validate_invariants();
        split.range.validate_invariants();
        split.after.validate_invariants();
    }

    #[test]
    fn deque_non_endpoint_edits_share_unchanged_subtrees() {
        let deque: PersistentDeque<_> = (0..256).collect();
        let changed = deque.set_item(128, -1).unwrap();
        let inserted = deque.insert_at(120, -2).unwrap();
        let removed = deque.remove_range(96, 32).unwrap();

        assert_eq!(deque.get(128), Some(&128));
        assert_eq!(changed.get(128), Some(&-1));
        assert!(deque.shared_node_count_with(&changed) > 100);
        assert!(deque.shared_node_count_with(&inserted) > 100);
        assert!(deque.shared_node_count_with(&removed) > 100);
        assert!(!deque.shares_storage_with(&changed));
        changed.validate_invariants();
        inserted.validate_invariants();
        removed.validate_invariants();
    }

    #[test]
    fn deque_depth_grows_logarithmically_for_bulk_build_and_pushes() {
        let deque: PersistentDeque<_> = (0..4096).collect();
        let pushed = (4096..8192).fold(deque.clone(), |current, value| current.push_back(value));

        assert!(deque.tree_depth() < 32, "depth was {}", deque.tree_depth());
        assert!(
            pushed.tree_depth() < 40,
            "depth was {}",
            pushed.tree_depth()
        );
        assert_eq!(pushed.front(), Some(&0));
        assert_eq!(pushed.back(), Some(&8191));
        pushed.validate_invariants();
    }

    #[test]
    fn sorted_search_returns_first_equal() {
        let deque: PersistentDeque<_> = [1, 2, 2, 2, 5].into_iter().collect();

        assert_eq!(deque.sorted_lower_bound(&2), 1);
        assert_eq!(deque.sorted_upper_bound(&2), 4);
        assert_eq!(deque.sorted_binary_search(&2), Ok(1));
        assert_eq!(deque.sorted_binary_search(&4), Err(4));
    }

    #[test]
    fn sorted_split_insert_and_remove_cover_equal_ranges_and_custom_order() {
        let deque: PersistentDeque<_> = [1, 3, 3, 3, 7, 9, 9, 12].into_iter().collect();

        let lower = deque.split_at_sorted_lower_bound(&3);
        assert_eq!(lower.left.to_vec(), vec![1]);
        assert_eq!(lower.right.to_vec(), vec![3, 3, 3, 7, 9, 9, 12]);

        let upper = deque.split_at_sorted_upper_bound(&3);
        assert_eq!(upper.left.to_vec(), vec![1, 3, 3, 3]);
        assert_eq!(upper.right.to_vec(), vec![7, 9, 9, 12]);

        let equal = deque.split_at_sorted_equal_range(&3);
        assert_eq!(equal.before.to_vec(), vec![1]);
        assert_eq!(equal.range.to_vec(), vec![3, 3, 3]);
        assert_eq!(equal.after.to_vec(), vec![7, 9, 9, 12]);
        assert_eq!(
            deque.insert_sorted(3).to_vec(),
            vec![1, 3, 3, 3, 3, 7, 9, 9, 12]
        );
        assert_eq!(deque.remove_all_sorted(&3).to_vec(), vec![1, 7, 9, 9, 12]);
        assert!(deque.shares_storage_with(&deque.remove_all_sorted(&4)));

        fn descending(left: &i32, right: &i32) -> Ordering {
            right.cmp(left)
        }

        let descending_deque: PersistentDeque<_> = [9, 7, 3, 3, 1].into_iter().collect();
        assert_eq!(descending_deque.sorted_lower_bound_by(&3, descending), 2);
        assert_eq!(
            descending_deque.insert_sorted_by(3, descending).to_vec(),
            vec![9, 7, 3, 3, 3, 1]
        );
        assert_eq!(
            descending_deque
                .remove_all_sorted_by(&3, descending)
                .to_vec(),
            vec![9, 7, 1]
        );
    }

    #[test]
    fn cached_endpoint_signposts_support_logarithmic_search_through_reversal() {
        let deque: PersistentDeque<_> = (0..4096).collect();
        let reversed = deque.reversed_view();
        let mut comparisons = 0;
        let index = reversed.sorted_lower_bound_by(&2048, |left, right| {
            comparisons += 1;
            right.cmp(left)
        });

        assert_eq!(index, 2047);
        assert!(comparisons < 64, "search made {comparisons} comparisons");
        assert_eq!(reversed.front(), Some(&4095));
        assert_eq!(reversed.back(), Some(&0));
        reversed.validate_invariants();
    }

    #[test]
    fn randomized_deque_model_replay_covers_branching_versions() {
        let mut rng = 0x5eed_1234_u64;
        let mut deque = PersistentDeque::new();
        let mut model = Vec::new();
        let mut retained = vec![(deque.clone(), model.clone())];

        for step in 0..500 {
            rng = rng.wrapping_mul(6364136223846793005).wrapping_add(1);
            match (rng >> 32) % 9 {
                0 if model.len() < 512 => {
                    let value = step;
                    deque = deque.push_front(value);
                    model.insert(0, value);
                }
                1 if model.len() < 512 => {
                    let value = step;
                    deque = deque.push_back(value);
                    model.push(value);
                }
                2 if !model.is_empty() => {
                    assert_eq!(deque.pop_first().unwrap().value, model.remove(0));
                    deque = deque.remove_first().unwrap();
                }
                3 if !model.is_empty() => {
                    assert_eq!(deque.pop_last().unwrap().value, model.pop().unwrap());
                    deque = deque.remove_last().unwrap();
                }
                4 if !model.is_empty() => {
                    let index = ((rng >> 24) as usize) % model.len();
                    let value = -step;
                    deque = deque.set_item(index, value).unwrap();
                    model[index] = value;
                }
                5 if model.len() < 512 => {
                    let index = ((rng >> 24) as usize) % (model.len() + 1);
                    let value = step * 3;
                    deque = deque.insert_at(index, value).unwrap();
                    model.insert(index, value);
                }
                6 if !model.is_empty() => {
                    let index = ((rng >> 24) as usize) % model.len();
                    deque = deque.remove_at(index).unwrap();
                    model.remove(index);
                }
                7 if !retained.is_empty() && model.len() < 256 => {
                    let selected = ((rng >> 24) as usize) % retained.len();
                    let (prefix, prefix_model) = retained[selected].clone();
                    if prefix_model.len() + model.len() <= 512 {
                        deque = prefix.concat(&deque);
                        let mut next = prefix_model;
                        next.extend(model);
                        model = next;
                    }
                }
                _ => {
                    retained.push((deque.clone(), model.clone()));
                    if retained.len() > 24 {
                        retained.remove(0);
                    }
                }
            }

            assert_eq!(deque.to_vec(), model);
            deque.validate_invariants();
        }

        for (version, expected) in retained {
            assert_eq!(version.to_vec(), expected);
            version.validate_invariants();
        }
    }

    #[test]
    fn reversible_reverse_is_storage_sharing_view() {
        let deque: ReversibleDeque<_> = [1, 2, 3].into_iter().collect();
        let reversed = deque.reverse();
        let restored = reversed.reverse();

        assert_eq!(reversed.to_vec(), vec![3, 2, 1]);
        assert_eq!(restored.to_vec(), vec![1, 2, 3]);
        assert!(deque.shares_storage_with(&reversed));
    }

    #[test]
    fn reversible_edits_share_underlying_deque_tree() {
        let deque: ReversibleDeque<_> = (0..256).collect();
        let reversed = deque.reverse();
        let changed = reversed.set_item(100, -1).unwrap();
        let inserted = reversed.insert_at(120, -2).unwrap();
        let removed = reversed.remove_at(80).unwrap();
        let pushed = reversed.push_front(-3).push_back(-4);

        assert_eq!(reversed.get(0), Some(&255));
        assert_eq!(changed.get(100), Some(&-1));
        assert_eq!(inserted.get(120), Some(&-2));
        assert_eq!(removed.len(), 255);
        assert_eq!(pushed.front(), Some(&-3));
        assert_eq!(pushed.back(), Some(&-4));
        assert!(deque.shares_storage_with(&reversed));
        assert!(reversed.items.shared_node_count_with(&changed.items) > 100);
        assert!(reversed.items.shared_node_count_with(&inserted.items) > 100);
        assert!(reversed.items.shared_node_count_with(&removed.items) > 100);
        assert!(reversed.items.shared_node_count_with(&pushed.items) > 100);
        assert!(reversed.items.tree_depth() < 24);
        changed.items.validate_invariants();
        inserted.items.validate_invariants();
        removed.items.validate_invariants();
        pushed.items.validate_invariants();
    }

    #[test]
    fn reversible_mixed_orientation_concat_split_and_pop_stay_tree_based() {
        let left: ReversibleDeque<_> = (0..512).collect();
        let right: ReversibleDeque<_> = (1000..1512).collect();
        let left_reversed = left.reverse();
        let right_reversed = right.reverse();
        let joined = left_reversed.concat(&right_reversed);

        let expected = (0..512).rev().chain((1000..1512).rev()).collect::<Vec<_>>();
        assert_eq!(joined.to_vec(), expected);
        assert!(left_reversed.items.shared_node_count_with(&joined.items) > 0);
        assert!(right_reversed.items.shared_node_count_with(&joined.items) > 0);

        let popped_front = left_reversed.pop_front().unwrap();
        let popped_back = left_reversed.pop_back().unwrap();
        assert_eq!(popped_front.value, 511);
        assert_eq!(popped_back.value, 0);
        assert!(
            left_reversed
                .items
                .shared_node_count_with(&popped_front.rest.items)
                > 0
        );
        assert!(
            left_reversed
                .items
                .shared_node_count_with(&popped_back.rest.items)
                > 0
        );

        let split = joined.split_at(400).unwrap();
        assert_eq!(split.left.len(), 400);
        assert_eq!(split.left.get(0), Some(&511));
        assert_eq!(split.right.get(0), Some(&111));
        assert!(joined.items.shared_node_count_with(&split.left.items) > 0);
        assert!(joined.items.shared_node_count_with(&split.right.items) > 0);

        let iterated = joined.iter().copied().collect::<Vec<_>>();
        assert_eq!(iterated, expected);
        assert_eq!((&joined).into_iter().count(), joined.len());
        assert_eq!(joined.clone().into_iter().collect::<Vec<_>>(), expected);

        let reversed_again = joined.reverse();
        assert!(joined.shares_storage_with(&reversed_again));
        joined.items.validate_invariants();
        popped_front.rest.items.validate_invariants();
        popped_back.rest.items.validate_invariants();
        split.left.items.validate_invariants();
        split.right.items.validate_invariants();
        reversed_again.items.validate_invariants();
    }
}
