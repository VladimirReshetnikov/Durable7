//! Persistent priority search queue: a search tree and a priority queue over the same entries.
//!
//! [`PrioritySearchQueue`] indexes each entry by a unique key *and* orders it by priority, so it
//! supports both the map operations (lookup, insert, delete, and adjust by key) and the queue
//! operations (find and remove the minimum by priority) in O(log n). A plain priority queue can do
//! the second group only; a plain sorted map can do the first group only.
//!
//! The representation is a pennant-based tournament tree in which each node carries a winner —
//! the minimum-priority entry of its subtree — so the overall minimum is available at the root
//! without a search. Ordering of both keys and priorities comes from retained
//! [`OrderPolicy`](crate::ordering::OrderPolicy) values, and operations that mix incompatible
//! policies fail rather than producing a malformed queue.
//!
//! Mutating operations report what they did through [`PrioritySearchAddResult`] and
//! [`PrioritySearchRemoveResult`]; [`PrioritySearchMinimumView`] borrows the current minimum without
//! removing it. Range enumeration ([`PrioritySearchRangeIter`]) rejects an inverted range with
//! [`PrioritySearchRangeError`], and a full audit returns [`PrioritySearchQueueStatistics`] or the
//! first [`PrioritySearchInvariantError`].

use crate::ordering::{OrderComparer, OrderPolicy};
use std::cmp::Ordering;
use std::collections::HashSet;
use std::error::Error;
use std::fmt;
use std::sync::Arc;

type NodeLink<K, P, V> = Option<Arc<Node<K, P, V>>>;

struct EntryInner<K, P, V> {
    key: Arc<K>,
    priority: Arc<P>,
    value: Arc<V>,
}

/// One unique ordered key, its priority, and its payload.
///
/// Each component is retained by [`Arc`], making cloned result handles independent of queue
/// lifetime without requiring `K`, `P`, or `V` to implement `Clone`.
pub struct PrioritySearchEntry<K, P, V> {
    inner: Arc<EntryInner<K, P, V>>,
}

impl<K, P, V> PrioritySearchEntry<K, P, V> {
    /// Creates an owned entry handle.
    #[must_use]
    pub fn new(key: K, priority: P, value: V) -> Self {
        Self {
            inner: Arc::new(EntryInner {
                key: Arc::new(key),
                priority: Arc::new(priority),
                value: Arc::new(value),
            }),
        }
    }

    /// Returns the retained key representative.
    #[must_use]
    pub fn key(&self) -> &K {
        self.inner.key.as_ref()
    }

    /// Returns the retained priority value.
    #[must_use]
    pub fn priority(&self) -> &P {
        self.inner.priority.as_ref()
    }

    /// Returns the retained payload.
    #[must_use]
    pub fn value(&self) -> &V {
        self.inner.value.as_ref()
    }

    /// Clones the shared key handle without cloning `K`.
    #[must_use]
    pub fn key_handle(&self) -> Arc<K> {
        Arc::clone(&self.inner.key)
    }

    /// Clones the shared priority handle without cloning `P`.
    #[must_use]
    pub fn priority_handle(&self) -> Arc<P> {
        Arc::clone(&self.inner.priority)
    }

    /// Clones the shared payload handle without cloning `V`.
    #[must_use]
    pub fn value_handle(&self) -> Arc<V> {
        Arc::clone(&self.inner.value)
    }

    fn replacing_from(&self, replacement: &Self) -> Self {
        Self {
            inner: Arc::new(EntryInner {
                key: Arc::clone(&self.inner.key),
                priority: Arc::clone(&replacement.inner.priority),
                value: Arc::clone(&replacement.inner.value),
            }),
        }
    }

    fn same_identity(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.inner, &other.inner)
    }
}

impl<K, P, V> Clone for PrioritySearchEntry<K, P, V> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<K, P, V> fmt::Debug for PrioritySearchEntry<K, P, V>
where
    K: fmt::Debug,
    P: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PrioritySearchEntry")
            .field("key", self.key())
            .field("priority", self.priority())
            .field("value", self.value())
            .finish()
    }
}

impl<K, P, V> PartialEq for PrioritySearchEntry<K, P, V>
where
    K: PartialEq,
    P: PartialEq,
    V: PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        self.key() == other.key()
            && self.priority() == other.priority()
            && self.value() == other.value()
    }
}

impl<K, P, V> Eq for PrioritySearchEntry<K, P, V>
where
    K: Eq,
    P: Eq,
    V: Eq,
{
}

/// Statistics returned by the winner-cached AVL invariant audit.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PrioritySearchQueueStatistics {
    /// Number of key-equivalence classes.
    pub len: usize,
    /// Cached AVL height.
    pub height: usize,
    /// Largest absolute AVL balance factor encountered.
    pub maximum_absolute_balance_factor: usize,
}

/// A structural invariant violation found by [`PrioritySearchQueue::validate_structure`].
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PrioritySearchInvariantError {
    message: &'static str,
}

impl PrioritySearchInvariantError {
    fn new(message: &'static str) -> Self {
        Self { message }
    }
}

impl fmt::Display for PrioritySearchInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl Error for PrioritySearchInvariantError {}

/// An inclusive key range had its lower bound after its upper bound.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PrioritySearchRangeError;

impl fmt::Display for PrioritySearchRangeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("the minimum key must not follow the maximum key")
    }
}

impl Error for PrioritySearchRangeError {}

