#![forbid(unsafe_code)]
#![doc = "Persistent HAMT map/set/bag/bimap, Patricia, and canonical Merkle search-tree collections."]

use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};
use std::iter::FusedIterator;
use std::ops::Index;
use std::sync::Arc;

mod bi_map;
mod hash_bag;
mod merkle_encoding;
mod merkle_persistence;
mod merkle_search_tree;
mod patricia;
pub use bi_map::{BiMapAddResult, BiMapConflict, BiMapRemoveResult, PersistentBiMap};
pub use hash_bag::{BagIter, HashBagEntry, HashBagError, PersistentHashBag};
pub use merkle_encoding::{
    Int32MerkleCodec, Int64MerkleCodec, MerkleCodec, MerkleCodecError, MerkleDigest,
    MerkleDigestParseError, MerkleDigestWriteError, MerkleKeyComparer, MerklePolicyError,
    MerklePolicyField, MerkleSearchTreePolicy, NaturalMerkleKeyComparer, NullableBytesMerkleCodec,
    NullableUtf8MerkleCodec, Rfc4122Guid, Rfc4122GuidMerkleCodec,
};
pub use merkle_persistence::{
    InMemoryMerkleBlockStore, MerkleBlock, MerkleBlockPack, MerkleBlockStore, MerkleBudgetError,
    MerkleMergeResolution, MerkleMergeValue, MerkleProof, MerkleProofCreationError,
    MerkleProofKind, MerkleProofStep, MerkleProofVerificationResult, MerkleSyncPlan,
    MerkleThreeWayMergeConflict, MerkleThreeWayMergeResolver, MerkleThreeWayMergeResult,
    MerkleVerificationBudget, MerkleVerificationError, MerkleVerificationFailureKind,
};
pub use merkle_search_tree::{
    MerkleEntry, MerkleMapDifference, MerkleRangeError, MerkleRangeIter, MerkleSearchTree,
    MerkleSearchTreeStatistics, MerkleShapeEntry, MerkleTreeError, MerkleTreeInvariantError,
    MerkleTreeIter,
};
pub use patricia::{PersistentIntMap, PersistentIntSet, PersistentLongMap, PersistentLongSet};

const BITS_PER_LEVEL: u32 = 5;
const BRANCH_MASK: u32 = 0x1f;
const BRANCH_FACTOR: usize = 32;
/// Deepest shift at which a `Branch` node can appear (32-bit hash, 5 bits per level).
const MAX_BRANCH_SHIFT: u32 = 30;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DuplicateKey;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MapDifference<K, V> {
    Added { key: K, value: V },
    Removed { key: K, value: V },
    Changed { key: K, before: V, after: V },
}

/// Result of one persistent map factory update.
///
/// `map` is the immutable successor and `value` is the actual stored value selected by the
/// operation. On a hit or an equal-value update, cloning an `Arc` value therefore preserves the
/// exact stored allocation rather than returning an equal factory-produced replacement.
#[must_use]
pub struct MapUpdateResult<K, V, S = RandomState> {
    pub map: PersistentHashMap<K, V, S>,
    pub value: V,
}

impl<K, V: Clone, S: Clone> Clone for MapUpdateResult<K, V, S> {
    fn clone(&self) -> Self {
        Self {
            map: self.map.clone(),
            value: self.value.clone(),
        }
    }
}

impl<K: fmt::Debug, V: fmt::Debug, S> fmt::Debug for MapUpdateResult<K, V, S> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MapUpdateResult")
            .field("map", &self.map)
            .field("value", &self.value)
            .finish()
    }
}

impl<K, V, S> MapUpdateResult<K, V, S> {
    /// Splits the result into its successor map and selected value.
    #[must_use]
    pub fn into_parts(self) -> (PersistentHashMap<K, V, S>, V) {
        (self.map, self.value)
    }
}

impl fmt::Display for DuplicateKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("an entry with the same key already exists")
    }
}

impl std::error::Error for DuplicateKey {}

pub struct PersistentHashMap<K, V, S = RandomState> {
    root: Option<Arc<Node<K, V>>>,
    len: usize,
    hasher: S,
    policy_identity: Arc<()>,
}

impl<K, V, S: Clone> Clone for PersistentHashMap<K, V, S> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            len: self.len,
            hasher: self.hasher.clone(),
            policy_identity: Arc::clone(&self.policy_identity),
        }
    }
}

#[derive(Clone)]
enum Node<K, V> {
    Leaf {
        hash: u32,
        key: K,
        value: V,
    },
    Collision {
        hash: u32,
        entries: Arc<[(K, V)]>,
    },
    Branch {
        data_map: u32,
        node_map: u32,
        data: Arc<[(u32, K, V)]>,
        children: Arc<[Arc<Node<K, V>>]>,
        count: usize,
    },
}

impl<K, V> Node<K, V> {
    fn entry_count(&self) -> usize {
        match self {
            Self::Leaf { .. } => 1,
            Self::Collision { entries, .. } => entries.len(),
            Self::Branch { count, .. } => *count,
        }
    }
}

fn make_branch<K, V>(
    data_map: u32,
    node_map: u32,
    data: Arc<[(u32, K, V)]>,
    children: Arc<[Arc<Node<K, V>>]>,
) -> Arc<Node<K, V>> {
    let count = data.len()
        + children
            .iter()
            .map(|child| child.entry_count())
            .sum::<usize>();
    Arc::new(Node::Branch {
        data_map,
        node_map,
        data,
        children,
        count,
    })
}

struct InsertResult<K, V> {
    node: Arc<Node<K, V>>,
    added: bool,
    changed: bool,
    duplicate: bool,
}

struct FactoryUpdateNodeResult<K, V> {
    node: Arc<Node<K, V>>,
    value: V,
    added: bool,
    changed: bool,
}

enum PresentFactorySelection<V> {
    Unchanged(V),
    Changed(V),
}

trait FactorySelector<K, V> {
    fn select_absent(&mut self, caller_key: &K) -> V;
    fn select_present(&mut self, caller_key: &K, stored_value: &V) -> PresentFactorySelection<V>;
}

struct GetOrAddSelector<F> {
    add_factory: Option<F>,
}

impl<K, V, F> FactorySelector<K, V> for GetOrAddSelector<F>
where
    V: Clone,
    F: FnOnce(&K) -> V,
{
    fn select_absent(&mut self, caller_key: &K) -> V {
        self.add_factory
            .take()
            .expect("the add factory is selected at most once")(caller_key)
    }

    fn select_present(&mut self, _caller_key: &K, stored_value: &V) -> PresentFactorySelection<V> {
        PresentFactorySelection::Unchanged(stored_value.clone())
    }
}

struct AddOrUpdateSelector<Add, Update> {
    add_factory: Option<Add>,
    update_factory: Option<Update>,
}

impl<K, V, Add, Update> FactorySelector<K, V> for AddOrUpdateSelector<Add, Update>
where
    V: Clone + PartialEq,
    Add: FnOnce(&K) -> V,
    Update: FnOnce(&K, &V) -> V,
{
    fn select_absent(&mut self, caller_key: &K) -> V {
        self.add_factory
            .take()
            .expect("the add factory is selected at most once")(caller_key)
    }

    fn select_present(&mut self, caller_key: &K, stored_value: &V) -> PresentFactorySelection<V> {
        let candidate = self
            .update_factory
            .take()
            .expect("the update factory is selected at most once")(
            caller_key, stored_value
        );
        if stored_value == &candidate {
            PresentFactorySelection::Unchanged(stored_value.clone())
        } else {
            PresentFactorySelection::Changed(candidate)
        }
    }
}

struct RemoveResult<K, V> {
    node: Option<Arc<Node<K, V>>>,
    removed: Option<(K, V)>,
    changed: bool,
}

impl<K, V> PersistentHashMap<K, V, RandomState> {
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S> PersistentHashMap<K, V, S> {
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            root: None,
            len: 0,
            hasher,
            policy_identity: Arc::new(()),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.len
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        &self.hasher
    }

    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (None, None) => true,
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            _ => false,
        }
    }

    fn has_same_policy_identity(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.policy_identity, &other.policy_identity)
    }

    #[must_use]
    pub fn iter(&self) -> Iter<'_, K, V> {
        let mut stack = Vec::new();
        if let Some(root) = self.root.as_deref() {
            stack.push(IterFrame::Node(root));
        }

        Iter {
            stack,
            remaining: self.len,
        }
    }

    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.iter().map(|(key, _)| key)
    }

    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.iter().map(|(_, value)| value)
    }

    /// Moves this map into a single-owner editing session in O(1).
    ///
    /// The session uses the persistent CHAMP update kernel: point edits path-copy the affected
    /// trie path and then replace the session's current root. Publishing consumes the session and
    /// moves that current persistent value back out in O(1).
    #[must_use]
    pub fn into_transient(self) -> TransientHashMap<K, V, S> {
        TransientHashMap { map: self }
    }
}

impl<K, V, S: Clone> PersistentHashMap<K, V, S> {
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }

        Self {
            root: None,
            len: 0,
            hasher: self.hasher.clone(),
            policy_identity: Arc::clone(&self.policy_identity),
        }
    }

    /// Creates a single-owner editing session over this map with O(1) trie work.
    ///
    /// The session shares this map's root and hash-policy identity until its first logical change.
    /// A session subjected only to logical no-ops publishes a map that still shares this exact
    /// root. Prefer [`PersistentHashMap::into_transient`] when the source value is no longer needed;
    /// that form moves the wrapper and avoids the otherwise additional cost of `S::clone`.
    #[must_use]
    pub fn to_transient(&self) -> TransientHashMap<K, V, S> {
        self.clone().into_transient()
    }
}

impl<K, V, S> PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.get(key).is_some()
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.get_key_value(key).map(|(_, value)| value)
    }

    #[must_use]
    pub fn get_key_value(&self, key: &K) -> Option<(&K, &V)> {
        let hash = self.hash_key(key);
        self.root
            .as_deref()
            .and_then(|node| get_in_node(node, hash, key, 0))
    }

    fn hash_key(&self, key: &K) -> u32 {
        self.hasher.hash_one(key) as u32
    }
}

impl<K, V, S> PersistentHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone,
    S: BuildHasher + Clone,
{
    /// Returns the value already stored for `key`, or persistently adds the value produced by
    /// `add_factory` when the key is absent.
    ///
    /// The key is hashed once and the trie is descended once. `add_factory` is invoked exactly
    /// once on a miss and is not invoked on a hit. A hit retains both stored representatives and
    /// returns a map sharing this map's root.
    #[must_use]
    pub fn get_or_add<F>(&self, key: K, add_factory: F) -> MapUpdateResult<K, V, S>
    where
        F: FnOnce(&K) -> V,
    {
        self.apply_factory_update(
            key,
            GetOrAddSelector {
                add_factory: Some(add_factory),
            },
        )
    }

    fn apply_factory_update<Selector>(
        &self,
        key: K,
        mut selector: Selector,
    ) -> MapUpdateResult<K, V, S>
    where
        Selector: FactorySelector<K, V>,
    {
        let hash = self.hash_key(&key);
        let Some(root) = &self.root else {
            let value = selector.select_absent(&key);
            let result_value = value.clone();
            return MapUpdateResult {
                map: Self {
                    root: Some(Arc::new(Node::Leaf { hash, key, value })),
                    len: 1,
                    hasher: self.hasher.clone(),
                    policy_identity: Arc::clone(&self.policy_identity),
                },
                value: result_value,
            };
        };

        let result = factory_update_node(root, hash, key, 0, &mut selector);
        let map = if result.changed {
            Self {
                root: Some(result.node),
                len: self.len + usize::from(result.added),
                hasher: self.hasher.clone(),
                policy_identity: Arc::clone(&self.policy_identity),
            }
        } else {
            self.clone()
        };
        MapUpdateResult {
            map,
            value: result.value,
        }
    }

    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        self.try_remove(key)
            .map_or_else(|| self.clone(), |(map, _)| map)
    }

    #[must_use]
    pub fn try_remove(&self, key: &K) -> Option<(Self, V)> {
        self.try_remove_entry(key)
            .map(|(map, _, value)| (map, value))
    }

    #[must_use]
    pub fn try_remove_entry(&self, key: &K) -> Option<(Self, K, V)> {
        let root = self.root.as_ref()?;
        let hash = self.hash_key(key);
        let result = remove_node(root, hash, key, 0);
        if !result.changed {
            return None;
        }

        let (removed_key, removed_value) =
            result.removed.expect("changed removal must carry an entry");
        Some((
            Self {
                root: result.node,
                len: self.len - 1,
                hasher: self.hasher.clone(),
                policy_identity: Arc::clone(&self.policy_identity),
            },
            removed_key,
            removed_value,
        ))
    }
}

impl<K, V, S> PersistentHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    /// Persistently adds or updates `key` with exactly one selected factory invocation.
    ///
    /// The key is hashed once and the trie is descended once. On a hit, `update_factory` receives
    /// the caller's lookup-key representative and the stored value; `add_factory` is not invoked.
    /// On a miss, only `add_factory` is invoked. An update equal to the stored value retains the
    /// stored key and value representatives and returns a map sharing this map's root.
    #[must_use]
    pub fn add_or_update<Add, Update>(
        &self,
        key: K,
        add_factory: Add,
        update_factory: Update,
    ) -> MapUpdateResult<K, V, S>
    where
        Add: FnOnce(&K) -> V,
        Update: FnOnce(&K, &V) -> V,
    {
        self.apply_factory_update(
            key,
            AddOrUpdateSelector {
                add_factory: Some(add_factory),
                update_factory: Some(update_factory),
            },
        )
    }

    #[must_use]
    pub fn insert(&self, key: K, value: V) -> Self {
        let hash = self.hash_key(&key);
        match &self.root {
            None => Self {
                root: Some(Arc::new(Node::Leaf { hash, key, value })),
                len: 1,
                hasher: self.hasher.clone(),
                policy_identity: Arc::clone(&self.policy_identity),
            },
            Some(root) => {
                let result = insert_node(root, hash, key, value, 0, true);
                Self {
                    root: Some(result.node),
                    len: self.len + usize::from(result.added),
                    hasher: self.hasher.clone(),
                    policy_identity: Arc::clone(&self.policy_identity),
                }
            }
        }
    }

    pub fn add(&self, key: K, value: V) -> Result<Self, DuplicateKey> {
        let (map, added) = self.try_add(key, value);
        if added { Ok(map) } else { Err(DuplicateKey) }
    }

    #[must_use]
    pub fn try_add(&self, key: K, value: V) -> (Self, bool) {
        let hash = self.hash_key(&key);
        match &self.root {
            None => (
                Self {
                    root: Some(Arc::new(Node::Leaf { hash, key, value })),
                    len: 1,
                    hasher: self.hasher.clone(),
                    policy_identity: Arc::clone(&self.policy_identity),
                },
                true,
            ),
            Some(root) => {
                let result = insert_node(root, hash, key, value, 0, false);
                if result.duplicate {
                    return (self.clone(), false);
                }

                (
                    Self {
                        root: Some(result.node),
                        len: self.len + usize::from(result.added),
                        hasher: self.hasher.clone(),
                        policy_identity: Arc::clone(&self.policy_identity),
                    },
                    result.added,
                )
            }
        }
    }

    #[must_use]
    pub fn set_items<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = (K, V)>,
    {
        let mut map = self.clone();
        for (key, value) in items {
            map = map.insert(key, value);
        }

        map
    }

    /// Combines two maps structurally when they descend from the same hash-policy identity.
    #[must_use]
    pub fn union_map(&self, other: &Self) -> Self {
        self.combine_map(other, ChampOperation::Union)
    }

    #[must_use]
    pub fn intersect_map(&self, other: &Self) -> Self {
        self.combine_map(other, ChampOperation::Intersect)
    }

    #[must_use]
    pub fn except_map(&self, other: &Self) -> Self {
        self.combine_map(other, ChampOperation::Except)
    }

    #[must_use]
    pub fn symmetric_except_map(&self, other: &Self) -> Self {
        self.combine_map(other, ChampOperation::SymmetricExcept)
    }

    fn combine_map(&self, other: &Self, operation: ChampOperation) -> Self {
        if !Arc::ptr_eq(&self.policy_identity, &other.policy_identity) {
            return self.combine_map_elementwise(other, operation);
        }
        let root = combine_champ_nodes(self.root.as_ref(), other.root.as_ref(), 0, operation);
        if same_optional_root(&root, &self.root) {
            return self.clone();
        }
        if same_optional_root(&root, &other.root) {
            return other.clone();
        }
        Self {
            len: root.as_deref().map_or(0, Node::entry_count),
            root,
            hasher: self.hasher.clone(),
            policy_identity: Arc::clone(&self.policy_identity),
        }
    }

    fn combine_map_elementwise(&self, other: &Self, operation: ChampOperation) -> Self {
        match operation {
            ChampOperation::Union => other.iter().fold(self.clone(), |map, (key, value)| {
                map.insert(key.clone(), value.clone())
            }),
            ChampOperation::Intersect => self.iter().fold(self.clear(), |map, (key, value)| {
                if other.contains_key(key) {
                    map.insert(key.clone(), value.clone())
                } else {
                    map
                }
            }),
            ChampOperation::Except => other.keys().fold(self.clone(), |map, key| map.remove(key)),
            ChampOperation::SymmetricExcept => {
                other.iter().fold(self.clone(), |map, (key, value)| {
                    if map.contains_key(key) {
                        map.remove(key)
                    } else {
                        map.insert(key.clone(), value.clone())
                    }
                })
            }
        }
    }

    /// Reports semantic additions, removals, and replacements.
    ///
    /// Maps descending from one hash-policy identity are traversed in lockstep and prune every
    /// `Arc`-identical descendant. Independently created policies retain the semantic fallback.
    #[must_use]
    pub fn diff(&self, other: &Self) -> Vec<MapDifference<K, V>> {
        if self.shares_root_with(other) {
            return Vec::new();
        }
        if Arc::ptr_eq(&self.policy_identity, &other.policy_identity) {
            let mut result = Vec::new();
            diff_champ_nodes(self.root.as_ref(), other.root.as_ref(), 0, &mut result);
            return result;
        }
        let mut result = Vec::new();
        for (key, before) in self {
            match other.get(key) {
                None => result.push(MapDifference::Removed {
                    key: key.clone(),
                    value: before.clone(),
                }),
                Some(after) if after != before => result.push(MapDifference::Changed {
                    key: key.clone(),
                    before: before.clone(),
                    after: after.clone(),
                }),
                _ => {}
            }
        }
        for (key, value) in other {
            if self.get(key).is_none() {
                result.push(MapDifference::Added {
                    key: key.clone(),
                    value: value.clone(),
                });
            }
        }
        result
    }
}

