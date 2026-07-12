use crate::ordering::{OrderComparer, OrderPolicy};
use std::collections::HashSet;
use std::error::Error;
use std::fmt;
use std::sync::Arc;

type TreeLink<T> = Option<Arc<Tree<T>>>;
type ForestLink<T> = Option<Arc<Forest<T>>>;

/// Statistics returned by a complete Brodal-Okasaki representation audit.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct BrodalOkasakiHeapStatistics {
    /// Logical element count.
    pub len: usize,
    /// Number of skew-binomial trees immediately below the global minimum root.
    pub root_forest_len: usize,
    /// Largest skew-binomial rank encountered.
    pub maximum_rank: usize,
    /// Deepest global-root-to-tree path.
    pub maximum_depth: usize,
}

/// A structural invariant violation found by [`BrodalOkasakiHeap::validate_structure`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct BrodalInvariantError {
    message: &'static str,
}

impl BrodalInvariantError {
    fn new(message: &'static str) -> Self {
        Self { message }
    }
}

impl fmt::Display for BrodalInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl Error for BrodalInvariantError {}

/// Meld was requested for heaps whose retained ordering policies are incompatible.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BrodalMeldError;

impl fmt::Display for BrodalMeldError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("Brodal-Okasaki melding requires compatible ordering policies")
    }
}

impl Error for BrodalMeldError {}

/// One successful minimum deletion.
///
/// The minimum is an owned shared handle, so the result remains usable independently of both the
/// source heap and the persistent remainder without requiring `T: Clone`.
#[derive(Debug)]
pub struct BrodalMinimumView<T> {
    /// Removed minimum representative.
    pub minimum: Arc<T>,
    /// Persistent heap after removing one occurrence of the minimum.
    pub remainder: BrodalOkasakiHeap<T>,
}

/// An immutable Brodal-Okasaki bootstrapped skew-binomial min-heap.
///
/// Insert, minimum, and meld are O(1) worst-case; delete-min is O(log n) worst-case. Values and
/// structural nodes are retained through [`Arc`], so all persistent operations avoid a `T: Clone`
/// requirement. Custom-policy heaps may meld only when their [`OrderPolicy`] identity is shared.
pub struct BrodalOkasakiHeap<T> {
    root: TreeLink<T>,
    len: usize,
    policy: OrderPolicy<T>,
}

impl<T> Clone for BrodalOkasakiHeap<T> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            len: self.len,
            policy: self.policy.clone(),
        }
    }
}

impl<T> fmt::Debug for BrodalOkasakiHeap<T>
where
    T: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("BrodalOkasakiHeap")
            .field("len", &self.len)
            .field("minimum", &self.minimum())
            .field("policy", &self.policy)
            .finish_non_exhaustive()
    }
}

impl<T: Ord> BrodalOkasakiHeap<T> {
    /// Creates an empty heap using canonical natural ordering.
    #[must_use]
    pub fn new() -> Self {
        Self::with_policy(OrderPolicy::natural())
    }
}

impl<T: Ord> Default for BrodalOkasakiHeap<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: Ord> FromIterator<T> for BrodalOkasakiHeap<T> {
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::from_items(iter)
    }
}

impl<T> BrodalOkasakiHeap<T> {
    /// Creates an empty heap retaining a distinct custom comparer policy.
    #[must_use]
    pub fn with_comparer<C>(comparer: C) -> Self
    where
        C: OrderComparer<T> + 'static,
    {
        Self::with_policy(OrderPolicy::custom(comparer))
    }

    /// Creates an empty heap retaining `policy`.
    #[must_use]
    pub fn with_policy(policy: OrderPolicy<T>) -> Self {
        Self {
            root: None,
            len: 0,
            policy,
        }
    }

    /// Builds a heap by repeated worst-case-O(1) insertion using canonical natural ordering.
    #[must_use]
    pub fn from_items<I>(items: I) -> Self
    where
        T: Ord,
        I: IntoIterator<Item = T>,
    {
        Self::from_items_with_policy(items, OrderPolicy::natural())
    }