/// Result of a non-overwriting keyed insertion.
pub struct PrioritySearchAddResult<K, P, V> {
    /// Whether a new key-equivalence class was inserted.
    pub added: bool,
    /// Resulting persistent queue; duplicates reuse the original root.
    pub queue: PrioritySearchQueue<K, P, V>,
}

/// Result of a keyed removal.
///
/// `entry.is_some()` is the removal flag. The entry itself may contain `None` key, priority, or
/// payload values without colliding with the outer absence marker.
pub struct PrioritySearchRemoveResult<K, P, V> {
    /// Removed stored representative, or `None` when the key was absent.
    pub entry: Option<PrioritySearchEntry<K, P, V>>,
    /// Resulting queue; an absent removal reuses the original root.
    pub queue: PrioritySearchQueue<K, P, V>,
}

impl<K, P, V> PrioritySearchRemoveResult<K, P, V> {
    /// Returns whether an entry was removed.
    #[must_use]
    pub fn removed(&self) -> bool {
        self.entry.is_some()
    }
}

/// One successful minimum deletion.
pub struct PrioritySearchMinimumView<K, P, V> {
    /// Removed priority-then-key winner.
    pub entry: PrioritySearchEntry<K, P, V>,
    /// Persistent queue after removing the winner.
    pub remainder: PrioritySearchQueue<K, P, V>,
}

/// A persistent ordered map whose AVL nodes cache their minimum-priority entry.
///
/// One entry is retained per key-policy equivalence class. The first concrete key representative
/// is stable, while priority and payload are last-wins. Priority ties break by retained key order.
pub struct PrioritySearchQueue<K, P, V> {
    root: NodeLink<K, P, V>,
    key_policy: OrderPolicy<K>,
    priority_policy: OrderPolicy<P>,
}

impl<K, P, V> Clone for PrioritySearchQueue<K, P, V> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            key_policy: self.key_policy.clone(),
            priority_policy: self.priority_policy.clone(),
        }
    }
}

impl<K, P, V> fmt::Debug for PrioritySearchQueue<K, P, V>
where
    K: fmt::Debug,
    P: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.debug_list().entries(self.iter()).finish()
    }
}

impl<K: Ord, P: Ord, V> PrioritySearchQueue<K, P, V> {
    /// Creates an empty queue with canonical natural key and priority ordering.
    #[must_use]
    pub fn new() -> Self {
        Self::with_policies(OrderPolicy::natural(), OrderPolicy::natural())
    }
}

impl<K: Ord, P: Ord, V> Default for PrioritySearchQueue<K, P, V> {
    fn default() -> Self {
        Self::new()
    }
}

impl<K: Ord, P: Ord, V> FromIterator<PrioritySearchEntry<K, P, V>>
    for PrioritySearchQueue<K, P, V>
{
    fn from_iter<I: IntoIterator<Item = PrioritySearchEntry<K, P, V>>>(iter: I) -> Self {
        Self::from_entries(iter)
    }
}

impl<K, P, V> PrioritySearchQueue<K, P, V> {
    /// Creates an empty queue retaining distinct custom key and priority policies.
    #[must_use]
    pub fn with_comparers<KC, PC>(key_comparer: KC, priority_comparer: PC) -> Self
    where
        KC: OrderComparer<K> + 'static,
        PC: OrderComparer<P> + 'static,
    {
        Self::with_policies(
            OrderPolicy::custom(key_comparer),
            OrderPolicy::custom(priority_comparer),
        )
    }

    /// Creates an empty queue retaining explicit policies.
    #[must_use]
    pub fn with_policies(key_policy: OrderPolicy<K>, priority_policy: OrderPolicy<P>) -> Self {
        Self {
            root: None,
            key_policy,
            priority_policy,
        }
    }

    /// Builds a last-wins queue using canonical natural ordering.
    #[must_use]
    pub fn from_entries<I>(entries: I) -> Self
    where
        K: Ord,
        P: Ord,
        I: IntoIterator<Item = PrioritySearchEntry<K, P, V>>,
    {
        Self::from_entries_with_policies(entries, OrderPolicy::natural(), OrderPolicy::natural())
    }

    /// Builds a last-wins queue while retaining the first equivalent key representative.
    #[must_use]
    pub fn from_entries_with_policies<I>(
        entries: I,
        key_policy: OrderPolicy<K>,
        priority_policy: OrderPolicy<P>,
    ) -> Self
    where
        I: IntoIterator<Item = PrioritySearchEntry<K, P, V>>,
    {
        let mut result = Self::with_policies(key_policy, priority_policy);
        for entry in entries {
            result.root = Some(result.set_always(result.root.as_ref(), entry));
        }
        result
    }

    /// Returns the retained key-ordering policy.
    #[must_use]
    pub fn key_policy(&self) -> &OrderPolicy<K> {
        &self.key_policy
    }

    /// Returns the retained priority-ordering policy.
    #[must_use]
    pub fn priority_policy(&self) -> &OrderPolicy<P> {
        &self.priority_policy
    }

    /// Returns the number of key-equivalence classes.
    #[must_use]
    pub fn len(&self) -> usize {
        count_of(self.root.as_ref())
    }

    /// Returns whether the queue is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns the cached AVL height.
    #[must_use]
    pub fn height(&self) -> usize {
        height_of(self.root.as_ref())
    }

    /// Returns the global priority-then-key winner in O(1).
    #[must_use]
    pub fn minimum(&self) -> Option<&PrioritySearchEntry<K, P, V>> {
        self.root.as_ref().map(|root| &root.winner)
    }

