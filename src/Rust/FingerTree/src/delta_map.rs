//! Checkpoint-differential persistent sorted map.
//!
//! [`PersistentDeltaMap`] is a fully persistent sorted map that carries a designated *checkpoint*
//! plus a coalesced ordered index of the exact net changes from that checkpoint. It answers one
//! narrow question cheaply: *which key classes differ from the last checkpoint, and what were their
//! checkpoint and current values?* A plain persistent map makes versions cheap, but a root handle
//! alone does not name the changed keys; a write log names operations but leaves repeated writes,
//! cancellations, and ordering to be resolved later. This structure maintains the exact net answer
//! online, while edits happen.
//!
//! A version stores three immutable roots: the checkpoint state `B`, the current state `S`, and an
//! ordered change index `D` holding exactly one `(before, after)` record for every key class on
//! which `B` and `S` differ. The first effective write to a class captures its checkpoint-relative
//! `before`; later writes replace only `after`; returning a class to its checkpoint state removes
//! its record entirely. When `D` becomes empty the current root snaps back to the *exact* checkpoint
//! root, so a wholly cancelled epoch is storage-clean as well as extensionally clean and
//! [`PersistentDeltaMap::checkpoint`]/[`PersistentDeltaMap::rollback`] are O(1) identity operations.
//!
//! # Bounds
//!
//! Let `N = max(2, |dom(B) union dom(S)|)`, counting key-policy equivalence classes rather than
//! concrete key objects, and let `k = |D|` be the number of net-changed classes. Point lookup, point
//! update and removal, rank, and neighbor queries are O(log N), matching the underlying persistent
//! ordered map. An effective point edit costs `O(log N + log(k + 1)) = O(log N)` because it touches
//! `S` and at most one entry of `D`. [`PersistentDeltaMap::checkpoint`] and
//! [`PersistentDeltaMap::rollback`] are O(1). [`PersistentDeltaMap::get_change`] is O(log(k + 1)),
//! searching `D` rather than `S`. Selecting an extreme entry by key, such as
//! [`PersistentDeltaMap::min_entry`], is Theta(log N) rather than O(1): the ordered substrate is a
//! measured balanced tree with no cached extremes, so an extreme costs the same descent as any
//! other rank. Fully consuming [`PersistentDeltaMap::get_changes`]
//! is Theta(k + 1), which is output-optimal and is the whole point of the design: it avoids the
//! Theta(k log(N/k + 1)) adversarial divergent-path walk that a reference-pruned comparison between
//! two constant-degree path-copied balanced trees must perform, including Theta(log N) work to
//! report a single change. A live version occupies O(N + k) nodes; retaining every successor adds
//! O(log N) nodes per changed point update, the same asymptotic persistence cost as an ordinary
//! path-copied ordered map. These are the shipped bounds of this path-copying implementation; no
//! stronger worst-case update-space refinement is claimed. The Theta(k + 1) enumeration figure is a
//! full-consumption bound: unlike the lazy C# `GetChanges()`, this implementation materializes the
//! change set when the iterator is constructed, so setup is Theta(k) and abandoning the enumeration
//! early saves nothing. See [`PersistentDeltaMap::get_changes`].
//!
//! The mutation surface is deliberately point-only. An eager bulk clear would produce Theta(N)
//! removal records and cannot simultaneously retain an ordinary map's O(1) clear bound, so callers
//! start a fresh epoch with [`PersistentDeltaMap::with_policies`] when they do not need an
//! enumerable delta, and remove entries explicitly when they do.
//!
//! # Policies
//!
//! Key identity and output order come from a retained [`OrderPolicy`]; two keys are the same class
//! exactly when the policy compares them equal. Semantic no-ops and delta cancellation come from a
//! retained [`EqualityPolicy`] over values. "Exact" therefore means exact *extensionally* under
//! those two policies, not by object identity of the retained key or value representatives. A
//! baseline key representative is retained across point updates and delete/re-add round trips; a
//! newly added class retains its first representative while its net-added record is active, and a
//! later addition after complete cancellation begins a new representative episode. The key policy
//! must define a stable total preorder and the value policy a stable equivalence relation. Raw
//! `f32` and `f64` values are deliberately excluded from the plain natural value policy, which is
//! why the natural-policy constructors bound `V: Eq`: IEEE equality is not reflexive, so writing
//! `NaN` over `NaN` would be recorded as a change that never cancels. Float values pass
//! [`EqualityPolicy::reflexive_ieee`] to [`PersistentDeltaMap::with_policies`] instead. Policy
//! side effects cannot be undone, but a panicking policy never publishes a partial successor: the
//! receiver and every retained branch remain valid and unchanged. [`PersistentDeltaMap::checkpoint`],
//! [`PersistentDeltaMap::rollback`], and [`PersistentDeltaMap::get_changes`] invoke no key or value
//! policy callback at all.
//!
//! Concurrent reads over retained versions are safe: every version is immutable, snapshots clone in
//! O(1), and both policy traits require [`Send`] + [`Sync`].

use crate::equality::EqualityPolicy;
use crate::ordering::OrderPolicy;
use crate::sorted::SortedMap;
use std::cmp::Ordering;
use std::error::Error;
use std::fmt;
use std::iter::FusedIterator;
use std::sync::Arc;

/// A stored key paired with the retained ordering policy that decides its class.
///
/// The crate's [`SortedMap`] takes ordering from `Ord`, while this structure takes it from a
/// retained [`OrderPolicy`] value. Wrapping each stored key with a clone of the policy handle
/// bridges the two without copying the comparer: the wrapper's `Ord` delegates to the policy, so one
/// map instance is ordered by exactly one comparer. The key itself is retained through [`Arc`], so
/// tree measures and representative bookkeeping clone a handle rather than the key.
struct DeltaMapKey<K> {
    key: Arc<K>,
    policy: OrderPolicy<K>,
}

impl<K> DeltaMapKey<K> {
    fn new(key: Arc<K>, policy: &OrderPolicy<K>) -> Self {
        Self {
            key,
            policy: policy.clone(),
        }
    }
}

impl<K> Clone for DeltaMapKey<K> {
    fn clone(&self) -> Self {
        Self {
            key: Arc::clone(&self.key),
            policy: self.policy.clone(),
        }
    }
}

impl<K> PartialEq for DeltaMapKey<K> {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other).is_eq()
    }
}

impl<K> Eq for DeltaMapKey<K> {}

impl<K> PartialOrd for DeltaMapKey<K> {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl<K> Ord for DeltaMapKey<K> {
    fn cmp(&self, other: &Self) -> Ordering {
        self.policy.compare(&self.key, &other.key)
    }
}

impl<K: fmt::Debug> fmt::Debug for DeltaMapKey<K> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.key.fmt(formatter)
    }
}

/// One `(before, after)` endpoint pair stored in the change index.
struct DeltaRecord<V> {
    before: Option<Arc<V>>,
    after: Option<Arc<V>>,
}

impl<V> Clone for DeltaRecord<V> {
    fn clone(&self) -> Self {
        Self {
            before: self.before.clone(),
            after: self.after.clone(),
        }
    }
}

/// Classifies one net key change relative to a [`PersistentDeltaMap`] checkpoint.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum PersistentMapChangeKind {
    /// The key class is absent at the checkpoint and present in the current map.
    Added,
    /// The key class is present at the checkpoint and absent from the current map.
    Removed,
    /// The key class is present at both endpoints with non-equivalent values.
    Updated,
}

/// One exact net change between a version's checkpoint and its current state.
///
/// The key is the checkpoint representative when the class was present at the checkpoint; otherwise
/// it is the current addition episode's first representative. Presence is carried by [`Option`], so
/// an absent endpoint stays distinct from a present value that happens to be `None`, empty, or a
/// default.
#[derive(Debug, Eq, PartialEq)]
pub struct PersistentMapChange<K, V> {
    key: Arc<K>,
    before: Option<Arc<V>>,
    after: Option<Arc<V>>,
}

impl<K, V> Clone for PersistentMapChange<K, V> {
    fn clone(&self) -> Self {
        Self {
            key: Arc::clone(&self.key),
            before: self.before.clone(),
            after: self.after.clone(),
        }
    }
}

impl<K, V> PersistentMapChange<K, V> {
    /// Borrows the retained key representative of the changed class.
    #[must_use]
    pub fn key(&self) -> &K {
        &self.key
    }

    /// Borrows the checkpoint endpoint, or `None` when the class was absent at the checkpoint.
    #[must_use]
    pub fn before(&self) -> Option<&V> {
        self.before.as_deref()
    }

    /// Borrows the current endpoint, or `None` when the class is absent from the current map.
    #[must_use]
    pub fn after(&self) -> Option<&V> {
        self.after.as_deref()
    }

    /// Returns the change classification implied by the two endpoints.
    ///
    /// A record absent at both endpoints is not a net change and is never produced.
    #[must_use]
    pub fn kind(&self) -> PersistentMapChangeKind {
        match (&self.before, &self.after) {
            (Some(_), Some(_)) => PersistentMapChangeKind::Updated,
            (Some(_), None) => PersistentMapChangeKind::Removed,
            (None, Some(_)) => PersistentMapChangeKind::Added,
            (None, None) => unreachable!("a recorded change is never absent at both endpoints"),
        }
    }

    /// Consumes the record and returns its retained handles, so a consumer can keep the key and
    /// endpoint values without cloning them or borrowing the map.
    #[must_use]
    pub fn into_parts(self) -> (Arc<K>, Option<Arc<V>>, Option<Arc<V>>) {
        (self.key, self.before, self.after)
    }
}