impl<K, V, S: Default> Default for PersistentHashMap<K, V, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V, S> FromIterator<(K, V)> for PersistentHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone + Default,
{
    fn from_iter<T: IntoIterator<Item = (K, V)>>(iter: T) -> Self {
        let mut builder = BulkBuilder::with_hasher(S::default());
        builder.set_items(iter);
        builder.into_immutable()
    }
}

impl<'a, K, V, S> IntoIterator for &'a PersistentHashMap<K, V, S> {
    type Item = (&'a K, &'a V);
    type IntoIter = Iter<'a, K, V>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<K, V, S> fmt::Debug for PersistentHashMap<K, V, S>
where
    K: fmt::Debug,
    V: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_map().entries(self.iter()).finish()
    }
}

impl<K, V, S> PartialEq for PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    V: PartialEq,
    S: BuildHasher,
{
    fn eq(&self, other: &Self) -> bool {
        if self.shares_root_with(other) {
            return true;
        }
        if self.len != other.len {
            return false;
        }
        if Arc::ptr_eq(&self.policy_identity, &other.policy_identity) {
            return champ_optional_nodes_equal(self.root.as_ref(), other.root.as_ref());
        }
        self.iter()
            .all(|(key, value)| other.get(key) == Some(value))
    }
}

impl<K, V, S> Eq for PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    V: Eq,
    S: BuildHasher,
{
}

impl<K, V, S> Index<&K> for PersistentHashMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    type Output = V;

    fn index(&self, key: &K) -> &V {
        self.get(key).expect("no entry found for key")
    }
}

/// A single-owner, one-way editing session for [`PersistentHashMap`].
///
/// Unlike [`BulkBuilder`], this type can adopt an existing persistent map and supports lookup,
/// removal, and clear as well as insertion. Unlike the C# owner-token kernel, this first Rust
/// implementation deliberately does not mutate adopted nodes in place: every changed point edit
/// delegates to the persistent CHAMP path-copy operation. This keeps retained snapshots isolated
/// and preserves the crate's safe-`Arc` representation without adding locks or unsafe code.
///
/// Publication is expressed by ownership. [`TransientHashMap::into_persistent`] consumes the
/// session, so Rust statically prevents reads, edits, iteration, or a second publication through
/// the same value afterward. An iterator borrows the session and therefore also statically blocks
/// mutation for its lifetime.
#[must_use = "a transient session has no effect unless it is published with into_persistent"]
pub struct TransientHashMap<K, V, S = RandomState> {
    map: PersistentHashMap<K, V, S>,
}

impl<K, V> TransientHashMap<K, V, RandomState> {
    /// Creates an empty session with a fresh default hash builder.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S: Default> Default for TransientHashMap<K, V, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V, S> TransientHashMap<K, V, S> {
    /// Creates an empty session that retains `hasher` as its hash policy.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        PersistentHashMap::with_hasher(hasher).into_transient()
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.map.hasher()
    }

    /// Iterates the session's current trie order.
    pub fn iter(&self) -> Iter<'_, K, V> {
        self.map.iter()
    }

    pub fn keys(&self) -> impl Iterator<Item = &K> {
        self.map.keys()
    }

    pub fn values(&self) -> impl Iterator<Item = &V> {
        self.map.values()
    }

    /// Consumes this session and publishes its current persistent value in O(1).
    #[must_use]
    pub fn into_persistent(self) -> PersistentHashMap<K, V, S> {
        self.map
    }
}

impl<K, V, S> TransientHashMap<K, V, S>
where
    K: Eq + Hash,
    S: BuildHasher,
{
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.map.contains_key(key)
    }

    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.map.get(key)
    }

    /// Returns the retained concrete key representative and its value.
    #[must_use]
    pub fn get_key_value(&self, key: &K) -> Option<(&K, &V)> {
        self.map.get_key_value(key)
    }
}

impl<K, V, S> TransientHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone + PartialEq,
    S: BuildHasher + Clone,
{
    /// Adds or replaces one entry, returning whether the logical map changed.
    ///
    /// An equivalent key retains the first concrete key representative. An equal replacement
    /// value is a logical no-op and retains both the current root and stored value instance.
    pub fn insert(&mut self, key: K, value: V) -> bool {
        let next = self.map.insert(key, value);
        if self.map.shares_root_with(&next) {
            return false;
        }

        self.map = next;
        true
    }

    /// Adds one entry unless an equivalent key is already present.
    pub fn try_add(&mut self, key: K, value: V) -> bool {
        let (next, added) = self.map.try_add(key, value);
        if added {
            self.map = next;
        }
        added
    }

    /// Adds one entry or returns [`DuplicateKey`] without changing the session.
    pub fn add(&mut self, key: K, value: V) -> Result<(), DuplicateKey> {
        if self.try_add(key, value) {
            Ok(())
        } else {
            Err(DuplicateKey)
        }
    }
}

impl<K, V, S> TransientHashMap<K, V, S>
where
    K: Eq + Hash + Clone,
    V: Clone,
    S: BuildHasher + Clone,
{
    /// Removes one entry and returns its value, if present.
    pub fn remove(&mut self, key: &K) -> Option<V> {
        self.remove_entry(key).map(|(_, value)| value)
    }

    /// Removes one entry and returns its retained concrete key and value representatives.
    pub fn remove_entry(&mut self, key: &K) -> Option<(K, V)> {
        let (next, actual_key, value) = self.map.try_remove_entry(key)?;
        self.map = next;
        Some((actual_key, value))
    }
}

impl<K, V, S: Clone> TransientHashMap<K, V, S> {
    /// Clears the session, returning whether it was nonempty.
    pub fn clear(&mut self) -> bool {
        if self.map.is_empty() {
            return false;
        }

        self.map = self.map.clear();
        true
    }
}

impl<'a, K, V, S> IntoIterator for &'a TransientHashMap<K, V, S> {
    type Item = (&'a K, &'a V);
    type IntoIter = Iter<'a, K, V>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

/// Builds an independent map in one pass by mutating unpublished nodes in
/// place, then freezes them into persistent nodes.
///
/// Mirrors the C# reference's internal bulk builder: `set_item` follows the
/// map's duplicate rule (the first stored key instance is retained, the last
/// supplied value wins, and a value equal under `PartialEq` to the stored one
/// keeps the earlier stored value), and the supplied hasher is preserved in
/// every frozen map. Each update costs O(w + c) node mutations — bounded trie
/// depth plus the applicable equal-hash collision scan — with no persistent
/// path copies between successive entries. Frozen maps never share mutable
/// storage with the builder.
pub struct BulkBuilder<K, V, S = RandomState> {
    root: Option<MutableNode<K, V>>,
    len: usize,
    hasher: S,
    policy_identity: Arc<()>,
}

enum MutableNode<K, V> {
    Leaf {
        hash: u32,
        key: K,
        value: V,
    },
    Collision {
        hash: u32,
        entries: Vec<(K, V)>,
    },
    Branch {
        data_map: u32,
        node_map: u32,
        data: Vec<(u32, K, V)>,
        children: Vec<MutableNode<K, V>>,
    },
}

impl<K, V> BulkBuilder<K, V, RandomState> {
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<K, V, S: Default> Default for BulkBuilder<K, V, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<K, V, S> BulkBuilder<K, V, S> {
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            root: None,
            len: 0,
            hasher,
            policy_identity: Arc::new(()),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.len
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        &self.hasher
    }

    /// Freezes the builder into a persistent map, consuming it. Nodes are
    /// moved, not cloned.
    #[must_use]
    pub fn into_immutable(self) -> PersistentHashMap<K, V, S> {
        PersistentHashMap {
            root: self.root.map(freeze_owned),
            len: self.len,
            hasher: self.hasher,
            policy_identity: self.policy_identity,
        }
    }
}

impl<K, V, S> BulkBuilder<K, V, S>
where
    K: Eq + Hash,
    V: PartialEq,
    S: BuildHasher,
{
    /// Adds or replaces the entry for `key` under the map's duplicate rule.
    pub fn set_item(&mut self, key: K, value: V) {
        let hash = self.hasher.hash_one(&key) as u32;
        match &mut self.root {
            None => {
                self.root = Some(MutableNode::Leaf { hash, key, value });
                self.len = 1;
            }
            Some(root) => {
                if set_in_mutable(root, hash, key, value, 0) {
                    self.len += 1;
                }
            }
        }
    }

    /// Adds or replaces every pair in order, applying the duplicate rule.
    pub fn set_items<I>(&mut self, items: I)
    where
        I: IntoIterator<Item = (K, V)>,
    {
        for (key, value) in items {
            self.set_item(key, value);
        }
    }
}

impl<K, V, S> BulkBuilder<K, V, S>
where
    K: Clone,
    V: Clone,
    S: Clone,
{
    /// Freezes the current contents into a persistent map. The builder stays
    /// usable and later mutations never affect the frozen snapshot.
    #[must_use]
    pub fn to_immutable(&self) -> PersistentHashMap<K, V, S> {
        PersistentHashMap {
            root: self.root.as_ref().map(freeze_cloned),
            len: self.len,
            hasher: self.hasher.clone(),
            policy_identity: Arc::clone(&self.policy_identity),
        }
    }
}

pub struct Iter<'a, K, V> {
    stack: Vec<IterFrame<'a, K, V>>,
    remaining: usize,
}

impl<K, V> Clone for Iter<'_, K, V> {
    fn clone(&self) -> Self {
        Self {
            stack: self.stack.clone(),
            remaining: self.remaining,
        }
    }
}