    /// Returns whether an equivalent key is present.
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.find_node(key).is_some()
    }

    /// Returns the stored entry, including its first retained concrete key representative.
    #[must_use]
    pub fn get_entry(&self, key: &K) -> Option<&PrioritySearchEntry<K, P, V>> {
        self.find_node(key).map(|node| &node.entry)
    }

    /// Adds or replaces an entry in O(log n).
    ///
    /// Replacement is an exact structural no-op only when priority ordering, ordinary priority
    /// equality, and payload equality all agree with the stored representation.
    #[must_use]
    pub fn set_item(&self, key: K, priority: P, value: V) -> Self
    where
        P: PartialEq,
        V: PartialEq,
    {
        let entry = PrioritySearchEntry::new(key, priority, value);
        let result = self.set_overwrite(self.root.as_ref(), entry);
        if !result.changed {
            return self.clone();
        }
        Self {
            root: Some(result.node),
            key_policy: self.key_policy.clone(),
            priority_policy: self.priority_policy.clone(),
        }
    }

    /// Attempts to insert a new key-equivalence class without replacement.
    #[must_use]
    pub fn try_add(&self, key: K, priority: P, value: V) -> PrioritySearchAddResult<K, P, V> {
        let result = self.insert_new(
            self.root.as_ref(),
            PrioritySearchEntry::new(key, priority, value),
        );
        if !result.added {
            return PrioritySearchAddResult {
                added: false,
                queue: self.clone(),
            };
        }
        PrioritySearchAddResult {
            added: true,
            queue: Self {
                root: Some(result.node),
                key_policy: self.key_policy.clone(),
                priority_policy: self.priority_policy.clone(),
            },
        }
    }

    /// Removes an equivalent key in O(log n), reusing the root when absent.
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let result = self.remove_node(self.root.as_ref(), key);
        if result.entry.is_none() {
            return self.clone();
        }
        Self {
            root: result.node,
            key_policy: self.key_policy.clone(),
            priority_policy: self.priority_policy.clone(),
        }
    }

    /// Attempts keyed removal while returning the retained entry representative.
    #[must_use]
    pub fn try_remove(&self, key: &K) -> PrioritySearchRemoveResult<K, P, V> {
        let result = self.remove_node(self.root.as_ref(), key);
        let Some(entry) = result.entry else {
            return PrioritySearchRemoveResult {
                entry: None,
                queue: self.clone(),
            };
        };
        PrioritySearchRemoveResult {
            entry: Some(entry),
            queue: Self {
                root: result.node,
                key_policy: self.key_policy.clone(),
                priority_policy: self.priority_policy.clone(),
            },
        }
    }

    /// Removes and returns the global winner in O(log n), or `None` when empty.
    #[must_use]
    pub fn delete_minimum(&self) -> Option<PrioritySearchMinimumView<K, P, V>> {
        let entry = self.root.as_ref()?.winner.clone();
        let result = self.remove_node(self.root.as_ref(), entry.key());
        debug_assert!(result.entry.is_some());
        Some(PrioritySearchMinimumView {
            entry,
            remainder: Self {
                root: result.node,
                key_policy: self.key_policy.clone(),
                priority_policy: self.priority_policy.clone(),
            },
        })
    }

    /// Alias for [`Self::delete_minimum`] emphasizing the returned entry-plus-remainder view.
    #[must_use]
    pub fn minimum_view(&self) -> Option<PrioritySearchMinimumView<K, P, V>> {
        self.delete_minimum()
    }

    /// Returns an eager-validated, lazy iterator over an inclusive key range and priority threshold.
    ///
    /// Results are in key order. Each subtree whose cached winner exceeds `maximum_priority` is
    /// pruned. The cost is O(log n + v), where `v` is the number of nodes not excluded by winner
    /// pruning and may be n.
    pub fn enumerate_at_most<'a>(
        &'a self,
        minimum_key: &'a K,
        maximum_key: &'a K,
        maximum_priority: &'a P,
    ) -> Result<PrioritySearchRangeIter<'a, K, P, V>, PrioritySearchRangeError> {
        if self.key_policy.compare(minimum_key, maximum_key).is_gt() {
            return Err(PrioritySearchRangeError);
        }
        Ok(PrioritySearchRangeIter {
            pending: self
                .root
                .as_deref()
                .map(QueryFrame::Visit)
                .into_iter()
                .collect(),
            minimum_key,
            maximum_key,
            maximum_priority,
            key_policy: &self.key_policy,
            priority_policy: &self.priority_policy,
        })
    }

    /// Returns an explicit-stack in-order iterator.
    #[must_use]
    pub fn iter(&self) -> PrioritySearchIter<'_, K, P, V> {
        PrioritySearchIter::new(self.root.as_deref())
    }

    /// Validates strict key bounds, AVL balance, cached count/height, and every cached winner using
    /// an explicit worklist.
    pub fn validate_structure(
        &self,
    ) -> Result<PrioritySearchQueueStatistics, PrioritySearchInvariantError> {
        let Some(root) = self.root.as_deref() else {
            return Ok(PrioritySearchQueueStatistics::default());
        };
        let mut pending = vec![ValidationFrame {
            node: root,
            lower: None,
            upper: None,
        }];
        let mut validated_count = 0_usize;
        let mut maximum_balance = 0_usize;
        while let Some(frame) = pending.pop() {
            let node = frame.node;
            if let Some(lower) = frame.lower
                && !self.key_policy.compare(node.entry.key(), lower).is_gt()
            {
                return Err(PrioritySearchInvariantError::new(
                    "a priority-search node crosses its lower key bound",
                ));
            }
            if let Some(upper) = frame.upper
                && !self.key_policy.compare(node.entry.key(), upper).is_lt()
            {
                return Err(PrioritySearchInvariantError::new(
                    "a priority-search node crosses its upper key bound",
                ));
            }

            let expected_count = 1_usize
                .checked_add(count_of(node.left.as_ref()))
                .and_then(|count| count.checked_add(count_of(node.right.as_ref())))
                .ok_or_else(|| PrioritySearchInvariantError::new("validated PSQ count overflow"))?;
            let expected_height = 1_usize
                .checked_add(height_of(node.left.as_ref()).max(height_of(node.right.as_ref())))
                .ok_or_else(|| {
                    PrioritySearchInvariantError::new("validated PSQ height overflow")
                })?;
            let balance = signed_balance(node);
            if node.count != expected_count || node.height != expected_height {
                return Err(PrioritySearchInvariantError::new(
                    "a priority-search node has invalid cached metadata",
                ));
            }
            if balance.unsigned_abs() > 1 {
                return Err(PrioritySearchInvariantError::new(
                    "a priority-search node violates AVL balance",
                ));
            }
            let expected_winner =
                self.select_winner(&node.entry, node.left.as_ref(), node.right.as_ref());
            if !expected_winner.same_identity(&node.winner) {
                return Err(PrioritySearchInvariantError::new(
                    "a priority-search node has an invalid cached winner",
                ));
            }

            validated_count = validated_count
                .checked_add(1)
                .ok_or_else(|| PrioritySearchInvariantError::new("validated PSQ count overflow"))?;
            maximum_balance = maximum_balance.max(balance.unsigned_abs());
            if let Some(right) = node.right.as_deref() {
                pending.push(ValidationFrame {
                    node: right,
                    lower: Some(node.entry.key()),
                    upper: frame.upper,
                });
            }
            if let Some(left) = node.left.as_deref() {
                pending.push(ValidationFrame {
                    node: left,
                    lower: frame.lower,
                    upper: Some(node.entry.key()),
                });
            }
        }
        if validated_count != self.len() {
            return Err(PrioritySearchInvariantError::new(
                "priority-search root metadata disagrees with the validated tree",
            ));
        }
        Ok(PrioritySearchQueueStatistics {
            len: validated_count,
            height: self.height(),
            maximum_absolute_balance_factor: maximum_balance,
        })
    }

    /// Counts distinct AVL nodes shared by identity with `other`.
    #[must_use]
    pub fn shared_node_count(&self, other: &Self) -> usize {
        let left = collect_node_identities(self.root.as_ref());
        let mut visited = HashSet::new();
        let mut pending = other.root.as_ref().into_iter().cloned().collect::<Vec<_>>();
        let mut shared = 0;
        while let Some(node) = pending.pop() {
            let identity = Arc::as_ptr(&node);
            if !visited.insert(identity) {
                continue;
            }
            if left.contains(&identity) {
                shared += 1;
            }
            if let Some(left) = &node.left {
                pending.push(Arc::clone(left));
            }
            if let Some(right) = &node.right {
                pending.push(Arc::clone(right));
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

    /// Returns whether both versions find the exact same retained node for `key`.
    #[must_use]
    pub fn shares_node_for_key(&self, other: &Self, key: &K) -> bool {
        match (self.find_node_link(key), other.find_node_link(key)) {
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            (None, None) => true,
            _ => false,
        }
    }

    fn find_node(&self, key: &K) -> Option<&Node<K, P, V>> {
        self.find_node_link(key).map(Arc::as_ref)
    }

    fn find_node_link(&self, key: &K) -> Option<&Arc<Node<K, P, V>>> {
        let mut current = self.root.as_ref();
        while let Some(node) = current {
            match self.key_policy.compare(key, node.entry.key()) {
                Ordering::Equal => return Some(node),
                Ordering::Less => current = node.left.as_ref(),
                Ordering::Greater => current = node.right.as_ref(),
            }
        }
        None
    }

    fn set_always(
        &self,
        current: Option<&Arc<Node<K, P, V>>>,
        entry: PrioritySearchEntry<K, P, V>,
    ) -> Arc<Node<K, P, V>> {
        let Some(node) = current else {
            return self.new_node(entry, None, None);
        };
        match self.key_policy.compare(entry.key(), node.entry.key()) {
            Ordering::Equal => self.new_node(
                node.entry.replacing_from(&entry),
                node.left.clone(),
                node.right.clone(),
            ),
            Ordering::Less => {
                let left = self.set_always(node.left.as_ref(), entry);
                self.balance(self.new_node(node.entry.clone(), Some(left), node.right.clone()))
            }
            Ordering::Greater => {
                let right = self.set_always(node.right.as_ref(), entry);
                self.balance(self.new_node(node.entry.clone(), node.left.clone(), Some(right)))
            }
        }
    }

    fn set_overwrite(
        &self,
        current: Option<&Arc<Node<K, P, V>>>,
        entry: PrioritySearchEntry<K, P, V>,
    ) -> SetResult<K, P, V>
    where
        P: PartialEq,
        V: PartialEq,
    {
        let Some(node) = current else {
            return SetResult {
                node: self.new_node(entry, None, None),
                added: true,
                changed: true,
            };
        };
        match self.key_policy.compare(entry.key(), node.entry.key()) {
            Ordering::Equal => {
                if self
                    .priority_policy
                    .compare(entry.priority(), node.entry.priority())
                    .is_eq()
                    && entry.priority() == node.entry.priority()
                    && entry.value() == node.entry.value()
                {
                    SetResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                    }
                } else {
                    SetResult {
                        node: self.new_node(
                            node.entry.replacing_from(&entry),
                            node.left.clone(),
                            node.right.clone(),
                        ),
                        added: false,
                        changed: true,
                    }
                }
            }
            Ordering::Less => {
                let result = self.set_overwrite(node.left.as_ref(), entry);
                if !result.changed {
                    return SetResult {
                        node: Arc::clone(node),
                        added: result.added,
                        changed: false,
                    };
                }
                SetResult {
                    node: self.balance(self.new_node(
                        node.entry.clone(),
                        Some(result.node),
                        node.right.clone(),
                    )),
                    added: result.added,
                    changed: true,
                }
            }
            Ordering::Greater => {
                let result = self.set_overwrite(node.right.as_ref(), entry);
                if !result.changed {
                    return SetResult {
                        node: Arc::clone(node),
                        added: result.added,
                        changed: false,
                    };
                }
                SetResult {
                    node: self.balance(self.new_node(
                        node.entry.clone(),
                        node.left.clone(),
                        Some(result.node),
                    )),
                    added: result.added,
                    changed: true,
                }
            }
        }
    }

    fn insert_new(
        &self,
        current: Option<&Arc<Node<K, P, V>>>,
        entry: PrioritySearchEntry<K, P, V>,
    ) -> SetResult<K, P, V> {
        let Some(node) = current else {
            return SetResult {
                node: self.new_node(entry, None, None),
                added: true,
                changed: true,
            };
        };
        match self.key_policy.compare(entry.key(), node.entry.key()) {
            Ordering::Equal => SetResult {
                node: Arc::clone(node),
                added: false,
                changed: false,
            },
            Ordering::Less => {
                let result = self.insert_new(node.left.as_ref(), entry);
                if !result.added {
                    return SetResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                    };
                }
                SetResult {
                    node: self.balance(self.new_node(
                        node.entry.clone(),
                        Some(result.node),
                        node.right.clone(),
                    )),
                    added: true,
                    changed: true,
                }
            }
            Ordering::Greater => {
                let result = self.insert_new(node.right.as_ref(), entry);
                if !result.added {
                    return SetResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                    };
                }
                SetResult {
                    node: self.balance(self.new_node(
                        node.entry.clone(),
                        node.left.clone(),
                        Some(result.node),
                    )),
                    added: true,
                    changed: true,
                }
            }
        }
    }

    fn remove_node(&self, current: Option<&Arc<Node<K, P, V>>>, key: &K) -> NodeRemoval<K, P, V> {
        let Some(node) = current else {
            return NodeRemoval {
                node: None,
                entry: None,
            };
        };
        match self.key_policy.compare(key, node.entry.key()) {
            Ordering::Equal => match (&node.left, &node.right) {
                (None, _) => NodeRemoval {
                    node: node.right.clone(),
                    entry: Some(node.entry.clone()),
                },
                (_, None) => NodeRemoval {
                    node: node.left.clone(),
                    entry: Some(node.entry.clone()),
                },
                (Some(_), Some(right)) => {
                    let successor = minimum_key_node(right.as_ref()).entry.clone();
                    let removed_successor = self.remove_node(node.right.as_ref(), successor.key());
                    NodeRemoval {
                        node: Some(self.balance(self.new_node(
                            successor,
                            node.left.clone(),
                            removed_successor.node,
                        ))),
                        entry: Some(node.entry.clone()),
                    }
                }
            },
            Ordering::Less => {
                let result = self.remove_node(node.left.as_ref(), key);
                if result.entry.is_none() {
                    return NodeRemoval {
                        node: Some(Arc::clone(node)),
                        entry: None,
                    };
                }
                NodeRemoval {
                    node: Some(self.balance(self.new_node(
                        node.entry.clone(),
                        result.node,
                        node.right.clone(),
                    ))),
                    entry: result.entry,
                }
            }
            Ordering::Greater => {
                let result = self.remove_node(node.right.as_ref(), key);
                if result.entry.is_none() {
                    return NodeRemoval {
                        node: Some(Arc::clone(node)),
                        entry: None,
                    };
                }
                NodeRemoval {
                    node: Some(self.balance(self.new_node(
                        node.entry.clone(),
                        node.left.clone(),
                        result.node,
                    ))),
                    entry: result.entry,
                }
            }
        }
    }

    fn balance(&self, node: Arc<Node<K, P, V>>) -> Arc<Node<K, P, V>> {
        let factor = signed_balance(node.as_ref());
        if factor > 1 {
            let left = node
                .left
                .as_ref()
                .expect("left-heavy AVL node has a left child");
            return if height_of(left.left.as_ref()) < height_of(left.right.as_ref()) {
                let rotated_left = self.rotate_left(Arc::clone(left));
                self.rotate_right(self.new_node(
                    node.entry.clone(),
                    Some(rotated_left),
                    node.right.clone(),
                ))
            } else {
                self.rotate_right(node)
            };
        }
        if factor < -1 {
            let right = node
                .right
                .as_ref()
                .expect("right-heavy AVL node has a right child");
            return if height_of(right.right.as_ref()) < height_of(right.left.as_ref()) {
                let rotated_right = self.rotate_right(Arc::clone(right));
                self.rotate_left(self.new_node(
                    node.entry.clone(),
                    node.left.clone(),
                    Some(rotated_right),
                ))
            } else {
                self.rotate_left(node)
            };
        }
        node
    }

    fn rotate_left(&self, node: Arc<Node<K, P, V>>) -> Arc<Node<K, P, V>> {
        let pivot = node
            .right
            .as_ref()
            .expect("left rotation requires a right child");
        self.new_node(
            pivot.entry.clone(),
            Some(self.new_node(node.entry.clone(), node.left.clone(), pivot.left.clone())),
            pivot.right.clone(),
        )
    }

    fn rotate_right(&self, node: Arc<Node<K, P, V>>) -> Arc<Node<K, P, V>> {
        let pivot = node
            .left
            .as_ref()
            .expect("right rotation requires a left child");
        self.new_node(
            pivot.entry.clone(),
            pivot.left.clone(),
            Some(self.new_node(node.entry.clone(), pivot.right.clone(), node.right.clone())),
        )
    }

    fn new_node(
        &self,
        entry: PrioritySearchEntry<K, P, V>,
        left: NodeLink<K, P, V>,
        right: NodeLink<K, P, V>,
    ) -> Arc<Node<K, P, V>> {
        let winner = self.select_winner(&entry, left.as_ref(), right.as_ref());
        Arc::new(Node {
            entry,
            height: 1_usize
                .checked_add(height_of(left.as_ref()).max(height_of(right.as_ref())))
                .expect("priority-search height overflow"),
            count: 1_usize
                .checked_add(count_of(left.as_ref()))
                .and_then(|count| count.checked_add(count_of(right.as_ref())))
                .expect("priority-search count overflow"),
            left,
            right,
            winner,
        })
    }

    fn select_winner(
        &self,
        entry: &PrioritySearchEntry<K, P, V>,
        left: Option<&Arc<Node<K, P, V>>>,
        right: Option<&Arc<Node<K, P, V>>>,
    ) -> PrioritySearchEntry<K, P, V> {
        let mut winner = entry;
        if let Some(left) = left
            && self.is_before(&left.winner, winner)
        {
            winner = &left.winner;
        }
        if let Some(right) = right
            && self.is_before(&right.winner, winner)
        {
            winner = &right.winner;
        }
        winner.clone()
    }

    fn is_before(
        &self,
        left: &PrioritySearchEntry<K, P, V>,
        right: &PrioritySearchEntry<K, P, V>,
    ) -> bool {
        match self
            .priority_policy
            .compare(left.priority(), right.priority())
        {
            Ordering::Less => true,
            Ordering::Greater => false,
            Ordering::Equal => self.key_policy.compare(left.key(), right.key()).is_lt(),
        }
    }
}

