//! Canonical, wire-compatible Merkle search tree — algorithm `mst-sha256-b16-v2`.
//!
//! [`MerkleSearchTree`] is a deterministic ordered map that is also a content-addressed block tree:
//! its root digest identifies its contents exactly, so two parties can compare or synchronize
//! entire maps by exchanging digests rather than entries. It is neither a probabilistic skip list
//! nor a binary Merkle tree.
//!
//! Shape is *history independent*. Each entry's level is the number of leading zero base-16 digits
//! of its keyed SHA-256 digest — zero with probability 1/16 per digit, which is the B=16
//! construction — so the tree built from a set of entries is the same tree no matter what order
//! they were inserted in. That is what makes the root digest a meaningful identity, and what lets a
//! tree built by one port verify against a proof produced by another: this module and its siblings
//! in the other eight languages agree byte for byte, down to the `MST2` block format defined here.
//!
//! Every tree retains a [`MerkleSearchTreePolicy`] holding the key comparer, the injective key and
//! value codecs, and the derived domain digest. The digest domain binds the algorithm ID, policy
//! ID, and both codec IDs, so trees built under different policies cannot be confused for one
//! another.
//!
//! Nodes and encoded bytes are shared through [`Arc`], so an update allocates only the affected
//! block path and a no-op — an encoded-value-identical write, an absent removal, clearing an empty
//! tree — retains the existing root. Persistence, proofs, synchronization, and three-way merge
//! live in [`crate::merkle_persistence`]; canonical value codecs live in
//! [`crate::merkle_encoding`].

use crate::{MerkleCodecError, MerkleDigest, MerkleSearchTreePolicy};
use std::cmp::Ordering;
use std::collections::HashSet;
use std::error::Error;
use std::fmt;
use std::iter::FusedIterator;
use std::sync::Arc;

pub(crate) const MERKLE_BLOCK_MAGIC: &[u8; 4] = b"MST2";
pub(crate) const MERKLE_NODE_BLOCK_TAG: u8 = 1;
pub(crate) const MERKLE_DIGEST_LENGTH: usize = MerkleDigest::BYTE_LENGTH;
pub(crate) const MERKLE_BLOCK_HEADER_LENGTH: usize = 4 + 1 + MERKLE_DIGEST_LENGTH + 1 + 4 + 4;

pub(crate) type MerkleNodeLink<K, V> = Option<Arc<MerkleNode<K, V>>>;
type MerkleNodeSplit<K, V> = (MerkleNodeLink<K, V>, MerkleNodeLink<K, V>);
type MerkleRemoval<K, V> = (MerkleNodeLink<K, V>, bool);

struct MerkleEntryInner<K, V> {
    key: Arc<K>,
    value: Arc<V>,
    key_bytes: Arc<[u8]>,
    value_bytes: Arc<[u8]>,
    level: u8,
}

/// One retained key/value representative and its canonical encodings.
pub struct MerkleEntry<K, V> {
    inner: Arc<MerkleEntryInner<K, V>>,
}

impl<K, V> MerkleEntry<K, V> {
    pub(crate) fn from_encoded(
        key: K,
        value: V,
        key_bytes: Vec<u8>,
        value_bytes: Vec<u8>,
        level: u8,
    ) -> Self {
        Self {
            inner: Arc::new(MerkleEntryInner {
                key: Arc::new(key),
                value: Arc::new(value),
                key_bytes: Arc::from(key_bytes),
                value_bytes: Arc::from(value_bytes),
                level,
            }),
        }
    }

    pub(crate) fn replacing_value(&self, value: V, value_bytes: Vec<u8>) -> Self {
        Self {
            inner: Arc::new(MerkleEntryInner {
                key: Arc::clone(&self.inner.key),
                value: Arc::new(value),
                key_bytes: Arc::clone(&self.inner.key_bytes),
                value_bytes: Arc::from(value_bytes),
                level: self.inner.level,
            }),
        }
    }

    /// Returns the first retained key representative.
    #[must_use]
    pub fn key(&self) -> &K {
        self.inner.key.as_ref()
    }

    /// Returns the retained value.
    #[must_use]
    pub fn value(&self) -> &V {
        self.inner.value.as_ref()
    }

    /// Clones the shared key handle without cloning `K`.
    #[must_use]
    pub fn key_handle(&self) -> Arc<K> {
        Arc::clone(&self.inner.key)
    }

    /// Clones the shared value handle without cloning `V`.
    #[must_use]
    pub fn value_handle(&self) -> Arc<V> {
        Arc::clone(&self.inner.value)
    }

    /// Returns the canonical key encoding.
    #[must_use]
    pub fn key_bytes(&self) -> &[u8] {
        &self.inner.key_bytes
    }

    /// Returns the canonical value encoding.
    #[must_use]
    pub fn value_bytes(&self) -> &[u8] {
        &self.inner.value_bytes
    }

    /// Returns the hash-derived level.
    #[must_use]
    pub fn level(&self) -> u8 {
        self.inner.level
    }

    pub(crate) fn same_identity(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.inner, &other.inner)
    }
}

impl<K, V> Clone for MerkleEntry<K, V> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<K: fmt::Debug, V: fmt::Debug> fmt::Debug for MerkleEntry<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MerkleEntry")
            .field("key", self.key())
            .field("value", self.value())
            .field("level", &self.level())
            .finish()
    }
}

impl<K: PartialEq, V: PartialEq> PartialEq for MerkleEntry<K, V> {
    fn eq(&self, other: &Self) -> bool {
        self.key() == other.key() && self.value() == other.value()
    }
}

impl<K: Eq, V: Eq> Eq for MerkleEntry<K, V> {}

pub(crate) struct MerkleNode<K, V> {
    pub(crate) level: u8,
    pub(crate) entries: Arc<[MerkleEntry<K, V>]>,
    pub(crate) children: Arc<[MerkleNodeLink<K, V>]>,
    pub(crate) count: usize,
    pub(crate) height: usize,
    pub(crate) block_count: usize,
    pub(crate) minimum_key: Arc<K>,
    pub(crate) maximum_key: Arc<K>,
    pub(crate) block_bytes: Arc<[u8]>,
    pub(crate) digest: MerkleDigest,
}

/// An immutable B=16 wide Merkle search tree.
///
/// Canonical encoded key hashes assign levels by leading zero SHA-256 nibbles. Every consecutive
/// same-level run occupies one block, so shape and root address depend only on policy-bound map
/// contents and not mutation history.
pub struct MerkleSearchTree<K, V> {
    pub(crate) root: Option<Arc<MerkleNode<K, V>>>,
    pub(crate) policy: MerkleSearchTreePolicy<K, V>,
}

impl<K, V> Clone for MerkleSearchTree<K, V> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.clone(),
            policy: self.policy.clone(),
        }
    }
}

