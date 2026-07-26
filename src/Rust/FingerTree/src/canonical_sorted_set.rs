//! Canonical persistent sorted set built on a zip-zip tree.
//!
//! [`CanonicalSortedSet`] is *history independent*: two sets holding the same elements under the
//! same policy have the same shape, no matter what sequence of insertions and removals produced
//! them. Shape is decided by per-element ranks derived from a keyed hash rather than from insertion
//! order, so the set is canonical and structurally comparable across versions and across ports.
//!
//! Ranks come from a [`ZipTreeRankPolicy`], which combines a [`ZipTreeComparer`] with a
//! [`ZipTreeRankHash`] and derives ranks through HMAC-SHA-256 over a policy-bound key. The HMAC step
//! makes the shape resistant to adversarially chosen elements; it cannot repair a rank hash that is
//! unstable or inconsistent with the comparer's equivalence classes, so those remain caller
//! obligations. Policy construction failures surface as [`ZipTreePolicyError`], operations that mix
//! incompatible policies as [`CanonicalSetError`], and audit failures as
//! [`CanonicalSetInvariantError`].

use hmac::{Hmac, Mac};
use sha2::{Digest, Sha256};
use std::cmp::Ordering;
use std::error::Error;
use std::fmt;
use std::sync::{Arc, OnceLock};

const MINIMUM_RANK_KEY_BYTES: usize = 32;
const EMPTY_LEFT_DIGEST: u64 = 0x243f_6a88_85a3_08d3;
const EMPTY_RIGHT_DIGEST: u64 = 0x1319_8a2e_0370_7344;

/// Compares values for canonical sorted-set ordering and equivalence.
///
/// Implementations must define a stable total preorder. Returning [`Ordering::Equal`] makes two
/// values members of the same set-equivalence class.
pub trait ZipTreeComparer<T>: Send + Sync {
    /// Compares two values.
    fn compare(&self, left: &T, right: &T) -> Ordering;
}

impl<T, F> ZipTreeComparer<T> for F
where
    F: Fn(&T, &T) -> Ordering + Send + Sync,
{
    fn compare(&self, left: &T, right: &T) -> Ordering {
        self(left, right)
    }
}

/// Supplies a deterministic 64-bit rank-hash input for a canonical sorted set.
///
/// The result must be stable and constant on the equivalence classes defined by the associated
/// [`ZipTreeComparer`]. HMAC cannot repair instability or collisions introduced here.
pub trait ZipTreeRankHash<T>: Send + Sync {
    /// Computes the deterministic rank-hash input.
    fn rank_hash(&self, value: &T) -> u64;
}

impl<T, F> ZipTreeRankHash<T> for F
where
    F: Fn(&T) -> u64 + Send + Sync,
{
    fn rank_hash(&self, value: &T) -> u64 {
        self(value)
    }
}

/// A stable, explicitly specified hash input for natural-order zip-tree policies.
///
/// This trait deliberately excludes `usize`, `isize`, and `std::hash::Hash`: their width or the
/// standard library's hash algorithm is not a cross-platform reproducibility contract.
pub trait StableRankHash {
    /// Returns a deterministic 64-bit value.
    fn stable_rank_hash(&self) -> u64;
}

macro_rules! stable_unsigned_hash {
    ($($type:ty),+ $(,)?) => {
        $(
            impl StableRankHash for $type {
                fn stable_rank_hash(&self) -> u64 {
                    u64::from(*self)
                }
            }
        )+
    };
}

macro_rules! stable_signed_hash {
    ($(($signed:ty, $unsigned:ty)),+ $(,)?) => {
        $(
            impl StableRankHash for $signed {
                fn stable_rank_hash(&self) -> u64 {
                    u64::from(*self as $unsigned)
                }
            }
        )+
    };
}

stable_unsigned_hash!(u8, u16, u32, u64);
stable_signed_hash!((i8, u8), (i16, u16), (i32, u32), (i64, u64));

impl StableRankHash for bool {
    fn stable_rank_hash(&self) -> u64 {
        u64::from(*self)
    }
}

impl StableRankHash for char {
    fn stable_rank_hash(&self) -> u64 {
        u64::from(u32::from(*self))
    }
}

impl StableRankHash for str {
    fn stable_rank_hash(&self) -> u64 {
        stable_byte_hash(self.as_bytes())
    }
}

impl StableRankHash for String {
    fn stable_rank_hash(&self) -> u64 {
        self.as_str().stable_rank_hash()
    }
}

impl StableRankHash for [u8] {
    fn stable_rank_hash(&self) -> u64 {
        stable_byte_hash(self)
    }
}

impl StableRankHash for Vec<u8> {
    fn stable_rank_hash(&self) -> u64 {
        self.as_slice().stable_rank_hash()
    }
}

fn stable_byte_hash(bytes: &[u8]) -> u64 {
    // FNV-1a is used only as a pinned 64-bit encoding-to-rank-hash mapping. The HMAC layer, not
    // FNV, provides pseudorandomization when the caller supplies a secret key.
    let mut hash = 0xcbf2_9ce4_8422_2325_u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    hash
}

/// Natural [`Ord`] comparison for a zip-tree rank policy.
#[derive(Clone, Copy, Debug, Default)]
pub struct NaturalZipTreeComparer;

impl<T: Ord> ZipTreeComparer<T> for NaturalZipTreeComparer {
    fn compare(&self, left: &T, right: &T) -> Ordering {
        left.cmp(right)
    }
}

/// Uses [`StableRankHash`] as the deterministic rank-hash input.
#[derive(Clone, Copy, Debug, Default)]
pub struct StableZipTreeRankHash;

impl<T: StableRankHash> ZipTreeRankHash<T> for StableZipTreeRankHash {
    fn rank_hash(&self, value: &T) -> u64 {
        value.stable_rank_hash()
    }
}

/// An error constructing a rank policy.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum ZipTreePolicyError {
    /// A caller-retained HMAC key was shorter than the required 32 bytes.
    RankKeyTooShort {
        /// Required minimum length.
        minimum: usize,
        /// Supplied length.
        actual: usize,
    },
    /// The operating system cryptographic random source was unavailable.
    EntropyUnavailable,
}

impl fmt::Display for ZipTreePolicyError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::RankKeyTooShort { minimum, actual } => write!(
                formatter,
                "a zip-tree rank key must contain at least {minimum} bytes; received {actual}",
            ),
            Self::EntropyUnavailable => formatter
                .write_str("the operating system cryptographic random source is unavailable"),
        }
    }
}

impl Error for ZipTreePolicyError {}

struct RankPolicyInner<T> {
    comparer: Arc<dyn ZipTreeComparer<T>>,
    rank_hash: Arc<dyn ZipTreeRankHash<T>>,
    rank_key: Box<[u8]>,
    public_seed: Option<u64>,
}

/// Retained ordering, deterministic hash, and HMAC-key policy for a canonical sorted set.
///
/// Policy identity is representation compatibility state. Cloning this value preserves identity;
/// independently constructing an equivalent policy reproduces ranks and topology but does not make
/// set algebra implicitly compatible.
pub struct ZipTreeRankPolicy<T> {
    inner: Arc<RankPolicyInner<T>>,
}

impl<T> Clone for ZipTreeRankPolicy<T> {
    fn clone(&self) -> Self {
        Self {
            inner: Arc::clone(&self.inner),
        }
    }
}

impl<T> fmt::Debug for ZipTreeRankPolicy<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("ZipTreeRankPolicy")
            .field("public_seed", &self.inner.public_seed)
            .field("key_bytes", &"<redacted>")
            .finish_non_exhaustive()
    }
}

impl<T> ZipTreeRankPolicy<T> {
    /// Creates a fresh policy with an unexposed 32-byte operating-system-random HMAC key.
    ///
    /// Each call creates a distinct rank space. Retain and clone the returned policy for all
    /// versions that must participate in canonical algebra.
    pub fn random<C, H>(comparer: C, rank_hash: H) -> Result<Self, ZipTreePolicyError>
    where
        C: ZipTreeComparer<T> + 'static,
        H: ZipTreeRankHash<T> + 'static,
    {
        let mut rank_key = [0_u8; MINIMUM_RANK_KEY_BYTES];
        getrandom::fill(&mut rank_key).map_err(|_| ZipTreePolicyError::EntropyUnavailable)?;
        Ok(Self::from_parts(
            comparer,
            rank_hash,
            rank_key.to_vec().into_boxed_slice(),
            None,
        ))
    }