enum IterFrame<'a, K, V> {
    Node(&'a Node<K, V>),
    Data(std::slice::Iter<'a, (u32, K, V)>),
    Branch(std::slice::Iter<'a, Arc<Node<K, V>>>),
    Collision(std::slice::Iter<'a, (K, V)>),
}

impl<K, V> Clone for IterFrame<'_, K, V> {
    fn clone(&self) -> Self {
        match self {
            Self::Node(node) => Self::Node(node),
            Self::Data(data) => Self::Data(data.clone()),
            Self::Branch(children) => Self::Branch(children.clone()),
            Self::Collision(entries) => Self::Collision(entries.clone()),
        }
    }
}

impl<'a, K, V> Iterator for Iter<'a, K, V> {
    type Item = (&'a K, &'a V);

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(frame) = self.stack.pop() {
            match frame {
                IterFrame::Node(node) => match node {
                    Node::Leaf { key, value, .. } => {
                        self.remaining -= 1;
                        return Some((key, value));
                    }
                    Node::Collision { entries, .. } => {
                        self.stack.push(IterFrame::Collision(entries.iter()));
                    }
                    Node::Branch { data, children, .. } => {
                        self.stack.push(IterFrame::Branch(children.iter()));
                        self.stack.push(IterFrame::Data(data.iter()));
                    }
                },
                IterFrame::Data(mut data) => {
                    if let Some((_, key, value)) = data.next() {
                        self.stack.push(IterFrame::Data(data));
                        self.remaining -= 1;
                        return Some((key, value));
                    }
                }
                IterFrame::Branch(mut children) => {
                    if let Some(child) = children.next() {
                        self.stack.push(IterFrame::Branch(children));
                        self.stack.push(IterFrame::Node(child.as_ref()));
                    }
                }
                IterFrame::Collision(mut entries) => {
                    if let Some((key, value)) = entries.next() {
                        self.stack.push(IterFrame::Collision(entries));
                        self.remaining -= 1;
                        return Some((key, value));
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

impl<K, V> ExactSizeIterator for Iter<'_, K, V> {}

impl<K, V> FusedIterator for Iter<'_, K, V> {}

pub struct PersistentHashSet<T, S = RandomState> {
    map: PersistentHashMap<T, (), S>,
}

impl<T, S: Clone> Clone for PersistentHashSet<T, S> {
    fn clone(&self) -> Self {
        Self {
            map: self.map.clone(),
        }
    }
}

impl<T> PersistentHashSet<T, RandomState> {
    #[must_use]
    pub fn new() -> Self {
        Self {
            map: PersistentHashMap::new(),
        }
    }
}

impl<T, S> PersistentHashSet<T, S> {
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            map: PersistentHashMap::with_hasher(hasher),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.map.hasher()
    }

    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        self.map.shares_root_with(&other.map)
    }

    #[must_use]
    pub fn iter(&self) -> SetIter<'_, T> {
        SetIter {
            inner: self.map.iter(),
        }
    }

    /// Moves this set into a single-owner editing session in O(1).
    #[must_use]
    pub fn into_transient(self) -> TransientHashSet<T, S> {
        TransientHashSet {
            map: self.map.into_transient(),
        }
    }
}

impl<T, S: Clone> PersistentHashSet<T, S> {
    #[must_use]
    pub fn clear(&self) -> Self {
        Self {
            map: self.map.clear(),
        }
    }

    /// Creates a single-owner editing session sharing this set's current CHAMP root.
    ///
    /// The trie work is O(1); the operation additionally has the cost of `S::clone`.
    #[must_use]
    pub fn to_transient(&self) -> TransientHashSet<T, S> {
        self.clone().into_transient()
    }
}

impl<T, S> PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    #[must_use]
    pub fn contains(&self, value: &T) -> bool {
        self.map.contains_key(value)
    }

    #[must_use]
    pub fn get(&self, value: &T) -> Option<&T> {
        self.map.get_key_value(value).map(|(key, _)| key)
    }
}

impl<T, S> PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    /// Bulk-constructs a set from scratch through the map's scratch builder,
    /// preserving the map's duplicate rule (first stored instance wins).
    fn bulk_from_items<I>(hasher: S, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut builder = BulkBuilder::with_hasher(hasher);
        builder.set_items(items.into_iter().map(|value| (value, ())));
        Self {
            map: builder.into_immutable(),
        }
    }
}

impl<T, S> PersistentHashSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    /// Builds the receiver-policy probe set the binary relations judge
    /// membership against, deduplicating the argument in one bulk pass.
    fn probe_from<I>(&self, items: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        Self::bulk_from_items(self.map.hasher().clone(), items)
    }

    #[must_use]
    pub fn insert(&self, value: T) -> Self {
        Self {
            map: self.map.insert(value, ()),
        }
    }

    pub fn add(&self, value: T) -> Result<Self, DuplicateKey> {
        self.map.add(value, ()).map(|map| Self { map })
    }

    #[must_use]
    pub fn try_add(&self, value: T) -> (Self, bool) {
        let (map, added) = self.map.try_add(value, ());
        (Self { map }, added)
    }

    #[must_use]
    pub fn remove(&self, value: &T) -> Self {
        Self {
            map: self.map.remove(value),
        }
    }

    #[must_use]
    pub fn try_remove(&self, value: &T) -> Option<(Self, T)> {
        let (map, actual, ()) = self.map.try_remove_entry(value)?;
        Some((Self { map }, actual))
    }

    #[must_use]
    pub fn union<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = self.clone();
        for value in other {
            result = result.insert(value);
        }

        result
    }

    #[must_use]
    pub fn intersect<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.probe_from(other);
        Self::bulk_from_items(
            self.map.hasher().clone(),
            self.iter().filter(|value| probe.contains(value)).cloned(),
        )
    }

    #[must_use]
    pub fn except<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let mut result = self.clone();
        for value in other {
            result = result.remove(&value);
        }

        result
    }

    #[must_use]
    pub fn symmetric_except<I>(&self, other: I) -> Self
    where
        I: IntoIterator<Item = T>,
    {
        let distinct = self.probe_from(other);
        let mut result = self.clone();
        for value in distinct.iter() {
            result = if result.contains(value) {
                result.remove(value)
            } else {
                result.insert(value.clone())
            };
        }

        result
    }

    /// Uses reference-pruned CHAMP algebra for a shared policy identity and a semantic fallback otherwise.
    #[must_use]
    pub fn union_set(&self, other: &Self) -> Self {
        Self {
            map: self.map.union_map(&other.map),
        }
    }

    #[must_use]
    pub fn intersect_set(&self, other: &Self) -> Self {
        Self {
            map: self.map.intersect_map(&other.map),
        }
    }

    #[must_use]
    pub fn except_set(&self, other: &Self) -> Self {
        Self {
            map: self.map.except_map(&other.map),
        }
    }

    #[must_use]
    pub fn symmetric_except_set(&self, other: &Self) -> Self {
        Self {
            map: self.map.symmetric_except_map(&other.map),
        }
    }

    #[must_use]
    pub fn is_subset_of_set(&self, other: &Self) -> bool {
        if Arc::ptr_eq(&self.map.policy_identity, &other.map.policy_identity) {
            self.len() <= other.len()
                && self
                    .map
                    .intersect_map(&other.map)
                    .shares_root_with(&self.map)
        } else {
            self.is_subset_of(other.iter().cloned())
        }
    }

    #[must_use]
    pub fn is_superset_of_set(&self, other: &Self) -> bool {
        other.is_subset_of_set(self)
    }

    #[must_use]
    pub fn is_proper_subset_of_set(&self, other: &Self) -> bool {
        self.len() < other.len() && self.is_subset_of_set(other)
    }

    #[must_use]
    pub fn is_proper_superset_of_set(&self, other: &Self) -> bool {
        self.len() > other.len() && other.is_subset_of_set(self)
    }

    #[must_use]
    pub fn overlaps_set(&self, other: &Self) -> bool {
        if Arc::ptr_eq(&self.map.policy_identity, &other.map.policy_identity) {
            !self.map.intersect_map(&other.map).is_empty()
        } else {
            self.overlaps(other.iter().cloned())
        }
    }

    #[must_use]
    pub fn set_equals_set(&self, other: &Self) -> bool {
        self == other
    }

    #[must_use]
    pub fn is_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.probe_from(other);
        self.iter().all(|value| probe.contains(value))
    }

    #[must_use]
    pub fn is_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        other.into_iter().all(|value| self.contains(&value))
    }

    #[must_use]
    pub fn is_proper_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.probe_from(other);
        self.len() < probe.len() && self.iter().all(|value| probe.contains(value))
    }

    #[must_use]
    pub fn is_proper_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.probe_from(other);
        self.len() > probe.len() && probe.iter().all(|value| self.contains(value))
    }

    #[must_use]
    pub fn overlaps<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        other.into_iter().any(|value| self.contains(&value))
    }

    #[must_use]
    pub fn set_equals<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let other_set = self.probe_from(other);
        self.len() == other_set.len() && self.iter().all(|value| other_set.contains(value))
    }
}

/// A single-owner, one-way editing session for [`PersistentHashSet`].
///
/// This is a thin set facade over [`TransientHashMap`]. Changed point edits use persistent CHAMP
/// path copying; publication consumes the session and moves the current set out in O(1).
#[must_use = "a transient session has no effect unless it is published with into_persistent"]
pub struct TransientHashSet<T, S = RandomState> {
    map: TransientHashMap<T, (), S>,
}

impl<T> TransientHashSet<T, RandomState> {
    /// Creates an empty session with a fresh default hash builder.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<T, S: Default> Default for TransientHashSet<T, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<T, S> TransientHashSet<T, S> {
    /// Creates an empty session that retains `hasher` as its hash policy.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            map: TransientHashMap::with_hasher(hasher),
        }
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.map.hasher()
    }

    /// Iterates the session's current trie order.
    pub fn iter(&self) -> SetIter<'_, T> {
        SetIter {
            inner: self.map.iter(),
        }
    }

    /// Consumes this session and publishes its current persistent value in O(1).
    #[must_use]
    pub fn into_persistent(self) -> PersistentHashSet<T, S> {
        PersistentHashSet {
            map: self.map.into_persistent(),
        }
    }
}

impl<T, S> TransientHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    #[must_use]
    pub fn contains(&self, value: &T) -> bool {
        self.map.contains_key(value)
    }

    /// Returns the retained concrete representative equivalent to `value`.
    #[must_use]
    pub fn get(&self, value: &T) -> Option<&T> {
        self.map.get_key_value(value).map(|(key, _)| key)
    }

    /// Tests whether every supplied value occurs in the active session.
    #[must_use]
    pub fn is_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        other.into_iter().all(|value| self.contains(&value))
    }

    /// Tests whether at least one supplied value occurs in the active session.
    #[must_use]
    pub fn overlaps<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        other.into_iter().any(|value| self.contains(&value))
    }
}

impl<T, S> TransientHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher + Clone,
{
    fn relation_probe<I>(&self, other: I) -> PersistentHashSet<T, S>
    where
        I: IntoIterator<Item = T>,
    {
        PersistentHashSet::bulk_from_items(self.hasher().clone(), other)
    }

    /// Tests subset membership after deduplicating the argument with this session's hash policy.
    #[must_use]
    pub fn is_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.relation_probe(other);
        self.iter().all(|value| probe.contains(value))
    }

    /// Tests strict subset membership under this session's hash policy.
    #[must_use]
    pub fn is_proper_subset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.relation_probe(other);
        self.len() < probe.len() && self.iter().all(|value| probe.contains(value))
    }

    /// Tests strict superset membership after deduplicating the argument with this policy.
    #[must_use]
    pub fn is_proper_superset_of<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.relation_probe(other);
        self.len() > probe.len() && probe.iter().all(|value| self.contains(value))
    }

    /// Tests set equality after deduplicating the argument with this session's hash policy.
    #[must_use]
    pub fn set_equals<I>(&self, other: I) -> bool
    where
        I: IntoIterator<Item = T>,
    {
        let probe = self.relation_probe(other);
        self.len() == probe.len() && self.iter().all(|value| probe.contains(value))
    }
}

impl<T, S> TransientHashSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    /// Inserts `value`, returning whether the logical set changed.
    pub fn insert(&mut self, value: T) -> bool {
        self.map.try_add(value, ())
    }

    /// Removes and returns the retained representative equivalent to `value`.
    pub fn remove(&mut self, value: &T) -> Option<T> {
        self.map.remove_entry(value).map(|(actual, ())| actual)
    }
}

impl<T, S: Clone> TransientHashSet<T, S> {
    /// Clears the session, returning whether it was nonempty.
    pub fn clear(&mut self) -> bool {
        self.map.clear()
    }
}

impl<'a, T, S> IntoIterator for &'a TransientHashSet<T, S> {
    type Item = &'a T;
    type IntoIter = SetIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T, S: Default> Default for PersistentHashSet<T, S> {
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<T, S> FromIterator<T> for PersistentHashSet<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone + Default,
{
    fn from_iter<I: IntoIterator<Item = T>>(iter: I) -> Self {
        Self::bulk_from_items(S::default(), iter)
    }
}

impl<'a, T, S> IntoIterator for &'a PersistentHashSet<T, S> {
    type Item = &'a T;
    type IntoIter = SetIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

impl<T, S> fmt::Debug for PersistentHashSet<T, S>
where
    T: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_set().entries(self.iter()).finish()
    }
}

impl<T, S> PartialEq for PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    fn eq(&self, other: &Self) -> bool {
        self.map == other.map
    }
}

impl<T, S> Eq for PersistentHashSet<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
}

pub struct SetIter<'a, T> {
    inner: Iter<'a, T, ()>,
}

impl<T> Clone for SetIter<'_, T> {
    fn clone(&self) -> Self {
        Self {
            inner: self.inner.clone(),
        }
    }
}

impl<'a, T> Iterator for SetIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next().map(|(key, ())| key)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> ExactSizeIterator for SetIter<'_, T> {}

impl<T> FusedIterator for SetIter<'_, T> {}

#[derive(Clone, Copy, PartialEq, Eq)]
enum ChampOperation {
    Union,
    Intersect,
    Except,
    SymmetricExcept,
}

fn same_optional_root<K, V>(
    left: &Option<Arc<Node<K, V>>>,
    right: &Option<Arc<Node<K, V>>>,
) -> bool {
    match (left, right) {
        (None, None) => true,
        (Some(left), Some(right)) => Arc::ptr_eq(left, right),
        _ => false,
    }
}

fn champ_optional_nodes_equal<K, V>(
    left: Option<&Arc<Node<K, V>>>,
    right: Option<&Arc<Node<K, V>>>,
) -> bool
where
    K: Eq,
    V: PartialEq,
{
    match (left, right) {
        (None, None) => true,
        (Some(left), Some(right)) => champ_nodes_equal(left, right),
        _ => false,
    }
}

fn champ_nodes_equal<K, V>(left: &Arc<Node<K, V>>, right: &Arc<Node<K, V>>) -> bool
where
    K: Eq,
    V: PartialEq,
{
    if Arc::ptr_eq(left, right) {
        return true;
    }
    match (left.as_ref(), right.as_ref()) {
        (
            Node::Leaf {
                hash: left_hash,
                key: left_key,
                value: left_value,
            },
            Node::Leaf {
                hash: right_hash,
                key: right_key,
                value: right_value,
            },
        ) => left_hash == right_hash && left_key == right_key && left_value == right_value,
        (
            Node::Collision {
                hash: left_hash,
                entries: left_entries,
            },
            Node::Collision {
                hash: right_hash,
                entries: right_entries,
            },
        ) => {
            left_hash == right_hash
                && left_entries.len() == right_entries.len()
                && left_entries.iter().all(|(left_key, left_value)| {
                    right_entries.iter().any(|(right_key, right_value)| {
                        left_key == right_key && left_value == right_value
                    })
                })
        }
        (
            Node::Branch {
                data_map: left_data_map,
                node_map: left_node_map,
                data: left_data,
                children: left_children,
                ..
            },
            Node::Branch {
                data_map: right_data_map,
                node_map: right_node_map,
                data: right_data,
                children: right_children,
                ..
            },
        ) => {
            left_data_map == right_data_map
                && left_node_map == right_node_map
                && left_data.len() == right_data.len()
                && left_data.iter().zip(right_data.iter()).all(
                    |((left_hash, left_key, left_value), (right_hash, right_key, right_value))| {
                        left_hash == right_hash
                            && left_key == right_key
                            && left_value == right_value
                    },
                )
                && left_children.len() == right_children.len()
                && left_children
                    .iter()
                    .zip(right_children.iter())
                    .all(|(left_child, right_child)| champ_nodes_equal(left_child, right_child))
        }
        _ => false,
    }
}

enum ChampSlotRef<'a, K, V> {
    Data(u32, &'a K, &'a V),
    Child(&'a Arc<Node<K, V>>),
}

fn champ_branch_slot<'a, K, V>(node: &'a Node<K, V>, index: u32) -> Option<ChampSlotRef<'a, K, V>> {
    let Node::Branch {
        data_map,
        node_map,
        data,
        children,
        ..
    } = node
    else {
        return None;
    };
    let bit = bit_position(index);
    if data_map & bit != 0 {
        let (hash, key, value) = &data[sparse_index(*data_map, bit)];
        Some(ChampSlotRef::Data(*hash, key, value))
    } else if node_map & bit != 0 {
        Some(ChampSlotRef::Child(&children[sparse_index(*node_map, bit)]))
    } else {
        None
    }
}

fn append_champ_subtree<K, V>(node: &Node<K, V>, added: bool, result: &mut Vec<MapDifference<K, V>>)
where
    K: Clone,
    V: Clone,
{
    match node {
        Node::Leaf { key, value, .. } => {
            if added {
                result.push(MapDifference::Added {
                    key: key.clone(),
                    value: value.clone(),
                });
            } else {
                result.push(MapDifference::Removed {
                    key: key.clone(),
                    value: value.clone(),
                });
            }
        }
        Node::Collision { entries, .. } => {
            for (key, value) in entries.iter() {
                if added {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                } else {
                    result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
        }
        Node::Branch { data, children, .. } => {
            for (_, key, value) in data.iter() {
                if added {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                } else {
                    result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
            for child in children.iter() {
                append_champ_subtree(child, added, result);
            }
        }
    }
}

fn diff_champ_region_left<K, V>(
    node: &Node<K, V>,
    right: &Node<K, V>,
    shift: u32,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    match node {
        Node::Leaf { hash, key, value } => match get_in_node(right, *hash, key, shift) {
            None => result.push(MapDifference::Removed {
                key: key.clone(),
                value: value.clone(),
            }),
            Some((_, after)) if after != value => result.push(MapDifference::Changed {
                key: key.clone(),
                before: value.clone(),
                after: after.clone(),
            }),
            _ => {}
        },
        Node::Collision { hash, entries } => {
            for (key, value) in entries.iter() {
                match get_in_node(right, *hash, key, shift) {
                    None => result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    }),
                    Some((_, after)) if after != value => result.push(MapDifference::Changed {
                        key: key.clone(),
                        before: value.clone(),
                        after: after.clone(),
                    }),
                    _ => {}
                }
            }
        }
        Node::Branch { data, children, .. } => {
            for (hash, key, value) in data.iter() {
                match get_in_node(right, *hash, key, shift) {
                    None => result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    }),
                    Some((_, after)) if after != value => result.push(MapDifference::Changed {
                        key: key.clone(),
                        before: value.clone(),
                        after: after.clone(),
                    }),
                    _ => {}
                }
            }
            for child in children.iter() {
                diff_champ_region_left(child, right, shift, result);
            }
        }
    }
}