impl<K: fmt::Debug, V: fmt::Debug> fmt::Debug for MerkleSearchTree<K, V> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MerkleSearchTree")
            .field("len", &self.len())
            .field("height", &self.height())
            .field("block_count", &self.block_count())
            .field("root_hash", &self.root_hash())
            .finish_non_exhaustive()
    }
}

impl<K, V> MerkleSearchTree<K, V> {
    /// Creates an empty tree retaining `policy`.
    #[must_use]
    pub fn new(policy: MerkleSearchTreePolicy<K, V>) -> Self {
        Self { root: None, policy }
    }

    pub(crate) fn from_verified_root(
        root: MerkleNodeLink<K, V>,
        policy: MerkleSearchTreePolicy<K, V>,
    ) -> Self {
        Self { root, policy }
    }

    /// Creates a canonical tree with first-equivalent-key and last-value semantics.
    pub fn from_entries<I>(
        entries: I,
        policy: MerkleSearchTreePolicy<K, V>,
    ) -> Result<Self, MerkleTreeError>
    where
        I: IntoIterator<Item = (K, V)>,
    {
        struct Pending<K, V> {
            key: K,
            value: V,
            sequence: usize,
        }

        let mut pending = entries
            .into_iter()
            .enumerate()
            .map(|(sequence, (key, value))| Pending {
                key,
                value,
                sequence,
            })
            .collect::<Vec<_>>();
        if pending.is_empty() {
            return Ok(Self::new(policy));
        }
        pending.sort_by(|left, right| {
            let ordering = policy.compare(&left.key, &right.key);
            if ordering == Ordering::Equal {
                left.sequence.cmp(&right.sequence)
            } else {
                ordering
            }
        });

        let tree = Self::new(policy);
        let mut grouped = Vec::new();
        let mut pending = pending.into_iter().peekable();
        while let Some(first) = pending.next() {
            let key = first.key;
            let mut value = first.value;
            while pending
                .peek()
                .is_some_and(|next| tree.policy.compare(&key, &next.key).is_eq())
            {
                value = pending.next().expect("peeked pending entry exists").value;
            }
            grouped.push(tree.create_entry(key, value)?);
        }
        let root = tree.build_canonical(&grouped)?;
        Ok(Self { root, ..tree })
    }

    /// Returns the deterministic comparison, codec, and hash policy.
    #[must_use]
    pub fn policy(&self) -> &MerkleSearchTreePolicy<K, V> {
        &self.policy
    }

    /// Returns the number of entries.
    #[must_use]
    pub fn len(&self) -> usize {
        self.root.as_ref().map_or(0, |root| root.count)
    }

    /// Returns whether no entries are present.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns the deepest block-path length.
    #[must_use]
    pub fn height(&self) -> usize {
        self.root.as_ref().map_or(0, |root| root.height)
    }

    /// Returns the number of content-addressed blocks.
    #[must_use]
    pub fn block_count(&self) -> usize {
        self.root.as_ref().map_or(0, |root| root.block_count)
    }

    /// Returns the policy-bound root content address.
    #[must_use]
    pub fn root_hash(&self) -> MerkleDigest {
        self.root
            .as_ref()
            .map_or_else(|| self.policy.empty_digest(), |root| root.digest)
    }

    /// Creates a cursor at the gap before the first entry.
    #[must_use]
    pub fn cursor(&self) -> MerkleSearchTreeCursor<K, V> {
        MerkleSearchTreeCursor {
            tree: self.clone(),
            position: 0,
        }
    }

    /// Creates a cursor at a rank gap, or returns `None` outside `0..=len`.
    #[must_use]
    pub fn cursor_at(&self, position: usize) -> Option<MerkleSearchTreeCursor<K, V>> {
        (position <= self.len()).then(|| MerkleSearchTreeCursor {
            tree: self.clone(),
            position,
        })
    }

    /// Creates a cursor after the final entry.
    #[must_use]
    pub fn cursor_at_end(&self) -> MerkleSearchTreeCursor<K, V> {
        MerkleSearchTreeCursor {
            tree: self.clone(),
            position: self.len(),
        }
    }

    /// Creates the lower-bound cursor for `key`.
    #[must_use]
    pub fn lower_bound_cursor(&self, key: &K) -> MerkleSearchTreeCursor<K, V> {
        let (position, _) = self.lower_bound_rank_for_cursor(key);
        MerkleSearchTreeCursor {
            tree: self.clone(),
            position,
        }
    }

    /// Creates the upper-bound cursor for `key`.
    #[must_use]
    pub fn upper_bound_cursor(&self, key: &K) -> MerkleSearchTreeCursor<K, V> {
        let (position, found) = self.lower_bound_rank_for_cursor(key);
        MerkleSearchTreeCursor {
            tree: self.clone(),
            position: position + usize::from(found),
        }
    }

    /// Creates a usable lower-bound cursor and reports whether its next entry is exact.
    #[must_use]
    pub fn cursor_at_key(&self, key: &K) -> (MerkleSearchTreeCursor<K, V>, bool) {
        let (position, found) = self.lower_bound_rank_for_cursor(key);
        (
            MerkleSearchTreeCursor {
                tree: self.clone(),
                position,
            },
            found,
        )
    }

    /// Returns whether two versions retain the exact same root node.
    #[must_use]
    pub fn shares_root_with(&self, other: &Self) -> bool {
        match (&self.root, &other.root) {
            (Some(left), Some(right)) => Arc::ptr_eq(left, right),
            (None, None) => true,
            _ => false,
        }
    }