    /// Creates a reproducible policy from a public seed.
    ///
    /// The 32-byte HMAC key is SHA-256 of ASCII `ZZT2` followed by the seed in big-endian order,
    /// matching the C# reference policy. A public seed is deterministic mixing, not a secret
    /// adversarial-security boundary.
    pub fn seeded<C, H>(seed: u64, comparer: C, rank_hash: H) -> Self
    where
        C: ZipTreeComparer<T> + 'static,
        H: ZipTreeRankHash<T> + 'static,
    {
        let mut material = Vec::with_capacity(12);
        material.extend_from_slice(b"ZZT2");
        material.extend_from_slice(&seed.to_be_bytes());
        let rank_key = sha256(&material);
        Self::from_parts(
            comparer,
            rank_hash,
            rank_key.to_vec().into_boxed_slice(),
            Some(seed),
        )
    }

    /// Creates a policy from caller-retained HMAC key material.
    ///
    /// At least 32 bytes are required. The policy owns a copy and never exposes it.
    pub fn keyed<C, H>(
        rank_key: &[u8],
        comparer: C,
        rank_hash: H,
    ) -> Result<Self, ZipTreePolicyError>
    where
        C: ZipTreeComparer<T> + 'static,
        H: ZipTreeRankHash<T> + 'static,
    {
        if rank_key.len() < MINIMUM_RANK_KEY_BYTES {
            return Err(ZipTreePolicyError::RankKeyTooShort {
                minimum: MINIMUM_RANK_KEY_BYTES,
                actual: rank_key.len(),
            });
        }
        Ok(Self::from_parts(
            comparer,
            rank_hash,
            rank_key.to_vec().into_boxed_slice(),
            None,
        ))
    }

    fn from_parts<C, H>(
        comparer: C,
        rank_hash: H,
        rank_key: Box<[u8]>,
        public_seed: Option<u64>,
    ) -> Self
    where
        C: ZipTreeComparer<T> + 'static,
        H: ZipTreeRankHash<T> + 'static,
    {
        Self {
            inner: Arc::new(RankPolicyInner {
                comparer: Arc::new(comparer),
                rank_hash: Arc::new(rank_hash),
                rank_key,
                public_seed,
            }),
        }
    }

    /// Returns the reproducibility seed for a publicly seeded policy.
    pub fn public_seed(&self) -> Option<u64> {
        self.inner.public_seed
    }

    /// Returns whether two handles retain the exact same policy object.
    pub fn is_same_policy(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.inner, &other.inner)
    }

    fn compare(&self, left: &T, right: &T) -> Ordering {
        self.inner.comparer.compare(left, right)
    }

    fn get_rank(&self, value: &T) -> Rank {
        let source = self.inner.rank_hash.rank_hash(value).to_be_bytes();
        let digest = hmac_sha256(&self.inner.rank_key, &source);
        Rank {
            geometric: u64::from_be_bytes(digest[0..8].try_into().expect("fixed digest word"))
                .leading_zeros(),
            secondary: u64::from_be_bytes(digest[8..16].try_into().expect("fixed digest word")),
            content: u64::from_be_bytes(digest[16..24].try_into().expect("fixed digest word")),
        }
    }
}

impl<T> ZipTreeRankPolicy<T>
where
    T: Ord + StableRankHash + 'static,
{
    /// Creates a fresh natural-order policy with an unexposed random HMAC key.
    pub fn random_natural() -> Result<Self, ZipTreePolicyError> {
        Self::random(NaturalZipTreeComparer, StableZipTreeRankHash)
    }

    /// Creates a publicly seeded natural-order policy with stable built-in hashing.
    pub fn seeded_natural(seed: u64) -> Self {
        Self::seeded(seed, NaturalZipTreeComparer, StableZipTreeRankHash)
    }

    /// Creates a caller-keyed natural-order policy with stable built-in hashing.
    pub fn keyed_natural(rank_key: &[u8]) -> Result<Self, ZipTreePolicyError> {
        Self::keyed(rank_key, NaturalZipTreeComparer, StableZipTreeRankHash)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Rank {
    geometric: u32,
    secondary: u64,
    content: u64,
}

/// An operation error caused by incompatible or incoherent rank policy.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CanonicalSetError {
    /// Algebra or diff received a set retaining a different policy object.
    IncompatiblePolicy,
    /// Comparer-equivalent values produced different ranks.
    IncoherentRankHash,
}

impl fmt::Display for CanonicalSetError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IncompatiblePolicy => formatter.write_str(
                "canonical set algebra and diff require the same retained rank-policy object",
            ),
            Self::IncoherentRankHash => formatter.write_str(
                "the rank hash is not constant on the set comparer's equivalence classes",
            ),
        }
    }
}

impl Error for CanonicalSetError {}

/// A structural invariant violation found by [`CanonicalSortedSet::validate_structure`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CanonicalSetInvariantError {
    /// In-order values are not strictly comparer-ordered.
    SearchOrder,
    /// A child has higher Cartesian priority than its parent.
    HeapOrder,
    /// A stored rank cannot be reproduced from the retained policy.
    RankMismatch,
    /// A node has incorrect cached count or height metadata.
    NodeMetadata,
    /// Root metadata disagrees with the complete traversal.
    RootMetadata,
}

impl fmt::Display for CanonicalSetInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::SearchOrder => "canonical sorted-set search order is invalid",
            Self::HeapOrder => "a canonical sorted-set child outranks its parent",
            Self::RankMismatch => "a canonical sorted-set rank is not reproducible",
            Self::NodeMetadata => "canonical sorted-set node metadata is invalid",
            Self::RootMetadata => "canonical sorted-set root metadata is invalid",
        };
        formatter.write_str(message)
    }
}

impl Error for CanonicalSetInvariantError {}

/// Statistics returned by complete canonical sorted-set validation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct CanonicalSortedSetStatistics {
    /// Item count.
    pub len: usize,
    /// Maximum root-to-node count.
    pub height: usize,
    /// Largest geometric rank coordinate.
    pub maximum_geometric_rank: u32,
    /// Number of nodes repeating an earlier geometric/secondary priority pair.
    pub priority_collision_count: usize,
}

/// An owned semantic difference between two policy-compatible canonical sets.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CanonicalSetDifference<T> {
    /// Values present only in the left operand.
    pub only_in_left: Vec<T>,
    /// Values present only in the right operand.
    pub only_in_right: Vec<T>,
}

impl<T> CanonicalSetDifference<T> {
    /// Returns whether the sets had no semantic differences.
    pub fn is_empty(&self) -> bool {
        self.only_in_left.is_empty() && self.only_in_right.is_empty()
    }
}

struct Node<T> {
    item: T,
    rank: Rank,
    left: Option<Arc<Node<T>>>,
    right: Option<Arc<Node<T>>>,
    count: usize,
    height: usize,
    digest: OnceLock<u64>,
}

type NodeLink<T> = Option<Arc<Node<T>>>;

impl<T> Node<T> {
    fn new(item: T, rank: Rank, left: Option<Arc<Self>>, right: Option<Arc<Self>>) -> Self {
        Self {
            count: 1
                + left.as_ref().map_or(0, |node| node.count)
                + right.as_ref().map_or(0, |node| node.count),
            height: 1 + left
                .as_ref()
                .map_or(0, |node| node.height)
                .max(right.as_ref().map_or(0, |node| node.height)),
            item,
            rank,
            left,
            right,
            digest: OnceLock::new(),
        }
    }
}

impl<T> Drop for Node<T> {
    fn drop(&mut self) {
        // Avoid recursively dropping an adversarial height-n chain. Shared descendants merely have
        // this version's reference released; uniquely owned descendants are dismantled iteratively.
        let mut pending = Vec::new();
        if let Some(left) = self.left.take() {
            pending.push(left);
        }
        if let Some(right) = self.right.take() {
            pending.push(right);
        }
        while let Some(node) = pending.pop() {
            match Arc::try_unwrap(node) {
                Ok(mut owned) => {
                    if let Some(left) = owned.left.take() {
                        pending.push(left);
                    }
                    if let Some(right) = owned.right.take() {
                        pending.push(right);
                    }
                }
                Err(shared) => drop(shared),
            }
        }
    }
}

/// An immutable policy-canonical sorted set backed by a zip-zip-inspired Cartesian tree.
///
/// Rank is HMAC-derived from each value's deterministic equivalence-class hash. Under one coherent
/// policy and the same retained representatives, contents determine topology independent of update
/// history. Point operations are expected O(log n), but colliding rank hashes can force height n;
/// all traversals, updates, validation, digest work, and destruction remain call-stack safe.
pub struct CanonicalSortedSet<T> {
    root: Option<Arc<Node<T>>>,
    policy: ZipTreeRankPolicy<T>,
}

impl<T> Clone for CanonicalSortedSet<T> {
    fn clone(&self) -> Self {
        Self {
            root: self.root.as_ref().map(Arc::clone),
            policy: self.policy.clone(),
        }
    }
}

impl<T: fmt::Debug> fmt::Debug for CanonicalSortedSet<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("CanonicalSortedSet")
            .field("len", &self.len())
            .field("height", &self.height())
            .field("items", &self.iter().collect::<Vec<_>>())
            .finish()
    }
}