fn append_champ_region_additions<K, V>(
    node: &Node<K, V>,
    left: &Node<K, V>,
    shift: u32,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone,
{
    match node {
        Node::Leaf { hash, key, value } => {
            if get_in_node(left, *hash, key, shift).is_none() {
                result.push(MapDifference::Added {
                    key: key.clone(),
                    value: value.clone(),
                });
            }
        }
        Node::Collision { hash, entries } => {
            for (key, value) in entries.iter() {
                if get_in_node(left, *hash, key, shift).is_none() {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
        }
        Node::Branch { data, children, .. } => {
            for (hash, key, value) in data.iter() {
                if get_in_node(left, *hash, key, shift).is_none() {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
            for child in children.iter() {
                append_champ_region_additions(child, left, shift, result);
            }
        }
    }
}

fn diff_champ_regions<K, V>(
    left: &Arc<Node<K, V>>,
    right: &Arc<Node<K, V>>,
    shift: u32,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    diff_champ_region_left(left, right, shift, result);
    append_champ_region_additions(right, left, shift, result);
}

fn append_champ_subtree_except_key<K, V>(
    node: &Node<K, V>,
    excluded: &K,
    added: bool,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone,
{
    match node {
        Node::Leaf { key, value, .. } => {
            if key != excluded {
                if added {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                } else {
                    result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
        }
        Node::Collision { entries, .. } => {
            for (key, value) in entries.iter().filter(|(key, _)| key != excluded) {
                if added {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                } else {
                    result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
        }
        Node::Branch { data, children, .. } => {
            for (_, key, value) in data.iter().filter(|(_, key, _)| key != excluded) {
                if added {
                    result.push(MapDifference::Added {
                        key: key.clone(),
                        value: value.clone(),
                    });
                } else {
                    result.push(MapDifference::Removed {
                        key: key.clone(),
                        value: value.clone(),
                    });
                }
            }
            for child in children.iter() {
                append_champ_subtree_except_key(child, excluded, added, result);
            }
        }
    }
}

fn diff_champ_entry_and_node<K, V>(
    hash: u32,
    key: &K,
    value: &V,
    right: &Arc<Node<K, V>>,
    shift: u32,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    match get_in_node(right, hash, key, shift) {
        None => result.push(MapDifference::Removed {
            key: key.clone(),
            value: value.clone(),
        }),
        Some((_, after)) if after != value => result.push(MapDifference::Changed {
            key: key.clone(),
            before: value.clone(),
            after: after.clone(),
        }),
        _ => {}
    }
    append_champ_subtree_except_key(right, key, true, result);
}

fn diff_champ_node_and_entry<K, V>(
    left: &Arc<Node<K, V>>,
    hash: u32,
    key: &K,
    value: &V,
    shift: u32,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    append_champ_subtree_except_key(left, key, false, result);
    match get_in_node(left, hash, key, shift) {
        None => result.push(MapDifference::Added {
            key: key.clone(),
            value: value.clone(),
        }),
        Some((actual_key, before)) if before != value => result.push(MapDifference::Changed {
            key: actual_key.clone(),
            before: before.clone(),
            after: value.clone(),
        }),
        _ => {}
    }
}

fn diff_champ_nodes<K, V>(
    left: Option<&Arc<Node<K, V>>>,
    right: Option<&Arc<Node<K, V>>>,
    shift: u32,
    result: &mut Vec<MapDifference<K, V>>,
) where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    match (left, right) {
        (None, None) => return,
        (Some(left), Some(right)) if Arc::ptr_eq(left, right) => return,
        (None, Some(right)) => {
            append_champ_subtree(right, true, result);
            return;
        }
        (Some(left), None) => {
            append_champ_subtree(left, false, result);
            return;
        }
        _ => {}
    }
    let left = left.unwrap();
    let right = right.unwrap();
    if !matches!(left.as_ref(), Node::Branch { .. })
        || !matches!(right.as_ref(), Node::Branch { .. })
    {
        diff_champ_regions(left, right, shift, result);
        return;
    }

    for index in 0..BRANCH_FACTOR as u32 {
        let left_slot = champ_branch_slot(left, index);
        let right_slot = champ_branch_slot(right, index);
        match (left_slot, right_slot) {
            (None, None) => {}
            (Some(ChampSlotRef::Data(_, key, value)), None) => {
                result.push(MapDifference::Removed {
                    key: key.clone(),
                    value: value.clone(),
                });
            }
            (None, Some(ChampSlotRef::Data(_, key, value))) => {
                result.push(MapDifference::Added {
                    key: key.clone(),
                    value: value.clone(),
                });
            }
            (Some(ChampSlotRef::Child(child)), None) => {
                append_champ_subtree(child, false, result);
            }
            (None, Some(ChampSlotRef::Child(child))) => {
                append_champ_subtree(child, true, result);
            }
            (
                Some(ChampSlotRef::Data(_, left_key, left_value)),
                Some(ChampSlotRef::Data(_, right_key, right_value)),
            ) => {
                if left_key == right_key {
                    if left_value != right_value {
                        result.push(MapDifference::Changed {
                            key: left_key.clone(),
                            before: left_value.clone(),
                            after: right_value.clone(),
                        });
                    }
                } else {
                    result.push(MapDifference::Removed {
                        key: left_key.clone(),
                        value: left_value.clone(),
                    });
                    result.push(MapDifference::Added {
                        key: right_key.clone(),
                        value: right_value.clone(),
                    });
                }
            }
            (Some(ChampSlotRef::Child(left_child)), Some(ChampSlotRef::Child(right_child))) => {
                diff_champ_nodes(
                    Some(left_child),
                    Some(right_child),
                    shift + BITS_PER_LEVEL,
                    result,
                );
            }
            (
                Some(ChampSlotRef::Data(hash, key, value)),
                Some(ChampSlotRef::Child(right_child)),
            ) => {
                diff_champ_entry_and_node(
                    hash,
                    key,
                    value,
                    right_child,
                    shift + BITS_PER_LEVEL,
                    result,
                );
            }
            (Some(ChampSlotRef::Child(left_child)), Some(ChampSlotRef::Data(hash, key, value))) => {
                diff_champ_node_and_entry(
                    left_child,
                    hash,
                    key,
                    value,
                    shift + BITS_PER_LEVEL,
                    result,
                );
            }
        }
    }
}

fn combine_champ_nodes<K, V>(
    left: Option<&Arc<Node<K, V>>>,
    right: Option<&Arc<Node<K, V>>>,
    shift: u32,
    operation: ChampOperation,
) -> Option<Arc<Node<K, V>>>
where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    match (left, right) {
        (None, None) => return None,
        (Some(left), Some(right)) if Arc::ptr_eq(left, right) => {
            return matches!(operation, ChampOperation::Union | ChampOperation::Intersect)
                .then(|| Arc::clone(left));
        }
        (None, Some(right)) => {
            return matches!(
                operation,
                ChampOperation::Union | ChampOperation::SymmetricExcept
            )
            .then(|| Arc::clone(right));
        }
        (Some(left), None) => {
            return (!matches!(operation, ChampOperation::Intersect)).then(|| Arc::clone(left));
        }
        _ => {}
    }
    let left = left.unwrap();
    let right = right.unwrap();
    if node_hash(left.as_ref()).is_some() && node_hash(right.as_ref()).is_some() {
        return combine_champ_hash_nodes(left, right, shift, operation);
    }

    let mut slots = vec![None; BRANCH_FACTOR];
    for (index, slot) in slots.iter_mut().enumerate() {
        let left_slot = champ_logical_slot(left, index as u32, shift);
        let right_slot = champ_logical_slot(right, index as u32, shift);
        *slot = combine_champ_nodes(
            left_slot.as_ref(),
            right_slot.as_ref(),
            shift + BITS_PER_LEVEL,
            operation,
        );
    }
    build_champ_logical_node(slots, left, shift)
}

fn combine_champ_hash_nodes<K, V>(
    left: &Arc<Node<K, V>>,
    right: &Arc<Node<K, V>>,
    shift: u32,
    operation: ChampOperation,
) -> Option<Arc<Node<K, V>>>
where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    let left_hash = node_hash(left).unwrap();
    let right_hash = node_hash(right).unwrap();
    if left_hash != right_hash {
        if operation == ChampOperation::Intersect {
            return None;
        }
        if operation == ChampOperation::Except {
            return Some(Arc::clone(left));
        }
        let mut slots = vec![None; BRANCH_FACTOR];
        let left_index = hash_fragment(left_hash, shift) as usize;
        let right_index = hash_fragment(right_hash, shift) as usize;
        if left_index != right_index {
            slots[left_index] = Some(Arc::clone(left));
            slots[right_index] = Some(Arc::clone(right));
        } else {
            slots[left_index] =
                combine_champ_hash_nodes(left, right, shift + BITS_PER_LEVEL, operation);
        }
        return build_champ_logical_node(slots, left, shift);
    }

    let left_entries = champ_owned_entries(left);
    let right_entries = champ_owned_entries(right);
    let mut result = Vec::new();
    for (left_key, left_value) in &left_entries {
        let right_entry = right_entries.iter().find(|(key, _)| key == left_key);
        match operation {
            ChampOperation::Union => result.push(match right_entry {
                Some((_, right_value)) if right_value != left_value => {
                    (left_key.clone(), right_value.clone())
                }
                _ => (left_key.clone(), left_value.clone()),
            }),
            ChampOperation::Intersect if right_entry.is_some() => {
                result.push((left_key.clone(), left_value.clone()));
            }
            ChampOperation::Except | ChampOperation::SymmetricExcept if right_entry.is_none() => {
                result.push((left_key.clone(), left_value.clone()));
            }
            _ => {}
        }
    }
    if matches!(
        operation,
        ChampOperation::Union | ChampOperation::SymmetricExcept
    ) {
        for (key, value) in &right_entries {
            if !left_entries.iter().any(|(left_key, _)| left_key == key) {
                result.push((key.clone(), value.clone()));
            }
        }
    }
    if result == left_entries {
        return Some(Arc::clone(left));
    }
    match result.len() {
        0 => None,
        1 => {
            let (key, value) = result.pop().unwrap();
            Some(Arc::new(Node::Leaf {
                hash: left_hash,
                key,
                value,
            }))
        }
        _ => Some(Arc::new(Node::Collision {
            hash: left_hash,
            entries: Arc::from(result),
        })),
    }
}

fn node_hash<K, V>(node: &Node<K, V>) -> Option<u32> {
    match node {
        Node::Leaf { hash, .. } | Node::Collision { hash, .. } => Some(*hash),
        Node::Branch { .. } => None,
    }
}

fn champ_owned_entries<K: Clone, V: Clone>(node: &Node<K, V>) -> Vec<(K, V)> {
    match node {
        Node::Leaf { key, value, .. } => vec![(key.clone(), value.clone())],
        Node::Collision { entries, .. } => entries.to_vec(),
        Node::Branch { .. } => unreachable!("branch is not an equal-hash run"),
    }
}

fn champ_logical_slot<K, V>(
    node: &Arc<Node<K, V>>,
    index: u32,
    shift: u32,
) -> Option<Arc<Node<K, V>>>
where
    K: Clone,
    V: Clone,
{
    match node.as_ref() {
        Node::Leaf { hash, .. } | Node::Collision { hash, .. } => {
            (hash_fragment(*hash, shift) == index).then(|| Arc::clone(node))
        }
        Node::Branch {
            data_map,
            node_map,
            data,
            children,
            ..
        } => {
            let bit = bit_position(index);
            if data_map & bit != 0 {
                let (hash, key, value) = &data[sparse_index(*data_map, bit)];
                Some(Arc::new(Node::Leaf {
                    hash: *hash,
                    key: key.clone(),
                    value: value.clone(),
                }))
            } else if node_map & bit != 0 {
                Some(Arc::clone(&children[sparse_index(*node_map, bit)]))
            } else {
                None
            }
        }
    }
}

fn build_champ_logical_node<K, V>(
    slots: Vec<Option<Arc<Node<K, V>>>>,
    original_left: &Arc<Node<K, V>>,
    shift: u32,
) -> Option<Arc<Node<K, V>>>
where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    if champ_logical_slots_match(&slots, original_left, shift) {
        return Some(Arc::clone(original_left));
    }
    let mut data_map = 0_u32;
    let mut node_map = 0_u32;
    let mut data = Vec::new();
    let mut children = Vec::new();
    for (index, slot) in slots.into_iter().enumerate() {
        let Some(node) = slot else { continue };
        let bit = bit_position(index as u32);
        match node.as_ref() {
            Node::Leaf { hash, key, value } => {
                data_map |= bit;
                data.push((*hash, key.clone(), value.clone()));
            }
            _ => {
                node_map |= bit;
                children.push(node);
            }
        }
    }
    normalize_branch(data_map, node_map, data, children)
}

fn champ_logical_slots_match<K, V>(
    slots: &[Option<Arc<Node<K, V>>>],
    original: &Arc<Node<K, V>>,
    shift: u32,
) -> bool
where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    slots.iter().enumerate().all(|(index, actual)| {
        let expected = champ_logical_slot(original, index as u32, shift);
        match (expected.as_ref(), actual.as_ref()) {
            (None, None) => true,
            (Some(left), Some(right)) if Arc::ptr_eq(left, right) => true,
            (Some(left), Some(right)) => match (left.as_ref(), right.as_ref()) {
                (
                    Node::Leaf {
                        hash: lh,
                        key: lk,
                        value: lv,
                    },
                    Node::Leaf {
                        hash: rh,
                        key: rk,
                        value: rv,
                    },
                ) => lh == rh && lk == rk && lv == rv,
                _ => false,
            },
            _ => false,
        }
    })
}

fn get_in_node<'a, K, V>(
    node: &'a Node<K, V>,
    hash: u32,
    key: &K,
    shift: u32,
) -> Option<(&'a K, &'a V)>
where
    K: Eq,
{
    match node {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value,
        } => (*leaf_hash == hash && leaf_key == key).then_some((leaf_key, value)),
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash != hash {
                return None;
            }

            entries
                .iter()
                .find(|(entry_key, _)| entry_key == key)
                .map(|(entry_key, value)| (entry_key, value))
        }
        Node::Branch {
            data_map,
            node_map,
            data,
            children,
            ..
        } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if data_map & bit != 0 {
                let (_, entry_key, value) = &data[sparse_index(*data_map, bit)];
                return (entry_key == key).then_some((entry_key, value));
            }
            if node_map & bit != 0 {
                return get_in_node(
                    &children[sparse_index(*node_map, bit)],
                    hash,
                    key,
                    shift + BITS_PER_LEVEL,
                );
            }
            None
        }
    }
}

fn factory_update_node<K, V, Selector>(
    node: &Arc<Node<K, V>>,
    hash: u32,
    key: K,
    shift: u32,
    selector: &mut Selector,
) -> FactoryUpdateNodeResult<K, V>
where
    K: Eq + Clone,
    V: Clone,
    Selector: FactorySelector<K, V>,
{
    match node.as_ref() {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value: leaf_value,
        } => {
            if *leaf_hash == hash && leaf_key == &key {
                return match selector.select_present(&key, leaf_value) {
                    PresentFactorySelection::Unchanged(value) => FactoryUpdateNodeResult {
                        node: Arc::clone(node),
                        value,
                        added: false,
                        changed: false,
                    },
                    PresentFactorySelection::Changed(value) => {
                        let result_value = value.clone();
                        FactoryUpdateNodeResult {
                            node: Arc::new(Node::Leaf {
                                hash,
                                key: leaf_key.clone(),
                                value,
                            }),
                            value: result_value,
                            added: false,
                            changed: true,
                        }
                    }
                };
            }

            let value = selector.select_absent(&key);
            let result_value = value.clone();
            let new_leaf = Arc::new(Node::Leaf { hash, key, value });
            let next = if *leaf_hash == hash {
                Arc::new(Node::Collision {
                    hash,
                    entries: Arc::from(vec![
                        (leaf_key.clone(), leaf_value.clone()),
                        leaf_entry(new_leaf),
                    ]),
                })
            } else {
                merge_two(Arc::clone(node), *leaf_hash, new_leaf, hash, shift)
            };
            FactoryUpdateNodeResult {
                node: next,
                value: result_value,
                added: true,
                changed: true,
            }
        }
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash == hash {
                if let Some(index) = entries.iter().position(|(entry_key, _)| entry_key == &key) {
                    return match selector.select_present(&key, &entries[index].1) {
                        PresentFactorySelection::Unchanged(value) => FactoryUpdateNodeResult {
                            node: Arc::clone(node),
                            value,
                            added: false,
                            changed: false,
                        },
                        PresentFactorySelection::Changed(value) => {
                            let result_value = value.clone();
                            let mut next = entries.to_vec();
                            next[index] = (next[index].0.clone(), value);
                            FactoryUpdateNodeResult {
                                node: Arc::new(Node::Collision {
                                    hash,
                                    entries: Arc::from(next),
                                }),
                                value: result_value,
                                added: false,
                                changed: true,
                            }
                        }
                    };
                }

                let value = selector.select_absent(&key);
                let result_value = value.clone();
                let mut next = entries.to_vec();
                next.push((key, value));
                return FactoryUpdateNodeResult {
                    node: Arc::new(Node::Collision {
                        hash,
                        entries: Arc::from(next),
                    }),
                    value: result_value,
                    added: true,
                    changed: true,
                };
            }

            let value = selector.select_absent(&key);
            let result_value = value.clone();
            let new_leaf = Arc::new(Node::Leaf { hash, key, value });
            FactoryUpdateNodeResult {
                node: merge_two(Arc::clone(node), *bucket_hash, new_leaf, hash, shift),
                value: result_value,
                added: true,
                changed: true,
            }
        }
        Node::Branch {
            data_map,
            node_map,
            data,
            children,
            ..
        } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if data_map & bit != 0 {
                let index = sparse_index(*data_map, bit);
                let (leaf_hash, leaf_key, leaf_value) = &data[index];
                if *leaf_hash == hash && leaf_key == &key {
                    return match selector.select_present(&key, leaf_value) {
                        PresentFactorySelection::Unchanged(value) => FactoryUpdateNodeResult {
                            node: Arc::clone(node),
                            value,
                            added: false,
                            changed: false,
                        },
                        PresentFactorySelection::Changed(value) => {
                            let result_value = value.clone();
                            let mut next_data = data.to_vec();
                            next_data[index] = (hash, leaf_key.clone(), value);
                            FactoryUpdateNodeResult {
                                node: make_branch(
                                    *data_map,
                                    *node_map,
                                    Arc::from(next_data),
                                    Arc::clone(children),
                                ),
                                value: result_value,
                                added: false,
                                changed: true,
                            }
                        }
                    };
                }

                let value = selector.select_absent(&key);
                let result_value = value.clone();
                let child = merge_two(
                    Arc::new(Node::Leaf {
                        hash: *leaf_hash,
                        key: leaf_key.clone(),
                        value: leaf_value.clone(),
                    }),
                    *leaf_hash,
                    Arc::new(Node::Leaf { hash, key, value }),
                    hash,
                    shift + BITS_PER_LEVEL,
                );
                let mut next_data = data.to_vec();
                next_data.remove(index);
                let mut next_children = children.to_vec();
                next_children.insert(sparse_index(*node_map, bit), child);
                return FactoryUpdateNodeResult {
                    node: make_branch(
                        data_map & !bit,
                        node_map | bit,
                        Arc::from(next_data),
                        Arc::from(next_children),
                    ),
                    value: result_value,
                    added: true,
                    changed: true,
                };
            }

            if node_map & bit == 0 {
                let value = selector.select_absent(&key);
                let result_value = value.clone();
                let mut next_data = data.to_vec();
                next_data.insert(sparse_index(*data_map, bit), (hash, key, value));
                return FactoryUpdateNodeResult {
                    node: make_branch(
                        data_map | bit,
                        *node_map,
                        Arc::from(next_data),
                        Arc::clone(children),
                    ),
                    value: result_value,
                    added: true,
                    changed: true,
                };
            }

            let index = sparse_index(*node_map, bit);
            let child_result = factory_update_node(
                &children[index],
                hash,
                key,
                shift + BITS_PER_LEVEL,
                selector,
            );
            if !child_result.changed {
                return FactoryUpdateNodeResult {
                    node: Arc::clone(node),
                    value: child_result.value,
                    added: false,
                    changed: false,
                };
            }

            let mut next_children = children.to_vec();
            next_children[index] = child_result.node;
            FactoryUpdateNodeResult {
                node: make_branch(
                    *data_map,
                    *node_map,
                    Arc::clone(data),
                    Arc::from(next_children),
                ),
                value: child_result.value,
                added: child_result.added,
                changed: true,
            }
        }
    }
}