    /// Counts distinct block nodes shared by identity.
    #[must_use]
    pub fn shared_block_count(&self, other: &Self) -> usize {
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
            for child in node.children.iter().flatten() {
                pending.push(Arc::clone(child));
            }
        }
        shared
    }

    /// Returns exact `(digest, canonical bytes)` blocks in deterministic preorder.
    #[must_use]
    pub fn blocks_preorder(&self) -> Vec<(MerkleDigest, Arc<[u8]>)> {
        enumerate_nodes_preorder(self.root.as_ref())
            .into_iter()
            .map(|node| (node.digest, Arc::clone(&node.block_bytes)))
            .collect()
    }

    /// Returns block-level shape records in deterministic preorder.
    #[must_use]
    pub fn shape(&self) -> Vec<MerkleShapeEntry<K>> {
        let mut result = Vec::with_capacity(self.len());
        for node in enumerate_nodes_preorder(self.root.as_ref()) {
            for entry in node.entries.iter() {
                result.push(MerkleShapeEntry {
                    level: node.level,
                    key: entry.key_handle(),
                    entries_in_block: node.entries.len(),
                    subtree_count: node.count,
                });
            }
        }
        result
    }

    /// Returns whether an equivalent key is present.
    #[must_use]
    pub fn contains_key(&self, key: &K) -> bool {
        self.get_entry(key).is_some()
    }

    /// Returns the retained value for an equivalent key.
    #[must_use]
    pub fn get(&self, key: &K) -> Option<&V> {
        self.get_entry(key).map(MerkleEntry::value)
    }

    /// Returns the retained entry and therefore the first key representative.
    #[must_use]
    pub fn get_entry(&self, key: &K) -> Option<&MerkleEntry<K, V>> {
        let mut node = self.root.as_deref();
        while let Some(current) = node {
            let (position, found) = self.find_position(&current.entries, key);
            if found {
                return Some(&current.entries[position]);
            }
            node = current.children[position].as_deref();
        }
        None
    }

    fn entry_at_rank_for_cursor(&self, mut rank: usize) -> Option<&MerkleEntry<K, V>> {
        if rank >= self.len() {
            return None;
        }
        let mut node = self.root.as_deref();
        while let Some(current) = node {
            let mut descended = false;
            for (index, entry) in current.entries.iter().enumerate() {
                let child_count = current.children[index]
                    .as_ref()
                    .map_or(0, |child| child.count);
                if rank < child_count {
                    node = current.children[index].as_deref();
                    descended = true;
                    break;
                }
                rank -= child_count;
                if rank == 0 {
                    return Some(entry);
                }
                rank -= 1;
            }
            if !descended {
                node = current.children.last().and_then(Option::as_deref);
            }
        }
        unreachable!("validated Merkle subtree counts contain every in-range rank")
    }

    fn lower_bound_rank_for_cursor(&self, key: &K) -> (usize, bool) {
        let mut rank = 0_usize;
        let mut node = self.root.as_deref();
        while let Some(current) = node {
            let (position, found) = self.find_position(&current.entries, key);
            for child in current.children.iter().take(position) {
                rank += child.as_ref().map_or(0, |child| child.count) + 1;
            }
            if found {
                return (
                    rank + current.children[position]
                        .as_ref()
                        .map_or(0, |child| child.count),
                    true,
                );
            }
            node = current.children[position].as_deref();
        }
        (rank, false)
    }

    /// Adds or replaces an entry by copying only affected blocks.
    pub fn set_item(&self, key: K, value: V) -> Result<Self, MerkleTreeError> {
        if let Some(existing) = self.get_entry(&key) {
            let value_bytes = self.policy.value_codec().encode(&value)?;
            if existing.value_bytes() == value_bytes {
                return Ok(self.clone());
            }
            let replacement = existing.replacing_value(value, value_bytes);
            let root = self.update_value(
                self.root.as_ref().expect("found entry requires root"),
                &key,
                replacement,
            )?;
            return Ok(Self {
                root: Some(root),
                policy: self.policy.clone(),
            });
        }

        let entry = self.create_entry(key, value)?;
        let root = self.insert(self.root.as_ref(), entry)?;
        Ok(Self {
            root: Some(root),
            policy: self.policy.clone(),
        })
    }

    fn set_value_at_key(&self, key: &K, value: V) -> Result<Self, MerkleTreeError> {
        let existing = self
            .get_entry(key)
            .ok_or(MerkleTreeError::InvalidRepresentation(
                "cursor replacement key is absent",
            ))?;
        let value_bytes = self.policy.value_codec().encode(&value)?;
        if existing.value_bytes() == value_bytes {
            return Ok(self.clone());
        }
        let replacement = existing.replacing_value(value, value_bytes);
        let root = self.update_value(
            self.root.as_ref().expect("found entry requires root"),
            key,
            replacement,
        )?;
        Ok(Self {
            root: Some(root),
            policy: self.policy.clone(),
        })
    }

    /// Removes an equivalent key and contracts empty block shells.
    #[must_use]
    pub fn remove(&self, key: &K) -> Self {
        let (root, removed) = self
            .remove_node(self.root.as_ref(), key)
            .expect("removing from an existing valid tree cannot overflow");
        if !removed {
            self.clone()
        } else {
            Self {
                root,
                policy: self.policy.clone(),
            }
        }
    }

    /// Returns an empty tree retaining the same policy.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::new(self.policy.clone())
        }
    }

    /// Compares policy domains and root content addresses in O(1).
    #[must_use]
    pub fn content_equals(&self, other: &Self) -> bool {
        self.policy.domain_digest() == other.policy.domain_digest()
            && self.root_hash() == other.root_hash()
    }

    /// Compares semantic contents with a caller-supplied value relation.
    #[must_use]
    pub fn map_equals_by<F>(&self, other: &Self, values_equal: F) -> bool
    where
        F: Fn(&V, &V) -> bool,
    {
        if self.len() != other.len() || self.policy.domain_digest() != other.policy.domain_digest()
        {
            return false;
        }
        if self.root_hash() == other.root_hash() {
            return self.nodes_equal(self.root.as_deref(), other.root.as_deref(), &values_equal);
        }
        self.iter().zip(other.iter()).all(|(left, right)| {
            self.policy.compare(left.key(), right.key()).is_eq()
                && values_equal(left.value(), right.value())
        })
    }

    /// Computes a digest-pruned semantic diff with a caller-supplied value relation.
    pub fn diff_by<F>(
        &self,
        other: &Self,
        values_equal: F,
    ) -> Result<Vec<MerkleMapDifference<K, V>>, MerkleTreeError>
    where
        F: Fn(&V, &V) -> bool,
    {
        self.ensure_compatible(other)?;
        let mut result = Vec::new();
        self.diff_nodes(
            self.root.as_deref(),
            other.root.as_deref(),
            &values_equal,
            &mut result,
        );
        Ok(result)
    }

    /// Returns an in-order explicit-stack iterator.
    #[must_use]
    pub fn iter(&self) -> MerkleTreeIter<'_, K, V> {
        MerkleTreeIter::new(self.root.as_deref(), self.len())
    }

    /// Eagerly validates and lazily enumerates an inclusive comparer-ordered key range.
    pub fn enumerate_range<'a>(
        &'a self,
        minimum_key: &'a K,
        maximum_key: &'a K,
    ) -> Result<MerkleRangeIter<'a, K, V>, MerkleRangeError> {
        if self.policy.compare(minimum_key, maximum_key).is_gt() {
            return Err(MerkleRangeError);
        }
        Ok(MerkleRangeIter {
            pending: self
                .root
                .as_deref()
                .map(|root| RangeFrame::Visit(root, 0))
                .into_iter()
                .collect(),
            policy: &self.policy,
            minimum_key,
            maximum_key,
        })
    }

    /// Validates ordering, hash levels, metadata, exact block bytes, and content addresses.
    pub fn validate_structure(
        &self,
    ) -> Result<MerkleSearchTreeStatistics, MerkleTreeInvariantError> {
        let Some(root) = self.root.as_deref() else {
            return Ok(MerkleSearchTreeStatistics::default());
        };
        let mut accumulator = ValidationAccumulator::default();
        self.validate_node(root, &mut accumulator)?;
        if accumulator.entry_count != self.len() || accumulator.block_count != self.block_count() {
            return Err(MerkleTreeInvariantError::new(
                "Merkle root metadata disagrees with validated contents",
            ));
        }
        Ok(accumulator.statistics(self.height()))
    }

    pub(crate) fn ensure_compatible(&self, other: &Self) -> Result<(), MerkleTreeError> {
        if self.policy.domain_digest() == other.policy.domain_digest() {
            Ok(())
        } else {
            Err(MerkleTreeError::IncompatiblePolicy)
        }
    }

    pub(crate) fn create_entry(
        &self,
        key: K,
        value: V,
    ) -> Result<MerkleEntry<K, V>, MerkleTreeError> {
        let key_bytes = self.policy.key_codec().encode(&key)?;
        let value_bytes = self.policy.value_codec().encode(&value)?;
        let level = MerkleSearchTreePolicy::<K, V>::level(self.policy.hash_key_bytes(&key_bytes));
        Ok(MerkleEntry::from_encoded(
            key,
            value,
            key_bytes,
            value_bytes,
            level,
        ))
    }

    pub(crate) fn find_position(&self, entries: &[MerkleEntry<K, V>], key: &K) -> (usize, bool) {
        let mut low = 0;
        let mut high = entries.len();
        while low < high {
            let middle = (low + high) / 2;
            if self.policy.compare(entries[middle].key(), key).is_lt() {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        (
            low,
            low < entries.len() && self.policy.compare(entries[low].key(), key).is_eq(),
        )
    }

    pub(crate) fn build_canonical(
        &self,
        entries: &[MerkleEntry<K, V>],
    ) -> Result<Option<Arc<MerkleNode<K, V>>>, MerkleTreeError> {
        if entries.is_empty() {
            return Ok(None);
        }
        let maximum_level = entries
            .iter()
            .map(MerkleEntry::level)
            .max()
            .expect("nonempty entries have a maximum level");
        let mut block_entries = Vec::new();
        let mut children = Vec::new();
        let mut segment_start = 0;
        for (index, entry) in entries.iter().enumerate() {
            if entry.level() == maximum_level {
                children.push(self.build_canonical(&entries[segment_start..index])?);
                block_entries.push(entry.clone());
                segment_start = index + 1;
            }
        }
        children.push(self.build_canonical(&entries[segment_start..])?);
        self.new_node(maximum_level, block_entries, children)
            .map(Some)
    }

    fn update_value(
        &self,
        node: &Arc<MerkleNode<K, V>>,
        key: &K,
        replacement: MerkleEntry<K, V>,
    ) -> Result<Arc<MerkleNode<K, V>>, MerkleTreeError> {
        let (position, found) = self.find_position(&node.entries, key);
        if found {
            let mut entries = node.entries.to_vec();
            entries[position] = replacement;
            return self.new_node(node.level, entries, node.children.to_vec());
        }
        let mut children = node.children.to_vec();
        children[position] = Some(
            self.update_value(
                children[position]
                    .as_ref()
                    .expect("existing key path has a child"),
                key,
                replacement,
            )?,
        );
        self.new_node(node.level, node.entries.to_vec(), children)
    }

    fn insert(
        &self,
        node: Option<&Arc<MerkleNode<K, V>>>,
        entry: MerkleEntry<K, V>,
    ) -> Result<Arc<MerkleNode<K, V>>, MerkleTreeError> {
        let Some(node) = node else {
            return self.new_node(entry.level(), vec![entry], vec![None, None]);
        };
        if entry.level() > node.level {
            let (left, right) = self.split(Some(node), entry.key())?;
            return self.new_node(entry.level(), vec![entry], vec![left, right]);
        }
        let (position, found) = self.find_position(&node.entries, entry.key());
        debug_assert!(!found);
        if entry.level() < node.level {
            let mut children = node.children.to_vec();
            children[position] = Some(self.insert(children[position].as_ref(), entry)?);
            return self.new_node(node.level, node.entries.to_vec(), children);
        }

        let (left, right) = self.split(node.children[position].as_ref(), entry.key())?;
        let mut entries = node.entries.to_vec();
        entries.insert(position, entry);
        let mut children = node.children.to_vec();
        children[position] = left;
        children.insert(position + 1, right);
        self.new_node(node.level, entries, children)
    }

    fn split(
        &self,
        node: Option<&Arc<MerkleNode<K, V>>>,
        key: &K,
    ) -> Result<MerkleNodeSplit<K, V>, MerkleTreeError> {
        let Some(node) = node else {
            return Ok((None, None));
        };
        let (position, found) = self.find_position(&node.entries, key);
        debug_assert!(!found);
        let (child_left, child_right) = self.split(node.children[position].as_ref(), key)?;

        let left_entries = node.entries[..position].to_vec();
        let mut left_children = node.children[..=position].to_vec();
        left_children[position] = child_left;
        let right_entries = node.entries[position..].to_vec();
        let mut right_children = node.children[position..].to_vec();
        right_children[0] = child_right;
        Ok((
            self.create_or_collapse(node.level, left_entries, left_children)?,
            self.create_or_collapse(node.level, right_entries, right_children)?,
        ))
    }

    fn remove_node(
        &self,
        node: Option<&Arc<MerkleNode<K, V>>>,
        key: &K,
    ) -> Result<MerkleRemoval<K, V>, MerkleTreeError> {
        let Some(node) = node else {
            return Ok((None, false));
        };
        let (position, found) = self.find_position(&node.entries, key);
        if !found {
            let (child, removed) = self.remove_node(node.children[position].as_ref(), key)?;
            if !removed {
                return Ok((Some(Arc::clone(node)), false));
            }
            let mut children = node.children.to_vec();
            children[position] = child;
            return self
                .new_node(node.level, node.entries.to_vec(), children)
                .map(|node| (Some(node), true));
        }

        let mut entries = node.entries.to_vec();
        entries.remove(position);
        let mut children = node.children.to_vec();
        let joined = self.join(children[position].as_ref(), children[position + 1].as_ref())?;
        children[position] = joined;
        children.remove(position + 1);
        self.create_or_collapse(node.level, entries, children)
            .map(|node| (node, true))
    }

    fn join(
        &self,
        left: Option<&Arc<MerkleNode<K, V>>>,
        right: Option<&Arc<MerkleNode<K, V>>>,
    ) -> Result<Option<Arc<MerkleNode<K, V>>>, MerkleTreeError> {
        match (left, right) {
            (None, None) => Ok(None),
            (Some(node), None) | (None, Some(node)) => Ok(Some(Arc::clone(node))),
            (Some(left), Some(right)) if left.level > right.level => {
                let mut children = left.children.to_vec();
                let last = children.len() - 1;
                children[last] = self.join(children[last].as_ref(), Some(right))?;
                self.new_node(left.level, left.entries.to_vec(), children)
                    .map(Some)
            }
            (Some(left), Some(right)) if left.level < right.level => {
                let mut children = right.children.to_vec();
                children[0] = self.join(Some(left), children[0].as_ref())?;
                self.new_node(right.level, right.entries.to_vec(), children)
                    .map(Some)
            }
            (Some(left), Some(right)) => {
                let mut entries = left.entries.to_vec();
                entries.extend(right.entries.iter().cloned());
                let mut children = left.children[..left.entries.len()].to_vec();
                children.push(self.join(
                    left.children.last().unwrap().as_ref(),
                    right.children[0].as_ref(),
                )?);
                children.extend(right.children[1..].iter().cloned());
                self.new_node(left.level, entries, children).map(Some)
            }
        }
    }

    fn create_or_collapse(
        &self,
        level: u8,
        entries: Vec<MerkleEntry<K, V>>,
        children: Vec<MerkleNodeLink<K, V>>,
    ) -> Result<Option<Arc<MerkleNode<K, V>>>, MerkleTreeError> {
        debug_assert_eq!(children.len(), entries.len() + 1);
        if entries.is_empty() {
            Ok(children.into_iter().next().flatten())
        } else {
            self.new_node(level, entries, children).map(Some)
        }
    }

    pub(crate) fn new_node(
        &self,
        level: u8,
        entries: Vec<MerkleEntry<K, V>>,
        children: Vec<MerkleNodeLink<K, V>>,
    ) -> Result<Arc<MerkleNode<K, V>>, MerkleTreeError> {
        if level > 64 || entries.is_empty() || children.len() != entries.len() + 1 {
            return Err(MerkleTreeError::InvalidRepresentation(
                "an MST block must contain a valid level, entries, and one extra child interval",
            ));
        }
        let mut count = entries.len();
        let mut height = 1_usize;
        let mut block_count = 1_usize;
        for child in children.iter().flatten() {
            count = count
                .checked_add(child.count)
                .ok_or(MerkleTreeError::SizeOverflow)?;
            height = height.max(
                1_usize
                    .checked_add(child.height)
                    .ok_or(MerkleTreeError::SizeOverflow)?,
            );
            block_count = block_count
                .checked_add(child.block_count)
                .ok_or(MerkleTreeError::SizeOverflow)?;
        }
        let block_bytes = self.encode_block(level, count, &entries, &children)?;
        let digest = MerkleDigest::hash(&block_bytes);
        let minimum_key = children[0].as_ref().map_or_else(
            || entries[0].key_handle(),
            |child| Arc::clone(&child.minimum_key),
        );
        let maximum_key = children.last().and_then(Option::as_ref).map_or_else(
            || entries.last().unwrap().key_handle(),
            |child| Arc::clone(&child.maximum_key),
        );
        Ok(Arc::new(MerkleNode {
            level,
            entries: Arc::from(entries),
            children: Arc::from(children),
            count,
            height,
            block_count,
            minimum_key,
            maximum_key,
            block_bytes: Arc::from(block_bytes),
            digest,
        }))
    }

    pub(crate) fn encode_block(
        &self,
        level: u8,
        subtree_count: usize,
        entries: &[MerkleEntry<K, V>],
        children: &[MerkleNodeLink<K, V>],
    ) -> Result<Vec<u8>, MerkleTreeError> {
        let subtree_count =
            i32::try_from(subtree_count).map_err(|_| MerkleTreeError::SizeOverflow)?;
        let entry_count =
            i32::try_from(entries.len()).map_err(|_| MerkleTreeError::SizeOverflow)?;
        let mut length = MERKLE_BLOCK_HEADER_LENGTH
            .checked_add(
                children
                    .len()
                    .checked_mul(MERKLE_DIGEST_LENGTH)
                    .ok_or(MerkleTreeError::SizeOverflow)?,
            )
            .ok_or(MerkleTreeError::SizeOverflow)?;
        for entry in entries {
            i32::try_from(entry.key_bytes().len()).map_err(|_| MerkleTreeError::SizeOverflow)?;
            i32::try_from(entry.value_bytes().len()).map_err(|_| MerkleTreeError::SizeOverflow)?;
            length = length
                .checked_add(8)
                .and_then(|value| value.checked_add(entry.key_bytes().len()))
                .and_then(|value| value.checked_add(entry.value_bytes().len()))
                .ok_or(MerkleTreeError::SizeOverflow)?;
        }
        let mut result = Vec::with_capacity(length);
        result.extend_from_slice(MERKLE_BLOCK_MAGIC);
        result.push(MERKLE_NODE_BLOCK_TAG);
        result.extend_from_slice(self.policy.domain_digest().as_bytes());
        result.push(level);
        result.extend_from_slice(&subtree_count.to_be_bytes());
        result.extend_from_slice(&entry_count.to_be_bytes());
        for entry in entries {
            result.extend_from_slice(&(entry.key_bytes().len() as i32).to_be_bytes());
            result.extend_from_slice(entry.key_bytes());
            result.extend_from_slice(&(entry.value_bytes().len() as i32).to_be_bytes());
            result.extend_from_slice(entry.value_bytes());
        }
        for child in children {
            result.extend_from_slice(
                child
                    .as_ref()
                    .map_or_else(|| self.policy.empty_digest(), |node| node.digest)
                    .as_bytes(),
            );
        }
        debug_assert_eq!(result.len(), length);
        Ok(result)
    }

    fn nodes_equal<F>(
        &self,
        left: Option<&MerkleNode<K, V>>,
        right: Option<&MerkleNode<K, V>>,
        values_equal: &F,
    ) -> bool
    where
        F: Fn(&V, &V) -> bool,
    {
        match (left, right) {
            (None, None) => true,
            (Some(left), Some(right))
                if left.level == right.level && left.entries.len() == right.entries.len() =>
            {
                left.entries
                    .iter()
                    .zip(right.entries.iter())
                    .all(|(left, right)| {
                        left.same_identity(right)
                            || (self.policy.compare(left.key(), right.key()).is_eq()
                                && values_equal(left.value(), right.value()))
                    })
                    && left
                        .children
                        .iter()
                        .zip(right.children.iter())
                        .all(|(left, right)| {
                            self.nodes_equal(left.as_deref(), right.as_deref(), values_equal)
                        })
            }
            _ => false,
        }
    }

    fn diff_nodes<F>(
        &self,
        left: Option<&MerkleNode<K, V>>,
        right: Option<&MerkleNode<K, V>>,
        values_equal: &F,
        result: &mut Vec<MerkleMapDifference<K, V>>,
    ) where
        F: Fn(&V, &V) -> bool,
    {
        if left.is_some_and(|left| right.is_some_and(|right| std::ptr::eq(left, right)))
            || left.map(|node| node.digest) == right.map(|node| node.digest)
        {
            return;
        }
        match (left, right) {
            (None, Some(right)) => {
                for entry in MerkleTreeIter::new(Some(right), right.count) {
                    result.push(MerkleMapDifference::Added {
                        key: entry.key_handle(),
                        value: entry.value_handle(),
                    });
                }
            }
            (Some(left), None) => {
                for entry in MerkleTreeIter::new(Some(left), left.count) {
                    result.push(MerkleMapDifference::Removed {
                        key: entry.key_handle(),
                        value: entry.value_handle(),
                    });
                }
            }
            (Some(left), Some(right)) if self.same_separators(left, right) => {
                for index in 0..left.entries.len() {
                    self.diff_nodes(
                        left.children[index].as_deref(),
                        right.children[index].as_deref(),
                        values_equal,
                        result,
                    );
                    if !values_equal(left.entries[index].value(), right.entries[index].value()) {
                        result.push(MerkleMapDifference::Changed {
                            key: left.entries[index].key_handle(),
                            before: left.entries[index].value_handle(),
                            after: right.entries[index].value_handle(),
                        });
                    }
                }
                self.diff_nodes(
                    left.children.last().unwrap().as_deref(),
                    right.children.last().unwrap().as_deref(),
                    values_equal,
                    result,
                );
            }
            (Some(left), Some(right)) => {
                let mut left = MerkleTreeIter::new(Some(left), left.count).peekable();
                let mut right = MerkleTreeIter::new(Some(right), right.count).peekable();
                while left.peek().is_some() || right.peek().is_some() {
                    let ordering = match (left.peek(), right.peek()) {
                        (Some(left), Some(right)) => self.policy.compare(left.key(), right.key()),
                        (Some(_), None) => Ordering::Less,
                        (None, Some(_)) => Ordering::Greater,
                        (None, None) => break,
                    };
                    match ordering {
                        Ordering::Less => {
                            let entry = left.next().unwrap();
                            result.push(MerkleMapDifference::Removed {
                                key: entry.key_handle(),
                                value: entry.value_handle(),
                            });
                        }
                        Ordering::Greater => {
                            let entry = right.next().unwrap();
                            result.push(MerkleMapDifference::Added {
                                key: entry.key_handle(),
                                value: entry.value_handle(),
                            });
                        }
                        Ordering::Equal => {
                            let old = left.next().unwrap();
                            let new = right.next().unwrap();
                            if !values_equal(old.value(), new.value()) {
                                result.push(MerkleMapDifference::Changed {
                                    key: old.key_handle(),
                                    before: old.value_handle(),
                                    after: new.value_handle(),
                                });
                            }
                        }
                    }
                }
            }
            (None, None) => {}
        }
    }

    fn same_separators(&self, left: &MerkleNode<K, V>, right: &MerkleNode<K, V>) -> bool {
        left.level == right.level
            && left.entries.len() == right.entries.len()
            && left
                .entries
                .iter()
                .zip(right.entries.iter())
                .all(|(left, right)| self.policy.compare(left.key(), right.key()).is_eq())
    }

    fn validate_node(
        &self,
        node: &MerkleNode<K, V>,
        accumulator: &mut ValidationAccumulator,
    ) -> Result<(), MerkleTreeInvariantError> {
        if node.entries.is_empty()
            || node.children.len() != node.entries.len() + 1
            || node.level > 64
        {
            return Err(MerkleTreeInvariantError::new(
                "an MST block has invalid level or entry/child arity",
            ));
        }
        let mut count = node.entries.len();
        let mut height = 1_usize;
        let mut blocks = 1_usize;
        for (index, entry) in node.entries.iter().enumerate() {
            if entry.level() != node.level {
                return Err(MerkleTreeInvariantError::new(
                    "an MST entry is stored in the wrong level",
                ));
            }
            if index != 0
                && !self
                    .policy
                    .compare(node.entries[index - 1].key(), entry.key())
                    .is_lt()
            {
                return Err(MerkleTreeInvariantError::new(
                    "MST block entries are not strictly ordered",
                ));
            }
        }
        for (index, child) in node.children.iter().enumerate() {
            let Some(child) = child.as_deref() else {
                continue;
            };
            if child.level >= node.level {
                return Err(MerkleTreeInvariantError::new(
                    "an MST child does not occupy a lower level",
                ));
            }
            if index != 0
                && !self
                    .policy
                    .compare(child.minimum_key.as_ref(), node.entries[index - 1].key())
                    .is_gt()
            {
                return Err(MerkleTreeInvariantError::new(
                    "an MST child crosses its lower key separator",
                ));
            }
            if index != node.entries.len()
                && !self
                    .policy
                    .compare(child.maximum_key.as_ref(), node.entries[index].key())
                    .is_lt()
            {
                return Err(MerkleTreeInvariantError::new(
                    "an MST child crosses its upper key separator",
                ));
            }
            self.validate_node(child, accumulator)?;
            count = count
                .checked_add(child.count)
                .ok_or_else(|| MerkleTreeInvariantError::new("validated MST count overflow"))?;
            height = height.max(child.height + 1);
            blocks = blocks.checked_add(child.block_count).ok_or_else(|| {
                MerkleTreeInvariantError::new("validated MST block count overflow")
            })?;
        }
        if count != node.count || height != node.height || blocks != node.block_count {
            return Err(MerkleTreeInvariantError::new(
                "an MST block has invalid cached metadata",
            ));
        }
        let encoded = self
            .encode_block(node.level, node.count, &node.entries, &node.children)
            .map_err(|_| MerkleTreeInvariantError::new("an MST block cannot be re-encoded"))?;
        if encoded.as_slice() != node.block_bytes.as_ref()
            || MerkleDigest::hash(&node.block_bytes) != node.digest
        {
            return Err(MerkleTreeInvariantError::new(
                "an MST block has invalid canonical bytes or digest",
            ));
        }
        accumulator.add(node);
        Ok(())
    }
}

impl<K, V: PartialEq> MerkleSearchTree<K, V> {
    /// Compares semantic map contents using ordinary value equality.
    #[must_use]
    pub fn map_equals(&self, other: &Self) -> bool {
        self.map_equals_by(other, PartialEq::eq)
    }

    /// Computes a semantic diff using ordinary value equality.
    pub fn diff(&self, other: &Self) -> Result<Vec<MerkleMapDifference<K, V>>, MerkleTreeError> {
        self.diff_by(other, PartialEq::eq)
    }
}

/// One block-level shape record.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleShapeEntry<K> {
    /// Block level.
    pub level: u8,
    /// Retained key representative.
    pub key: Arc<K>,
    /// Number of entries in this block run.
    pub entries_in_block: usize,
    /// Complete subtree entry count.
    pub subtree_count: usize,
}