impl<T> CanonicalSortedSet<T> {
    /// Creates an empty set retaining `policy`.
    pub fn new(policy: ZipTreeRankPolicy<T>) -> Self {
        Self { root: None, policy }
    }

    /// Returns the retained rank policy.
    pub fn policy(&self) -> &ZipTreeRankPolicy<T> {
        &self.policy
    }

    /// Returns the item count.
    pub fn len(&self) -> usize {
        self.root.as_ref().map_or(0, |node| node.count)
    }

    /// Returns whether the set is empty.
    pub fn is_empty(&self) -> bool {
        self.root.is_none()
    }

    /// Returns the cached tree height.
    pub fn height(&self) -> usize {
        self.root.as_ref().map_or(0, |node| node.height)
    }

    /// Returns whether two values share the same root and policy identity.
    ///
    /// This is the Rust observable for persistent no-op identity.
    pub fn is_same_version(&self, other: &Self) -> bool {
        self.policy.is_same_policy(&other.policy)
            && match (&self.root, &other.root) {
                (None, None) => true,
                (Some(left), Some(right)) => Arc::ptr_eq(left, right),
                _ => false,
            }
    }

    /// Returns whether the set contains a comparer-equivalent value.
    pub fn contains(&self, value: &T) -> bool {
        self.find_node(value).is_some()
    }

    /// Returns the retained representative comparer-equivalent to `value`.
    pub fn get(&self, value: &T) -> Option<&T> {
        self.find_node(value).map(|node| &node.item)
    }

    /// Iterates in comparer order.
    pub fn iter(&self) -> CanonicalSortedSetIter<'_, T> {
        CanonicalSortedSetIter::new(self.root.as_deref())
    }

    /// Returns a memoized non-cryptographic digest of the canonical tree, or zero when empty.
    ///
    /// Under one coherent policy, inequality proves set inequality. Equality is not proof, and
    /// digests from distinct policy objects are not generally comparable.
    pub fn content_hash(&self) -> u64 {
        let Some(root) = &self.root else {
            return 0;
        };
        if let Some(digest) = root.digest.get() {
            return *digest;
        }
        let mut pending = vec![(Arc::clone(root), false)];
        while let Some((node, expanded)) = pending.pop() {
            if node.digest.get().is_some() {
                continue;
            }
            if !expanded {
                pending.push((Arc::clone(&node), true));
                if let Some(right) = &node.right
                    && right.digest.get().is_none()
                {
                    pending.push((Arc::clone(right), false));
                }
                if let Some(left) = &node.left
                    && left.digest.get().is_none()
                {
                    pending.push((Arc::clone(left), false));
                }
                continue;
            }
            let left_hash = node.left.as_ref().map_or(EMPTY_LEFT_DIGEST, |left| {
                *left.digest.get().expect("expanded left digest")
            });
            let right_hash = node.right.as_ref().map_or(EMPTY_RIGHT_DIGEST, |right| {
                *right.digest.get().expect("expanded right digest")
            });
            let digest =
                mix64(node.rank.content ^ left_hash.rotate_left(17) ^ right_hash.rotate_left(43));
            let _ = node.digest.set(digest);
        }
        *root.digest.get().expect("root digest published")
    }

    /// Returns a pre-order sequence of `(left_count, right_count)` pairs for topology diagnostics.
    pub fn topology_signature(&self) -> Vec<(usize, usize)> {
        let mut result = Vec::with_capacity(self.len());
        let mut pending = Vec::new();
        if let Some(root) = &self.root {
            pending.push(Arc::clone(root));
        }
        while let Some(node) = pending.pop() {
            result.push((
                node.left.as_ref().map_or(0, |child| child.count),
                node.right.as_ref().map_or(0, |child| child.count),
            ));
            if let Some(right) = &node.right {
                pending.push(Arc::clone(right));
            }
            if let Some(left) = &node.left {
                pending.push(Arc::clone(left));
            }
        }
        result
    }

    /// Validates search order, rank reproducibility, heap order, and cached metadata.
    pub fn validate_structure(
        &self,
    ) -> Result<CanonicalSortedSetStatistics, CanonicalSetInvariantError> {
        let Some(root) = &self.root else {
            return Ok(CanonicalSortedSetStatistics {
                len: 0,
                height: 0,
                maximum_geometric_rank: 0,
                priority_collision_count: 0,
            });
        };

        let mut pending = vec![(Arc::clone(root), 1_usize)];
        let mut count = 0_usize;
        let mut maximum_depth = 0_usize;
        let mut maximum_geometric_rank = 0_u32;
        let mut priorities = Vec::with_capacity(self.len());
        while let Some((node, depth)) = pending.pop() {
            if node.rank != self.policy.get_rank(&node.item) {
                return Err(CanonicalSetInvariantError::RankMismatch);
            }
            if let Some(left) = &node.left {
                if higher_node(left, &node, &self.policy) {
                    return Err(CanonicalSetInvariantError::HeapOrder);
                }
                pending.push((Arc::clone(left), depth + 1));
            }
            if let Some(right) = &node.right {
                if higher_node(right, &node, &self.policy) {
                    return Err(CanonicalSetInvariantError::HeapOrder);
                }
                pending.push((Arc::clone(right), depth + 1));
            }
            let expected_count = 1
                + node.left.as_ref().map_or(0, |child| child.count)
                + node.right.as_ref().map_or(0, |child| child.count);
            let expected_height = 1 + node
                .left
                .as_ref()
                .map_or(0, |child| child.height)
                .max(node.right.as_ref().map_or(0, |child| child.height));
            if node.count != expected_count || node.height != expected_height {
                return Err(CanonicalSetInvariantError::NodeMetadata);
            }
            count += 1;
            maximum_depth = maximum_depth.max(depth);
            maximum_geometric_rank = maximum_geometric_rank.max(node.rank.geometric);
            priorities.push((node.rank.geometric, node.rank.secondary));
        }

        let mut previous = None;
        for item in self.iter() {
            if previous.is_some_and(|left| self.policy.compare(left, item) != Ordering::Less) {
                return Err(CanonicalSetInvariantError::SearchOrder);
            }
            previous = Some(item);
        }
        if count != self.len() || maximum_depth != self.height() {
            return Err(CanonicalSetInvariantError::RootMetadata);
        }

        priorities.sort_unstable();
        let priority_collision_count = priorities
            .windows(2)
            .filter(|pair| pair[0] == pair[1])
            .count();
        Ok(CanonicalSortedSetStatistics {
            len: count,
            height: maximum_depth,
            maximum_geometric_rank,
            priority_collision_count,
        })
    }

    /// Bulk-builds a canonical set, retaining the first representative of each equivalence class.
    ///
    /// Construction takes ownership of each item and does not require `T: Clone`.
    pub fn from_items<I>(policy: ZipTreeRankPolicy<T>, values: I) -> Result<Self, CanonicalSetError>
    where
        I: IntoIterator<Item = T>,
    {
        let mut values = values.into_iter().collect::<Vec<_>>();
        values.sort_by(|left, right| policy.compare(left, right));
        let mut unique = Vec::with_capacity(values.len());
        for value in values {
            let rank = policy.get_rank(&value);
            if let Some((previous, previous_rank)) = unique.last()
                && policy.compare(previous, &value) == Ordering::Equal
            {
                if *previous_rank != rank {
                    return Err(CanonicalSetError::IncoherentRankHash);
                }
                continue;
            }
            unique.push((value, rank));
        }
        Ok(Self {
            root: build_canonical(unique, &policy),
            policy,
        })
    }

    /// Returns an empty set retaining this policy without cloning an item.
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            return self.clone();
        }
        Self::new(self.policy.clone())
    }

    /// Compares mathematical set contents under this set's comparer.
    ///
    /// Same-policy comparison uses count and memoized digest rejection followed by a lockstep
    /// structural traversal that prunes shared nodes. Cross-policy comparison stably sorts and
    /// deduplicates borrowed representatives under this receiver's comparer, so it does not require
    /// `T: Clone`. Different comparer semantics can make the receiver-defined result asymmetric.
    pub fn set_equals(&self, other: &Self) -> bool {
        if self.policy.is_same_policy(&other.policy) {
            if self.len() != other.len() || self.content_hash() != other.content_hash() {
                return false;
            }
            return nodes_equal(self.root.as_deref(), other.root.as_deref(), &self.policy);
        }
        let mut other_values = other.iter().collect::<Vec<_>>();
        other_values.sort_by(|left, right| self.policy.compare(left, right));
        other_values.dedup_by(|left, right| self.policy.compare(left, right) == Ordering::Equal);
        self.len() == other_values.len()
            && self
                .iter()
                .zip(other_values)
                .all(|(left, right)| self.policy.compare(left, right) == Ordering::Equal)
    }

    fn ensure_compatible(&self, other: &Self) -> Result<(), CanonicalSetError> {
        if self.policy.is_same_policy(&other.policy) {
            Ok(())
        } else {
            Err(CanonicalSetError::IncompatiblePolicy)
        }
    }

    fn ensure_equivalent_ranks(&self, left: &T, right: &T) -> Result<(), CanonicalSetError> {
        if self.policy.get_rank(left) == self.policy.get_rank(right) {
            Ok(())
        } else {
            Err(CanonicalSetError::IncoherentRankHash)
        }
    }

    fn find_node(&self, value: &T) -> Option<&Node<T>> {
        let mut node = self.root.as_deref();
        while let Some(current) = node {
            match self.policy.compare(value, &current.item) {
                Ordering::Equal => return Some(current),
                Ordering::Less => node = current.left.as_deref(),
                Ordering::Greater => node = current.right.as_deref(),
            }
        }
        None
    }
}