/// An immutable ordered snapshot of one [`PersistentDeltaMap`] state root.
///
/// This is the escape hatch that hands an underlying persistent ordered map to other code:
/// [`PersistentDeltaMap::current_snapshot`] and [`PersistentDeltaMap::checkpoint_snapshot`] borrow
/// one in O(1), and cloning one is O(1). Keys and values are retained through [`Arc`], so a snapshot
/// outlives the version it came from without copying payloads. Two snapshots for which
/// [`DeltaMapSnapshot::shares_storage_with`] holds are the same root and cannot observe a change
/// made through the other.
pub struct DeltaMapSnapshot<K, V> {
    entries: SortedMap<DeltaMapKey<K>, Arc<V>>,
    policy: OrderPolicy<K>,
}

impl<K, V> Clone for DeltaMapSnapshot<K, V> {
    fn clone(&self) -> Self {
        Self {
            entries: self.entries.clone(),
            policy: self.policy.clone(),
        }
    }
}

impl<K, V> fmt::Debug for DeltaMapSnapshot<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("DeltaMapSnapshot")
            .field("len", &self.len())
            .finish_non_exhaustive()
    }
}

impl<K, V> DeltaMapSnapshot<K, V> {
    fn from_entries(entries: SortedMap<DeltaMapKey<K>, Arc<V>>, policy: OrderPolicy<K>) -> Self {
        Self { entries, policy }
    }

    /// Returns the retained key-ordering policy.
    #[must_use]
    pub fn key_policy(&self) -> &OrderPolicy<K> {
        &self.policy
    }

    /// Returns the number of entries. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Returns `true` when the snapshot holds no entries. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Borrows the entry at zero-based key rank, or `None` when out of range. O(log N).
    #[must_use]
    pub fn entry_at(&self, index: usize) -> Option<(&K, &V)> {
        self.entries.entry_at(index).map(unwrap_entry)
    }

    /// Borrows the least entry by key, or `None` when empty. Theta(log N).
    ///
    /// This is a rank-0 select, not a cached extreme: the substrate is a measured balanced tree, so
    /// reaching the leftmost leaf costs a full descent like any other rank.
    #[must_use]
    pub fn min_entry(&self) -> Option<(&K, &V)> {
        self.entries.min_entry().map(unwrap_entry)
    }

    /// Borrows the greatest entry by key, or `None` when empty. Theta(log N).
    ///
    /// This is a rank-`len - 1` select, not a cached extreme: the substrate is a measured balanced
    /// tree, so reaching the rightmost leaf costs a full descent like any other rank.
    #[must_use]
    pub fn max_entry(&self) -> Option<(&K, &V)> {
        self.entries.max_entry().map(unwrap_entry)
    }

    /// Copies the entries into a vector in ascending key order, retaining the stored key and value
    /// handles. Theta(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<(Arc<K>, Arc<V>)> {
        self.entries
            .to_vec()
            .into_iter()
            .map(|(key, value)| (key.key, value))
            .collect()
    }

    /// Copies the keys into a vector in ascending order. Theta(n).
    #[must_use]
    pub fn keys_to_vec(&self) -> Vec<Arc<K>> {
        self.to_vec().into_iter().map(|(key, _)| key).collect()
    }

    /// Copies the values into a vector in ascending key order. Theta(n).
    #[must_use]
    pub fn values_to_vec(&self) -> Vec<Arc<V>> {
        self.to_vec().into_iter().map(|(_, value)| value).collect()
    }

    /// Iterates the entries in ascending key order. Theta(n) to construct, because the entries are
    /// materialized into a vector of retained handles rather than walked lazily.
    #[must_use]
    pub fn iter(&self) -> DeltaMapIter<K, V> {
        DeltaMapIter {
            entries: self.to_vec().into_iter(),
        }
    }

    /// Returns `true` when both snapshots are the same root, so neither can observe a change made
    /// through the other. A representation test, not an equality test. O(1).
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.entries.shares_storage_with(&other.entries)
    }
}

impl<K, V> DeltaMapSnapshot<K, V>
where
    K: Clone,
{
    /// Reports whether an entry exists for `key`'s class. O(log N).
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.get(key).is_some()
    }

    /// Borrows the value stored for `key`'s class, or `None` when absent. O(log N).
    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.get_entry(key).map(|(_, value)| value)
    }

    /// Borrows the retained representative and value for `key`'s class. O(log N).
    #[must_use]
    pub fn get_entry(&self, key: &K) -> Option<(&K, &V)> {
        lookup_entry(&self.entries, &self.probe(key)).map(unwrap_entry)
    }

    /// Returns the zero-based rank of `key`'s class, or `None` when absent. O(log N).
    #[must_use]
    pub fn index_of_key(&self, key: &K) -> Option<usize> {
        self.entries.index_of_key(&self.probe(key))
    }

    /// Borrows the greatest entry whose key is at most `key`. O(log N).
    #[must_use]
    pub fn floor_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.entries.floor_entry(&self.probe(key)).map(unwrap_entry)
    }

    /// Borrows the least entry whose key is at least `key`. O(log N).
    #[must_use]
    pub fn ceiling_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.entries
            .ceiling_entry(&self.probe(key))
            .map(unwrap_entry)
    }

    /// Borrows the greatest entry whose key is strictly below `key`. O(log N).
    #[must_use]
    pub fn lower_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.entries.lower_entry(&self.probe(key)).map(unwrap_entry)
    }

    /// Borrows the least entry whose key is strictly above `key`. O(log N).
    #[must_use]
    pub fn higher_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.entries.higher_entry(&self.probe(key)).map(unwrap_entry)
    }

    /// Returns the entries whose keys lie in the inclusive range `[low, high]`. An inverted range
    /// yields an empty snapshot. O(log N).
    #[must_use]
    pub fn get_key_range(&self, low: &K, high: &K) -> Self {
        Self::from_entries(
            self.entries
                .get_key_range(&self.probe(low), &self.probe(high)),
            self.policy.clone(),
        )
    }

    fn probe(&self, key: &K) -> DeltaMapKey<K> {
        DeltaMapKey::new(Arc::new(key.clone()), &self.policy)
    }
}

/// Iterates a [`DeltaMapSnapshot`]'s entries in ascending key order, yielding retained handles.
pub struct DeltaMapIter<K, V> {
    entries: std::vec::IntoIter<(Arc<K>, Arc<V>)>,
}

impl<K, V> Iterator for DeltaMapIter<K, V> {
    type Item = (Arc<K>, Arc<V>);

    fn next(&mut self) -> Option<Self::Item> {
        self.entries.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.entries.size_hint()
    }
}

impl<K, V> DoubleEndedIterator for DeltaMapIter<K, V> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.entries.next_back()
    }
}

impl<K, V> ExactSizeIterator for DeltaMapIter<K, V> {}

impl<K, V> FusedIterator for DeltaMapIter<K, V> {}

impl<K, V> fmt::Debug for DeltaMapIter<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("DeltaMapIter")
            .field("remaining", &self.entries.len())
            .finish_non_exhaustive()
    }
}

/// Iterates the exact net changes of one immutable version, once each, in ascending key order.
///
/// Fully consuming this iterator is Theta(k + 1) and invokes no key or value policy callback. The
/// change set is materialized when the iterator is constructed rather than walked lazily; see
/// [`PersistentDeltaMap::get_changes`] for why, and for what that costs a partial enumeration.
pub struct DeltaMapChanges<K, V> {
    changes: std::vec::IntoIter<PersistentMapChange<K, V>>,
}

impl<K, V> Iterator for DeltaMapChanges<K, V> {
    type Item = PersistentMapChange<K, V>;

    fn next(&mut self) -> Option<Self::Item> {
        self.changes.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.changes.size_hint()
    }
}

impl<K, V> DoubleEndedIterator for DeltaMapChanges<K, V> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.changes.next_back()
    }
}

impl<K, V> ExactSizeIterator for DeltaMapChanges<K, V> {}

impl<K, V> FusedIterator for DeltaMapChanges<K, V> {}

impl<K, V> fmt::Debug for DeltaMapChanges<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("DeltaMapChanges")
            .field("remaining", &self.changes.len())
            .finish_non_exhaustive()
    }
}

/// Representation statistics returned by [`PersistentDeltaMap::validate_structure`].
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct DeltaMapStatistics {
    /// Number of current entries.
    pub len: usize,
    /// Number of checkpoint entries.
    pub checkpoint_len: usize,
    /// Number of exact net-changed key classes.
    pub change_count: usize,
    /// Number of records absent at the checkpoint and present now.
    pub added_count: usize,
    /// Number of records present at the checkpoint and absent now.
    pub removed_count: usize,
    /// Number of records present at both endpoints with non-equivalent values.
    pub updated_count: usize,
    /// Whether the current state is storage-identical to the checkpoint.
    pub is_clean: bool,
}

/// An invariant failure detected by [`PersistentDeltaMap::validate_structure`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct DeltaMapInvariantError {
    message: &'static str,
}

impl fmt::Display for DeltaMapInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl Error for DeltaMapInvariantError {}

/// An immutable, fully persistent sorted map with an explicit checkpoint and a coalesced ordered
/// index of the exact net changes from that checkpoint.
///
/// See the [module documentation](self) for the representation, the policy contract, and the exact
/// complexity statements. Every operation returns a new version and leaves the receiver, and every
/// other retained version, valid and unchanged. A semantic no-op returns a version sharing all three
/// of the receiver's roots.
pub struct PersistentDeltaMap<K, V> {
    current: DeltaMapSnapshot<K, V>,
    checkpoint: DeltaMapSnapshot<K, V>,
    changes: SortedMap<DeltaMapKey<K>, DeltaRecord<V>>,
    key_policy: OrderPolicy<K>,
    value_policy: EqualityPolicy<V>,
}

impl<K, V> Clone for PersistentDeltaMap<K, V> {
    fn clone(&self) -> Self {
        Self {
            current: self.current.clone(),
            checkpoint: self.checkpoint.clone(),
            changes: self.changes.clone(),
            key_policy: self.key_policy.clone(),
            value_policy: self.value_policy.clone(),
        }
    }
}