    /// Builds a heap by repeated worst-case-O(1) insertion using `policy`.
    #[must_use]
    pub fn from_items_with_policy<I>(items: I, policy: OrderPolicy<T>) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = Self::with_policy(policy);
        for item in items {
            result = result.insert(item);
        }
        result
    }

    /// Returns the retained ordering policy.
    #[must_use]
    pub fn policy(&self) -> &OrderPolicy<T> {
        &self.policy
    }

    /// Returns the number of elements.
    #[must_use]
    pub fn len(&self) -> usize {
        self.len
    }

    /// Returns whether the heap is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns the minimum representative in O(1), or `None` when empty.
    #[must_use]
    pub fn minimum(&self) -> Option<&T> {
        self.root.as_ref().map(|root| root.value.as_ref())
    }

    /// Inserts `item` in worst-case O(1).
    #[must_use]
    pub fn insert(&self, item: T) -> Self {
        let value = Arc::new(item);
        let Some(root) = &self.root else {
            return Self {
                root: Some(tree(0, value, None)),
                len: 1,
                policy: self.policy.clone(),
            };
        };

        let updated_root = if self.less_or_equal(value.as_ref(), root.value.as_ref()) {
            tree(0, value, Some(forest(Arc::clone(root), None)))
        } else {
            let singleton = tree(0, value, None);
            tree(
                0,
                Arc::clone(&root.value),
                Some(self.skew_insert(singleton, root.children.clone())),
            )
        };
        Self {
            root: Some(updated_root),
            len: self
                .len
                .checked_add(1)
                .expect("Brodal heap length overflow"),
            policy: self.policy.clone(),
        }
    }

    /// Melds two heaps in worst-case O(1).
    ///
    /// Canonical natural-order policies are mutually compatible. Custom policies must be clones of
    /// the same retained [`OrderPolicy`].
    pub fn meld(&self, other: &Self) -> Result<Self, BrodalMeldError> {
        if !self.policy.is_compatible_with(&other.policy) {
            return Err(BrodalMeldError);
        }
        let Some(left_root) = &self.root else {
            return Ok(other.clone());
        };
        let Some(right_root) = &other.root else {
            return Ok(self.clone());
        };

        let updated_root =
            if self.less_or_equal(left_root.value.as_ref(), right_root.value.as_ref()) {
                tree(
                    0,
                    Arc::clone(&left_root.value),
                    Some(self.skew_insert(Arc::clone(right_root), left_root.children.clone())),
                )
            } else {
                tree(
                    0,
                    Arc::clone(&right_root.value),
                    Some(self.skew_insert(Arc::clone(left_root), right_root.children.clone())),
                )
            };
        Ok(Self {
            root: Some(updated_root),
            len: self
                .len
                .checked_add(other.len)
                .expect("Brodal heap length overflow"),
            policy: self.policy.clone(),
        })
    }

    /// Removes one minimum occurrence in worst-case O(log n), returning `None` when empty.
    #[must_use]
    pub fn delete_minimum(&self) -> Option<Self> {
        let root = self.root.as_ref()?;
        let Some(children) = &root.children else {
            return Some(Self::with_policy(self.policy.clone()));
        };

        let (minimum_tree, remainder) = self.get_minimum(Arc::clone(children));
        let split = self.split_forest(minimum_tree.rank, minimum_tree.children.clone());
        let mut merged = self.skew_meld(
            self.skew_meld(split.trees, remainder),
            split.embedded_forest,
        );
        let zeros = forest_to_vec(split.zeros);
        for zero in zeros.into_iter().rev() {
            merged = Some(self.skew_insert(zero, merged));
        }
        Some(Self {
            root: Some(tree(0, Arc::clone(&minimum_tree.value), merged)),
            len: self.len - 1,
            policy: self.policy.clone(),
        })
    }

    /// Removes and returns one minimum occurrence, or `None` when empty.
    #[must_use]
    pub fn minimum_view(&self) -> Option<BrodalMinimumView<T>> {
        let minimum = Arc::clone(&self.root.as_ref()?.value);
        let remainder = self
            .delete_minimum()
            .expect("a present root must permit minimum deletion");
        Some(BrodalMinimumView { minimum, remainder })
    }

    /// Iterates structural order without performing comparisons.
    #[must_use]
    pub fn iter(&self) -> BrodalIter<'_, T> {
        BrodalIter {
            pending: self.root.as_deref().into_iter().collect(),
        }
    }

    /// Validates fused primitive-child/embedded-forest boundaries, skew ranks, heap order, count,
    /// and depth using an explicit O(n)-space worklist.
    pub fn validate_structure(&self) -> Result<BrodalOkasakiHeapStatistics, BrodalInvariantError> {
        let Some(root) = &self.root else {
            if self.len != 0 {
                return Err(BrodalInvariantError::new(
                    "an empty Brodal-Okasaki heap has a nonzero length",
                ));
            }
            return Ok(BrodalOkasakiHeapStatistics::default());
        };
        if root.rank != 0 {
            return Err(BrodalInvariantError::new(
                "the Brodal-Okasaki global root must have rank zero",
            ));
        }

        let root_forest_len = validate_skew_forest(root.children.as_deref())?;
        let mut pending = vec![(root.as_ref(), 1_usize)];
        let mut logical_count = 0_usize;
        let mut maximum_rank = 0_usize;
        let mut maximum_depth = 0_usize;
        while let Some((current, depth)) = pending.pop() {
            logical_count = logical_count
                .checked_add(1)
                .ok_or_else(|| BrodalInvariantError::new("validated Brodal count overflow"))?;
            maximum_rank = maximum_rank.max(current.rank);
            maximum_depth = maximum_depth.max(depth);
            let embedded = validate_fused_children(current)?;
            validate_skew_forest(embedded.as_deref())?;

            let mut children = current.children.as_deref();
            while let Some(link) = children {
                if !self.less_or_equal(current.value.as_ref(), link.head.value.as_ref()) {
                    return Err(BrodalInvariantError::new(
                        "a Brodal-Okasaki child outranks its parent",
                    ));
                }
                pending.push((link.head.as_ref(), depth + 1));
                children = link.tail.as_deref();
            }
        }
        if logical_count != self.len {
            return Err(BrodalInvariantError::new(
                "Brodal-Okasaki logical length disagrees with its tree graph",
            ));
        }
        Ok(BrodalOkasakiHeapStatistics {
            len: self.len,
            root_forest_len,
            maximum_rank,
            maximum_depth,
        })
    }

    /// Counts distinct tree nodes shared by identity with `other`.
    #[must_use]
    pub fn shared_tree_count(&self, other: &Self) -> usize {
        let left = collect_tree_identities(self.root.as_ref());
        let mut visited = HashSet::new();
        let mut pending = other.root.as_ref().into_iter().cloned().collect::<Vec<_>>();
        let mut shared = 0;
        while let Some(current) = pending.pop() {
            let identity = Arc::as_ptr(&current);
            if !visited.insert(identity) {
                continue;
            }
            if left.contains(&identity) {
                shared += 1;
            }
            let mut children = current.children.clone();
            while let Some(link) = children {
                pending.push(Arc::clone(&link.head));
                children = link.tail.clone();
            }
        }
        shared
    }

    /// Returns whether two versions retain exactly the same root node.
    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            (None, None) => true,
            _ => false,
        }
    }

    fn less_or_equal(&self, left: &T, right: &T) -> bool {
        self.policy.compare(left, right).is_le()
    }

    fn skew_insert(&self, value: Arc<Tree<T>>, current: ForestLink<T>) -> Arc<Forest<T>> {
        if let Some(first) = &current
            && let Some(second) = &first.tail
            && first.head.rank == second.head.rank
        {
            return forest(
                self.skew_link(value, Arc::clone(&first.head), Arc::clone(&second.head)),
                second.tail.clone(),
            );
        }
        forest(value, current)
    }

    fn skew_link(
        &self,
        zero: Arc<Tree<T>>,
        first: Arc<Tree<T>>,
        second: Arc<Tree<T>>,
    ) -> Arc<Tree<T>> {
        debug_assert_eq!(first.rank, second.rank);
        if self.less_or_equal(first.value.as_ref(), zero.value.as_ref())
            && self.less_or_equal(first.value.as_ref(), second.value.as_ref())
        {
            tree(
                first.rank + 1,
                Arc::clone(&first.value),
                Some(forest(zero, Some(forest(second, first.children.clone())))),
            )
        } else if self.less_or_equal(second.value.as_ref(), zero.value.as_ref())
            && self.less_or_equal(second.value.as_ref(), first.value.as_ref())
        {
            tree(
                second.rank + 1,
                Arc::clone(&second.value),
                Some(forest(zero, Some(forest(first, second.children.clone())))),
            )
        } else {
            tree(
                first.rank + 1,
                Arc::clone(&zero.value),
                Some(forest(first, Some(forest(second, zero.children.clone())))),
            )
        }
    }

    fn link(&self, first: Arc<Tree<T>>, second: Arc<Tree<T>>) -> Arc<Tree<T>> {
        debug_assert_eq!(first.rank, second.rank);
        if self.less_or_equal(first.value.as_ref(), second.value.as_ref()) {
            tree(
                first.rank + 1,
                Arc::clone(&first.value),
                Some(forest(second, first.children.clone())),
            )
        } else {
            tree(
                second.rank + 1,
                Arc::clone(&second.value),
                Some(forest(first, second.children.clone())),
            )
        }
    }

    fn get_minimum(&self, initial: Arc<Forest<T>>) -> (Arc<Tree<T>>, ForestLink<T>) {
        let mut minimum_forest = Arc::clone(&initial);
        let mut cursor = initial.tail.clone();
        while let Some(current) = cursor {
            if self
                .policy
                .compare(
                    current.head.value.as_ref(),
                    minimum_forest.head.value.as_ref(),
                )
                .is_lt()
            {
                minimum_forest = Arc::clone(&current);
            }
            cursor = current.tail.clone();
        }

        let mut prefix = Vec::new();
        let mut cursor = Some(initial);
        while let Some(current) = cursor {
            if Arc::ptr_eq(&current, &minimum_forest) {
                break;
            }
            prefix.push(Arc::clone(&current.head));
            cursor = current.tail.clone();
        }
        let mut remainder = minimum_forest.tail.clone();
        for tree in prefix.into_iter().rev() {
            remainder = Some(forest(tree, remainder));
        }
        (Arc::clone(&minimum_forest.head), remainder)
    }

    fn split_forest(&self, initial_rank: usize, initial: ForestLink<T>) -> SplitForest<T> {
        let mut rank = initial_rank;
        let mut zeros = None;
        let mut trees = None;
        let mut current = initial;
        loop {
            if rank == 0 {
                return SplitForest {
                    zeros,
                    trees,
                    embedded_forest: current,
                };
            }
            let first_forest = current
                .clone()
                .expect("valid skew-binomial trees have structural children");
            let tail = first_forest.tail.clone();
            if rank == 1 && tail.is_none() {
                return SplitForest {
                    zeros,
                    trees: Some(forest(Arc::clone(&first_forest.head), trees)),
                    embedded_forest: None,
                };
            }
            let tail = tail.expect("valid ranked skew-binomial trees have complete children");
            let first = Arc::clone(&first_forest.head);
            let second = Arc::clone(&tail.head);
            let rest = tail.tail.clone();
            if rank == 1 {
                return if second.rank == 0 {
                    SplitForest {
                        zeros: Some(forest(first, zeros)),
                        trees: Some(forest(second, trees)),
                        embedded_forest: rest,
                    }
                } else {
                    SplitForest {
                        zeros,
                        trees: Some(forest(first, trees)),
                        embedded_forest: Some(forest(second, rest)),
                    }
                };
            }
            if first.rank == second.rank {
                return SplitForest {
                    zeros,
                    trees: Some(forest(first, Some(forest(second, trees)))),
                    embedded_forest: rest,
                };
            }
            if first.rank == 0 {
                zeros = Some(forest(first, zeros));
                trees = Some(forest(second, trees));
                current = rest;
            } else {
                trees = Some(forest(first, trees));
                current = Some(forest(second, rest));
            }
            rank -= 1;
        }
    }

    fn skew_meld(&self, left: ForestLink<T>, right: ForestLink<T>) -> ForestLink<T> {
        let left = self.uniquify(left);
        let right = self.uniquify(right);
        self.union_unique(left, right)
    }

    fn uniquify(&self, current: ForestLink<T>) -> ForestLink<T> {
        current.map(|link| {
            let tail = self.uniquify(link.tail.clone());
            self.insert_ranked(Arc::clone(&link.head), tail)
        })
    }

    fn insert_ranked(&self, value: Arc<Tree<T>>, current: ForestLink<T>) -> Arc<Forest<T>> {
        let Some(first) = current else {
            return forest(value, None);
        };
        debug_assert!(value.rank <= first.head.rank);
        if value.rank < first.head.rank {
            forest(value, Some(first))
        } else {
            self.insert_ranked(
                self.link(value, Arc::clone(&first.head)),
                first.tail.clone(),
            )
        }
    }

    fn union_unique(&self, left: ForestLink<T>, right: ForestLink<T>) -> ForestLink<T> {
        match (left, right) {
            (None, current) | (current, None) => current,
            (Some(left), Some(right)) if left.head.rank < right.head.rank => Some(forest(
                Arc::clone(&left.head),
                self.union_unique(left.tail.clone(), Some(right)),
            )),
            (Some(left), Some(right)) if left.head.rank > right.head.rank => Some(forest(
                Arc::clone(&right.head),
                self.union_unique(Some(left), right.tail.clone()),
            )),
            (Some(left), Some(right)) => {
                let tail = self.union_unique(left.tail.clone(), right.tail.clone());
                Some(self.insert_ranked(
                    self.link(Arc::clone(&left.head), Arc::clone(&right.head)),
                    tail,
                ))
            }
        }
    }
}