impl<T: Clone> CanonicalSortedSet<T> {
    /// Inserts a value, preserving root identity when an equivalent representative already exists.
    pub fn insert(&self, value: T) -> Result<Self, CanonicalSetError> {
        if let Some(existing) = self.find_node(&value) {
            if existing.rank != self.policy.get_rank(&value) {
                return Err(CanonicalSetError::IncoherentRankHash);
            }
            return Ok(self.clone());
        }
        let rank = self.policy.get_rank(&value);
        let item = Arc::new(Node::new(value, rank, None, None));
        Ok(Self {
            root: Some(insert_node(self.root.as_ref(), item, &self.policy)),
            policy: self.policy.clone(),
        })
    }

    /// Removes a comparer-equivalent value, preserving root identity when absent.
    pub fn remove(&self, value: &T) -> Self {
        let (root, removed) = remove_node(self.root.as_ref(), value, &self.policy);
        if !removed {
            return self.clone();
        }
        Self {
            root,
            policy: self.policy.clone(),
        }
    }

    /// Returns the union with a set retaining the same policy object.
    pub fn union(&self, other: &Self) -> Result<Self, CanonicalSetError> {
        self.ensure_compatible(other)?;
        if self.is_same_version(other) {
            return Ok(self.clone());
        }
        let mut result = self.clone();
        for value in other.iter() {
            result = result.insert(value.clone())?;
        }
        Ok(result)
    }

    /// Returns the intersection with a set retaining the same policy object.
    pub fn intersection(&self, other: &Self) -> Result<Self, CanonicalSetError> {
        self.ensure_compatible(other)?;
        if self.is_same_version(other) {
            return Ok(self.clone());
        }
        let mut result = Self::new(self.policy.clone());
        for value in self.iter() {
            if let Some(equivalent) = other.get(value) {
                self.ensure_equivalent_ranks(value, equivalent)?;
                result = result.insert(value.clone())?;
            }
        }
        Ok(result)
    }

    /// Returns the set difference from a set retaining the same policy object.
    pub fn except(&self, other: &Self) -> Result<Self, CanonicalSetError> {
        self.ensure_compatible(other)?;
        if self.is_same_version(other) {
            return Ok(Self::new(self.policy.clone()));
        }
        let mut result = self.clone();
        for value in other.iter() {
            if let Some(equivalent) = result.get(value) {
                result.ensure_equivalent_ranks(equivalent, value)?;
            }
            result = result.remove(value);
        }
        Ok(result)
    }

    /// Computes an owned semantic diff under the shared retained policy.
    pub fn diff(&self, other: &Self) -> Result<CanonicalSetDifference<T>, CanonicalSetError> {
        self.ensure_compatible(other)?;
        if self.is_same_version(other) {
            return Ok(CanonicalSetDifference {
                only_in_left: Vec::new(),
                only_in_right: Vec::new(),
            });
        }
        let mut left = self.iter().peekable();
        let mut right = other.iter().peekable();
        let mut only_in_left = Vec::new();
        let mut only_in_right = Vec::new();
        loop {
            match (left.peek(), right.peek()) {
                (Some(left_value), Some(right_value)) => {
                    match self.policy.compare(left_value, right_value) {
                        Ordering::Less => {
                            only_in_left.push((*left.next().expect("peeked left")).clone());
                        }
                        Ordering::Greater => {
                            only_in_right.push((*right.next().expect("peeked right")).clone());
                        }
                        Ordering::Equal => {
                            self.ensure_equivalent_ranks(left_value, right_value)?;
                            let _ = left.next();
                            let _ = right.next();
                        }
                    }
                }
                (Some(_), None) => {
                    only_in_left.extend(left.cloned());
                    break;
                }
                (None, Some(_)) => {
                    only_in_right.extend(right.cloned());
                    break;
                }
                (None, None) => break,
            }
        }
        Ok(CanonicalSetDifference {
            only_in_left,
            only_in_right,
        })
    }
}

impl<'a, T> IntoIterator for &'a CanonicalSortedSet<T> {
    type Item = &'a T;
    type IntoIter = CanonicalSortedSetIter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

/// Borrowed in-order iterator over a [`CanonicalSortedSet`].
/// Presence-discriminated canonical-set cursor search.
pub struct CanonicalCursorSearch<T> {
    pub found: bool,
    pub cursor: CanonicalSortedSetCursor<T>,
}

/// Immutable policy-preserving root-plus-rank cursor.
pub struct CanonicalSortedSetCursor<T> {
    set: CanonicalSortedSet<T>,
    position: usize,
}

impl<T> Clone for CanonicalSortedSetCursor<T> {
    fn clone(&self) -> Self {
        Self {
            set: self.set.clone(),
            position: self.position,
        }
    }
}

impl<T> CanonicalSortedSet<T> {
    fn cursor_bound_rank(&self, value: &T, upper: bool) -> usize {
        let mut rank = 0;
        let mut node = self.root.as_deref();
        while let Some(current) = node {
            let comparison = self.policy.compare(&current.item, value);
            if comparison.is_lt() || upper && comparison.is_eq() {
                rank += current.left.as_ref().map_or(0, |child| child.count) + 1;
                node = current.right.as_deref();
            } else {
                node = current.left.as_deref();
            }
        }
        rank
    }
    /// Selects the item at `rank` in O(h) using the cached subtree counts.
    ///
    /// Returns `None` when `rank` addresses the end gap or lies beyond it.
    fn cursor_item_at(&self, rank: usize) -> Option<&T> {
        let mut node = self.root.as_deref();
        let mut remaining = rank;
        while let Some(current) = node {
            let left_count = current.left.as_ref().map_or(0, |child| child.count);
            match remaining.cmp(&left_count) {
                Ordering::Less => node = current.left.as_deref(),
                Ordering::Equal => return Some(&current.item),
                Ordering::Greater => {
                    remaining -= left_count + 1;
                    node = current.right.as_deref();
                }
            }
        }
        None
    }
    /// Creates a cursor at the gap `position` in `0..=len`, or `None` when `position` exceeds the
    /// element count.
    pub fn cursor_at(&self, position: usize) -> Option<CanonicalSortedSetCursor<T>> {
        (position <= self.len()).then(|| CanonicalSortedSetCursor {
            set: self.clone(),
            position,
        })
    }
    /// Creates a cursor before the first element not below `value`, that is, where `value` would be
    /// inserted. O(log n).
    pub fn cursor_at_lower_bound(&self, value: &T) -> CanonicalSortedSetCursor<T> {
        self.cursor_at(self.cursor_bound_rank(value, false))
            .expect("lower bound is valid")
    }
    /// Creates a cursor after any element equivalent to `value`, that is, before the first element
    /// strictly above it. O(log n).
    pub fn cursor_at_upper_bound(&self, value: &T) -> CanonicalSortedSetCursor<T> {
        self.cursor_at(self.cursor_bound_rank(value, true))
            .expect("upper bound is valid")
    }
    /// Seeks to `value` and reports whether it is present.
    ///
    /// The returned cursor is usable either way: on a miss it sits at the lower bound, which is
    /// where `value` would be inserted. Membership is decided by the retained policy's comparer,
    /// not by `PartialEq`. O(log n).
    pub fn find_cursor(&self, value: &T) -> CanonicalCursorSearch<T> {
        let cursor = self.cursor_at_lower_bound(value);
        CanonicalCursorSearch {
            found: cursor
                .peek_next()
                .is_some_and(|item| self.policy.compare(item, value).is_eq()),
            cursor,
        }
    }
}

impl<T> CanonicalSortedSetCursor<T> {
    /// Returns the element count of the set this cursor is positioned in.
    pub fn len(&self) -> usize {
        self.set.len()
    }
    /// Returns `true` when the set this cursor is positioned in holds no elements.
    pub fn is_empty(&self) -> bool {
        self.set.is_empty()
    }
    /// Returns the cursor's gap index in `0..=len`, which is also the rank of the next element.
    pub fn position(&self) -> usize {
        self.position
    }
    /// Returns `true` when the gap is before the first element.
    pub fn is_at_start(&self) -> bool {
        self.position == 0
    }
    /// Returns `true` when the gap is after the last element.
    pub fn is_at_end(&self) -> bool {
        self.position == self.len()
    }
    /// Borrows the element immediately before the gap, or `None` at the start. O(log n).
    pub fn peek_previous(&self) -> Option<&T> {
        self.position
            .checked_sub(1)
            .and_then(|rank| self.set.cursor_item_at(rank))
    }
    /// Borrows the element immediately after the gap, or `None` at the end. O(log n).
    pub fn peek_next(&self) -> Option<&T> {
        self.set.cursor_item_at(self.position)
    }
    /// Returns a cursor one position earlier, or `None` at the start. The receiver is unchanged.
    pub fn move_previous(&self) -> Option<Self> {
        self.position
            .checked_sub(1)
            .and_then(|position| self.set.cursor_at(position))
    }
    /// Returns a cursor one position later, or `None` at the end. The receiver is unchanged.
    pub fn move_next(&self) -> Option<Self> {
        (!self.is_at_end()).then(|| Self {
            set: self.set.clone(),
            position: self.position + 1,
        })
    }
    /// Jumps to the gap at `position` within the same set version, or `None` when `position`
    /// exceeds the element count.
    pub fn seek_rank(&self, position: usize) -> Option<Self> {
        if position == self.position {
            Some(self.clone())
        } else {
            self.set.cursor_at(position)
        }
    }
    /// Borrows the set version this cursor is positioned in.
    pub fn snapshot(&self) -> &CanonicalSortedSet<T> {
        &self.set
    }
}

impl<T: Clone> CanonicalSortedSetCursor<T> {
    /// Inserts `value` into the underlying set and returns a cursor just after it.
    ///
    /// The gap moves to `value`'s sorted position rather than staying where the receiver was: in a
    /// canonical set an element's place is decided by the ordering, not by where the cursor
    /// happened to be. Fails with [`CanonicalSetError`] when `value` cannot be ranked under the
    /// retained policy. The receiver keeps its own version.
    pub fn insert(&self, value: T) -> Result<Self, CanonicalSetError> {
        let position = self.set.cursor_bound_rank(&value, false);
        self.set.insert(value).map(|set| Self {
            set,
            position: position + 1,
        })
    }
    /// Removes the element before the gap and returns a cursor in its place, or `None` at the
    /// start.
    pub fn delete_previous(&self) -> Option<Self> {
        let position = self.position.checked_sub(1)?;
        let item = self.set.cursor_item_at(position)?.clone();
        Some(Self {
            set: self.set.remove(&item),
            position,
        })
    }
    /// Removes the element after the gap and returns a cursor in its place, or `None` at the end.
    pub fn delete_next(&self) -> Option<Self> {
        let item = self.set.cursor_item_at(self.position)?.clone();
        Some(Self {
            set: self.set.remove(&item),
            position: self.position,
        })
    }
}

/// Borrowing iterator over a [`CanonicalSortedSet`] in ascending order.
pub struct CanonicalSortedSetIter<'a, T> {
    stack: Vec<&'a Node<T>>,
    remaining: usize,
}