/// Borrowed in-order iterator for [`PrioritySearchQueue`].
/// Presence-discriminated key-order cursor search.
pub struct PrioritySearchCursorSearch<K, P, V> {
    pub found: bool,
    pub cursor: PrioritySearchQueueCursor<K, P, V>,
}

/// Immutable key-order root-plus-rank cursor over a priority-search queue.
pub struct PrioritySearchQueueCursor<K, P, V> {
    queue: PrioritySearchQueue<K, P, V>,
    position: usize,
}

impl<K, P, V> Clone for PrioritySearchQueueCursor<K, P, V> {
    fn clone(&self) -> Self {
        Self {
            queue: self.queue.clone(),
            position: self.position,
        }
    }
}

impl<K, P, V> PrioritySearchQueue<K, P, V> {
    fn cursor_bound_rank(&self, key: &K, upper: bool) -> usize {
        let mut rank = 0;
        let mut node = self.root.as_deref();
        while let Some(current) = node {
            let comparison = self.key_policy.compare(current.entry.key(), key);
            if comparison.is_lt() || upper && comparison.is_eq() {
                rank += count_of(current.left.as_ref()) + 1;
                node = current.right.as_deref();
            } else {
                node = current.left.as_deref();
            }
        }
        rank
    }
    /// Selects the key-order entry at `rank` in O(h) using the cached subtree counts.
    ///
    /// Returns `None` when `rank` addresses the end gap or lies beyond it.
    fn cursor_entry_at(&self, rank: usize) -> Option<&PrioritySearchEntry<K, P, V>> {
        let mut node = self.root.as_deref();
        let mut remaining = rank;
        while let Some(current) = node {
            let left_count = current.left.as_ref().map_or(0, |child| child.count);
            match remaining.cmp(&left_count) {
                Ordering::Less => node = current.left.as_deref(),
                Ordering::Equal => return Some(&current.entry),
                Ordering::Greater => {
                    remaining -= left_count + 1;
                    node = current.right.as_deref();
                }
            }
        }
        None
    }
    /// Creates a cursor at the gap `position` in `0..=len` of the key-ordered sequence, or `None` when
    /// it exceeds the entry count.
    pub fn cursor_at(&self, position: usize) -> Option<PrioritySearchQueueCursor<K, P, V>> {
        (position <= self.len()).then(|| PrioritySearchQueueCursor {
            queue: self.clone(),
            position,
        })
    }
    /// Creates a cursor before the first entry whose key is not below `key`, that is, where `key`
    /// would be inserted. O(log n).
    pub fn cursor_at_lower_bound(&self, key: &K) -> PrioritySearchQueueCursor<K, P, V> {
        self.cursor_at(self.cursor_bound_rank(key, false))
            .expect("lower bound is valid")
    }
    /// Creates a cursor after any entry equivalent to `key`, that is, before the first entry whose key
    /// is strictly above it. O(log n).
    pub fn cursor_at_upper_bound(&self, key: &K) -> PrioritySearchQueueCursor<K, P, V> {
        self.cursor_at(self.cursor_bound_rank(key, true))
            .expect("upper bound is valid")
    }
    /// Seeks to `key` and reports whether it is present.
    ///
    /// On a miss the cursor sits at the lower bound, which is where `key` would be inserted, so it
    /// remains usable. Key equivalence is decided by the retained key ordering policy.
    pub fn find_cursor(&self, key: &K) -> PrioritySearchCursorSearch<K, P, V> {
        let cursor = self.cursor_at_lower_bound(key);
        PrioritySearchCursorSearch {
            found: cursor
                .peek_next()
                .is_some_and(|entry| self.key_policy.compare(entry.key(), key).is_eq()),
            cursor,
        }
    }
    /// Creates a cursor positioned at the entry with the smallest priority.
    ///
    /// This is the bridge between the queue's two indexes: the minimum is found by priority, and the
    /// cursor that comes back walks the *key* order from there. An empty queue yields a start cursor.
    pub fn cursor_at_minimum_priority(&self) -> PrioritySearchQueueCursor<K, P, V> {
        self.minimum().map_or_else(
            || self.cursor_at(0).expect("empty start is valid"),
            |entry| self.cursor_at_lower_bound(entry.key()),
        )
    }
}