/// Validated wide-tree representation statistics.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct MerkleSearchTreeStatistics {
    /// Entry count.
    pub count: usize,
    /// Content-addressed block count.
    pub block_count: usize,
    /// Maximum block-path length.
    pub height: usize,
    /// Smallest block entry run.
    pub minimum_entries_per_block: usize,
    /// Largest block entry run.
    pub maximum_entries_per_block: usize,
    /// Smallest canonical block encoding.
    pub minimum_block_bytes: usize,
    /// Largest canonical block encoding.
    pub maximum_block_bytes: usize,
}

#[derive(Default)]
struct ValidationAccumulator {
    entry_count: usize,
    block_count: usize,
    minimum_entries: usize,
    maximum_entries: usize,
    minimum_block_bytes: usize,
    maximum_block_bytes: usize,
}

impl ValidationAccumulator {
    fn add<K, V>(&mut self, node: &MerkleNode<K, V>) {
        self.entry_count += node.entries.len();
        self.block_count += 1;
        if self.block_count == 1 {
            self.minimum_entries = node.entries.len();
            self.minimum_block_bytes = node.block_bytes.len();
        } else {
            self.minimum_entries = self.minimum_entries.min(node.entries.len());
            self.minimum_block_bytes = self.minimum_block_bytes.min(node.block_bytes.len());
        }
        self.maximum_entries = self.maximum_entries.max(node.entries.len());
        self.maximum_block_bytes = self.maximum_block_bytes.max(node.block_bytes.len());
    }