impl<'a, T> CanonicalSortedSetIter<'a, T> {
    fn new(root: Option<&'a Node<T>>) -> Self {
        let mut iterator = Self {
            stack: Vec::new(),
            remaining: root.map_or(0, |node| node.count),
        };
        iterator.push_left(root);
        iterator
    }

    fn push_left(&mut self, mut node: Option<&'a Node<T>>) {
        while let Some(current) = node {
            self.stack.push(current);
            node = current.left.as_deref();
        }
    }
}

impl<'a, T> Iterator for CanonicalSortedSetIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        let node = self.stack.pop()?;
        self.push_left(node.right.as_deref());
        self.remaining -= 1;
        Some(&node.item)
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.remaining, Some(self.remaining))
    }
}

impl<T> ExactSizeIterator for CanonicalSortedSetIter<'_, T> {}

impl<T> std::iter::FusedIterator for CanonicalSortedSetIter<'_, T> {}

struct BuilderNode<T> {
    item: Option<T>,
    rank: Rank,
    left: Option<usize>,
    right: Option<usize>,
    frozen: Option<Arc<Node<T>>>,
}

fn build_canonical<T>(
    values: Vec<(T, Rank)>,
    policy: &ZipTreeRankPolicy<T>,
) -> Option<Arc<Node<T>>> {
    if values.is_empty() {
        return None;
    }
    let mut nodes = Vec::<BuilderNode<T>>::with_capacity(values.len());
    let mut spine = Vec::<usize>::new();
    for (item, rank) in values {
        let index = nodes.len();
        let mut left = None;
        while let Some(previous_index) = spine.last().copied() {
            let previous = &nodes[previous_index];
            if !higher_values(
                &item,
                rank,
                previous.item.as_ref().expect("unfrozen builder item"),
                previous.rank,
                policy,
            ) {
                break;
            }
            left = spine.pop();
        }
        nodes.push(BuilderNode {
            item: Some(item),
            rank,
            left,
            right: None,
            frozen: None,
        });
        if let Some(parent) = spine.last().copied() {
            nodes[parent].right = Some(index);
        }
        spine.push(index);
    }
    let root = spine[0];
    let mut pending = vec![(root, false)];
    while let Some((index, expanded)) = pending.pop() {
        if expanded {
            let left = nodes[index]
                .left
                .and_then(|child| nodes[child].frozen.as_ref().map(Arc::clone));
            let right = nodes[index]
                .right
                .and_then(|child| nodes[child].frozen.as_ref().map(Arc::clone));
            let item = nodes[index].item.take().expect("builder item frozen once");
            let rank = nodes[index].rank;
            nodes[index].frozen = Some(Arc::new(Node::new(item, rank, left, right)));
            continue;
        }
        pending.push((index, true));
        if let Some(right) = nodes[index].right {
            pending.push((right, false));
        }
        if let Some(left) = nodes[index].left {
            pending.push((left, false));
        }
    }
    nodes[root].frozen.as_ref().map(Arc::clone)
}

fn insert_node<T: Clone>(
    root: Option<&Arc<Node<T>>>,
    item: Arc<Node<T>>,
    policy: &ZipTreeRankPolicy<T>,
) -> Arc<Node<T>> {
    let Some(root) = root else {
        return item;
    };
    let mut path = Vec::<(Arc<Node<T>>, bool)>::new();
    let mut node = Some(Arc::clone(root));
    while let Some(current) = node.as_ref().map(Arc::clone) {
        if higher_node(&item, &current, policy) {
            break;
        }
        let went_left = policy.compare(&item.item, &current.item) == Ordering::Less;
        node = if went_left {
            current.left.as_ref().map(Arc::clone)
        } else {
            current.right.as_ref().map(Arc::clone)
        };
        path.push((current, went_left));
    }
    let (left, right) = split_node(node, &item.item, policy);
    let mut result = Arc::new(Node::new(item.item.clone(), item.rank, left, right));
    while let Some((parent, went_left)) = path.pop() {
        result = if went_left {
            Arc::new(Node::new(
                parent.item.clone(),
                parent.rank,
                Some(result),
                parent.right.as_ref().map(Arc::clone),
            ))
        } else {
            Arc::new(Node::new(
                parent.item.clone(),
                parent.rank,
                parent.left.as_ref().map(Arc::clone),
                Some(result),
            ))
        };
    }
    result
}

fn split_node<T: Clone>(
    mut node: Option<Arc<Node<T>>>,
    item: &T,
    policy: &ZipTreeRankPolicy<T>,
) -> (NodeLink<T>, NodeLink<T>) {
    let mut path = Vec::<(Arc<Node<T>>, bool)>::new();
    while let Some(current) = node {
        let went_left = policy.compare(item, &current.item) == Ordering::Less;
        node = if went_left {
            current.left.as_ref().map(Arc::clone)
        } else {
            current.right.as_ref().map(Arc::clone)
        };
        path.push((current, went_left));
    }
    let mut left = None;
    let mut right = None;
    while let Some((current, went_left)) = path.pop() {
        if went_left {
            right = Some(Arc::new(Node::new(
                current.item.clone(),
                current.rank,
                right,
                current.right.as_ref().map(Arc::clone),
            )));
        } else {
            left = Some(Arc::new(Node::new(
                current.item.clone(),
                current.rank,
                current.left.as_ref().map(Arc::clone),
                left,
            )));
        }
    }
    (left, right)
}