fn insert_node<K, V>(
    node: &Arc<Node<K, V>>,
    hash: u32,
    key: K,
    value: V,
    shift: u32,
    overwrite: bool,
) -> InsertResult<K, V>
where
    K: Eq + Clone,
    V: Clone + PartialEq,
{
    match node.as_ref() {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value: leaf_value,
        } => {
            if *leaf_hash == hash && leaf_key == &key {
                if !overwrite {
                    return InsertResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                        duplicate: true,
                    };
                }

                if leaf_value == &value {
                    return InsertResult {
                        node: Arc::clone(node),
                        added: false,
                        changed: false,
                        duplicate: false,
                    };
                }

                return InsertResult {
                    node: Arc::new(Node::Leaf {
                        hash,
                        key: leaf_key.clone(),
                        value,
                    }),
                    added: false,
                    changed: true,
                    duplicate: false,
                };
            }

            let new_leaf = Arc::new(Node::Leaf { hash, key, value });
            if *leaf_hash == hash {
                return InsertResult {
                    node: Arc::new(Node::Collision {
                        hash,
                        entries: Arc::from(vec![
                            (leaf_key.clone(), leaf_value.clone()),
                            leaf_entry(new_leaf),
                        ]),
                    }),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            InsertResult {
                node: merge_two(Arc::clone(node), *leaf_hash, new_leaf, hash, shift),
                added: true,
                changed: true,
                duplicate: false,
            }
        }
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash == hash {
                if let Some(index) = entries.iter().position(|(entry_key, _)| entry_key == &key) {
                    if !overwrite {
                        return InsertResult {
                            node: Arc::clone(node),
                            added: false,
                            changed: false,
                            duplicate: true,
                        };
                    }

                    if entries[index].1 == value {
                        return InsertResult {
                            node: Arc::clone(node),
                            added: false,
                            changed: false,
                            duplicate: false,
                        };
                    }

                    let mut next = entries.to_vec();
                    next[index] = (next[index].0.clone(), value);
                    return InsertResult {
                        node: Arc::new(Node::Collision {
                            hash,
                            entries: Arc::from(next),
                        }),
                        added: false,
                        changed: true,
                        duplicate: false,
                    };
                }

                let mut next = entries.to_vec();
                next.push((key, value));
                return InsertResult {
                    node: Arc::new(Node::Collision {
                        hash,
                        entries: Arc::from(next),
                    }),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            let new_leaf = Arc::new(Node::Leaf { hash, key, value });
            InsertResult {
                node: merge_two(Arc::clone(node), *bucket_hash, new_leaf, hash, shift),
                added: true,
                changed: true,
                duplicate: false,
            }
        }
        Node::Branch {
            data_map,
            node_map,
            data,
            children,
            ..
        } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if data_map & bit != 0 {
                let index = sparse_index(*data_map, bit);
                let (leaf_hash, leaf_key, leaf_value) = &data[index];
                if *leaf_hash == hash && leaf_key == &key {
                    if !overwrite {
                        return InsertResult {
                            node: Arc::clone(node),
                            added: false,
                            changed: false,
                            duplicate: true,
                        };
                    }
                    if leaf_value == &value {
                        return InsertResult {
                            node: Arc::clone(node),
                            added: false,
                            changed: false,
                            duplicate: false,
                        };
                    }
                    let mut next_data = data.to_vec();
                    next_data[index] = (hash, leaf_key.clone(), value);
                    return InsertResult {
                        node: make_branch(
                            *data_map,
                            *node_map,
                            Arc::from(next_data),
                            Arc::clone(children),
                        ),
                        added: false,
                        changed: true,
                        duplicate: false,
                    };
                }

                let child = merge_two(
                    Arc::new(Node::Leaf {
                        hash: *leaf_hash,
                        key: leaf_key.clone(),
                        value: leaf_value.clone(),
                    }),
                    *leaf_hash,
                    Arc::new(Node::Leaf { hash, key, value }),
                    hash,
                    shift + BITS_PER_LEVEL,
                );
                let mut next_data = data.to_vec();
                next_data.remove(index);
                let mut next_children = children.to_vec();
                next_children.insert(sparse_index(*node_map, bit), child);
                return InsertResult {
                    node: make_branch(
                        data_map & !bit,
                        node_map | bit,
                        Arc::from(next_data),
                        Arc::from(next_children),
                    ),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            if node_map & bit == 0 {
                let mut next_data = data.to_vec();
                next_data.insert(sparse_index(*data_map, bit), (hash, key, value));
                return InsertResult {
                    node: make_branch(
                        data_map | bit,
                        *node_map,
                        Arc::from(next_data),
                        Arc::clone(children),
                    ),
                    added: true,
                    changed: true,
                    duplicate: false,
                };
            }

            let index = sparse_index(*node_map, bit);
            let child_result = insert_node(
                &children[index],
                hash,
                key,
                value,
                shift + BITS_PER_LEVEL,
                overwrite,
            );
            if child_result.duplicate || !child_result.changed {
                return InsertResult {
                    node: Arc::clone(node),
                    added: false,
                    changed: false,
                    duplicate: child_result.duplicate,
                };
            }

            let mut next_children = children.to_vec();
            next_children[index] = child_result.node;
            InsertResult {
                node: make_branch(
                    *data_map,
                    *node_map,
                    Arc::clone(data),
                    Arc::from(next_children),
                ),
                added: child_result.added,
                changed: true,
                duplicate: false,
            }
        }
    }
}

fn remove_node<K, V>(node: &Arc<Node<K, V>>, hash: u32, key: &K, shift: u32) -> RemoveResult<K, V>
where
    K: Eq + Clone,
    V: Clone,
{
    match node.as_ref() {
        Node::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value,
        } => {
            if *leaf_hash == hash && leaf_key == key {
                RemoveResult {
                    node: None,
                    removed: Some((leaf_key.clone(), value.clone())),
                    changed: true,
                }
            } else {
                RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                }
            }
        }
        Node::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash != hash {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            }

            let Some(index) = entries.iter().position(|(entry_key, _)| entry_key == key) else {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            };

            let removed = entries[index].clone();
            let mut next = entries.to_vec();
            next.remove(index);
            let node = match next.as_slice() {
                [] => None,
                [(key, value)] => Some(Arc::new(Node::Leaf {
                    hash,
                    key: key.clone(),
                    value: value.clone(),
                })),
                _ => Some(Arc::new(Node::Collision {
                    hash,
                    entries: Arc::from(next),
                })),
            };

            RemoveResult {
                node,
                removed: Some(removed),
                changed: true,
            }
        }
        Node::Branch {
            data_map,
            node_map,
            data,
            children,
            ..
        } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if data_map & bit != 0 {
                let index = sparse_index(*data_map, bit);
                let (leaf_hash, leaf_key, leaf_value) = &data[index];
                if *leaf_hash != hash || leaf_key != key {
                    return RemoveResult {
                        node: Some(Arc::clone(node)),
                        removed: None,
                        changed: false,
                    };
                }
                let mut next_data = data.to_vec();
                next_data.remove(index);
                return RemoveResult {
                    node: normalize_branch(
                        data_map & !bit,
                        *node_map,
                        next_data,
                        children.to_vec(),
                    ),
                    removed: Some((leaf_key.clone(), leaf_value.clone())),
                    changed: true,
                };
            }
            if node_map & bit == 0 {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            }

            let index = sparse_index(*node_map, bit);
            let child_result = remove_node(&children[index], hash, key, shift + BITS_PER_LEVEL);
            if !child_result.changed {
                return RemoveResult {
                    node: Some(Arc::clone(node)),
                    removed: None,
                    changed: false,
                };
            }

            let mut next_children = children.to_vec();
            let mut next_data = data.to_vec();
            let mut next_data_map = *data_map;
            let mut next_node_map = *node_map;
            match child_result.node {
                Some(child) => {
                    if let Some(entry) = singleton_entry(&child) {
                        next_children.remove(index);
                        next_node_map &= !bit;
                        next_data.insert(sparse_index(next_data_map, bit), entry);
                        next_data_map |= bit;
                    } else {
                        next_children[index] = child;
                    }
                }
                None => {
                    next_children.remove(index);
                    next_node_map &= !bit;
                }
            }

            RemoveResult {
                node: normalize_branch(next_data_map, next_node_map, next_data, next_children),
                removed: child_result.removed,
                changed: true,
            }
        }
    }
}

fn merge_two<K, V>(
    left: Arc<Node<K, V>>,
    left_hash: u32,
    right: Arc<Node<K, V>>,
    right_hash: u32,
    shift: u32,
) -> Arc<Node<K, V>>
where
    K: Clone,
    V: Clone,
{
    debug_assert!(
        shift <= MAX_BRANCH_SHIFT,
        "branch nodes only exist at shifts 0..=30 for 32-bit hashes",
    );
    if left_hash == right_hash {
        let mut entries = Vec::new();
        collect_owned_entries(&left, &mut entries);
        collect_owned_entries(&right, &mut entries);
        return Arc::new(Node::Collision {
            hash: left_hash,
            entries: Arc::from(entries),
        });
    }

    let left_fragment = hash_fragment(left_hash, shift);
    let right_fragment = hash_fragment(right_hash, shift);
    let left_bit = bit_position(left_fragment);
    let right_bit = bit_position(right_fragment);

    if left_bit == right_bit {
        let child = merge_two(left, left_hash, right, right_hash, shift + BITS_PER_LEVEL);
        return make_branch(0, left_bit, Arc::from([]), Arc::from(vec![child]));
    }

    let mut data = Vec::new();
    let mut children = Vec::new();
    let mut data_map = 0;
    let mut node_map = 0;
    let mut add = |bit: u32, node: Arc<Node<K, V>>| match node.as_ref() {
        Node::Leaf { hash, key, value } => {
            data_map |= bit;
            data.push((bit, *hash, key.clone(), value.clone()));
        }
        _ => {
            node_map |= bit;
            children.push((bit, node));
        }
    };
    add(left_bit, left);
    add(right_bit, right);
    data.sort_by_key(|(bit, ..)| *bit);
    children.sort_by_key(|(bit, _)| *bit);
    make_branch(
        data_map,
        node_map,
        Arc::from(
            data.into_iter()
                .map(|(_, hash, key, value)| (hash, key, value))
                .collect::<Vec<_>>(),
        ),
        Arc::from(
            children
                .into_iter()
                .map(|(_, node)| node)
                .collect::<Vec<_>>(),
        ),
    )
}

fn collect_owned_entries<K, V>(node: &Node<K, V>, entries: &mut Vec<(K, V)>)
where
    K: Clone,
    V: Clone,
{
    match node {
        Node::Leaf { key, value, .. } => entries.push((key.clone(), value.clone())),
        Node::Collision {
            entries: bucket, ..
        } => entries.extend(bucket.iter().cloned()),
        Node::Branch { data, children, .. } => {
            entries.extend(
                data.iter()
                    .map(|(_, key, value)| (key.clone(), value.clone())),
            );
            for child in children.iter() {
                collect_owned_entries(child, entries);
            }
        }
    }
}

fn singleton_entry<K, V>(node: &Node<K, V>) -> Option<(u32, K, V)>
where
    K: Clone,
    V: Clone,
{
    match node {
        Node::Leaf { hash, key, value } => Some((*hash, key.clone(), value.clone())),
        Node::Collision { hash, entries } if entries.len() == 1 => {
            Some((*hash, entries[0].0.clone(), entries[0].1.clone()))
        }
        Node::Branch { data, children, .. } if data.len() == 1 && children.is_empty() => {
            Some(data[0].clone())
        }
        _ => None,
    }
}