    fn statistics(&self, height: usize) -> MerkleSearchTreeStatistics {
        MerkleSearchTreeStatistics {
            count: self.entry_count,
            block_count: self.block_count,
            height,
            minimum_entries_per_block: self.minimum_entries,
            maximum_entries_per_block: self.maximum_entries,
            minimum_block_bytes: self.minimum_block_bytes,
            maximum_block_bytes: self.maximum_block_bytes,
        }
    }
}

/// A semantic key-level map difference with owned shared representatives.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MerkleMapDifference<K, V> {
    /// Key exists only in the target.
    Added { key: Arc<K>, value: Arc<V> },
    /// Key exists only in the source.
    Removed { key: Arc<K>, value: Arc<V> },
    /// Key exists with different values.
    Changed {
        key: Arc<K>,
        before: Arc<V>,
        after: Arc<V>,
    },
}

/// A core construction or operation failure.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MerkleTreeError {
    /// A codec rejected a key or value.
    Codec(MerkleCodecError),
    /// A wire-visible size exceeded `i32` or native arithmetic.
    SizeOverflow,
    /// Tree domains differ.
    IncompatiblePolicy,
    /// Internal arguments could not describe a valid block.
    InvalidRepresentation(&'static str),
}

impl From<MerkleCodecError> for MerkleTreeError {
    fn from(error: MerkleCodecError) -> Self {
        Self::Codec(error)
    }
}