/// Borrowed structural-order iterator for [`BrodalOkasakiHeap`].
pub struct BrodalIter<'a, T> {
    pending: Vec<&'a Tree<T>>,
}

impl<'a, T> Iterator for BrodalIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        let current = self.pending.pop()?;
        let mut children = current.children.as_deref();
        while let Some(link) = children {
            self.pending.push(link.head.as_ref());
            children = link.tail.as_deref();
        }
        Some(current.value.as_ref())
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (0, None)
    }
}

impl<'a, T> IntoIterator for &'a BrodalOkasakiHeap<T> {
    type Item = &'a T;
    type IntoIter = BrodalIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

struct Tree<T> {
    rank: usize,
    value: Arc<T>,
    children: ForestLink<T>,
}

struct Forest<T> {
    head: Arc<Tree<T>>,
    tail: ForestLink<T>,
}

struct SplitForest<T> {
    zeros: ForestLink<T>,
    trees: ForestLink<T>,
    embedded_forest: ForestLink<T>,
}

fn tree<T>(rank: usize, value: Arc<T>, children: ForestLink<T>) -> Arc<Tree<T>> {
    Arc::new(Tree {
        rank,
        value,
        children,
    })
}

fn forest<T>(head: Arc<Tree<T>>, tail: ForestLink<T>) -> Arc<Forest<T>> {
    Arc::new(Forest { head, tail })
}

fn forest_to_vec<T>(mut current: ForestLink<T>) -> Vec<Arc<Tree<T>>> {
    let mut result = Vec::new();
    while let Some(link) = current {
        result.push(Arc::clone(&link.head));
        current = link.tail.clone();
    }
    result
}

fn validate_fused_children<T>(tree: &Tree<T>) -> Result<ForestLink<T>, BrodalInvariantError> {
    let mut rank = tree.rank;
    let mut current = tree.children.clone();
    while rank > 0 {
        let first_forest = current.clone().ok_or_else(|| {
            BrodalInvariantError::new("a ranked Brodal-Okasaki tree is missing structural children")
        })?;
        let first = &first_forest.head;
        if rank == 1 {
            if first.rank != 0 {
                return Err(BrodalInvariantError::new(
                    "a rank-one Brodal-Okasaki tree must begin with a rank-zero child",
                ));
            }
            let Some(tail) = &first_forest.tail else {
                return Ok(None);
            };
            return if tail.head.rank == 0 {
                Ok(tail.tail.clone())
            } else {
                Ok(Some(Arc::clone(tail)))
            };
        }
        let tail = first_forest.tail.as_ref().ok_or_else(|| {
            BrodalInvariantError::new("a ranked Brodal-Okasaki tree has incomplete child encoding")
        })?;
        let second = &tail.head;
        if first.rank == second.rank {
            if first.rank != rank - 1 {
                return Err(BrodalInvariantError::new(
                    "a skew-linked Brodal-Okasaki tree has invalid child ranks",
                ));
            }
            return Ok(tail.tail.clone());
        }
        if first.rank == 0 {
            if second.rank != rank - 1 {
                return Err(BrodalInvariantError::new(
                    "a skew-linked Brodal-Okasaki tree has an invalid ranked child",
                ));
            }
            current = tail.tail.clone();
        } else {
            if first.rank != rank - 1 {
                return Err(BrodalInvariantError::new(
                    "a linked Brodal-Okasaki tree has an invalid ranked child",
                ));
            }
            current = Some(Arc::clone(tail));
        }
        rank -= 1;
    }
    Ok(current)
}

fn validate_skew_forest<T>(mut current: Option<&Forest<T>>) -> Result<usize, BrodalInvariantError> {
    let mut len = 0_usize;
    let mut previous_rank = None;
    let mut duplicate = false;
    while let Some(link) = current {
        let rank = link.head.rank;
        if let Some(previous) = previous_rank {
            if rank < previous {
                return Err(BrodalInvariantError::new(
                    "Brodal-Okasaki root-forest ranks are not nondecreasing",
                ));
            }
            if rank == previous {
                if len != 1 || duplicate {
                    return Err(BrodalInvariantError::new(
                        "only the first two skew-forest ranks may be equal",
                    ));
                }
                duplicate = true;
            }
        }
        previous_rank = Some(rank);
        len += 1;
        current = link.tail.as_deref();
    }
    Ok(len)
}

fn collect_tree_identities<T>(root: Option<&Arc<Tree<T>>>) -> HashSet<*const Tree<T>> {
    let mut identities = HashSet::new();
    let mut pending = root.into_iter().cloned().collect::<Vec<_>>();
    while let Some(current) = pending.pop() {
        if !identities.insert(Arc::as_ptr(&current)) {
            continue;
        }
        let mut children = current.children.clone();
        while let Some(link) = children {
            pending.push(Arc::clone(&link.head));
            children = link.tail.clone();
        }
    }
    identities
}