fn normalize_branch<K, V>(
    data_map: u32,
    node_map: u32,
    data: Vec<(u32, K, V)>,
    children: Vec<Arc<Node<K, V>>>,
) -> Option<Arc<Node<K, V>>>
where
    K: Clone,
    V: Clone,
{
    if data.is_empty() && children.is_empty() {
        None
    } else if data.len() == 1 && children.is_empty() {
        let (hash, key, value) = data.into_iter().next().unwrap();
        Some(Arc::new(Node::Leaf { hash, key, value }))
    } else if data.is_empty()
        && children.len() == 1
        && !matches!(children[0].as_ref(), Node::Branch { .. })
    {
        Some(Arc::clone(&children[0]))
    } else {
        Some(make_branch(
            data_map,
            node_map,
            Arc::from(data),
            Arc::from(children),
        ))
    }
}

/// Adds or replaces `key` in an unpublished mutable subtree, returning whether
/// a new entry was added. Mirrors `insert_node` with `overwrite = true`,
/// mutating in place instead of path-copying.
fn set_in_mutable<K, V>(
    node: &mut MutableNode<K, V>,
    hash: u32,
    key: K,
    value: V,
    shift: u32,
) -> bool
where
    K: Eq,
    V: PartialEq,
{
    match node {
        MutableNode::Leaf {
            hash: leaf_hash,
            key: leaf_key,
            value: leaf_value,
        } => {
            if *leaf_hash == hash && *leaf_key == key {
                if *leaf_value != value {
                    *leaf_value = value;
                }
                return false;
            }

            let existing = take_mutable(node);
            *node = merge_mutable(existing, MutableNode::Leaf { hash, key, value }, shift);
            true
        }
        MutableNode::Collision {
            hash: bucket_hash,
            entries,
        } => {
            if *bucket_hash != hash {
                let existing = take_mutable(node);
                *node = merge_mutable(existing, MutableNode::Leaf { hash, key, value }, shift);
                return true;
            }

            if let Some(index) = entries.iter().position(|(entry_key, _)| *entry_key == key) {
                if entries[index].1 != value {
                    entries[index].1 = value;
                }
                return false;
            }

            entries.push((key, value));
            true
        }
        MutableNode::Branch {
            data_map,
            node_map,
            data,
            children,
        } => {
            let bit = bit_position(hash_fragment(hash, shift));
            if *data_map & bit != 0 {
                let index = sparse_index(*data_map, bit);
                if data[index].0 == hash && data[index].1 == key {
                    if data[index].2 != value {
                        data[index].2 = value;
                    }
                    return false;
                }
                let (old_hash, old_key, old_value) = data.remove(index);
                let child = merge_mutable(
                    MutableNode::Leaf {
                        hash: old_hash,
                        key: old_key,
                        value: old_value,
                    },
                    MutableNode::Leaf { hash, key, value },
                    shift + BITS_PER_LEVEL,
                );
                *data_map &= !bit;
                children.insert(sparse_index(*node_map, bit), child);
                *node_map |= bit;
                return true;
            }
            if *node_map & bit != 0 {
                return set_in_mutable(
                    &mut children[sparse_index(*node_map, bit)],
                    hash,
                    key,
                    value,
                    shift + BITS_PER_LEVEL,
                );
            }
            data.insert(sparse_index(*data_map, bit), (hash, key, value));
            *data_map |= bit;
            true
        }
    }
}

/// Moves a mutable node out of its slot, leaving a placeholder the caller
/// immediately overwrites.
fn take_mutable<K, V>(node: &mut MutableNode<K, V>) -> MutableNode<K, V> {
    std::mem::replace(
        node,
        MutableNode::Branch {
            data_map: 0,
            node_map: 0,
            data: Vec::new(),
            children: Vec::new(),
        },
    )
}

/// Combines two hash-carrying mutable nodes with different key positions,
/// mirroring `merge_two` for the unpublished tree.
fn merge_mutable<K, V>(
    left: MutableNode<K, V>,
    right: MutableNode<K, V>,
    shift: u32,
) -> MutableNode<K, V> {
    debug_assert!(
        shift <= MAX_BRANCH_SHIFT,
        "branch nodes only exist at shifts 0..=30 for 32-bit hashes",
    );
    let left_hash = mutable_hash(&left);
    let right_hash = mutable_hash(&right);
    if left_hash == right_hash {
        let mut entries = Vec::with_capacity(2);
        push_mutable_entries(left, &mut entries);
        push_mutable_entries(right, &mut entries);
        return MutableNode::Collision {
            hash: left_hash,
            entries,
        };
    }

    let left_fragment = hash_fragment(left_hash, shift);
    let right_fragment = hash_fragment(right_hash, shift);
    let left_bit = bit_position(left_fragment);
    let right_bit = bit_position(right_fragment);
    if left_bit == right_bit {
        let child = merge_mutable(left, right, shift + BITS_PER_LEVEL);
        return MutableNode::Branch {
            data_map: 0,
            node_map: left_bit,
            data: Vec::new(),
            children: vec![child],
        };
    }

    let mut data_map = 0;
    let mut node_map = 0;
    let mut data = Vec::new();
    let mut children = Vec::new();
    push_mutable_slot(
        left_bit,
        left,
        &mut data_map,
        &mut node_map,
        &mut data,
        &mut children,
    );
    push_mutable_slot(
        right_bit,
        right,
        &mut data_map,
        &mut node_map,
        &mut data,
        &mut children,
    );
    data.sort_by_key(|(bit, ..)| *bit);
    children.sort_by_key(|(bit, _)| *bit);
    MutableNode::Branch {
        data_map,
        node_map,
        data: data
            .into_iter()
            .map(|(_, hash, key, value)| (hash, key, value))
            .collect(),
        children: children.into_iter().map(|(_, node)| node).collect(),
    }
}

fn push_mutable_slot<K, V>(
    bit: u32,
    node: MutableNode<K, V>,
    data_map: &mut u32,
    node_map: &mut u32,
    data: &mut Vec<(u32, u32, K, V)>,
    children: &mut Vec<(u32, MutableNode<K, V>)>,
) {
    match node {
        MutableNode::Leaf { hash, key, value } => {
            *data_map |= bit;
            data.push((bit, hash, key, value));
        }
        other => {
            *node_map |= bit;
            children.push((bit, other));
        }
    }
}

fn mutable_hash<K, V>(node: &MutableNode<K, V>) -> u32 {
    match node {
        MutableNode::Leaf { hash, .. } | MutableNode::Collision { hash, .. } => *hash,
        MutableNode::Branch { .. } => unreachable!("only hash-carrying nodes merge"),
    }
}

fn push_mutable_entries<K, V>(node: MutableNode<K, V>, entries: &mut Vec<(K, V)>) {
    match node {
        MutableNode::Leaf { key, value, .. } => entries.push((key, value)),
        MutableNode::Collision {
            entries: bucket, ..
        } => entries.extend(bucket),
        MutableNode::Branch { .. } => unreachable!("only hash-carrying nodes merge"),
    }
}

/// Freezes an owned mutable subtree into persistent nodes without cloning.
fn freeze_owned<K, V>(node: MutableNode<K, V>) -> Arc<Node<K, V>> {
    Arc::new(match node {
        MutableNode::Leaf { hash, key, value } => Node::Leaf { hash, key, value },
        MutableNode::Collision { hash, entries } => Node::Collision {
            hash,
            entries: Arc::from(entries),
        },
        MutableNode::Branch {
            data_map,
            node_map,
            data,
            children,
        } => {
            let children = Arc::<[Arc<Node<K, V>>]>::from(
                children.into_iter().map(freeze_owned).collect::<Vec<_>>(),
            );
            let count = data.len()
                + children
                    .iter()
                    .map(|child| child.entry_count())
                    .sum::<usize>();
            Node::Branch {
                data_map,
                node_map,
                data: Arc::from(data),
                children,
                count,
            }
        }
    })
}

/// Freezes a borrowed mutable subtree into detached persistent nodes; the
/// builder's storage stays fully mutable afterwards.
fn freeze_cloned<K, V>(node: &MutableNode<K, V>) -> Arc<Node<K, V>>
where
    K: Clone,
    V: Clone,
{
    Arc::new(match node {
        MutableNode::Leaf { hash, key, value } => Node::Leaf {
            hash: *hash,
            key: key.clone(),
            value: value.clone(),
        },
        MutableNode::Collision { hash, entries } => Node::Collision {
            hash: *hash,
            entries: Arc::from(entries.clone()),
        },
        MutableNode::Branch {
            data_map,
            node_map,
            data,
            children,
        } => {
            let children = Arc::<[Arc<Node<K, V>>]>::from(
                children.iter().map(freeze_cloned).collect::<Vec<_>>(),
            );
            let count = data.len()
                + children
                    .iter()
                    .map(|child| child.entry_count())
                    .sum::<usize>();
            Node::Branch {
                data_map: *data_map,
                node_map: *node_map,
                data: Arc::from(data.clone()),
                children,
                count,
            }
        }
    })
}

fn leaf_entry<K, V>(node: Arc<Node<K, V>>) -> (K, V) {
    match Arc::try_unwrap(node) {
        Ok(Node::Leaf { key, value, .. }) => (key, value),
        _ => unreachable!("newly allocated leaf must unwrap"),
    }
}

fn hash_fragment(hash: u32, shift: u32) -> u32 {
    (hash >> shift) & BRANCH_MASK
}

fn bit_position(fragment: u32) -> u32 {
    1_u32 << fragment
}