impl<K, V> fmt::Debug for PersistentDeltaMap<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PersistentDeltaMap")
            .field("len", &self.len())
            .field("change_count", &self.change_count())
            .finish_non_exhaustive()
    }
}

impl<K, V> PersistentDeltaMap<K, V>
where
    K: Ord,
    V: Eq,
{
    /// Creates an empty map using the canonical natural key order and natural value equality. O(1).
    #[must_use]
    pub fn new() -> Self {
        Self::with_policies(OrderPolicy::natural(), EqualityPolicy::natural())
    }

    /// Creates a clean checkpoint from `entries` under the natural policies, with the last
    /// equivalent key winning. O(m log m) for `m` supplied entries.
    ///
    /// The `V: Eq` bound excludes raw `f32` and `f64`; build those with
    /// [`Self::from_entries_with_policies`] and [`EqualityPolicy::reflexive_ieee`].
    #[must_use]
    pub fn from_entries<I>(entries: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        Self::from_entries_with_policies(entries, OrderPolicy::natural(), EqualityPolicy::natural())
    }
}

impl<K, V> Default for PersistentDeltaMap<K, V>
where
    K: Ord,
    V: Eq,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<K, V> FromIterator<(K, V)> for PersistentDeltaMap<K, V>
where
    K: Ord,
    V: Eq,
{
    fn from_iter<I: IntoIterator<Item = (K, V)>>(iter: I) -> Self {
        Self::from_entries(iter)
    }
}

impl<K, V> PersistentDeltaMap<K, V> {
    /// Creates an empty map with explicit key-order and value-equality policies. O(1).
    ///
    /// This is also the O(1) way to start an unrelated clean epoch. It is not equivalent to clearing
    /// while retaining the old checkpoint, which the point-only mutation surface deliberately omits.
    #[must_use]
    pub fn with_policies(key_policy: OrderPolicy<K>, value_policy: EqualityPolicy<V>) -> Self {
        let state = DeltaMapSnapshot::from_entries(SortedMap::new(), key_policy.clone());
        Self {
            current: state.clone(),
            checkpoint: state,
            changes: SortedMap::new(),
            key_policy,
            value_policy,
        }
    }

    /// Creates a clean checkpoint from `entries` with explicit policies, with the last equivalent
    /// key winning: the last entry of an equivalence class supplies both the retained key
    /// representative and the value. O(m log m) for `m` supplied entries.
    #[must_use]
    pub fn from_entries_with_policies<I>(
        entries: I,
        key_policy: OrderPolicy<K>,
        value_policy: EqualityPolicy<V>,
    ) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        let stored: SortedMap<DeltaMapKey<K>, Arc<V>> = entries
            .into_iter()
            .map(|(key, value)| (DeltaMapKey::new(Arc::new(key), &key_policy), Arc::new(value)))
            .collect();
        let state = DeltaMapSnapshot::from_entries(stored, key_policy.clone());
        Self {
            current: state.clone(),
            checkpoint: state,
            changes: SortedMap::new(),
            key_policy,
            value_policy,
        }
    }

    /// Returns the retained policy defining key equivalence and ascending order.
    #[must_use]
    pub fn key_policy(&self) -> &OrderPolicy<K> {
        &self.key_policy
    }

    /// Returns the retained policy defining value no-ops and change cancellation.
    #[must_use]
    pub fn value_policy(&self) -> &EqualityPolicy<V> {
        &self.value_policy
    }

    /// Borrows the current persistent ordered-map snapshot. O(1).
    #[must_use]
    pub fn current_snapshot(&self) -> &DeltaMapSnapshot<K, V> {
        &self.current
    }

    /// Borrows the checkpoint persistent ordered-map snapshot. O(1).
    #[must_use]
    pub fn checkpoint_snapshot(&self) -> &DeltaMapSnapshot<K, V> {
        &self.checkpoint
    }

    /// Returns the number of current entries. O(1).
    #[must_use]
    pub fn len(&self) -> usize {
        self.current.len()
    }

    /// Returns `true` when the current map is empty. O(1).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.current.is_empty()
    }

    /// Returns the number of exact net-changed key classes. O(1).
    #[must_use]
    pub fn change_count(&self) -> usize {
        self.changes.len()
    }

    /// Returns `true` when the current map differs semantically from its checkpoint. O(1).
    #[must_use]
    pub fn has_changes(&self) -> bool {
        !self.changes.is_empty()
    }

    /// Borrows the current entry at zero-based key rank, or `None` when out of range. O(log N).
    #[must_use]
    pub fn entry_at(&self, index: usize) -> Option<(&K, &V)> {
        self.current.entry_at(index)
    }

    /// Borrows the least current entry by key, or `None` when empty. Theta(log N).
    ///
    /// Delegates to [`DeltaMapSnapshot::min_entry`], which is a rank select rather than a cached
    /// extreme.
    #[must_use]
    pub fn min_entry(&self) -> Option<(&K, &V)> {
        self.current.min_entry()
    }

    /// Borrows the greatest current entry by key, or `None` when empty. Theta(log N).
    ///
    /// Delegates to [`DeltaMapSnapshot::max_entry`], which is a rank select rather than a cached
    /// extreme.
    #[must_use]
    pub fn max_entry(&self) -> Option<(&K, &V)> {
        self.current.max_entry()
    }

    /// Copies the current entries into a vector in ascending key order. Theta(n).
    #[must_use]
    pub fn to_vec(&self) -> Vec<(Arc<K>, Arc<V>)> {
        self.current.to_vec()
    }

    /// Copies the current keys into a vector in ascending order. Theta(n).
    #[must_use]
    pub fn keys_to_vec(&self) -> Vec<Arc<K>> {
        self.current.keys_to_vec()
    }

    /// Copies the current values into a vector in ascending key order. Theta(n).
    #[must_use]
    pub fn values_to_vec(&self) -> Vec<Arc<V>> {
        self.current.values_to_vec()
    }

    /// Iterates the current entries in ascending key order. Theta(n) to construct; see
    /// [`DeltaMapSnapshot::iter`].
    #[must_use]
    pub fn iter(&self) -> DeltaMapIter<K, V> {
        self.current.iter()
    }

    /// Iterates the exact net changes once each in ascending key order. Theta(k + 1) when fully
    /// consumed, and free of key and value policy callbacks.
    ///
    /// # Divergence from the C# baseline
    ///
    /// The C# `GetChanges()` is lazy: Theta(1) to obtain, Theta(k + 1) to consume, so abandoning it
    /// after one element costs Theta(log k). This implementation is eager. Setup is Theta(k): the
    /// change index is materialized into a vector of retained handles before the first `next` call,
    /// because the crate's [`SortedMap`] publishes ordered contents only through `to_vec` and a
    /// borrowing tree cursor cannot be held by the owned iterator this method returns. Full
    /// consumption is therefore Theta(k + 1) as documented above and matches the baseline, but a
    /// partial enumeration does not: it pays for the whole change set. The copying is of `Arc`
    /// handles, not of keys or values, and no key or value policy callback is invoked.
    #[must_use]
    pub fn get_changes(&self) -> DeltaMapChanges<K, V> {
        let changes: Vec<PersistentMapChange<K, V>> = self
            .changes
            .to_vec()
            .into_iter()
            .map(|(key, record)| PersistentMapChange {
                key: key.key,
                before: record.before,
                after: record.after,
            })
            .collect();
        DeltaMapChanges {
            changes: changes.into_iter(),
        }
    }

    /// Makes the current snapshot the new checkpoint and resets the change index. O(1).
    ///
    /// An already-clean version returns a version sharing all of its storage, and no key or value
    /// policy callback is invoked.
    #[must_use]
    pub fn checkpoint(&self) -> Self {
        if self.is_clean() {
            return self.clone();
        }

        Self {
            current: self.current.clone(),
            checkpoint: self.current.clone(),
            changes: SortedMap::new(),
            key_policy: self.key_policy.clone(),
            value_policy: self.value_policy.clone(),
        }
    }

    /// Returns to this version's checkpoint and resets the change index. O(1).
    ///
    /// An already-clean version returns a version sharing all of its storage, and no key or value
    /// policy callback is invoked.
    #[must_use]
    pub fn rollback(&self) -> Self {
        if self.is_clean() {
            return self.clone();
        }

        Self {
            current: self.checkpoint.clone(),
            checkpoint: self.checkpoint.clone(),
            changes: SortedMap::new(),
            key_policy: self.key_policy.clone(),
            value_policy: self.value_policy.clone(),
        }
    }

    /// Returns `true` when both versions are backed by the same three roots, so neither can observe
    /// a change made through the other. A representation test, not an equality test. O(1).
    #[must_use]
    pub fn shares_storage_with(&self, other: &Self) -> bool {
        self.current.shares_storage_with(&other.current)
            && self.checkpoint.shares_storage_with(&other.checkpoint)
            && self.changes.shares_storage_with(&other.changes)
    }

    /// Walks the whole representation and returns [`DeltaMapStatistics`], or the first
    /// [`DeltaMapInvariantError`].
    ///
    /// This is diagnostic validation, not part of any operation's cost: it is O((N + k) log N) and
    /// does invoke key and value policy callbacks.
    pub fn validate_structure(&self) -> Result<DeltaMapStatistics, DeltaMapInvariantError> {
        let current = self.current.entries.to_vec();
        let checkpoint = self.checkpoint.entries.to_vec();
        let changes = self.changes.to_vec();

        self.check_ascending(
            current.iter().map(|(key, _)| key),
            "the current state is not strictly ascending by key",
        )?;
        self.check_ascending(
            checkpoint.iter().map(|(key, _)| key),
            "the checkpoint state is not strictly ascending by key",
        )?;
        self.check_ascending(
            changes.iter().map(|(key, _)| key),
            "the change index is not strictly ascending by key",
        )?;

        let mut statistics = DeltaMapStatistics {
            len: current.len(),
            checkpoint_len: checkpoint.len(),
            change_count: changes.len(),
            is_clean: self.current.shares_storage_with(&self.checkpoint),
            ..DeltaMapStatistics::default()
        };

        if changes.is_empty() && !statistics.is_clean {
            return Err(DeltaMapInvariantError {
                message: "a version without changes does not reuse its checkpoint root",
            });
        }

        for (key, record) in &changes {
            if self.endpoints_equivalent(&record.before, &record.after) {
                return Err(DeltaMapInvariantError {
                    message: "a recorded change has equivalent endpoints",
                });
            }

            let checkpoint_value = lookup_entry(&self.checkpoint.entries, key).map(|(_, v)| v);
            if !self.endpoint_matches(&record.before, checkpoint_value) {
                return Err(DeltaMapInvariantError {
                    message: "a change's before endpoint differs from the checkpoint state",
                });
            }

            let current_value = lookup_entry(&self.current.entries, key).map(|(_, v)| v);
            if !self.endpoint_matches(&record.after, current_value) {
                return Err(DeltaMapInvariantError {
                    message: "a change's after endpoint differs from the current state",
                });
            }

            match (&record.before, &record.after) {
                (None, Some(_)) => statistics.added_count += 1,
                (Some(_), None) => statistics.removed_count += 1,
                (Some(_), Some(_)) => statistics.updated_count += 1,
                (None, None) => {
                    return Err(DeltaMapInvariantError {
                        message: "a recorded change is absent at both endpoints",
                    });
                }
            }
        }

        let mut expected = 0;
        for (key, value) in &checkpoint {
            let current_value = lookup_entry(&self.current.entries, key).map(|(_, v)| v);
            let unchanged =
                current_value.is_some_and(|other| self.value_policy.equals(value, other));
            if !unchanged {
                expected += 1;
                if lookup_entry(&self.changes, key).is_none() {
                    return Err(DeltaMapInvariantError {
                        message: "a checkpoint difference has no recorded change",
                    });
                }
            }
        }

        for (key, _) in &current {
            if lookup_entry(&self.checkpoint.entries, key).is_none() {
                expected += 1;
                if lookup_entry(&self.changes, key).is_none() {
                    return Err(DeltaMapInvariantError {
                        message: "a current addition has no recorded change",
                    });
                }
            }
        }

        if expected != changes.len() {
            return Err(DeltaMapInvariantError {
                message: "the recorded change cardinality is not exact",
            });
        }

        Ok(statistics)
    }

    /// Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N).
    ///
    /// A write whose value is equivalent to the current value under the retained value policy is a
    /// semantic no-op and returns a version sharing all of the receiver's storage. A write that
    /// returns a class to its checkpoint state removes that class's record.
    #[must_use]
    pub fn set_item(&self, key: K, value: V) -> Self {
        let probe = DeltaMapKey::new(Arc::new(key), &self.key_policy);
        let value = Arc::new(value);
        let current_entry = lookup_entry(&self.current.entries, &probe);
        if let Some((_, current_value)) = current_entry
            && self.value_policy.equals(current_value, &value)
        {
            return self.clone();
        }

        let change_entry = lookup_entry(&self.changes, &probe);
        // A class present at the checkpoint keeps its baseline representative; a still-active
        // addition episode keeps its first representative; only a genuinely new class takes the
        // supplied key.
        let representative = if let Some((stored, _)) = current_entry {
            stored.clone()
        } else if let Some((stored, _)) = change_entry {
            stored.clone()
        } else {
            probe.clone()
        };
        // The first effective write captures `before`; later writes preserve it.
        let before = if let Some((_, record)) = change_entry {
            record.before.clone()
        } else {
            current_entry.map(|(_, stored)| Arc::clone(stored))
        };
        let after = Some(Arc::clone(&value));

        let next_current = self.current.entries.set_item(representative.clone(), value);
        let next_changes = if self.endpoints_equivalent(&before, &after) {
            self.changes.remove(&representative)
        } else {
            self.changes
                .set_item(representative, DeltaRecord { before, after })
        };

        self.successor(next_current, next_changes)
    }

    fn is_clean(&self) -> bool {
        self.changes.is_empty() && self.current.shares_storage_with(&self.checkpoint)
    }

    fn endpoints_equivalent(&self, left: &Option<Arc<V>>, right: &Option<Arc<V>>) -> bool {
        match (left, right) {
            (None, None) => true,
            (Some(left), Some(right)) => self.value_policy.equals(left, right),
            _ => false,
        }
    }

    fn endpoint_matches(&self, endpoint: &Option<Arc<V>>, stored: Option<&Arc<V>>) -> bool {
        match (endpoint, stored) {
            (None, None) => true,
            (Some(endpoint), Some(stored)) => self.value_policy.equals(endpoint, stored),
            _ => false,
        }
    }

    fn check_ascending<'a, I>(
        &self,
        keys: I,
        message: &'static str,
    ) -> Result<(), DeltaMapInvariantError>
    where
        I: IntoIterator<Item = &'a DeltaMapKey<K>>,
        K: 'a,
    {
        let mut previous: Option<&DeltaMapKey<K>> = None;
        for key in keys {
            if let Some(previous) = previous
                && !self.key_policy.compare(&previous.key, &key.key).is_lt()
            {
                return Err(DeltaMapInvariantError { message });
            }

            previous = Some(key);
        }

        Ok(())
    }

    fn successor(
        &self,
        current: SortedMap<DeltaMapKey<K>, Arc<V>>,
        changes: SortedMap<DeltaMapKey<K>, DeltaRecord<V>>,
    ) -> Self {
        // A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
        // canonical and later checkpoint, rollback, and change queries stay O(1).
        let current = if changes.is_empty() {
            self.checkpoint.entries.clone()
        } else {
            current
        };
        Self {
            current: DeltaMapSnapshot::from_entries(current, self.key_policy.clone()),
            checkpoint: self.checkpoint.clone(),
            changes,
            key_policy: self.key_policy.clone(),
            value_policy: self.value_policy.clone(),
        }
    }
}