impl<K, P, V> PrioritySearchQueueCursor<K, P, V> {
    /// Returns the entry count of the queue version this cursor is positioned in.
    pub fn len(&self) -> usize {
        self.queue.len()
    }
    /// Returns `true` when that queue version holds no entries.
    pub fn is_empty(&self) -> bool {
        self.queue.is_empty()
    }
    /// Returns the cursor's gap index in the key-ordered sequence, in `0..=len`.
    pub fn position(&self) -> usize {
        self.position
    }
    /// Returns `true` when the gap precedes the first entry in key order.
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }
    /// Returns `true` when the gap follows the last entry in key order.
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }
    /// Borrows the entry immediately before the gap, or `None` at the start.
    pub fn peek_previous(&self) -> Option<&PrioritySearchEntry<K, P, V>> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.queue.cursor_entry_at(rank))
    }
    /// Borrows the entry immediately after the gap, or `None` at the end.
    pub fn peek_next(&self) -> Option<&PrioritySearchEntry<K, P, V>> {
        self.queue.cursor_entry_at(self.position)
    }
    /// Returns a cursor one position earlier in key order, or `None` at the start. The receiver is
    /// unchanged.
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.queue.cursor_at(position))
    }
    /// Returns a cursor one position later in key order, or `None` at the end. The receiver is
    /// unchanged.
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            queue: self.queue.clone(),
            position: self.position + 1,
        })
    }
    /// Jumps to the gap at `position` within the same queue version, or `None` when `position`
    /// exceeds the entry count.
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.queue.cursor_at(position)
        }
    }
    /// Borrows the queue version this cursor is positioned in.
    pub fn snapshot(&self) -> &PrioritySearchQueue<K, P, V> {
        &self.queue
    }
    /// Removes the entry before the focused gap, leaving the gap where that entry was.
    ///
    /// Deletion only has to name a key, so it borrows the retained [`PrioritySearchEntry`] key
    /// handle rather than cloning `K`; the family's non-`Clone` payload support therefore extends
    /// to cursor deletion.
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let key = self.queue.cursor_entry_at(position)?.key_handle();
        Some(Self {
            queue: self.queue.remove(&key),
            position,
        })
    }
    /// Removes the entry after the focused gap, leaving the gap where that entry was.
    ///
    /// Like [`Self::delete_previous`], this requires no `Clone` bound on `K`, `P`, or `V`.
    pub fn delete_next(&self) -> Option<Self> {
        let key = self.queue.cursor_entry_at(self.position)?.key_handle();
        Some(Self {
            queue: self.queue.remove(&key),
            position: self.position,
        })
    }
}