impl fmt::Display for MerkleTreeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Codec(error) => fmt::Display::fmt(error, formatter),
            Self::SizeOverflow => formatter.write_str("Merkle tree size exceeds its wire limits"),
            Self::IncompatiblePolicy => formatter
                .write_str("Merkle trees must use the same algorithm, policy, and codec domain"),
            Self::InvalidRepresentation(message) => formatter.write_str(message),
        }
    }
}

impl Error for MerkleTreeError {}

/// Failure from a Merkle cursor edit that cannot publish at the current ordered gap.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MerkleCursorEditError {
    /// Strict insertion found an equivalent key.
    DuplicateKey,
    /// The key's lower-bound rank differs from the cursor's current gap.
    WrongGap { expected: usize, actual: usize },
    /// Canonical encoding or tree construction failed.
    Tree(MerkleTreeError),
}

impl From<MerkleTreeError> for MerkleCursorEditError {
    fn from(error: MerkleTreeError) -> Self {
        Self::Tree(error)
    }
}

impl fmt::Display for MerkleCursorEditError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::DuplicateKey => formatter.write_str("the Merkle key is already present"),
            Self::WrongGap { expected, actual } => write!(
                formatter,
                "the key belongs at gap {expected}, not at the current gap {actual}"
            ),
            Self::Tree(error) => fmt::Display::fmt(error, formatter),
        }
    }
}