impl<K, V> PersistentDeltaMap<K, V>
where
    K: Clone,
{
    /// Reports whether a current entry exists for `key`'s class. O(log N).
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.current.contains_key(key)
    }

    /// Borrows the current value for `key`'s class, or `None` when absent. O(log N).
    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.current.get(key)
    }

    /// Borrows the retained current representative and value for `key`'s class. O(log N).
    #[must_use]
    pub fn get_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.current.get_entry(key)
    }

    /// Returns the zero-based rank of `key`'s class in the current map, or `None`. O(log N).
    #[must_use]
    pub fn index_of_key(&self, key: &K) -> Option<usize> {
        self.current.index_of_key(key)
    }

    /// Borrows the greatest current entry whose key is at most `key`. O(log N).
    #[must_use]
    pub fn floor_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.current.floor_entry(key)
    }

    /// Borrows the least current entry whose key is at least `key`. O(log N).
    #[must_use]
    pub fn ceiling_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.current.ceiling_entry(key)
    }

    /// Borrows the greatest current entry whose key is strictly below `key`. O(log N).
    #[must_use]
    pub fn lower_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.current.lower_entry(key)
    }

    /// Borrows the least current entry whose key is strictly above `key`. O(log N).
    #[must_use]
    pub fn higher_entry(&self, key: &K) -> Option<(&K, &V)> {
        self.current.higher_entry(key)
    }

    /// Returns a plain current-state snapshot for the inclusive key range `[low, high]`. O(log N).
    ///
    /// This is a read: the result is an ordinary ordered snapshot and does not open a delta-map
    /// epoch of its own.
    #[must_use]
    pub fn get_key_range(&self, low: &K, high: &K) -> DeltaMapSnapshot<K, V> {
        self.current.get_key_range(low, high)
    }

    /// Looks up one exact checkpoint-relative net change. O(log(k + 1)).
    #[must_use]
    pub fn get_change(&self, key: &K) -> Option<PersistentMapChange<K, V>> {
        let probe = DeltaMapKey::new(Arc::new(key.clone()), &self.key_policy);
        lookup_entry(&self.changes, &probe).map(|(stored, record)| PersistentMapChange {
            key: Arc::clone(&stored.key),
            before: record.before.clone(),
            after: record.after.clone(),
        })
    }

    /// Removes one current entry and coalesces its checkpoint-relative change. O(log N).
    ///
    /// Removing an absent key is a no-op that returns a version sharing all of the receiver's
    /// storage. Removing a class that was added since the checkpoint cancels its record instead of
    /// recording a removal.
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let probe = DeltaMapKey::new(Arc::new(key.clone()), &self.key_policy);
        let Some((current_key, current_value)) = lookup_entry(&self.current.entries, &probe) else {
            return self.clone();
        };

        let change_entry = lookup_entry(&self.changes, &probe);
        let representative = if let Some((stored, _)) = change_entry {
            stored.clone()
        } else {
            current_key.clone()
        };
        let before = if let Some((_, record)) = change_entry {
            record.before.clone()
        } else {
            Some(Arc::clone(current_value))
        };
        let after = None;

        let next_current = self.current.entries.remove(current_key);
        let next_changes = if self.endpoints_equivalent(&before, &after) {
            self.changes.remove(&representative)
        } else {
            self.changes
                .set_item(representative, DeltaRecord { before, after })
        };

        self.successor(next_current, next_changes)
    }
}

fn unwrap_entry<'a, K, V>(entry: (&'a DeltaMapKey<K>, &'a Arc<V>)) -> (&'a K, &'a V) {
    (&entry.0.key, entry.1)
}