impl<K: Clone, P: Clone + PartialEq, V: Clone + PartialEq> PrioritySearchQueueCursor<K, P, V> {
    /// Adds a new entry and returns a cursor just after it in key order.
    ///
    /// The gap moves to the key's ordered position rather than staying where the receiver was, since
    /// the entry's place is decided by the key ordering. An already present key fails, handing back
    /// the [`PrioritySearchAddResult`] that describes the rejection; the receiver keeps its version.
    pub fn insert(
        &self,
        key: K,
        priority: P,
        value: V,
    ) -> Result<Self, PrioritySearchAddResult<K, P, V>> {
        let position = self.queue.cursor_bound_rank(&key, false);
        let result = self.queue.try_add(key, priority, value);
        if result.added {
            Ok(Self {
                queue: result.queue,
                position: position + 1,
            })
        } else {
            Err(result)
        }
    }
    /// Adds `key`, or replaces its priority and value when already present, and returns a cursor
    /// positioned at the resulting entry.
    ///
    /// Changing a priority re-places the entry in the priority index while leaving key order intact.
    pub fn set_item(&self, key: K, priority: P, value: V) -> Self {
        let location = self.queue.find_cursor(&key);
        Self {
            queue: self.queue.set_item(key, priority, value),
            position: if location.found {
                location.cursor.position
            } else {
                location.cursor.position + 1
            },
        }
    }
    /// Replaces the priority and value of the entry after the gap, keeping its key and position, or
    /// returns `None` at the end.
    pub fn set_next(&self, priority: P, value: V) -> Option<Self> {
        let key = self.peek_next()?.key().clone();
        Some(Self {
            queue: self.queue.set_item(key, priority, value),
            position: self.position,
        })
    }
}