impl Error for MerkleCursorEditError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::Tree(error) => Some(error),
            Self::DuplicateKey | Self::WrongGap { .. } => None,
        }
    }
}

/// Immutable tree-snapshot-plus-rank gap cursor in policy-comparer order.
pub struct MerkleSearchTreeCursor<K, V> {
    tree: MerkleSearchTree<K, V>,
    position: usize,
}

impl<K, V> Clone for MerkleSearchTreeCursor<K, V> {
    fn clone(&self) -> Self {
        Self {
            tree: self.tree.clone(),
            position: self.position,
        }
    }
}

impl<K, V> MerkleSearchTreeCursor<K, V> {
    /// Returns the number of entries in this cursor version.
    #[must_use]
    pub fn len(&self) -> usize {
        self.tree.len()
    }

    /// Returns whether this cursor version is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.tree.is_empty()
    }

    /// Returns the number of entries before the gap.
    #[must_use]
    pub fn position(&self) -> usize {
        self.position
    }

    /// Returns whether the gap precedes every entry.
    #[must_use]
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }

    /// Returns whether the gap follows every entry.
    #[must_use]
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }

    /// Borrows the entry immediately before the gap.
    #[must_use]
    pub fn peek_previous(&self) -> Option<&MerkleEntry<K, V>> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.tree.entry_at_rank_for_cursor(rank))
    }

    /// Borrows the entry immediately after the gap.
    #[must_use]
    pub fn peek_next(&self) -> Option<&MerkleEntry<K, V>> {
        self.tree.entry_at_rank_for_cursor(self.position)
    }

    /// Returns a cursor one entry toward the start.
    #[must_use]
    pub fn move_previous(&self) -> Option<Self> {
        Some(Self {
            tree: self.tree.clone(),
            position: self.position.checked_sub(1)?,
        })
    }

    /// Returns a cursor one entry toward the end.
    #[must_use]
    pub fn move_next(&self) -> Option<Self> {
        if self.is_at_end() {
            return None;
        }
        Some(Self {
            tree: self.tree.clone(),
            position: self.position + 1,
        })
    }

    /// Returns a cursor at a valid rank gap.
    #[must_use]
    pub fn seek(&self, position: usize) -> Option<Self> {
        (position <= self.len()).then(|| Self {
            tree: self.tree.clone(),
            position,
        })
    }

    /// Strictly inserts a missing key at its lower-bound gap.
    pub fn insert(&self, key: K, value: V) -> Result<Self, MerkleCursorEditError> {
        let (expected, found) = self.tree.lower_bound_rank_for_cursor(&key);
        if found {
            return Err(MerkleCursorEditError::DuplicateKey);
        }
        self.ensure_current_gap(expected)?;
        Ok(Self {
            tree: self.tree.set_item(key, value)?,
            position: self.position + 1,
        })
    }

    /// Updates an exact next entry or inserts at a missing lower-bound gap.
    pub fn set_item(&self, key: K, value: V) -> Result<Self, MerkleCursorEditError> {
        let (expected, found) = self.tree.lower_bound_rank_for_cursor(&key);
        self.ensure_current_gap(expected)?;
        let tree = self.tree.set_item(key, value)?;
        if tree.shares_root_with(&self.tree) {
            return Ok(self.clone());
        }
        Ok(Self {
            tree,
            position: self.position + usize::from(!found),
        })
    }

    /// Replaces the next value while retaining its key representative and the current gap.
    pub fn set_next_value(&self, value: V) -> Result<Option<Self>, MerkleTreeError> {
        let Some(next) = self.peek_next() else {
            return Ok(None);
        };
        let tree = self.tree.set_value_at_key(next.key(), value)?;
        Ok(Some(if tree.shares_root_with(&self.tree) {
            self.clone()
        } else {
            Self {
                tree,
                position: self.position,
            }
        }))
    }

    /// Deletes the entry before the gap and moves the gap left.
    #[must_use]
    pub fn delete_previous(&self) -> Option<Self> {
        let previous = self.peek_previous()?;
        Some(Self {
            tree: self.tree.remove(previous.key()),
            position: self.position - 1,
        })
    }

    /// Deletes the entry after the gap and keeps the gap fixed.
    #[must_use]
    pub fn delete_next(&self) -> Option<Self> {
        let next = self.peek_next()?;
        Some(Self {
            tree: self.tree.remove(next.key()),
            position: self.position,
        })
    }

    /// Returns this cursor version's canonical immutable tree by root sharing.
    #[must_use]
    pub fn snapshot(&self) -> MerkleSearchTree<K, V> {
        self.tree.clone()
    }

    fn ensure_current_gap(&self, expected: usize) -> Result<(), MerkleCursorEditError> {
        if expected == self.position {
            Ok(())
        } else {
            Err(MerkleCursorEditError::WrongGap {
                expected,
                actual: self.position,
            })
        }
    }
}