fn lookup_entry<'a, K, T>(
    map: &'a SortedMap<DeltaMapKey<K>, T>,
    probe: &DeltaMapKey<K>,
) -> Option<(&'a DeltaMapKey<K>, &'a T)>
where
    T: Clone,
{
    match map.ceiling_entry(probe) {
        Some((stored, value)) if stored == probe => Some((stored, value)),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;
    use std::panic::{self, AssertUnwindSafe};
    use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};
    use std::thread;

    type IntMap = PersistentDeltaMap<i32, String>;

    fn entries(pairs: &[(i32, &str)]) -> Vec<(i32, String)> {
        pairs
            .iter()
            .map(|(key, value)| (*key, (*value).to_owned()))
            .collect()
    }

    fn contents(map: &IntMap) -> Vec<(i32, String)> {
        map.iter()
            .map(|(key, value)| (*key, (*value).clone()))
            .collect()
    }

    fn change_keys<K: Clone, V>(map: &PersistentDeltaMap<K, V>) -> Vec<K> {
        map.get_changes()
            .map(|change| change.key().clone())
            .collect()
    }

    fn descending() -> OrderPolicy<i32> {
        OrderPolicy::custom(|left: &i32, right: &i32| right.cmp(left))
    }

    fn case_insensitive_keys() -> OrderPolicy<String> {
        OrderPolicy::custom(|left: &String, right: &String| {
            left.to_lowercase().cmp(&right.to_lowercase())
        })
    }

    fn case_insensitive_values() -> EqualityPolicy<String> {
        EqualityPolicy::custom(|left: &String, right: &String| left.eq_ignore_ascii_case(right))
    }

    /// A policy counter so tests can assert that an operation performed no callbacks at all.
    #[derive(Clone)]
    struct Counter(Arc<AtomicUsize>);

    impl Counter {
        fn new() -> Self {
            Self(Arc::new(AtomicUsize::new(0)))
        }

        fn hit(&self) {
            self.0.fetch_add(1, AtomicOrdering::Relaxed);
        }

        fn reset(&self) {
            self.0.store(0, AtomicOrdering::Relaxed);
        }

        fn count(&self) -> usize {
            self.0.load(AtomicOrdering::Relaxed)
        }
    }

    fn counting_key_policy() -> (OrderPolicy<i32>, Counter) {
        let counter = Counter::new();
        let observed = counter.clone();
        (
            OrderPolicy::custom(move |left: &i32, right: &i32| {
                observed.hit();
                left.cmp(right)
            }),
            counter,
        )
    }

    fn counting_value_policy() -> (EqualityPolicy<String>, Counter) {
        let counter = Counter::new();
        let observed = counter.clone();
        (
            EqualityPolicy::custom(move |left: &String, right: &String| {
                observed.hit();
                left == right
            }),
            counter,
        )
    }

    /// An ordinal failpoint: the `n`-th policy callback after arming panics.
    #[derive(Clone)]
    struct Failpoint(Arc<AtomicUsize>);

    impl Failpoint {
        fn new() -> Self {
            Self(Arc::new(AtomicUsize::new(0)))
        }

        fn arm(&self, ordinal: usize) {
            self.0.store(ordinal, AtomicOrdering::Relaxed);
        }

        fn disarm(&self) {
            self.0.store(0, AtomicOrdering::Relaxed);
        }

        fn hit(&self) {
            let remaining = self.0.load(AtomicOrdering::Relaxed);
            if remaining == 0 {
                return;
            }

            if remaining == 1 {
                self.0.store(0, AtomicOrdering::Relaxed);
                panic!("policy failpoint");
            }

            self.0.store(remaining - 1, AtomicOrdering::Relaxed);
        }
    }

    type PanicHook = Box<dyn Fn(&panic::PanicHookInfo<'_>) + Sync + Send + 'static>;

    struct SilentPanics(Option<PanicHook>);

    impl SilentPanics {
        fn install() -> Self {
            let previous = panic::take_hook();
            panic::set_hook(Box::new(|_| {}));
            Self(Some(previous))
        }
    }

    impl Drop for SilentPanics {
        fn drop(&mut self) {
            if let Some(previous) = self.0.take() {
                panic::set_hook(previous);
            }
        }
    }

    /// A retained version paired with the model state it must keep reproducing.
    type RetainedVersion = (
        PersistentDeltaMap<i32, i32>,
        BTreeMap<i32, i32>,
        BTreeMap<i32, i32>,
    );

    /// A deterministic generator so randomized histories replay exactly from a seed.
    struct Lcg(u64);

    impl Lcg {
        fn next_u32(&mut self) -> u32 {
            self.0 = self
                .0
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1_442_695_040_888_963_407);
            (self.0 >> 33) as u32
        }

        fn below(&mut self, bound: u32) -> u32 {
            self.next_u32() % bound
        }

        fn between(&mut self, low: i32, high: i32) -> i32 {
            let span = u32::try_from(high - low).expect("a positive span");
            low + i32::try_from(self.below(span)).expect("the span fits in i32")
        }
    }

    #[test]
    fn point_edits_classify_added_updated_and_removed_keys() {
        let source: IntMap = PersistentDeltaMap::from_entries(entries(&[(1, "one"), (2, "two")]));
        let changed = source
            .set_item(0, "zero".to_owned())
            .set_item(1, "ONE".to_owned())
            .remove(&2);

        assert_eq!(changed.change_count(), 3);
        assert!(changed.has_changes());
        assert_eq!(contents(&changed), entries(&[(0, "zero"), (1, "ONE")]));

        let changes: Vec<_> = changed.get_changes().collect();
        assert_eq!(change_keys(&changed), vec![0, 1, 2]);

        assert_eq!(changes[0].kind(), PersistentMapChangeKind::Added);
        assert_eq!(changes[0].before(), None);
        assert_eq!(changes[0].after().map(String::as_str), Some("zero"));

        assert_eq!(changes[1].kind(), PersistentMapChangeKind::Updated);
        assert_eq!(changes[1].before().map(String::as_str), Some("one"));
        assert_eq!(changes[1].after().map(String::as_str), Some("ONE"));

        assert_eq!(changes[2].kind(), PersistentMapChangeKind::Removed);
        assert_eq!(changes[2].before().map(String::as_str), Some("two"));
        assert_eq!(changes[2].after(), None);

        for change in &changes {
            assert_eq!(changed.get_change(change.key()).as_ref(), Some(change));
        }
        assert!(changed.get_change(&99).is_none());

        assert!(!source.has_changes());
        assert_eq!(source.get_changes().count(), 0);
        assert_eq!(contents(&source), entries(&[(1, "one"), (2, "two")]));
        assert!(changed.validate_structure().is_ok());
        assert!(source.validate_structure().is_ok());
    }

    #[test]
    fn endpoints_distinguish_a_present_none_from_absence() {
        // `Option<V>` carries endpoint presence, so a *stored* `None` value stays distinguishable
        // from an absent endpoint. This is the case the C# reference needs a wrapper struct for.
        let source: PersistentDeltaMap<String, Option<i32>> =
            PersistentDeltaMap::from_entries([("present".to_owned(), None)]);
        let changed = source
            .remove(&"present".to_owned())
            .set_item("added".to_owned(), None);

        let changes: Vec<_> = changed.get_changes().collect();
        assert_eq!(
            changes
                .iter()
                .map(|change| change.key().as_str())
                .collect::<Vec<_>>(),
            vec!["added", "present"]
        );

        assert_eq!(changes[0].kind(), PersistentMapChangeKind::Added);
        assert_eq!(changes[0].before(), None);
        assert_eq!(changes[0].after(), Some(&None));

        assert_eq!(changes[1].kind(), PersistentMapChangeKind::Removed);
        assert_eq!(changes[1].before(), Some(&None));
        assert_eq!(changes[1].after(), None);
        assert!(changed.validate_structure().is_ok());
    }

    #[test]
    fn repeated_writes_and_round_trips_coalesce_and_cancel_changes() {
        let source: IntMap = PersistentDeltaMap::from_entries(entries(&[(1, "one")]));

        assert!(source.shares_storage_with(&source.set_item(1, "one".to_owned())));
        assert!(source.shares_storage_with(&source.remove(&99)));

        let first = source.set_item(1, "two".to_owned());
        let second = first.set_item(1, "three".to_owned());
        let changes: Vec<_> = second.get_changes().collect();
        assert_eq!(changes.len(), 1);
        assert_eq!(changes[0].kind(), PersistentMapChangeKind::Updated);
        assert_eq!(changes[0].before().map(String::as_str), Some("one"));
        assert_eq!(changes[0].after().map(String::as_str), Some("three"));
        assert_eq!(second.change_count(), 1);

        let restored = second.set_item(1, "one".to_owned());
        assert!(!restored.has_changes());
        assert_eq!(restored.get_changes().count(), 0);
        assert!(
            restored
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );
        assert!(
            restored
                .current_snapshot()
                .shares_storage_with(restored.checkpoint_snapshot())
        );

        let added_then_removed = source
            .set_item(2, "first".to_owned())
            .set_item(2, "second".to_owned())
            .remove(&2);
        assert!(!added_then_removed.has_changes());
        assert!(
            added_then_removed
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );

        let removed_then_restored = source.remove(&1).set_item(1, "one".to_owned());
        assert!(!removed_then_restored.has_changes());
        assert!(
            removed_then_restored
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );

        // Retained intermediate versions keep their own coalesced records.
        assert!(first.has_changes());
        assert_eq!(
            first
                .get_changes()
                .next()
                .and_then(|change| change.after().cloned()),
            Some("two".to_owned())
        );
        assert!(second.has_changes());
        assert_eq!(
            second
                .get_changes()
                .next()
                .and_then(|change| change.after().cloned()),
            Some("three".to_owned())
        );
        for version in [&source, &first, &second, &restored, &removed_then_restored] {
            assert!(version.validate_structure().is_ok());
        }
    }

    #[test]
    fn get_changes_uses_the_retained_key_order() {
        let source: IntMap = PersistentDeltaMap::from_entries_with_policies(
            entries(&[(1, "one"), (3, "three"), (5, "five")]),
            descending(),
            EqualityPolicy::natural(),
        );

        let changed = source
            .set_item(6, "six".to_owned())
            .set_item(4, "four".to_owned())
            .set_item(3, "THREE".to_owned())
            .remove(&1);

        assert!(!changed.key_policy().is_natural());
        assert_eq!(change_keys(&changed), vec![6, 4, 3, 1]);
        assert_eq!(
            changed
                .get_changes()
                .map(|change| change.kind())
                .collect::<Vec<_>>(),
            vec![
                PersistentMapChangeKind::Added,
                PersistentMapChangeKind::Added,
                PersistentMapChangeKind::Updated,
                PersistentMapChangeKind::Removed,
            ]
        );
        assert_eq!(
            changed
                .keys_to_vec()
                .iter()
                .map(|key| **key)
                .collect::<Vec<_>>(),
            vec![6, 5, 4, 3]
        );
        assert!(changed.validate_structure().is_ok());
    }

    #[test]
    fn checkpoint_and_rollback_share_exact_roots_without_policy_calls() {
        let (key_policy, key_counter) = counting_key_policy();
        let (value_policy, value_counter) = counting_value_policy();
        let source: IntMap = PersistentDeltaMap::from_entries_with_policies(
            entries(&[(1, "one"), (2, "two")]),
            key_policy,
            value_policy,
        );
        let dirty = source
            .set_item(1, "ONE".to_owned())
            .set_item(3, "three".to_owned());

        key_counter.reset();
        value_counter.reset();
        let checkpoint = dirty.checkpoint();
        assert_eq!(key_counter.count(), 0);
        assert_eq!(value_counter.count(), 0);
        assert!(
            checkpoint
                .current_snapshot()
                .shares_storage_with(dirty.current_snapshot())
        );
        assert!(
            checkpoint
                .current_snapshot()
                .shares_storage_with(checkpoint.checkpoint_snapshot())
        );
        assert!(!checkpoint.has_changes());
        assert!(checkpoint.shares_storage_with(&checkpoint.checkpoint()));
        assert!(checkpoint.shares_storage_with(&checkpoint.rollback()));

        key_counter.reset();
        value_counter.reset();
        let rollback = dirty.rollback();
        assert_eq!(key_counter.count(), 0);
        assert_eq!(value_counter.count(), 0);
        assert!(
            rollback
                .current_snapshot()
                .shares_storage_with(dirty.checkpoint_snapshot())
        );
        assert!(
            rollback
                .current_snapshot()
                .shares_storage_with(rollback.checkpoint_snapshot())
        );
        assert!(!rollback.has_changes());
        assert!(rollback.shares_storage_with(&rollback.checkpoint()));
        assert!(rollback.shares_storage_with(&rollback.rollback()));

        assert!(dirty.has_changes());
        assert_eq!(dirty.change_count(), 2);
        assert!(
            dirty
                .checkpoint_snapshot()
                .shares_storage_with(source.current_snapshot())
        );
        assert_eq!(contents(&rollback), entries(&[(1, "one"), (2, "two")]));
        assert_eq!(
            contents(&checkpoint),
            entries(&[(1, "ONE"), (2, "two"), (3, "three")])
        );
    }

    #[test]
    fn retained_branches_evolve_independently_across_checkpoints() {
        let root: IntMap = PersistentDeltaMap::from_entries(entries(&[(1, "one"), (2, "two")]));
        let left = root.set_item(1, "left".to_owned());
        let right = root.set_item(3, "right".to_owned());

        assert!(
            left.checkpoint_snapshot()
                .shares_storage_with(root.current_snapshot())
        );
        assert!(
            right
                .checkpoint_snapshot()
                .shares_storage_with(root.current_snapshot())
        );
        assert_eq!(change_keys(&left), vec![1]);
        assert_eq!(change_keys(&right), vec![3]);
        assert!(!root.has_changes());

        let left_checkpoint = left.checkpoint();
        let branch_a = left_checkpoint.remove(&2);
        let branch_b = left_checkpoint.set_item(1, "branch-b".to_owned());

        assert!(
            left_checkpoint
                .current_snapshot()
                .shares_storage_with(left.current_snapshot())
        );
        assert!(
            branch_a
                .checkpoint_snapshot()
                .shares_storage_with(left_checkpoint.current_snapshot())
        );
        assert!(
            branch_b
                .checkpoint_snapshot()
                .shares_storage_with(left_checkpoint.current_snapshot())
        );
        assert_eq!(change_keys(&branch_a), vec![2]);
        assert_eq!(change_keys(&branch_b), vec![1]);

        assert_eq!(
            contents(&left_checkpoint),
            entries(&[(1, "left"), (2, "two")])
        );
        assert_eq!(contents(&branch_a), entries(&[(1, "left")]));
        assert_eq!(contents(&branch_b), entries(&[(1, "branch-b"), (2, "two")]));
        assert_eq!(contents(&root), entries(&[(1, "one"), (2, "two")]));
        assert_eq!(
            contents(&right),
            entries(&[(1, "one"), (2, "two"), (3, "right")])
        );
        for version in [&root, &left, &right, &left_checkpoint, &branch_a, &branch_b] {
            assert!(version.validate_structure().is_ok());
        }
    }

    #[test]
    fn equivalent_keys_retain_checkpoint_and_active_addition_representatives() {
        let policy = case_insensitive_keys();
        let source: PersistentDeltaMap<String, i32> = PersistentDeltaMap::from_entries_with_policies(
            [("Alpha".to_owned(), 1)],
            policy.clone(),
            EqualityPolicy::natural(),
        );
        let probe = "ALPHA".to_owned();

        let updated = source.set_item(probe.clone(), 2);
        assert_eq!(updated.len(), 1);
        assert_eq!(
            updated.entry_at(0).map(|(key, _)| key.as_str()),
            Some("Alpha")
        );
        assert_eq!(updated.get(&probe), Some(&2));
        assert_eq!(change_keys(&updated), vec!["Alpha".to_owned()]);

        // A delete/re-add round trip keeps the checkpoint representative and cancels the record.
        let round_trip = updated.remove(&probe).set_item(probe.clone(), 1);
        assert!(!round_trip.has_changes());
        assert!(
            round_trip
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );
        assert_eq!(
            round_trip.entry_at(0).map(|(key, _)| key.as_str()),
            Some("Alpha")
        );

        // The same round trip beside an unrelated change cannot snap back to the checkpoint root,
        // so the representative rule has to hold on its own.
        let with_other_change = source.set_item("Omega".to_owned(), 9).remove(&probe);
        let restored = with_other_change.set_item(probe.clone(), 1);
        assert_eq!(change_keys(&restored), vec!["Omega".to_owned()]);
        assert_eq!(
            restored.get_entry(&probe).map(|(key, _)| key.as_str()),
            Some("Alpha")
        );
        assert!(restored.validate_structure().is_ok());

        // A net-added class keeps its first representative while its record stays active.
        let added = source
            .set_item("Beta".to_owned(), 3)
            .set_item("BETA".to_owned(), 4);
        assert_eq!(
            added
                .get_entry(&"beta".to_owned())
                .map(|(key, _)| key.as_str()),
            Some("Beta")
        );
        assert_eq!(change_keys(&added), vec!["Beta".to_owned()]);
        assert_eq!(added.get(&"BETA".to_owned()), Some(&4));

        // After complete cancellation a later addition starts a new representative episode.
        let cancelled: PersistentDeltaMap<String, i32> =
            PersistentDeltaMap::with_policies(policy, EqualityPolicy::natural())
                .set_item("Beta".to_owned(), 1)
                .remove(&"BETA".to_owned());
        let new_episode = cancelled.set_item("BETA".to_owned(), 2);
        assert!(!cancelled.has_changes());
        assert_eq!(
            new_episode.entry_at(0).map(|(key, _)| key.as_str()),
            Some("BETA")
        );
        assert_eq!(change_keys(&new_episode), vec!["BETA".to_owned()]);
        assert!(new_episode.validate_structure().is_ok());
    }

    #[test]
    fn reflexive_float_value_policy_keeps_a_nan_round_trip_a_no_op() {
        // Raw `f64` cannot reach the natural value policy at all, because `NaN != NaN` would record
        // a change that never cancels.
        let source: PersistentDeltaMap<i32, f64> = PersistentDeltaMap::from_entries_with_policies(
            [(1, f64::NAN), (2, 0.0)],
            OrderPolicy::natural(),
            EqualityPolicy::<f64>::reflexive_ieee(),
        );
        assert!(source.value_policy().is_natural());

        let rewritten = source.set_item(1, f64::NAN);
        assert!(!rewritten.has_changes());
        assert_eq!(rewritten.change_count(), 0);
        assert!(rewritten.shares_storage_with(&source));
        assert!(rewritten.get(&1).expect("current value").is_nan());
        assert!(rewritten.validate_structure().is_ok());

        // Signed zeros are one class too, matching the C# baseline's default comparer.
        assert!(source.shares_storage_with(&source.set_item(2, -0.0)));

        // A genuine change is still recorded, and returning NaN cancels it.
        let dirty = source.set_item(1, 3.5);
        assert_eq!(change_keys(&dirty), vec![1]);
        let cancelled = dirty.set_item(1, f64::NAN);
        assert!(!cancelled.has_changes());
        assert!(
            cancelled
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );
        assert!(cancelled.validate_structure().is_ok());
    }

    #[test]
    fn from_entries_keeps_the_last_equivalent_entry() {
        let map: PersistentDeltaMap<String, i32> = PersistentDeltaMap::from_entries_with_policies(
            [
                ("Alpha".to_owned(), 1),
                ("Beta".to_owned(), 2),
                ("ALPHA".to_owned(), 3),
            ],
            case_insensitive_keys(),
            EqualityPolicy::natural(),
        );

        assert_eq!(map.len(), 2);
        assert_eq!(map.get(&"alpha".to_owned()), Some(&3));
        assert_eq!(
            map.get_entry(&"alpha".to_owned())
                .map(|(key, _)| key.as_str()),
            Some("ALPHA")
        );
        assert!(!map.has_changes());
        assert!(map.validate_structure().is_ok());
    }

    #[test]
    fn custom_value_policy_defines_no_ops_and_cancellation() {
        let source: IntMap = PersistentDeltaMap::from_entries_with_policies(
            entries(&[(1, "Alpha")]),
            OrderPolicy::natural(),
            case_insensitive_values(),
        );

        assert!(!source.value_policy().is_natural());
        assert!(source.shares_storage_with(&source.set_item(1, "ALPHA".to_owned())));

        let dirty = source.set_item(1, "Beta".to_owned());
        assert!(dirty.shares_storage_with(&dirty.set_item(1, "BETA".to_owned())));
        let change = dirty.get_changes().next().expect("one recorded change");
        assert_eq!(change.before().map(String::as_str), Some("Alpha"));
        assert_eq!(change.after().map(String::as_str), Some("Beta"));

        let restored = dirty.set_item(1, "alpha".to_owned());
        assert!(!restored.has_changes());
        assert!(
            restored
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );
        // Cancellation is extensional: the retained value is whatever the checkpoint root holds.
        assert_eq!(restored.get(&1).map(String::as_str), Some("Alpha"));

        let added = source.set_item(2, "Value".to_owned());
        assert!(added.shares_storage_with(&added.set_item(2, "VALUE".to_owned())));
        let cancelled_addition = added.remove(&2);
        assert!(!cancelled_addition.has_changes());
        assert!(
            cancelled_addition
                .current_snapshot()
                .shares_storage_with(source.current_snapshot())
        );

        // Cancelling one class while another stays dirty keeps the equivalent current value.
        let two_keys: IntMap = PersistentDeltaMap::from_entries_with_policies(
            entries(&[(1, "Alpha"), (2, "Other")]),
            OrderPolicy::natural(),
            case_insensitive_values(),
        );
        let two_dirty = two_keys
            .set_item(1, "Beta".to_owned())
            .set_item(2, "Changed".to_owned());
        let one_cancelled = two_dirty.set_item(1, "alpha".to_owned());
        assert_eq!(one_cancelled.get(&1).map(String::as_str), Some("alpha"));
        assert_eq!(change_keys(&one_cancelled), vec![2]);
        assert!(one_cancelled.validate_structure().is_ok());

        let dirty_again = one_cancelled.set_item(1, "Gamma".to_owned());
        let redirtied = dirty_again.get_change(&1).expect("the class is dirty again");
        assert_eq!(redirtied.before().map(String::as_str), Some("alpha"));
        assert_eq!(redirtied.after().map(String::as_str), Some("Gamma"));
        assert!(dirty_again.validate_structure().is_ok());
    }

    #[test]
    fn randomized_histories_match_the_checkpoint_and_current_models() {
        for seed in [0_u64, 1, 0x5eed, 0x7fff_ffff] {
            let initial: Vec<(i32, i32)> = (-8..=8)
                .filter(|key| key % 2 == 0)
                .map(|key| (key, key * 10))
                .collect();
            let mut actual: PersistentDeltaMap<i32, i32> =
                PersistentDeltaMap::from_entries(initial.clone());
            let mut checkpoint: BTreeMap<i32, i32> = initial.iter().copied().collect();
            let mut current = checkpoint.clone();
            let mut random = Lcg(seed);
            let mut retained: Vec<RetainedVersion> = Vec::new();

            for step in 0..750 {
                let operation = random.below(100);
                let key = random.between(-25, 26);
                if operation < 50 {
                    let value = random.between(-7, 8);
                    let was_no_op = current.get(&key) == Some(&value);
                    let previous = actual.clone();
                    actual = actual.set_item(key, value);
                    current.insert(key, value);
                    if was_no_op {
                        assert!(previous.shares_storage_with(&actual));
                    }
                } else if operation < 75 {
                    let was_absent = current.remove(&key).is_none();
                    let previous = actual.clone();
                    actual = actual.remove(&key);
                    if was_absent {
                        assert!(previous.shares_storage_with(&actual));
                    }
                } else if operation < 86 {
                    let was_clean = checkpoint == current;
                    let previous = actual.clone();
                    actual = actual.checkpoint();
                    checkpoint = current.clone();
                    if was_clean {
                        assert!(previous.shares_storage_with(&actual));
                    }
                } else if operation < 97 {
                    let was_clean = checkpoint == current;
                    let previous = actual.clone();
                    actual = actual.rollback();
                    current = checkpoint.clone();
                    if was_clean {
                        assert!(previous.shares_storage_with(&actual));
                    }
                } else {
                    assert_eq!(actual.get(&key), current.get(&key));
                }

                assert_model(&actual, &checkpoint, &current);

                // Occasionally retain an old version, then keep editing both it and the head.
                if step % 97 == 0 {
                    retained.push((actual.clone(), checkpoint.clone(), current.clone()));
                }
                if step % 131 == 0
                    && let Some((old, old_checkpoint, old_current)) = retained.first()
                {
                    assert_model(old, old_checkpoint, old_current);
                    let forked = old.set_item(1_000, 1_000);
                    let mut forked_current = old_current.clone();
                    forked_current.insert(1_000, 1_000);
                    assert_model(&forked, old_checkpoint, &forked_current);
                    assert_model(old, old_checkpoint, old_current);
                }
            }
        }
    }

    fn assert_model(
        actual: &PersistentDeltaMap<i32, i32>,
        checkpoint: &BTreeMap<i32, i32>,
        current: &BTreeMap<i32, i32>,
    ) {
        let statistics = actual.validate_structure().expect("representation is valid");
        assert_eq!(
            actual
                .iter()
                .map(|(key, value)| (*key, *value))
                .collect::<Vec<_>>(),
            current
                .iter()
                .map(|(key, value)| (*key, *value))
                .collect::<Vec<_>>()
        );
        assert_eq!(
            actual
                .checkpoint_snapshot()
                .to_vec()
                .into_iter()
                .map(|(key, value)| (*key, *value))
                .collect::<Vec<_>>(),
            checkpoint
                .iter()
                .map(|(key, value)| (*key, *value))
                .collect::<Vec<_>>()
        );
        assert_eq!(actual.len(), current.len());
        assert_eq!(actual.is_empty(), current.is_empty());
        assert_eq!(statistics.len, current.len());
        assert_eq!(statistics.checkpoint_len, checkpoint.len());

        let mut expected: Vec<(i32, Option<i32>, Option<i32>)> = Vec::new();
        let mut keys: Vec<i32> = checkpoint.keys().chain(current.keys()).copied().collect();
        keys.sort_unstable();
        keys.dedup();
        for key in keys {
            let before = checkpoint.get(&key).copied();
            let after = current.get(&key).copied();
            if before != after {
                expected.push((key, before, after));
            }
        }

        let observed: Vec<_> = actual.get_changes().collect();
        assert_eq!(actual.change_count(), expected.len());
        assert_eq!(actual.has_changes(), !expected.is_empty());
        assert_eq!(observed.len(), expected.len());
        assert_eq!(statistics.change_count, expected.len());

        for (index, (key, before, after)) in expected.iter().enumerate() {
            let change = &observed[index];
            assert_eq!(change.key(), key);
            assert_eq!(change.before().copied(), *before);
            assert_eq!(change.after().copied(), *after);
            let expected_kind = match (before, after) {
                (Some(_), Some(_)) => PersistentMapChangeKind::Updated,
                (Some(_), None) => PersistentMapChangeKind::Removed,
                (None, Some(_)) => PersistentMapChangeKind::Added,
                (None, None) => unreachable!("equal endpoints are filtered above"),
            };
            assert_eq!(change.kind(), expected_kind);
            assert_eq!(actual.get_change(key).as_ref(), Some(change));
        }
        assert!(actual.get_change(&10_000).is_none());

        // Ordered queries delegate to the current state.
        for probe in [-30, -1, 0, 3, 30] {
            assert_eq!(
                actual.floor_entry(&probe).map(|(key, _)| *key),
                current.range(..=probe).next_back().map(|(key, _)| *key)
            );
            assert_eq!(
                actual.ceiling_entry(&probe).map(|(key, _)| *key),
                current.range(probe..).next().map(|(key, _)| *key)
            );
            assert_eq!(
                actual.lower_entry(&probe).map(|(key, _)| *key),
                current.range(..probe).next_back().map(|(key, _)| *key)
            );
            assert_eq!(
                actual.higher_entry(&probe).map(|(key, _)| *key),
                current.range((probe + 1)..).next().map(|(key, _)| *key)
            );
            assert_eq!(
                actual.index_of_key(&probe),
                current.keys().position(|key| *key == probe)
            );
            assert_eq!(actual.contains_key(&probe), current.contains_key(&probe));
        }
        if let Some((key, value)) = current.iter().next() {
            assert_eq!(actual.min_entry(), Some((key, value)));
            assert_eq!(actual.entry_at(0), Some((key, value)));
        } else {
            assert!(actual.min_entry().is_none());
            assert!(actual.max_entry().is_none());
            assert!(actual.entry_at(0).is_none());
        }
        assert_eq!(actual.max_entry(), current.iter().next_back());
        assert!(actual.entry_at(current.len()).is_none());
    }

    #[test]
    fn change_enumeration_is_independent_of_the_baseline_size() {
        let small = build_three_change_map(128);
        let large = build_three_change_map(16_384);

        small.1.reset();
        small.2.reset();
        let small_changes: Vec<_> = small.0.get_changes().collect();

        large.1.reset();
        large.2.reset();
        let large_changes: Vec<_> = large.0.get_changes().collect();

        assert_eq!(small_changes.len(), 3);
        assert_eq!(large_changes.len(), 3);
        assert_eq!(
            small_changes
                .iter()
                .map(|change| *change.key())
                .collect::<Vec<_>>(),
            vec![-1, 64, 127]
        );
        assert_eq!(
            large_changes
                .iter()
                .map(|change| *change.key())
                .collect::<Vec<_>>(),
            vec![-1, 8_192, 16_383]
        );
        // The whole point of the maintained index: the walk is output-sized and policy-free.
        assert_eq!(small.1.count(), 0);
        assert_eq!(large.1.count(), 0);
        assert_eq!(small.2.count(), 0);
        assert_eq!(large.2.count(), 0);
    }

    fn build_three_change_map(count: i32) -> (IntMap, Counter, Counter) {
        let (key_policy, key_counter) = counting_key_policy();
        let (value_policy, value_counter) = counting_value_policy();
        let map: IntMap = PersistentDeltaMap::from_entries_with_policies(
            (0..count).map(|key| (key, key.to_string())),
            key_policy,
            value_policy,
        )
        .set_item(-1, "low".to_owned())
        .set_item(count / 2, "middle".to_owned())
        .remove(&(count - 1));

        assert_eq!(map.change_count(), 3);
        (map, key_counter, value_counter)
    }

    #[test]
    fn policy_failures_do_not_publish_a_partial_successor() {
        let _silent = SilentPanics::install();

        let value_failpoint = Failpoint::new();
        let observed = value_failpoint.clone();
        let value_policy = EqualityPolicy::custom(move |left: &String, right: &String| {
            observed.hit();
            left == right
        });
        let by_value: IntMap = PersistentDeltaMap::from_entries_with_policies(
            entries(&[(1, "one")]),
            OrderPolicy::natural(),
            value_policy,
        )
        .set_item(1, "two".to_owned());

        let value_failures = exercise_every_failure(
            &value_failpoint,
            || {
                let _ = by_value.set_item(1, "one".to_owned());
            },
            || {
                value_failpoint.disarm();
                assert_eq!(contents(&by_value), entries(&[(1, "two")]));
                let change = by_value.get_changes().next().expect("one change");
                assert_eq!(change.before().map(String::as_str), Some("one"));
                assert_eq!(change.after().map(String::as_str), Some("two"));
            },
        );
        assert!(value_failures > 0);

        let key_failpoint = Failpoint::new();
        let observed = key_failpoint.clone();
        let key_policy = OrderPolicy::custom(move |left: &i32, right: &i32| {
            observed.hit();
            left.cmp(right)
        });
        let by_key: IntMap = PersistentDeltaMap::from_entries_with_policies(
            entries(&[(1, "one"), (2, "two"), (3, "three")]),
            key_policy,
            EqualityPolicy::natural(),
        )
        .set_item(2, "TWO".to_owned());

        let key_failures = exercise_every_failure(
            &key_failpoint,
            || {
                let _ = by_key.set_item(4, "four".to_owned());
            },
            || {
                key_failpoint.disarm();
                assert_eq!(
                    contents(&by_key),
                    entries(&[(1, "one"), (2, "TWO"), (3, "three")])
                );
                assert_eq!(change_keys(&by_key), vec![2]);
            },
        );
        assert!(key_failures > 0);

        let remove_failures = exercise_every_failure(
            &key_failpoint,
            || {
                let _ = by_key.remove(&1);
            },
            || {
                key_failpoint.disarm();
                assert_eq!(
                    contents(&by_key),
                    entries(&[(1, "one"), (2, "TWO"), (3, "three")])
                );
                assert_eq!(change_keys(&by_key), vec![2]);
                assert!(by_key.validate_structure().is_ok());
            },
        );
        assert!(remove_failures > 0);
    }

    fn exercise_every_failure<F, A>(
        failpoint: &Failpoint,
        operation: F,
        assert_unchanged: A,
    ) -> usize
    where
        F: Fn(),
        A: Fn(),
    {
        for ordinal in 1..1_000 {
            failpoint.arm(ordinal);
            let result = panic::catch_unwind(AssertUnwindSafe(&operation));
            failpoint.disarm();
            match result {
                Ok(()) => return ordinal - 1,
                Err(_) => assert_unchanged(),
            }
        }

        panic!("the operation never completed within the failpoint ceiling");
    }

    #[test]
    fn empty_and_singleton_boundaries_behave() {
        let empty: IntMap = PersistentDeltaMap::new();
        assert!(empty.is_empty());
        assert_eq!(empty.len(), 0);
        assert!(!empty.has_changes());
        assert_eq!(empty.change_count(), 0);
        assert!(empty.min_entry().is_none());
        assert!(empty.max_entry().is_none());
        assert!(empty.entry_at(0).is_none());
        assert!(empty.get(&0).is_none());
        assert!(!empty.contains_key(&0));
        assert!(empty.index_of_key(&0).is_none());
        assert!(empty.floor_entry(&0).is_none());
        assert!(empty.higher_entry(&0).is_none());
        assert!(empty.get_changes().next().is_none());
        assert!(empty.shares_storage_with(&empty.remove(&7)));
        assert!(empty.shares_storage_with(&empty.checkpoint()));
        assert!(empty.shares_storage_with(&empty.rollback()));
        assert!(empty.validate_structure().is_ok());
        assert_eq!(empty.get_key_range(&0, &10).len(), 0);

        let single = empty.set_item(5, "five".to_owned());
        assert_eq!(single.len(), 1);
        assert_eq!(single.change_count(), 1);
        assert_eq!(single.min_entry(), single.max_entry());
        assert_eq!(single.index_of_key(&5), Some(0));
        assert_eq!(single.get_key_range(&0, &10).len(), 1);
        assert_eq!(single.get_key_range(&10, &0).len(), 0);
        assert!(!single.remove(&5).has_changes());
        assert!(single.remove(&5).is_empty());

        let statistics = single.validate_structure().expect("valid");
        assert_eq!(statistics.added_count, 1);
        assert_eq!(statistics.removed_count, 0);
        assert_eq!(statistics.updated_count, 0);
        assert!(!statistics.is_clean);
        assert_eq!(statistics.len, 1);
        assert_eq!(statistics.checkpoint_len, 0);
    }

    #[test]
    fn default_and_from_iterator_build_clean_versions() {
        let default: IntMap = PersistentDeltaMap::default();
        assert!(default.is_empty());
        assert!(!default.has_changes());

        let collected: IntMap = entries(&[(3, "three"), (1, "one"), (3, "THREE")])
            .into_iter()
            .collect();
        assert_eq!(collected.len(), 2);
        assert_eq!(collected.get(&3).map(String::as_str), Some("THREE"));
        assert!(!collected.has_changes());
        assert!(collected.key_policy().is_natural());
        assert!(collected.value_policy().is_natural());
        assert!(
            collected
                .current_snapshot()
                .shares_storage_with(collected.checkpoint_snapshot())
        );
    }

    #[test]
    fn snapshots_expose_the_ordered_read_surface() {
        let map: IntMap =
            PersistentDeltaMap::from_entries(entries(&[(1, "one"), (3, "three"), (5, "five")]));
        let dirty = map.set_item(4, "four".to_owned());
        let snapshot = dirty.current_snapshot();

        assert_eq!(snapshot.len(), 4);
        assert!(!snapshot.is_empty());
        assert_eq!(snapshot.get(&4).map(String::as_str), Some("four"));
        assert!(snapshot.contains_key(&1));
        assert_eq!(snapshot.index_of_key(&4), Some(2));
        assert_eq!(snapshot.entry_at(0).map(|(key, _)| *key), Some(1));
        assert_eq!(snapshot.min_entry().map(|(key, _)| *key), Some(1));
        assert_eq!(snapshot.max_entry().map(|(key, _)| *key), Some(5));
        assert_eq!(snapshot.floor_entry(&2).map(|(key, _)| *key), Some(1));
        assert_eq!(snapshot.ceiling_entry(&2).map(|(key, _)| *key), Some(3));
        assert_eq!(snapshot.lower_entry(&3).map(|(key, _)| *key), Some(1));
        assert_eq!(snapshot.higher_entry(&3).map(|(key, _)| *key), Some(4));
        assert_eq!(
            snapshot
                .get_key_range(&3, &4)
                .keys_to_vec()
                .iter()
                .map(|key| **key)
                .collect::<Vec<_>>(),
            vec![3, 4]
        );
        assert_eq!(snapshot.keys_to_vec().len(), 4);
        assert_eq!(snapshot.values_to_vec().len(), 4);
        assert_eq!(snapshot.iter().count(), 4);
        assert!(snapshot.key_policy().is_natural());
        assert!(snapshot.shares_storage_with(&snapshot.clone()));
        assert!(!snapshot.shares_storage_with(map.current_snapshot()));

        // A retained snapshot keeps its own state after the version moves on.
        let retained = snapshot.clone();
        let later = dirty.remove(&4);
        assert_eq!(retained.len(), 4);
        assert_eq!(later.len(), 3);
        assert!(
            later
                .current_snapshot()
                .shares_storage_with(map.current_snapshot())
        );
    }

    #[test]
    fn versions_are_send_and_sync_and_readable_from_many_threads() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<PersistentDeltaMap<i32, String>>();
        assert_send_sync::<DeltaMapSnapshot<i32, String>>();
        assert_send_sync::<PersistentMapChange<i32, String>>();

        let map: IntMap =
            PersistentDeltaMap::from_entries((0..256).map(|key| (key, key.to_string())))
                .set_item(-1, "low".to_owned())
                .remove(&255);

        thread::scope(|scope| {
            for _ in 0..8 {
                let map = map.clone();
                scope.spawn(move || {
                    for _ in 0..64 {
                        assert_eq!(map.len(), 256);
                        assert_eq!(map.change_count(), 2);
                        assert_eq!(map.get(&128).map(String::as_str), Some("128"));
                        assert_eq!(change_keys(&map), vec![-1, 255]);
                        assert!(map.validate_structure().is_ok());
                    }
                });
            }
        });
    }
}