fn remove_node<T: Clone>(
    root: Option<&Arc<Node<T>>>,
    item: &T,
    policy: &ZipTreeRankPolicy<T>,
) -> (Option<Arc<Node<T>>>, bool) {
    let mut path = Vec::<(Arc<Node<T>>, bool)>::new();
    let mut node = root.map(Arc::clone);
    while let Some(current) = node {
        match policy.compare(item, &current.item) {
            Ordering::Equal => {
                let mut result = merge_nodes(
                    current.left.as_ref().map(Arc::clone),
                    current.right.as_ref().map(Arc::clone),
                    policy,
                );
                while let Some((parent, went_left)) = path.pop() {
                    result = Some(if went_left {
                        Arc::new(Node::new(
                            parent.item.clone(),
                            parent.rank,
                            result,
                            parent.right.as_ref().map(Arc::clone),
                        ))
                    } else {
                        Arc::new(Node::new(
                            parent.item.clone(),
                            parent.rank,
                            parent.left.as_ref().map(Arc::clone),
                            result,
                        ))
                    });
                }
                return (result, true);
            }
            Ordering::Less => {
                node = current.left.as_ref().map(Arc::clone);
                path.push((current, true));
            }
            Ordering::Greater => {
                node = current.right.as_ref().map(Arc::clone);
                path.push((current, false));
            }
        }
    }
    (root.map(Arc::clone), false)
}

fn merge_nodes<T: Clone>(
    mut left: Option<Arc<Node<T>>>,
    mut right: Option<Arc<Node<T>>>,
    policy: &ZipTreeRankPolicy<T>,
) -> Option<Arc<Node<T>>> {
    let mut path = Vec::<(Arc<Node<T>>, bool)>::new();
    while let (Some(left_node), Some(right_node)) = (&left, &right) {
        if higher_node(left_node, right_node, policy) {
            let chosen = left.take().expect("matched left");
            left = chosen.right.as_ref().map(Arc::clone);
            path.push((chosen, true));
        } else {
            let chosen = right.take().expect("matched right");
            right = chosen.left.as_ref().map(Arc::clone);
            path.push((chosen, false));
        }
    }
    let mut result = left.or(right);
    while let Some((node, chose_left)) = path.pop() {
        result = Some(if chose_left {
            Arc::new(Node::new(
                node.item.clone(),
                node.rank,
                node.left.as_ref().map(Arc::clone),
                result,
            ))
        } else {
            Arc::new(Node::new(
                node.item.clone(),
                node.rank,
                result,
                node.right.as_ref().map(Arc::clone),
            ))
        });
    }
    result
}

fn higher_node<T>(left: &Node<T>, right: &Node<T>, policy: &ZipTreeRankPolicy<T>) -> bool {
    higher_values(&left.item, left.rank, &right.item, right.rank, policy)
}

fn higher_values<T>(
    left_item: &T,
    left: Rank,
    right_item: &T,
    right: Rank,
    policy: &ZipTreeRankPolicy<T>,
) -> bool {
    left.geometric
        .cmp(&right.geometric)
        .then_with(|| left.secondary.cmp(&right.secondary))
        .then_with(|| policy.compare(right_item, left_item))
        == Ordering::Greater
}

fn nodes_equal<T>(
    left: Option<&Node<T>>,
    right: Option<&Node<T>>,
    policy: &ZipTreeRankPolicy<T>,
) -> bool {
    let mut pending = vec![(left, right)];
    while let Some((left, right)) = pending.pop() {
        match (left, right) {
            (None, None) => {}
            (Some(left), Some(right)) => {
                if std::ptr::eq(left, right) {
                    continue;
                }
                if policy.compare(&left.item, &right.item) != Ordering::Equal {
                    return false;
                }
                pending.push((left.right.as_deref(), right.right.as_deref()));
                pending.push((left.left.as_deref(), right.left.as_deref()));
            }
            _ => return false,
        }
    }
    true
}

fn mix64(mut value: u64) -> u64 {
    value ^= value >> 30;
    value = value.wrapping_mul(0xbf58_476d_1ce4_e5b9);
    value ^= value >> 27;
    value = value.wrapping_mul(0x94d0_49bb_1331_11eb);
    value ^ (value >> 31)
}

fn hmac_sha256(key: &[u8], message: &[u8]) -> [u8; 32] {
    let mut hmac = Hmac::<Sha256>::new_from_slice(key).expect("HMAC accepts every key length");
    hmac.update(message);
    hmac.finalize().into_bytes().into()
}