/// An inclusive range had reversed bounds.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MerkleRangeError;

impl fmt::Display for MerkleRangeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("the minimum key must not follow the maximum key")
    }
}

impl Error for MerkleRangeError {}

/// A tree representation invariant failed.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleTreeInvariantError {
    message: &'static str,
}

impl MerkleTreeInvariantError {
    fn new(message: &'static str) -> Self {
        Self { message }
    }
}

impl fmt::Display for MerkleTreeInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(self.message)
    }
}

impl Error for MerkleTreeInvariantError {}

enum IterFrame<'a, K, V> {
    Visit(&'a MerkleNode<K, V>, usize),
    Emit(&'a MerkleEntry<K, V>),
}

/// Borrowed in-order iterator for [`MerkleSearchTree`].
pub struct MerkleTreeIter<'a, K, V> {
    pending: Vec<IterFrame<'a, K, V>>,
    remaining: usize,
}

impl<'a, K, V> MerkleTreeIter<'a, K, V> {
    fn new(root: Option<&'a MerkleNode<K, V>>, remaining: usize) -> Self {
        Self {
            pending: root
                .map(|root| IterFrame::Visit(root, 0))
                .into_iter()
                .collect(),
            remaining,
        }
    }
}

impl<'a, K, V> Iterator for MerkleTreeIter<'a, K, V> {
    type Item = &'a MerkleEntry<K, V>;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(frame) = self.pending.pop() {
            match frame {
                IterFrame::Emit(entry) => {
                    self.remaining -= 1;
                    return Some(entry);
                }
                IterFrame::Visit(node, index) if index < node.entries.len() => {
                    self.pending.push(IterFrame::Visit(node, index + 1));
                    self.pending.push(IterFrame::Emit(&node.entries[index]));
                    if let Some(child) = node.children[index].as_deref() {
                        self.pending.push(IterFrame::Visit(child, 0));
                    }
                }
                IterFrame::Visit(node, _) => {
                    if let Some(child) = node.children.last().and_then(Option::as_deref) {
                        self.pending.push(IterFrame::Visit(child, 0));
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

impl<K, V> ExactSizeIterator for MerkleTreeIter<'_, K, V> {}
impl<K, V> FusedIterator for MerkleTreeIter<'_, K, V> {}

impl<'a, K, V> IntoIterator for &'a MerkleSearchTree<K, V> {
    type Item = &'a MerkleEntry<K, V>;
    type IntoIter = MerkleTreeIter<'a, K, V>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

enum RangeFrame<'a, K, V> {
    Visit(&'a MerkleNode<K, V>, usize),
    Emit(&'a MerkleEntry<K, V>),
}

/// Borrowed iterator over an inclusive key range.
pub struct MerkleRangeIter<'a, K, V> {
    pending: Vec<RangeFrame<'a, K, V>>,
    policy: &'a MerkleSearchTreePolicy<K, V>,
    minimum_key: &'a K,
    maximum_key: &'a K,
}

impl<'a, K, V> Iterator for MerkleRangeIter<'a, K, V> {
    type Item = &'a MerkleEntry<K, V>;

    fn next(&mut self) -> Option<Self::Item> {
        while let Some(frame) = self.pending.pop() {
            match frame {
                RangeFrame::Emit(entry) => return Some(entry),
                RangeFrame::Visit(node, index) if index < node.entries.len() => {
                    let entry = &node.entries[index];
                    let low = self.policy.compare(entry.key(), self.minimum_key);
                    let high = self.policy.compare(entry.key(), self.maximum_key);
                    if !high.is_gt() {
                        self.pending.push(RangeFrame::Visit(node, index + 1));
                        if !low.is_lt() {
                            self.pending.push(RangeFrame::Emit(entry));
                        }
                    }
                    if low.is_gt()
                        && let Some(child) = node.children[index].as_deref()
                    {
                        self.pending.push(RangeFrame::Visit(child, 0));
                    }
                }
                RangeFrame::Visit(node, _) => {
                    if self
                        .policy
                        .compare(node.entries.last().unwrap().key(), self.maximum_key)
                        .is_lt()
                        && let Some(child) = node.children.last().and_then(Option::as_deref)
                    {
                        self.pending.push(RangeFrame::Visit(child, 0));
                    }
                }
            }
        }
        None
    }
}

pub(crate) fn enumerate_nodes_preorder<K, V>(
    root: Option<&Arc<MerkleNode<K, V>>>,
) -> Vec<Arc<MerkleNode<K, V>>> {
    let mut result = Vec::new();
    let mut pending = root.into_iter().cloned().collect::<Vec<_>>();
    while let Some(node) = pending.pop() {
        for child in node.children.iter().rev().flatten() {
            pending.push(Arc::clone(child));
        }
        result.push(node);
    }
    result
}

fn collect_node_identities<K, V>(
    root: Option<&Arc<MerkleNode<K, V>>>,
) -> HashSet<*const MerkleNode<K, V>> {
    enumerate_nodes_preorder(root)
        .iter()
        .map(Arc::as_ptr)
        .collect()
}