fn sparse_index(bitmap: u32, bit: u32) -> usize {
    (bitmap & (bit - 1)).count_ones() as usize
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{BuildHasherDefault, Hasher};
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::thread;

    #[derive(Default)]
    struct ConstantHasher;

    impl Hasher for ConstantHasher {
        fn finish(&self) -> u64 {
            0
        }

        fn write(&mut self, _bytes: &[u8]) {}
    }

    type ConstantState = BuildHasherDefault<ConstantHasher>;

    #[derive(Clone, Default)]
    struct CountingState {
        builds: Arc<AtomicUsize>,
    }

    struct CountingHasher(DefaultHasher);

    impl BuildHasher for CountingState {
        type Hasher = CountingHasher;

        fn build_hasher(&self) -> Self::Hasher {
            self.builds.fetch_add(1, Ordering::Relaxed);
            CountingHasher(DefaultHasher::new())
        }
    }

    impl Hasher for CountingHasher {
        fn finish(&self) -> u64 {
            self.0.finish()
        }

        fn write(&mut self, bytes: &[u8]) {
            self.0.write(bytes);
        }
    }

    #[derive(Clone, Default)]
    struct RoutedState {
        builds: Arc<AtomicUsize>,
    }

    #[derive(Default)]
    struct RoutedHasher(u32);

    impl BuildHasher for RoutedState {
        type Hasher = RoutedHasher;

        fn build_hasher(&self) -> Self::Hasher {
            self.builds.fetch_add(1, Ordering::Relaxed);
            RoutedHasher::default()
        }
    }

    impl Hasher for RoutedHasher {
        fn finish(&self) -> u64 {
            u64::from(self.0)
        }

        fn write(&mut self, bytes: &[u8]) {
            let mut value = [0_u8; 4];
            let count = bytes.len().min(value.len());
            value[..count].copy_from_slice(&bytes[..count]);
            self.0 = u32::from_le_bytes(value);
        }

        fn write_u32(&mut self, value: u32) {
            self.0 = value;
        }
    }

    #[derive(Clone, Debug)]
    struct RoutedKey {
        identity: i32,
        hash: u32,
    }

    impl Hash for RoutedKey {
        fn hash<H: Hasher>(&self, state: &mut H) {
            state.write_u32(self.hash);
        }
    }

    impl PartialEq for RoutedKey {
        fn eq(&self, other: &Self) -> bool {
            self.identity == other.identity
        }
    }

    impl Eq for RoutedKey {}

    #[derive(Clone, Debug)]
    struct RoutedValue {
        value: i32,
        comparisons: Arc<AtomicUsize>,
    }

    impl PartialEq for RoutedValue {
        fn eq(&self, other: &Self) -> bool {
            self.comparisons.fetch_add(1, Ordering::Relaxed);
            self.value == other.value
        }
    }

    #[test]
    fn map_updates_preserve_old_versions() {
        let empty = PersistentHashMap::new();
        let one = empty.insert("a", 1);
        let two = one.insert("b", 2);
        let replaced = two.insert("a", 3);

        assert_eq!(empty.get(&"a"), None);
        assert_eq!(one.get(&"a"), Some(&1));
        assert_eq!(two.get(&"a"), Some(&1));
        assert_eq!(replaced.get(&"a"), Some(&3));
        assert_eq!(replaced.get(&"b"), Some(&2));
    }

    #[test]
    fn no_op_update_and_absent_remove_share_roots() {
        let map = PersistentHashMap::new().insert("a", 1).insert("b", 2);
        let same_value = map.insert("a", 1);
        let absent_removed = map.remove(&"c");

        assert!(map.shares_root_with(&same_value));
        assert!(map.shares_root_with(&absent_removed));
    }

    #[test]
    fn add_rejects_duplicates() {
        let map = PersistentHashMap::new().insert("a", 1);
        let (same, added) = map.try_add("a", 2);

        assert!(!added);
        assert!(map.shares_root_with(&same));
        assert!(matches!(map.add("a", 2), Err(DuplicateKey)));
    }

    #[test]
    fn collisions_are_stored_and_removed() {
        let map: PersistentHashMap<i32, i32, ConstantState> =
            PersistentHashMap::with_hasher(ConstantState::default())
                .insert(1, 10)
                .insert(2, 20)
                .insert(3, 30);

        assert_eq!(map.get(&1), Some(&10));
        assert_eq!(map.get(&2), Some(&20));
        assert_eq!(map.get(&3), Some(&30));

        let (removed, value) = map.try_remove(&2).unwrap();
        assert_eq!(value, 20);
        assert_eq!(removed.get(&1), Some(&10));
        assert_eq!(removed.get(&2), None);
        assert_eq!(removed.get(&3), Some(&30));
    }

    #[test]
    fn iterator_streams_entries_with_exact_remaining_count() {
        let map: PersistentHashMap<i32, i32, ConstantState> =
            PersistentHashMap::with_hasher(ConstantState::default())
                .set_items((0..64).map(|value| (value, value * value)));
        let mut iter = map.iter();
        let mut seen = Vec::new();

        for remaining in (1..=map.len()).rev() {
            assert_eq!(iter.size_hint(), (remaining, Some(remaining)));
            assert_eq!(iter.len(), remaining);
            let (key, value) = iter.next().expect("iterator has remaining entries");
            seen.push((*key, *value));
        }

        assert_eq!(iter.size_hint(), (0, Some(0)));
        assert_eq!(iter.len(), 0);
        assert_eq!(iter.next(), None);
        assert_eq!(
            seen,
            (0..64)
                .map(|value| (value, value * value))
                .collect::<Vec<_>>()
        );
    }

    #[test]
    fn create_range_is_last_wins_and_retains_original_key_on_replace() {
        #[derive(Clone, Debug)]
        struct Key(&'static str, usize);

        impl Hash for Key {
            fn hash<H: Hasher>(&self, state: &mut H) {
                self.0.hash(state);
            }
        }

        impl PartialEq for Key {
            fn eq(&self, other: &Self) -> bool {
                self.0 == other.0
            }
        }

        impl Eq for Key {}

        let map = PersistentHashMap::new().set_items([
            (Key("x", 1), 10),
            (Key("x", 2), 20),
            (Key("y", 3), 30),
        ]);

        let (stored_key, value) = map.get_key_value(&Key("x", 99)).unwrap();
        assert_eq!(stored_key.1, 1);
        assert_eq!(*value, 20);
        assert_eq!(map.len(), 2);
    }

    #[test]
    fn bulk_builder_snapshots_stay_detached_from_later_mutations() {
        let mut builder: BulkBuilder<i32, i32, ConstantState> =
            BulkBuilder::with_hasher(ConstantState::default());
        for value in 0..32 {
            builder.set_item(value, value);
        }

        let snapshot = builder.to_immutable();
        for value in 0..64 {
            builder.set_item(value, value * 10);
        }
        let updated = builder.to_immutable();

        assert_eq!(snapshot.len(), 32);
        assert_eq!(updated.len(), 64);
        for value in 0..32 {
            assert_eq!(snapshot.get(&value), Some(&value));
            assert_eq!(updated.get(&value), Some(&(value * 10)));
        }
        assert_eq!(snapshot.get(&40), None);
        assert_eq!(updated.get(&40), Some(&400));
        assert!(!snapshot.shares_root_with(&updated));

        let consumed = builder.into_immutable();
        assert_eq!(consumed, updated);

        let empty: PersistentHashMap<i32, i32, ConstantState> =
            BulkBuilder::with_hasher(ConstantState::default()).into_immutable();
        assert!(empty.is_empty());
    }

    #[test]
    fn bulk_builder_keeps_first_key_and_earlier_equal_value() {
        #[derive(Clone, Debug)]
        struct Key(&'static str, usize);

        impl Hash for Key {
            fn hash<H: Hasher>(&self, state: &mut H) {
                self.0.hash(state);
            }
        }

        impl PartialEq for Key {
            fn eq(&self, other: &Self) -> bool {
                self.0 == other.0
            }
        }

        impl Eq for Key {}

        let mut builder = BulkBuilder::new();
        let first_value = Arc::new(10);
        builder.set_item(Key("x", 1), Arc::clone(&first_value));
        builder.set_item(Key("x", 2), Arc::new(10));

        let map = builder.to_immutable();
        let (stored_key, stored_value) = map.get_key_value(&Key("x", 99)).unwrap();
        assert_eq!(stored_key.1, 1);
        // An equal replacement value keeps the earlier stored value instance.
        assert!(Arc::ptr_eq(stored_value, &first_value));

        builder.set_item(Key("x", 3), Arc::new(20));
        builder.set_item(Key("y", 4), Arc::new(30));
        let map = builder.into_immutable();
        assert_eq!(map.len(), 2);
        let (stored_key, stored_value) = map.get_key_value(&Key("x", 99)).unwrap();
        // The first stored key instance survives replacement; the last value wins.
        assert_eq!(stored_key.1, 1);
        assert_eq!(**stored_value, 20);
    }

    #[test]
    fn bulk_builder_splits_at_the_final_hash_level() {
        #[derive(Default)]
        struct IdentityHasher(u64);

        impl Hasher for IdentityHasher {
            fn finish(&self) -> u64 {
                self.0
            }

            fn write(&mut self, _bytes: &[u8]) {}

            fn write_u32(&mut self, value: u32) {
                self.0 = u64::from(value);
            }
        }

        type IdentityState = BuildHasherDefault<IdentityHasher>;

        // These hashes share every 5-bit fragment below shift 30 and occupy
        // slots 0, 2, and 3 at the final branch level.
        let mut builder: BulkBuilder<u32, u32, IdentityState> = BulkBuilder::default();
        builder.set_item(0, 1);
        builder.set_item(1 << 31, 2);
        builder.set_item(3 << 30, 4);
        builder.set_item(0, 3);

        let map = builder.into_immutable();
        assert_eq!(map.len(), 3);
        assert_eq!(map.get(&0), Some(&3));
        assert_eq!(map.get(&(1 << 31)), Some(&2));
        assert_eq!(map.get(&(3 << 30)), Some(&4));
        let (_, bitmap_nodes, invalid_leaf_children, underfull, invalid_routing) =
            champ_statistics(map.root.as_deref().unwrap(), 0, 0, 0);
        assert!(bitmap_nodes >= 7);
        assert_eq!(
            (invalid_leaf_children, underfull, invalid_routing),
            (0, 0, 0)
        );
    }

    #[test]
    fn bulk_builder_matches_incremental_construction() {
        let pairs: Vec<(u32, u32)> = (0..10_000_u32).map(|i| ((i * 37) % 512, i)).collect();

        // Collision-heavy: every key lands in one bucket.
        let mut builder: BulkBuilder<u32, u32, ConstantState> =
            BulkBuilder::with_hasher(ConstantState::default());
        builder.set_items(pairs.iter().copied());
        let bulk = builder.into_immutable();
        let incremental = PersistentHashMap::with_hasher(ConstantState::default())
            .set_items(pairs.iter().copied());
        assert_eq!(bulk.len(), 512);
        assert_eq!(bulk, incremental);

        // Branch-heavy: default hashing spreads the keys across the trie.
        let mut builder = BulkBuilder::new();
        builder.set_items(pairs.iter().copied());
        let bulk_spread = builder.into_immutable();
        let incremental_spread = PersistentHashMap::new().set_items(pairs.iter().copied());
        assert_eq!(bulk_spread, incremental_spread);

        // from_iter routes through the builder and must agree as well.
        let collected: PersistentHashMap<u32, u32> = pairs.iter().copied().collect();
        assert_eq!(collected, incremental_spread);
    }

    #[test]
    fn transient_map_clean_and_no_op_publication_preserve_exact_shared_state() {
        let state = CountingState::default();
        let retained_value = Arc::new(10);
        let source = PersistentHashMap::with_hasher(state.clone())
            .insert("a", Arc::clone(&retained_value))
            .insert("b", Arc::new(20));

        let mut transient = source.to_transient();
        assert_eq!(transient.len(), 2);
        assert_eq!(transient.get(&"a").map(|value| value.as_ref()), Some(&10));
        assert_eq!(transient.iter().len(), 2);
        assert_eq!(transient.keys().count(), 2);
        assert_eq!(transient.values().count(), 2);

        assert!(!transient.insert("a", Arc::new(10)));
        assert!(!transient.try_add("a", Arc::new(99)));
        assert_eq!(transient.remove(&"missing"), None);
        assert!(Arc::ptr_eq(
            transient.get(&"a").expect("retained entry"),
            &retained_value
        ));

        let published = transient.into_persistent();
        assert!(source.shares_root_with(&published));
        assert!(Arc::ptr_eq(
            &source.policy_identity,
            &published.policy_identity
        ));
        assert!(Arc::ptr_eq(
            &source.hasher().builds,
            &published.hasher().builds
        ));

        let mut empty: TransientHashMap<i32, i32, ConstantState> =
            TransientHashMap::with_hasher(ConstantState::default());
        assert!(!empty.clear());
        assert!(empty.into_persistent().is_empty());
    }

    #[test]
    fn transient_map_preserves_representatives_collisions_and_source_snapshot() {
        #[derive(Clone, Debug)]
        struct Key(&'static str, usize);

        impl Hash for Key {
            fn hash<H: Hasher>(&self, state: &mut H) {
                self.0.hash(state);
            }
        }

        impl PartialEq for Key {
            fn eq(&self, other: &Self) -> bool {
                self.0 == other.0
            }
        }

        impl Eq for Key {}

        let original_value = Arc::new(10);
        let source: PersistentHashMap<Key, Arc<i32>, ConstantState> =
            PersistentHashMap::with_hasher(ConstantState::default())
                .insert(Key("x", 1), Arc::clone(&original_value))
                .insert(Key("z", 9), Arc::new(90));
        let retained_source = source.clone();
        let mut transient = source.to_transient();

        assert!(!transient.insert(Key("x", 2), Arc::new(10)));
        let (stored_key, stored_value) = transient
            .get_key_value(&Key("x", 99))
            .expect("equivalent key");
        assert_eq!(stored_key.1, 1);
        assert!(Arc::ptr_eq(stored_value, &original_value));

        assert!(transient.insert(Key("x", 3), Arc::new(20)));
        assert!(transient.try_add(Key("y", 4), Arc::new(40)));
        assert!(matches!(
            transient.add(Key("y", 5), Arc::new(50)),
            Err(DuplicateKey)
        ));
        let (removed_key, removed_value) = transient
            .remove_entry(&Key("x", 100))
            .expect("retained representative is returned");
        assert_eq!(removed_key.1, 1);
        assert_eq!(*removed_value, 20);

        let published = transient.into_persistent();
        assert_eq!(published.len(), 2);
        assert!(Arc::ptr_eq(
            &source.policy_identity,
            &published.policy_identity
        ));
        assert!(published.get(&Key("x", 0)).is_none());
        assert_eq!(**published.get(&Key("y", 0)).expect("new entry"), 40);
        assert_eq!(**published.get(&Key("z", 0)).expect("retained entry"), 90);

        assert!(retained_source.shares_root_with(&source));
        let (source_key, source_value) = source
            .get_key_value(&Key("x", 0))
            .expect("source remains unchanged");
        assert_eq!(source_key.1, 1);
        assert!(Arc::ptr_eq(source_value, &original_value));
        assert!(source.get(&Key("y", 0)).is_none());
    }

    #[test]
    fn transient_map_deterministic_collision_model_matches_btree_map() {
        let source: PersistentHashMap<i32, i32, ConstantState> =
            PersistentHashMap::with_hasher(ConstantState::default()).set_items([
                (-31, 1),
                (0, 2),
                (31, 3),
            ]);
        let retained_source = source.clone();
        let mut transient = source.to_transient();
        let mut model = BTreeMap::from([(-31, 1), (0, 2), (31, 3)]);
        let mut random = 0x9e37_79b9_7f4a_7c15_u64;

        for step in 0..4_096_u64 {
            random ^= random << 7;
            random ^= random >> 9;
            random ^= random << 8;
            let key = ((random >> 17) % 97) as i32 - 48;
            let value = ((random >> 33) % 2_003) as i32 - 1_001;

            match (random ^ step) % 7 {
                0 | 1 | 2 => {
                    let expected_changed = model.get(&key) != Some(&value);
                    model.insert(key, value);
                    assert_eq!(transient.insert(key, value), expected_changed);
                }
                3 => {
                    let expected_added = !model.contains_key(&key);
                    if expected_added {
                        model.insert(key, value);
                    }
                    assert_eq!(transient.try_add(key, value), expected_added);
                }
                4 => {
                    let expected = model.remove(&key);
                    assert_eq!(transient.remove(&key), expected);
                }
                5 if step % 257 == 0 => {
                    let expected_changed = !model.is_empty();
                    model.clear();
                    assert_eq!(transient.clear(), expected_changed);
                }
                _ => {
                    assert_eq!(transient.get(&key), model.get(&key));
                    assert_eq!(transient.contains_key(&key), model.contains_key(&key));
                }
            }

            assert_eq!(transient.len(), model.len());
            let actual = transient
                .iter()
                .map(|(key, value)| (*key, *value))
                .collect::<BTreeMap<_, _>>();
            assert_eq!(actual, model);
        }

        let published = transient.into_persistent();
        assert_eq!(
            published
                .iter()
                .map(|(key, value)| (*key, *value))
                .collect::<BTreeMap<_, _>>(),
            model
        );
        assert_eq!(retained_source.len(), 3);
        assert_eq!(retained_source.get(&-31), Some(&1));
        assert_eq!(retained_source.get(&0), Some(&2));
        assert_eq!(retained_source.get(&31), Some(&3));
    }

    #[test]
    fn transient_set_is_a_one_way_representative_preserving_facade() {
        #[derive(Clone, Debug)]
        struct Item(&'static str, usize);

        impl Hash for Item {
            fn hash<H: Hasher>(&self, state: &mut H) {
                self.0.hash(state);
            }
        }

        impl PartialEq for Item {
            fn eq(&self, other: &Self) -> bool {
                self.0 == other.0
            }
        }

        impl Eq for Item {}

        let source: PersistentHashSet<Item, ConstantState> =
            PersistentHashSet::with_hasher(ConstantState::default())
                .insert(Item("x", 1))
                .insert(Item("z", 9));
        let mut transient = source.to_transient();

        assert!(!transient.insert(Item("x", 2)));
        assert_eq!(transient.get(&Item("x", 0)).expect("stored item").1, 1);
        assert!(transient.is_subset_of([Item("x", 100), Item("z", 101), Item("extra", 102),]));
        assert!(transient.is_proper_subset_of([
            Item("x", 100),
            Item("z", 101),
            Item("extra", 102),
        ]));
        assert!(transient.is_superset_of([Item("x", 200)]));
        assert!(transient.is_proper_superset_of([Item("x", 200)]));
        assert!(transient.overlaps([Item("missing", 0), Item("z", 200)]));
        assert!(transient.set_equals([Item("z", 300), Item("x", 301), Item("x", 302),]));
        assert!(transient.insert(Item("y", 3)));
        assert!(transient.contains(&Item("y", 0)));
        assert_eq!(transient.iter().count(), 3);
        assert_eq!(transient.remove(&Item("x", 100)).expect("removed").1, 1);

        let published = transient.into_persistent();
        assert!(Arc::ptr_eq(
            &source.map.policy_identity,
            &published.map.policy_identity
        ));
        assert!(published.contains(&Item("y", 0)));
        assert!(!published.contains(&Item("x", 0)));
        assert!(source.contains(&Item("x", 0)));
        assert!(!source.contains(&Item("y", 0)));

        let clean = source.to_transient().into_persistent();
        assert!(source.shares_root_with(&clean));
        assert!(Arc::ptr_eq(
            &source.map.policy_identity,
            &clean.map.policy_identity
        ));

        let mut clearing = published.into_transient();
        assert!(clearing.clear());
        assert!(!clearing.clear());
        assert!(clearing.into_persistent().is_empty());
        assert_eq!(source.len(), 2);
    }

    #[test]
    fn set_algebra_uses_set_membership() {
        let left: PersistentHashSet<_> = [1, 2, 3].into_iter().collect();
        let right = [3, 4, 5];

        let union = left.union(right);
        assert!(union.set_equals([1, 2, 3, 4, 5]));

        let intersection = left.intersect([2, 3, 9]);
        assert!(intersection.set_equals([2, 3]));

        let except = left.except([1, 3]);
        assert!(except.set_equals([2]));

        let symmetric = left.symmetric_except([3, 4]);
        assert!(symmetric.set_equals([1, 2, 4]));
        assert!(intersection.is_proper_subset_of([1, 2, 3, 3]));
        assert!(left.is_proper_superset_of([1, 3, 3]));
        assert!(!left.is_proper_subset_of([1, 2, 3]));
        assert!(!left.is_proper_superset_of([1, 2, 3]));
    }

    #[test]
    fn same_policy_set_algebra_prunes_shared_champ_nodes_without_rehashing() {
        let state = CountingState::default();
        let basis = PersistentHashSet::with_hasher(state.clone()).union(0..256);
        let left = basis.insert(1_000);
        let right = basis.insert(2_000);

        state.builds.store(0, Ordering::Relaxed);
        let self_union = left.union_set(&left);
        let self_intersection = left.intersect_set(&left);
        let self_except = left.except_set(&left);
        let self_symmetric = left.symmetric_except_set(&left);
        let union = left.union_set(&right);
        let intersection = left.intersect_set(&right);
        let except = left.except_set(&right);
        let symmetric = left.symmetric_except_set(&right);
        assert!(left.is_subset_of_set(&union));
        assert!(union.is_superset_of_set(&right));
        assert!(left.overlaps_set(&right));
        assert!(left.set_equals_set(&left));
        let structural_hashes = state.builds.load(Ordering::Relaxed);

        assert_eq!(structural_hashes, 0);
        assert!(self_union.shares_root_with(&left));
        assert!(self_intersection.shares_root_with(&left));
        assert!(self_except.is_empty());
        assert!(self_symmetric.is_empty());
        assert!(union.set_equals((0..256).chain([1_000, 2_000])));
        assert!(intersection.set_equals(0..256));
        assert!(except.set_equals([1_000]));
        assert!(symmetric.set_equals([1_000, 2_000]));
    }

    #[test]
    fn independently_created_policies_use_semantic_set_algebra_fallback() {
        let state = CountingState::default();
        let left = PersistentHashSet::with_hasher(state.clone()).union(0..64);
        let right = PersistentHashSet::with_hasher(state.clone()).union(32..96);

        state.builds.store(0, Ordering::Relaxed);
        let union = left.union_set(&right);
        let fallback_hashes = state.builds.load(Ordering::Relaxed);

        assert!(fallback_hashes > 0);
        assert!(union.set_equals(0..96));
    }

    #[test]
    fn structural_collision_algebra_matches_set_models() {
        let basis = PersistentHashSet::with_hasher(ConstantState::default());
        let mut state = 0x9e37_79b9_u32;
        for _ in 0..200 {
            let mut left_model = std::collections::BTreeSet::new();
            let mut right_model = std::collections::BTreeSet::new();
            for value in -20..=20 {
                state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                if state & 1 != 0 {
                    left_model.insert(value);
                }
                state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
                if state & 1 != 0 {
                    right_model.insert(value);
                }
            }
            let left = basis.union(left_model.iter().copied());
            let right = basis.union(right_model.iter().copied());

            assert!(
                left.union_set(&right)
                    .set_equals(left_model.union(&right_model).copied())
            );
            assert!(
                left.intersect_set(&right)
                    .set_equals(left_model.intersection(&right_model).copied())
            );
            assert!(
                left.except_set(&right)
                    .set_equals(left_model.difference(&right_model).copied())
            );
            assert!(
                left.symmetric_except_set(&right)
                    .set_equals(left_model.symmetric_difference(&right_model).copied())
            );
        }
    }

    #[test]
    fn except_and_symmetric_except_preserve_untouched_roots() {
        let set: PersistentHashSet<i32> = (0..64).collect();

        let except_nothing = set.except(std::iter::empty());
        assert!(set.shares_root_with(&except_nothing));

        let symmetric_nothing = set.symmetric_except(std::iter::empty());
        assert!(set.shares_root_with(&symmetric_nothing));

        let except = set.except([1, 63, 100]);
        assert_eq!(except.len(), 62);
        assert!(!except.contains(&1));
        assert!(!except.contains(&63));

        let symmetric = set.symmetric_except([0, 1, 64, 65]);
        assert_eq!(symmetric.len(), 64);
        assert!(!symmetric.contains(&0));
        assert!(symmetric.contains(&64));
        assert!(symmetric.contains(&65));
    }

    #[test]
    fn set_supports_into_iterator_and_debug_and_equality() {
        let set: PersistentHashSet<i32> = (0..8).collect();
        let mut collected = Vec::new();
        for value in &set {
            collected.push(*value);
        }
        collected.sort_unstable();
        assert_eq!(collected, (0..8).collect::<Vec<_>>());

        let same: PersistentHashSet<i32> = (0..8).rev().collect();
        assert_eq!(set, same);
        let different: PersistentHashSet<i32> = (0..9).collect();
        assert_ne!(set, different);

        let printed = format!("{:?}", PersistentHashSet::new().insert(5));
        assert_eq!(printed, "{5}");
    }

    #[test]
    fn map_supports_index_debug_and_equality() {
        let map = PersistentHashMap::new().insert("a", 1).insert("b", 2);
        assert_eq!(map[&"a"], 1);

        let same = PersistentHashMap::new().insert("b", 2).insert("a", 1);
        assert_eq!(map, same);
        assert_ne!(map, same.insert("a", 3));
        assert_ne!(map, same.remove(&"a"));

        let printed = format!("{:?}", PersistentHashMap::new().insert("a", 1));
        assert_eq!(printed, "{\"a\": 1}");
    }

    #[test]
    fn champ_inlines_payloads_and_classifies_diff() {
        let empty = PersistentHashMap::new();
        let ascending = empty.set_items((0..512).map(|key| (key, key)));
        let descending = empty.set_items((0..512).rev().map(|key| (key, key)));
        assert_eq!(ascending, descending);
        assert!(ascending.diff(&descending).is_empty());

        let (inline, bitmap_nodes, invalid_leaf_children, underfull_bitmap_nodes, invalid_routing) =
            champ_statistics(ascending.root.as_deref().unwrap(), 0, 0, 0);
        assert_eq!(inline, 512);
        assert!(bitmap_nodes > 1);
        assert_eq!(invalid_leaf_children, 0);
        assert_eq!(underfull_bitmap_nodes, 0);
        assert_eq!(invalid_routing, 0);
        assert!(champ_topology_equal(
            ascending.root.as_deref().unwrap(),
            descending.root.as_deref().unwrap()
        ));

        let mut churned = ascending.clone();
        for key in (0..512).step_by(3) {
            churned = churned.remove(&key);
        }
        for key in (0..512).step_by(3).rev() {
            churned = churned.insert(key, key);
        }
        let (_, _, churn_leaf_children, churn_underfull, churn_invalid_routing) =
            champ_statistics(churned.root.as_deref().unwrap(), 0, 0, 0);
        assert_eq!(
            (churn_leaf_children, churn_underfull, churn_invalid_routing),
            (0, 0, 0)
        );
        assert!(champ_topology_equal(
            ascending.root.as_deref().unwrap(),
            churned.root.as_deref().unwrap()
        ));

        let changed = descending.remove(&7).insert(9, -9).insert(1_000, 1_000);
        let diff = ascending.diff(&changed);
        assert_eq!(diff.len(), 3);
        assert!(
            diff.iter()
                .any(|item| matches!(item, MapDifference::Removed { key: 7, .. }))
        );
        assert!(
            diff.iter()
                .any(|item| matches!(item, MapDifference::Changed { key: 9, .. }))
        );
        assert!(
            diff.iter()
                .any(|item| matches!(item, MapDifference::Added { key: 1_000, .. }))
        );
    }

    #[test]
    fn champ_equality_and_diff_prune_shared_descendants() {
        let state = RoutedState::default();
        let comparisons = Arc::new(AtomicUsize::new(0));
        let value = |value| RoutedValue {
            value,
            comparisons: Arc::clone(&comparisons),
        };
        let base = PersistentHashMap::with_hasher(state.clone())
            .insert(
                RoutedKey {
                    identity: 0,
                    hash: 0,
                },
                value(0),
            )
            .insert(
                RoutedKey {
                    identity: 1,
                    hash: 32,
                },
                value(1),
            )
            .insert(
                RoutedKey {
                    identity: 2,
                    hash: 64,
                },
                value(2),
            )
            .insert(
                RoutedKey {
                    identity: 31,
                    hash: 31,
                },
                value(31),
            );
        let changed = base.insert(
            RoutedKey {
                identity: 31,
                hash: 31,
            },
            value(-31),
        );
        let restored = changed.insert(
            RoutedKey {
                identity: 31,
                hash: 31,
            },
            value(31),
        );

        let shared_child = match (base.root.as_deref(), restored.root.as_deref()) {
            (
                Some(Node::Branch {
                    node_map: left_map,
                    children: left_children,
                    ..
                }),
                Some(Node::Branch {
                    node_map: right_map,
                    children: right_children,
                    ..
                }),
            ) => {
                let bit = bit_position(0);
                assert_ne!(left_map & bit, 0);
                assert_ne!(right_map & bit, 0);
                Arc::ptr_eq(
                    &left_children[sparse_index(*left_map, bit)],
                    &right_children[sparse_index(*right_map, bit)],
                )
            }
            _ => false,
        };
        assert!(shared_child);

        state.builds.store(0, Ordering::Relaxed);
        comparisons.store(0, Ordering::Relaxed);
        assert!(base == restored);
        assert_eq!(state.builds.load(Ordering::Relaxed), 0);
        assert_eq!(comparisons.load(Ordering::Relaxed), 1);

        state.builds.store(0, Ordering::Relaxed);
        comparisons.store(0, Ordering::Relaxed);
        let differences = base.diff(&changed);
        assert_eq!(differences.len(), 1);
        assert!(matches!(
            &differences[0],
            MapDifference::Changed { key, before, after }
                if key.identity == 31 && before.value == 31 && after.value == -31
        ));
        assert_eq!(state.builds.load(Ordering::Relaxed), 0);
        assert_eq!(comparisons.load(Ordering::Relaxed), 1);
    }

    fn champ_statistics<K, V>(
        node: &Node<K, V>,
        shift: u32,
        prefix: u32,
        prefix_mask: u32,
    ) -> (usize, usize, usize, usize, usize) {
        match node {
            Node::Leaf { hash, .. } => (1, 0, 0, 0, usize::from(hash & prefix_mask != prefix)),
            Node::Collision { hash, entries } => (
                entries.len(),
                0,
                0,
                0,
                usize::from(hash & prefix_mask != prefix),
            ),
            Node::Branch {
                data_map,
                node_map,
                data,
                children,
                ..
            } => {
                let mut result = (
                    data.len(),
                    1,
                    children
                        .iter()
                        .filter(|child| matches!(child.as_ref(), Node::Leaf { .. }))
                        .count(),
                    usize::from(
                        data.len() + children.len() < 2
                            && !(data.is_empty()
                                && matches!(
                                    children.first().map(Arc::as_ref),
                                    Some(Node::Branch { .. })
                                )),
                    ),
                    0,
                );
                if shift > MAX_BRANCH_SHIFT {
                    result.4 = 1;
                    return result;
                }
                let next_mask = if shift >= 27 {
                    u32::MAX
                } else {
                    (1u32 << (shift + BITS_PER_LEVEL)) - 1
                };
                let mut data_index = 0;
                let mut child_index = 0;
                for slot in 0..BRANCH_FACTOR {
                    let bit = 1u32 << slot;
                    let invalid_terminal_slot = shift == 30 && slot > 3;
                    let slot_prefix = prefix | ((slot as u32) << shift);
                    if data_map & bit != 0 {
                        let routed = !invalid_terminal_slot
                            && data
                                .get(data_index)
                                .is_some_and(|entry| entry.0 & next_mask == slot_prefix);
                        result.4 += usize::from(!routed);
                        data_index += 1;
                    }
                    if node_map & bit != 0 {
                        if invalid_terminal_slot {
                            result.4 += 1;
                        }
                        if let Some(child) = children.get(child_index) {
                            let child_stats = champ_statistics(
                                child,
                                shift + BITS_PER_LEVEL,
                                slot_prefix,
                                next_mask,
                            );
                            result.0 += child_stats.0;
                            result.1 += child_stats.1;
                            result.2 += child_stats.2;
                            result.3 += child_stats.3;
                            result.4 += child_stats.4;
                        } else {
                            result.4 += 1;
                        }
                        child_index += 1;
                    }
                }
                if data_index < data.len() {
                    result.4 += data.len() - data_index;
                }
                if child_index < children.len() {
                    result.4 += children.len() - child_index;
                }
                result
            }
        }
    }

    fn champ_topology_equal<K: Eq, V, V2>(left: &Node<K, V>, right: &Node<K, V2>) -> bool {
        match (left, right) {
            (Node::Leaf { hash: left, .. }, Node::Leaf { hash: right, .. }) => left == right,
            (
                Node::Collision {
                    hash: left_hash,
                    entries: left_entries,
                },
                Node::Collision {
                    hash: right_hash,
                    entries: right_entries,
                },
            ) => {
                left_hash == right_hash
                    && left_entries.len() == right_entries.len()
                    && left_entries.iter().all(|(left_key, _)| {
                        right_entries
                            .iter()
                            .any(|(right_key, _)| left_key == right_key)
                    })
            }
            (
                Node::Branch {
                    data_map: ld,
                    node_map: ln,
                    data: lp,
                    children: lc,
                    count: lcount,
                },
                Node::Branch {
                    data_map: rd,
                    node_map: rn,
                    data: rp,
                    children: rc,
                    count: rcount,
                },
            ) => {
                ld == rd
                    && ln == rn
                    && lcount == rcount
                    && lp
                        .iter()
                        .map(|entry| entry.0)
                        .eq(rp.iter().map(|entry| entry.0))
                    && lc.len() == rc.len()
                    && lc
                        .iter()
                        .zip(rc.iter())
                        .all(|(l, r)| champ_topology_equal(l, r))
            }
            _ => false,
        }
    }

    #[test]
    fn champ_validator_and_topology_comparator_reject_mismatches() {
        let misrouted = Node::Branch {
            data_map: (1u32 << 1) | (1u32 << 2),
            node_map: 0,
            data: Arc::from([(2u32, 1i32, ()), (1u32, 2i32, ())]),
            children: Arc::from([]),
            count: 2,
        };
        let (_, _, _, _, invalid_routing) = champ_statistics(&misrouted, 0, 0, 0);
        assert_eq!(invalid_routing, 2);

        let trailing_data = Node::Branch {
            data_map: 1,
            node_map: 0,
            data: Arc::from([(0u32, 1i32, ()), (1u32, 2i32, ())]),
            children: Arc::from([]),
            count: 2,
        };
        let (_, _, _, _, trailing_routing) = champ_statistics(&trailing_data, 0, 0, 0);
        assert!(trailing_routing > 0);

        let missing_child: Node<i32, ()> = Node::Branch {
            data_map: 0,
            node_map: 1,
            data: Arc::from([]),
            children: Arc::from([]),
            count: 0,
        };
        let (_, _, _, _, missing_routing) = champ_statistics(&missing_child, 0, 0, 0);
        assert!(missing_routing > 0);

        let over_depth = Node::Branch {
            data_map: 1,
            node_map: 0,
            data: Arc::from([(0u32, 1i32, ())]),
            children: Arc::from([]),
            count: 1,
        };
        let (_, _, _, _, depth_routing) = champ_statistics(&over_depth, 35, 0, u32::MAX);
        assert!(depth_routing > 0);

        let left = Node::Collision {
            hash: 7,
            entries: Arc::from([(1i32, ()), (2i32, ())]),
        };
        let same_reversed = Node::Collision {
            hash: 7,
            entries: Arc::from([(2i32, "b"), (1i32, "a")]),
        };
        let different = Node::Collision {
            hash: 7,
            entries: Arc::from([(1i32, "a"), (3i32, "c")]),
        };
        assert!(champ_topology_equal(&left, &same_reversed));
        assert!(!champ_topology_equal(&left, &different));
    }

    #[test]
    fn read_only_operations_do_not_require_value_bounds() {
        // Box<dyn Fn> is neither Clone nor PartialEq; construction, length,
        // lookup, and iteration must still work.
        let map: PersistentHashMap<&str, Box<dyn Fn() -> i32>> = PersistentHashMap::new();
        assert!(map.is_empty());
        assert!(map.get(&"missing").is_none());
        assert_eq!(map.iter().count(), 0);
    }

    #[test]
    fn from_iterator_supports_custom_default_hashers() {
        let map: PersistentHashMap<i32, i32, ConstantState> =
            (0..16).map(|value| (value, value)).collect();
        assert_eq!(map.len(), 16);

        let set: PersistentHashSet<i32, ConstantState> = (0..16).collect();
        assert_eq!(set.len(), 16);
        assert_eq!(set.hasher().hash_one(42), 0);
    }

    #[test]
    fn duplicate_key_error_propagates() {
        fn try_it() -> Result<(), Box<dyn std::error::Error>> {
            let map = PersistentHashMap::new().insert("a", 1);
            let _ = map.add("a", 2)?;
            Ok(())
        }

        let error = try_it().unwrap_err();
        assert_eq!(
            error.to_string(),
            "an entry with the same key already exists"
        );
    }

    fn assert_send_sync<T: Send + Sync>() {}

    #[test]
    fn snapshots_are_send_sync_when_contents_are() {
        assert_send_sync::<PersistentHashMap<i32, i32>>();
        assert_send_sync::<PersistentHashSet<i32>>();
    }

    #[test]
    fn concurrent_readers_share_retained_snapshots() {
        let map = PersistentHashMap::new().set_items((0..256).map(|key| (key, key * 3 - 100)));
        let set: PersistentHashSet<_> = (0..256).collect();
        let expected_map = (0..256).map(|key| (key, key * 3 - 100)).collect::<Vec<_>>();

        let mut handles = Vec::new();
        for _ in 0..8 {
            let map = map.clone();
            let set = set.clone();
            let expected_map = expected_map.clone();
            handles.push(thread::spawn(move || {
                for _ in 0..128 {
                    assert_eq!(map.len(), 256);
                    assert_eq!(map.get(&128), Some(&284));
                    let mut entries = map
                        .iter()
                        .map(|(key, value)| (*key, *value))
                        .collect::<Vec<_>>();
                    entries.sort_unstable();
                    assert_eq!(entries, expected_map);

                    assert_eq!(set.len(), 256);
                    assert!(set.contains(&200));
                    let mut values = set.iter().copied().collect::<Vec<_>>();
                    values.sort_unstable();
                    assert_eq!(values, (0..256).collect::<Vec<_>>());
                }
            }));
        }

        for handle in handles {
            handle.join().expect("reader thread failed");
        }
    }
}