fn sha256(message: &[u8]) -> [u8; 32] {
    Sha256::digest(message).into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    use std::sync::Barrier;
    use std::thread;

    #[derive(Debug)]
    struct NonCloneItem {
        key: i32,
        identity: Arc<()>,
    }

    fn compare_non_clone(left: &NonCloneItem, right: &NonCloneItem) -> Ordering {
        left.key.cmp(&right.key)
    }

    fn hash_non_clone(value: &NonCloneItem) -> u64 {
        value.key.stable_rank_hash()
    }

    fn non_clone_item(key: i32) -> NonCloneItem {
        NonCloneItem {
            key,
            identity: Arc::new(()),
        }
    }

    #[test]
    fn sha256_and_hmac_match_published_vectors() {
        assert_eq!(
            hex(&sha256(b"abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        assert_eq!(
            hex(&hmac_sha256(&[0x0b; 20], b"Hi There")),
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"
        );
    }

    #[test]
    fn stable_rank_hash_has_fixed_cross_platform_outputs() {
        assert_eq!(0_u8.stable_rank_hash(), 0);
        assert_eq!(u64::MAX.stable_rank_hash(), u64::MAX);
        assert_eq!((-1_i8).stable_rank_hash(), 0xff);
        assert_eq!((-2_i16).stable_rank_hash(), 0xfffe);
        assert_eq!((-1_i32).stable_rank_hash(), 0xffff_ffff);
        assert_eq!(i32::MIN.stable_rank_hash(), 0x8000_0000);
        assert_eq!((-1_i64).stable_rank_hash(), u64::MAX);
        assert_eq!(false.stable_rank_hash(), 0);
        assert_eq!(true.stable_rank_hash(), 1);
        assert_eq!('\u{1f4a3}'.stable_rank_hash(), 0x1f4a3);
        assert_eq!("".stable_rank_hash(), 0xcbf2_9ce4_8422_2325);
        assert_eq!("a".stable_rank_hash(), 0xaf63_dc4c_8601_ec8c);
        assert_eq!("hello".stable_rank_hash(), 0xa430_d846_80aa_bd0b);
        assert_eq!(
            [0_u8, 0xff].as_slice().stable_rank_hash(),
            0x0831_c907_b4ea_2b60
        );
        assert_eq!(
            String::from("hello").stable_rank_hash(),
            0xa430_d846_80aa_bd0b
        );
    }

    #[test]
    fn seeded_ranks_and_topology_match_the_csharp_zzt2_vector() {
        let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x0123_4567_89ab_cdef);
        let expected_ranks = [
            (
                -8,
                Rank {
                    geometric: 3,
                    secondary: 0x4ef7_4626_3c07_be78,
                    content: 0xdff3_83ca_3c9b_d836,
                },
            ),
            (
                -7,
                Rank {
                    geometric: 0,
                    secondary: 0x277b_fe0b_88eb_bbd6,
                    content: 0x6584_0b96_da55_799d,
                },
            ),
            (
                -5,
                Rank {
                    geometric: 3,
                    secondary: 0x9967_e7aa_50bd_e867,
                    content: 0xa716_fcdf_59ea_1c33,
                },
            ),
            (
                -4,
                Rank {
                    geometric: 0,
                    secondary: 0xf711_6a6f_141e_9a12,
                    content: 0xbf5f_3e7c_c063_4025,
                },
            ),
            (
                -2,
                Rank {
                    geometric: 4,
                    secondary: 0xd8d8_988e_b86c_7729,
                    content: 0xa1ec_1091_84c3_7524,
                },
            ),
            (
                0,
                Rank {
                    geometric: 0,
                    secondary: 0x9d9d_19b2_1f20_94af,
                    content: 0xaab3_97ec_882f_6628,
                },
            ),
            (
                1,
                Rank {
                    geometric: 2,
                    secondary: 0x5f67_2c26_3f99_e296,
                    content: 0x8121_0b5f_03e9_130b,
                },
            ),
            (
                2,
                Rank {
                    geometric: 1,
                    secondary: 0x09c5_3346_f4b8_484e,
                    content: 0x80d7_f7d6_12a1_2f48,
                },
            ),
            (
                3,
                Rank {
                    geometric: 0,
                    secondary: 0xc132_a48d_e566_43f0,
                    content: 0x392e_a718_a621_950f,
                },
            ),
            (
                8,
                Rank {
                    geometric: 6,
                    secondary: 0xe922_d788_b967_870b,
                    content: 0x61ff_73f6_a1c4_4820,
                },
            ),
        ];
        for (value, expected) in expected_ranks {
            assert_eq!(policy.get_rank(&value), expected, "rank for {value}");
        }

        let minus_five = policy.get_rank(&-5);
        let minus_eight = policy.get_rank(&-8);
        assert!(minus_five.secondary > i64::MAX as u64);
        assert!(minus_eight.secondary < i64::MAX as u64);
        assert!(higher_values(&-5, minus_five, &-8, minus_eight, &policy));

        let values = [-8, -7, -5, -4, -2, 0, 1, 2, 3, 8];
        let set = CanonicalSortedSet::from_items(policy, values.into_iter().rev()).unwrap();
        let mut actual = Vec::new();
        let mut pending = vec![set.root.as_deref().unwrap()];
        while let Some(node) = pending.pop() {
            actual.push((
                node.item,
                node.left.as_ref().map_or(0, |child| child.count),
                node.right.as_ref().map_or(0, |child| child.count),
            ));
            if let Some(right) = node.right.as_deref() {
                pending.push(right);
            }
            if let Some(left) = node.left.as_deref() {
                pending.push(left);
            }
        }
        assert_eq!(
            actual,
            [
                (8, 9, 0),
                (-2, 4, 4),
                (-5, 2, 1),
                (-8, 0, 1),
                (-7, 0, 0),
                (-4, 0, 0),
                (1, 1, 2),
                (0, 0, 0),
                (2, 0, 1),
                (3, 0, 0),
            ]
        );
    }

    #[test]
    fn non_clone_values_support_bulk_reads_validation_equality_and_clear() {
        let first_identity = Arc::new(());
        let discarded_identity = Arc::new(());
        let policy = ZipTreeRankPolicy::seeded(31, compare_non_clone, hash_non_clone);
        let set = CanonicalSortedSet::from_items(
            policy.clone(),
            [
                NonCloneItem {
                    key: 2,
                    identity: Arc::clone(&first_identity),
                },
                non_clone_item(1),
                NonCloneItem {
                    key: 2,
                    identity: Arc::clone(&discarded_identity),
                },
                non_clone_item(3),
            ],
        )
        .unwrap();

        assert_eq!(
            set.iter().map(|item| item.key).collect::<Vec<_>>(),
            [1, 2, 3]
        );
        let probe = non_clone_item(2);
        let retained = set.get(&probe).unwrap();
        assert!(Arc::ptr_eq(&retained.identity, &first_identity));
        assert!(!Arc::ptr_eq(&retained.identity, &discarded_identity));
        assert_eq!(set.validate_structure().unwrap().len, 3);

        let same_policy = CanonicalSortedSet::from_items(
            policy,
            [non_clone_item(3), non_clone_item(1), non_clone_item(2)],
        )
        .unwrap();
        assert!(set.set_equals(&same_policy));

        let other_policy = ZipTreeRankPolicy::seeded(31, compare_non_clone, hash_non_clone);
        let cross_policy = CanonicalSortedSet::from_items(
            other_policy,
            [non_clone_item(2), non_clone_item(3), non_clone_item(1)],
        )
        .unwrap();
        assert!(set.set_equals(&cross_policy));

        let cleared = set.clear();
        assert!(cleared.is_empty());
        assert!(cleared.policy().is_same_policy(set.policy()));
        assert!(cleared.is_same_version(&cleared.clear()));
    }

    #[test]
    fn insertion_order_and_delete_reinsert_converge_on_one_topology() {
        let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x1234_5678_9abc_def0);
        let values = (-500..500).collect::<Vec<_>>();
        let expected = CanonicalSortedSet::from_items(policy.clone(), values.clone()).unwrap();
        let mut random = XorShift64::new(20_260_716);
        for _ in 0..20 {
            let mut shuffled = values.clone();
            shuffle(&mut shuffled, &mut random);
            let built = CanonicalSortedSet::from_items(policy.clone(), shuffled.clone()).unwrap();
            assert_eq!(expected.topology_signature(), built.topology_signature());
            assert_eq!(expected.content_hash(), built.content_hash());
            assert!(expected.set_equals(&built));

            let mut edited = built;
            for value in shuffled.iter().take(100) {
                edited = edited.remove(value);
            }
            for value in shuffled.iter().take(100).rev() {
                edited = edited.insert(*value).unwrap();
            }
            assert_eq!(expected.topology_signature(), edited.topology_signature());
            assert_eq!(expected.content_hash(), edited.content_hash());
        }
    }

    #[test]
    fn comparator_equivalence_retains_first_and_rejects_incoherent_hashes() {
        let compare_case = |left: &String, right: &String| {
            left.to_ascii_lowercase().cmp(&right.to_ascii_lowercase())
        };
        let hash_case = |value: &String| value.to_ascii_lowercase().stable_rank_hash();
        let policy = ZipTreeRankPolicy::seeded(7, compare_case, hash_case);
        let stored = String::from("Alpha");
        let set = CanonicalSortedSet::new(policy.clone())
            .insert(stored.clone())
            .unwrap();
        let duplicate = set.insert(String::from("ALPHA")).unwrap();
        assert!(set.is_same_version(&duplicate));
        assert_eq!(duplicate.get(&String::from("alpha")), Some(&stored));

        let incoherent = ZipTreeRankPolicy::seeded(17, compare_case, |value: &String| {
            u64::from(value.as_bytes()[0])
        });
        let set = CanonicalSortedSet::new(incoherent.clone())
            .insert(String::from("alpha"))
            .unwrap();
        assert!(matches!(
            set.insert(String::from("ALPHA")),
            Err(CanonicalSetError::IncoherentRankHash)
        ));
        assert!(matches!(
            CanonicalSortedSet::from_items(
                incoherent,
                [String::from("alpha"), String::from("ALPHA")]
            ),
            Err(CanonicalSetError::IncoherentRankHash)
        ));
    }

    #[test]
    fn cross_policy_equality_uses_receiver_comparer_and_deduplicates_borrowed_values() {
        let sensitive_policy = ZipTreeRankPolicy::<String>::seeded(
            41,
            |left: &String, right: &String| left.cmp(right),
            |value: &String| value.stable_rank_hash(),
        );
        let insensitive_policy = ZipTreeRankPolicy::<String>::seeded(
            41,
            |left: &String, right: &String| {
                left.to_ascii_lowercase().cmp(&right.to_ascii_lowercase())
            },
            |value: &String| value.to_ascii_lowercase().stable_rank_hash(),
        );
        let sensitive = CanonicalSortedSet::from_items(
            sensitive_policy,
            [String::from("Alpha"), String::from("ALPHA")],
        )
        .unwrap();
        let insensitive =
            CanonicalSortedSet::from_items(insensitive_policy, [String::from("alpha")]).unwrap();

        assert!(insensitive.set_equals(&sensitive));
        assert!(!sensitive.set_equals(&insensitive));
        assert_eq!(sensitive.len(), 2);
        assert_eq!(insensitive.len(), 1);
    }

    #[test]
    fn keyed_policies_own_keys_reproduce_shape_and_keep_identity_separate() {
        let mut key = (0_u8..32)
            .map(|value| value.wrapping_mul(7).wrapping_add(3))
            .collect::<Vec<_>>();
        let retained = key.clone();
        let first_policy = ZipTreeRankPolicy::<i32>::keyed_natural(&key).unwrap();
        key.fill(u8::MAX);
        let second_policy = ZipTreeRankPolicy::<i32>::keyed_natural(&retained).unwrap();
        let different_policy = ZipTreeRankPolicy::<i32>::keyed_natural(&[0xa5; 32]).unwrap();
        let values = (-1_000..=1_000)
            .filter(|value| value % 3 != 0)
            .collect::<Vec<_>>();
        let first = CanonicalSortedSet::from_items(first_policy, values.clone()).unwrap();
        let second =
            CanonicalSortedSet::from_items(second_policy, values.iter().rev().copied()).unwrap();
        let different = CanonicalSortedSet::from_items(different_policy, values).unwrap();

        assert!(!first.policy().is_same_policy(second.policy()));
        assert_eq!(first.topology_signature(), second.topology_signature());
        assert_eq!(first.content_hash(), second.content_hash());
        assert!(first.set_equals(&second));
        assert!(matches!(
            first.union(&second),
            Err(CanonicalSetError::IncompatiblePolicy)
        ));
        assert_ne!(first.content_hash(), different.content_hash());
        assert!(first.set_equals(&different));
        assert!(matches!(
            ZipTreeRankPolicy::<i32>::keyed_natural(&[0; 31]),
            Err(ZipTreePolicyError::RankKeyTooShort {
                minimum: 32,
                actual: 31
            })
        ));
    }

    #[test]
    fn random_and_seeded_policy_modes_preserve_their_trust_boundaries() {
        let first_random = ZipTreeRankPolicy::<i32>::random_natural().unwrap();
        let second_random = ZipTreeRankPolicy::<i32>::random_natural().unwrap();
        assert_eq!(first_random.public_seed(), None);
        assert_eq!(second_random.public_seed(), None);
        assert!(!first_random.is_same_policy(&second_random));

        let seed = 0xfeed_face_cafe_beef_u64;
        let seeded = ZipTreeRankPolicy::<i32>::seeded_natural(seed);
        let mut material = Vec::from(&b"ZZT2"[..]);
        material.extend_from_slice(&seed.to_be_bytes());
        let keyed = ZipTreeRankPolicy::<i32>::keyed_natural(&sha256(&material)).unwrap();
        let seeded_set = CanonicalSortedSet::from_items(seeded, -256..256).unwrap();
        let keyed_set = CanonicalSortedSet::from_items(keyed, (-256..256).rev()).unwrap();
        assert_eq!(
            seeded_set.topology_signature(),
            keyed_set.topology_signature()
        );
        assert_eq!(seeded_set.content_hash(), keyed_set.content_hash());
    }

    #[test]
    fn complete_rank_ties_are_deterministic_deep_and_stack_safe() {
        let policy = ZipTreeRankPolicy::seeded(1, NaturalZipTreeComparer, |_value: &i32| 0_u64);
        let forward = CanonicalSortedSet::from_items(policy.clone(), 0..4_096).unwrap();
        let reverse = CanonicalSortedSet::from_items(policy, (0..4_096).rev()).unwrap();
        assert_eq!(forward.height(), 4_096);
        assert_eq!(forward.topology_signature(), reverse.topology_signature());
        assert_eq!(forward.content_hash(), reverse.content_hash());
        let statistics = forward.validate_structure().unwrap();
        assert_eq!(statistics.priority_collision_count, 4_095);

        let without_last = forward.remove(&4_095);
        assert_eq!(without_last.height(), 4_095);
        let restored = without_last.insert(4_095).unwrap();
        assert_eq!(forward.content_hash(), restored.content_hash());
        assert!(forward.set_equals(&restored));
    }

    #[test]
    fn algebra_diff_and_no_ops_preserve_contracts() {
        let policy = ZipTreeRankPolicy::<i32>::seeded_natural(99);
        let left = CanonicalSortedSet::from_items(policy.clone(), [1, 2, 4, 8]).unwrap();
        let right = CanonicalSortedSet::from_items(policy.clone(), [2, 3, 4, 5]).unwrap();
        let empty = CanonicalSortedSet::new(policy);

        assert_eq!(
            left.union(&right)
                .unwrap()
                .iter()
                .copied()
                .collect::<Vec<_>>(),
            [1, 2, 3, 4, 5, 8]
        );
        assert_eq!(
            left.intersection(&right)
                .unwrap()
                .iter()
                .copied()
                .collect::<Vec<_>>(),
            [2, 4]
        );
        assert_eq!(
            left.except(&right)
                .unwrap()
                .iter()
                .copied()
                .collect::<Vec<_>>(),
            [1, 8]
        );
        assert_eq!(
            left.diff(&right).unwrap(),
            CanonicalSetDifference {
                only_in_left: vec![1, 8],
                only_in_right: vec![3, 5]
            }
        );

        assert!(left.is_same_version(&left.insert(2).unwrap()));
        assert!(left.is_same_version(&left.remove(&99)));
        assert!(left.is_same_version(&left.union(&left).unwrap()));
        assert!(left.is_same_version(&left.intersection(&left).unwrap()));
        assert!(left.is_same_version(&left.except(&empty).unwrap()));
        assert!(empty.is_same_version(&empty.clear()));
        assert!(left.diff(&left).unwrap().is_empty());
        assert!(left.validate_structure().is_ok());
    }

    #[test]
    fn localized_edit_shares_the_untouched_root_branch() {
        let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x0051_a71c);
        let set = CanonicalSortedSet::from_items(policy, 0..1_000).unwrap();
        let root = set.root.as_ref().unwrap();
        let root_rank = root.rank;

        if let Some(shared_right) = &root.right {
            let candidate = (-100_000..0)
                .rev()
                .find(|value| {
                    !higher_values(
                        value,
                        set.policy.get_rank(value),
                        &root.item,
                        root_rank,
                        &set.policy,
                    )
                })
                .expect("low-priority left candidate");
            let updated = set.insert(candidate).unwrap();
            let updated_root = updated.root.as_ref().unwrap();
            assert!(Arc::ptr_eq(
                shared_right,
                updated_root.right.as_ref().unwrap()
            ));
        } else {
            let shared_left = root.left.as_ref().expect("nontrivial root branch");
            let candidate = (1_000..100_000)
                .find(|value| {
                    !higher_values(
                        value,
                        set.policy.get_rank(value),
                        &root.item,
                        root_rank,
                        &set.policy,
                    )
                })
                .expect("low-priority right candidate");
            let updated = set.insert(candidate).unwrap();
            let updated_root = updated.root.as_ref().unwrap();
            assert!(Arc::ptr_eq(
                shared_left,
                updated_root.left.as_ref().unwrap()
            ));
        }
    }

    #[test]
    fn randomized_history_matches_btree_model_and_retains_snapshots() {
        let policy = ZipTreeRankPolicy::<i32>::seeded_natural(42);
        let mut set = CanonicalSortedSet::new(policy.clone());
        let mut model = BTreeSet::new();
        let mut snapshots = Vec::new();
        let mut random = XorShift64::new(20_260_717);
        for operation in 0..20_000 {
            let value = i32::try_from(random.next_u64() % 10_001).unwrap() - 5_000;
            if random.next_u64().is_multiple_of(3) {
                set = set.remove(&value);
                model.remove(&value);
            } else {
                set = set.insert(value).unwrap();
                model.insert(value);
            }
            if operation % 997 == 0 {
                snapshots.push((set.clone(), model.iter().copied().collect::<Vec<_>>()));
            }
        }
        assert_eq!(
            set.iter().copied().collect::<Vec<_>>(),
            model.into_iter().collect::<Vec<_>>()
        );
        assert!(set.validate_structure().is_ok());
        for (snapshot, expected) in snapshots {
            assert_eq!(snapshot.iter().copied().collect::<Vec<_>>(), expected);
            assert!(snapshot.validate_structure().is_ok());
        }

        let mut reversed = set.iter().copied().collect::<Vec<_>>();
        reversed.reverse();
        let rebuilt = CanonicalSortedSet::from_items(policy, reversed).unwrap();
        assert_eq!(set.topology_signature(), rebuilt.topology_signature());
        assert!(set.set_equals(&rebuilt));
    }

    #[test]
    fn immutable_sets_and_policies_are_send_sync() {
        fn assert_send_sync<T: Send + Sync>() {}
        assert_send_sync::<ZipTreeRankPolicy<i32>>();
        assert_send_sync::<CanonicalSortedSet<i32>>();

        let policy = ZipTreeRankPolicy::<i32>::seeded_natural(0x0051_a71c);
        let expected_set = CanonicalSortedSet::from_items(policy.clone(), 0..20_000).unwrap();
        let expected = expected_set.content_hash();
        let set = CanonicalSortedSet::from_items(policy, 0..20_000).unwrap();
        assert!(!Arc::ptr_eq(
            expected_set.root.as_ref().unwrap(),
            set.root.as_ref().unwrap()
        ));
        assert!(set.root.as_ref().unwrap().digest.get().is_none());

        let barrier = Arc::new(Barrier::new(9));
        let handles = (0..8)
            .map(|_| {
                let snapshot = set.clone();
                let barrier = Arc::clone(&barrier);
                thread::spawn(move || {
                    barrier.wait();
                    for _ in 0..128 {
                        assert_eq!(snapshot.content_hash(), expected);
                        assert!(snapshot.contains(&10_000));
                    }
                })
            })
            .collect::<Vec<_>>();
        barrier.wait();
        for handle in handles {
            handle.join().unwrap();
        }
        assert_eq!(set.root.as_ref().unwrap().digest.get(), Some(&expected));
    }

    fn hex(bytes: &[u8]) -> String {
        bytes.iter().map(|byte| format!("{byte:02x}")).collect()
    }

    fn shuffle<T>(values: &mut [T], random: &mut XorShift64) {
        for index in (1..values.len()).rev() {
            let other = usize::try_from(random.next_u64()).unwrap() % (index + 1);
            values.swap(index, other);
        }
    }

    struct XorShift64(u64);

    impl XorShift64 {
        fn new(seed: u64) -> Self {
            Self(seed)
        }

        fn next_u64(&mut self) -> u64 {
            let mut value = self.0;
            value ^= value << 13;
            value ^= value >> 7;
            value ^= value << 17;
            self.0 = value;
            value
        }
    }
}