/// Borrowing iterator over a [`PrioritySearchQueue`]'s entries, in ascending key order.
pub struct PrioritySearchIter<'a, K, P, V> {
    pending: Vec<&'a Node<K, P, V>>,
    cursor: Option<&'a Node<K, P, V>>,
}

impl<'a, K, P, V> PrioritySearchIter<'a, K, P, V> {
    fn new(root: Option<&'a Node<K, P, V>>) -> Self {
        let mut result = Self {
            pending: Vec::new(),
            cursor: root,
        };
        result.push_left_spine();
        result
    }

    fn push_left_spine(&mut self) {
        while let Some(node) = self.cursor {
            self.pending.push(node);
            self.cursor = node.left.as_deref();
        }
    }
}

impl<'a, K, P, V> Iterator for PrioritySearchIter<'a, K, P, V> {
    type Item = &'a PrioritySearchEntry<K, P, V>;

    fn next(&mut self) -> Option<Self::Item> {
        let node = self.pending.pop()?;
        self.cursor = node.right.as_deref();
        self.push_left_spine();
        Some(&node.entry)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.pending.len(), None)
    }
}

impl<'a, K, P, V> IntoIterator for &'a PrioritySearchQueue<K, P, V> {
    type Item = &'a PrioritySearchEntry<K, P, V>;
    type IntoIter = PrioritySearchIter<'a, K, P, V>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

enum QueryFrame<'a, K, P, V> {
    Visit(&'a Node<K, P, V>),
    Emit(&'a PrioritySearchEntry<K, P, V>),
}

/// Borrowed, key-ordered iterator over a priority-search range query.
pub struct PrioritySearchRangeIter<'a, K, P, V> {
    pending: Vec<QueryFrame<'a, K, P, V>>,
    minimum_key: &'a K,
    maximum_key: &'a K,
    maximum_priority: &'a P,
    key_policy: &'a OrderPolicy<K>,
    priority_policy: &'a OrderPolicy<P>,
}

impl<'a, K, P, V> Iterator for PrioritySearchRangeIter<'a, K, P, V> {
    type Item = &'a PrioritySearchEntry<K, P, V>;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(frame) = self.pending.pop() {
            match frame {
                QueryFrame::Emit(entry) => return Some(entry),
                QueryFrame::Visit(node) => {
                    if self
                        .priority_policy
                        .compare(node.winner.priority(), self.maximum_priority)
                        .is_gt()
                    {
                        continue;
                    }
                    let lower = self.key_policy.compare(node.entry.key(), self.minimum_key);
                    let upper = self.key_policy.compare(node.entry.key(), self.maximum_key);
                    if upper.is_lt()
                        && let Some(right) = node.right.as_deref()
                    {
                        self.pending.push(QueryFrame::Visit(right));
                    }
                    if !lower.is_lt()
                        && !upper.is_gt()
                        && !self
                            .priority_policy
                            .compare(node.entry.priority(), self.maximum_priority)
                            .is_gt()
                    {
                        self.pending.push(QueryFrame::Emit(&node.entry));
                    }
                    if lower.is_gt()
                        && let Some(left) = node.left.as_deref()
                    {
                        self.pending.push(QueryFrame::Visit(left));
                    }
                }
            }
        }
        None
    }
}

struct Node<K, P, V> {
    entry: PrioritySearchEntry<K, P, V>,
    left: NodeLink<K, P, V>,
    right: NodeLink<K, P, V>,
    winner: PrioritySearchEntry<K, P, V>,
    height: usize,
    count: usize,
}

struct SetResult<K, P, V> {
    node: Arc<Node<K, P, V>>,
    added: bool,
    changed: bool,
}

struct NodeRemoval<K, P, V> {
    node: NodeLink<K, P, V>,
    entry: Option<PrioritySearchEntry<K, P, V>>,
}

struct ValidationFrame<'a, K, P, V> {
    node: &'a Node<K, P, V>,
    lower: Option<&'a K>,
    upper: Option<&'a K>,
}

fn height_of<K, P, V>(node: Option<&Arc<Node<K, P, V>>>) -> usize {
    node.map_or(0, |node| node.height)
}

fn count_of<K, P, V>(node: Option<&Arc<Node<K, P, V>>>) -> usize {
    node.map_or(0, |node| node.count)
}

fn signed_balance<K, P, V>(node: &Node<K, P, V>) -> isize {
    height_of(node.left.as_ref()) as isize - height_of(node.right.as_ref()) as isize
}

fn minimum_key_node<K, P, V>(mut node: &Node<K, P, V>) -> &Node<K, P, V> {
    while let Some(left) = node.left.as_deref() {
        node = left;
    }
    node
}

fn collect_node_identities<K, P, V>(
    root: Option<&Arc<Node<K, P, V>>>,
) -> HashSet<*const Node<K, P, V>> {
    let mut identities = HashSet::new();
    let mut pending = root.into_iter().cloned().collect::<Vec<_>>();
    while let Some(node) = pending.pop() {
        if !identities.insert(Arc::as_ptr(&node)) {
            continue;
        }
        if let Some(left) = &node.left {
            pending.push(Arc::clone(left));
        }
        if let Some(right) = &node.right {
            pending.push(Arc::clone(right));
        }
    }
    identities
}
