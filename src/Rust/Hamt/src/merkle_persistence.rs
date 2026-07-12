use crate::merkle_search_tree::{
    MERKLE_BLOCK_HEADER_LENGTH, MERKLE_BLOCK_MAGIC, MERKLE_DIGEST_LENGTH, MERKLE_NODE_BLOCK_TAG,
    MerkleNode, MerkleNodeLink, enumerate_nodes_preorder,
};
use crate::{
    MerkleCodecError, MerkleDigest, MerkleEntry, MerkleSearchTree, MerkleSearchTreePolicy,
    MerkleTreeError,
};
use std::cmp::Ordering;
use std::collections::{HashMap, HashSet};
use std::error::Error;
use std::fmt;
use std::sync::{Arc, RwLock, RwLockReadGuard, RwLockWriteGuard};

const PROOF_MAGIC: &[u8; 4] = b"MSP2";

/// One immutable serialized block paired with its claimed content address.
///
/// Construction deliberately does not recompute `digest`: a block store is format agnostic, while
/// verified load/import/proof operations authenticate the pair before creating trusted tree state.
#[derive(Clone, Eq, PartialEq)]
pub struct MerkleBlock {
    digest: MerkleDigest,
    content: Arc<[u8]>,
}

impl MerkleBlock {
    /// Copies complete block bytes into an immutable value.
    #[must_use]
    pub fn new(digest: MerkleDigest, content: impl AsRef<[u8]>) -> Self {
        Self {
            digest,
            content: Arc::from(content.as_ref()),
        }
    }

    /// Returns the claimed content address.
    #[must_use]
    pub const fn digest(&self) -> MerkleDigest {
        self.digest
    }

    /// Returns the complete serialized bytes.
    #[must_use]
    pub fn content(&self) -> &[u8] {
        &self.content
    }

    /// Returns the serialized byte length.
    #[must_use]
    pub fn len(&self) -> usize {
        self.content.len()
    }

    /// Returns whether the serialized block is empty.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.content.is_empty()
    }

    /// Copies the serialized bytes into a mutable vector.
    #[must_use]
    pub fn to_vec(&self) -> Vec<u8> {
        self.content.to_vec()
    }
}

impl fmt::Debug for MerkleBlock {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("MerkleBlock")
            .field("digest", &self.digest)
            .field("length", &self.len())
            .finish()
    }
}

impl fmt::Display for MerkleBlock {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(formatter, "{} ({} bytes)", self.digest, self.len())
    }
}

/// Identifies why block loading or proof verification failed.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MerkleVerificationFailureKind {
    /// No failure occurred.
    None,
    /// The block/proof algorithm identifier is unsupported.
    UnsupportedAlgorithm,
    /// Policy or codec domains differ.
    DomainMismatch,
    /// A required closure or proof block is absent.
    MissingBlock,
    /// Serialized bytes do not hash to their claimed digest.
    DigestMismatch,
    /// Serialized bytes are truncated or otherwise malformed.
    MalformedBlock,
    /// Validly parseable bytes are not the unique canonical encoding.
    NonCanonicalBlock,
    /// A pack/proof/request repeats a digest.
    DuplicateBlock,
    /// One digest is associated with different serialized bytes.
    ConflictingBlock,
    /// A child violates layer, ordering, count, or interval constraints.
    InvalidReference,
    /// A supposedly acyclic expansion repeats an active block.
    CycleDetected,
    /// Verified blocks do not establish the proof claim.
    ProofMismatch,
    /// A computed root differs from the declared root.
    RootMismatch,
    /// A finite verification limit was exceeded.
    ResourceLimitExceeded,
}

/// A precise load, import, storage, or verification failure.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleVerificationError {
    kind: MerkleVerificationFailureKind,
    message: String,
    block_digest: Option<MerkleDigest>,
}

impl MerkleVerificationError {
    fn new(
        kind: MerkleVerificationFailureKind,
        message: impl Into<String>,
        block_digest: Option<MerkleDigest>,
    ) -> Self {
        debug_assert_ne!(kind, MerkleVerificationFailureKind::None);
        Self {
            kind,
            message: message.into(),
            block_digest,
        }
    }

    /// Creates the address-conflict failure required by custom [`MerkleBlockStore`] implementations.
    #[must_use]
    pub fn conflicting_block(digest: MerkleDigest, message: impl Into<String>) -> Self {
        Self::new(
            MerkleVerificationFailureKind::ConflictingBlock,
            message,
            Some(digest),
        )
    }

    /// Returns the stable failure classification.
    #[must_use]
    pub const fn kind(&self) -> MerkleVerificationFailureKind {
        self.kind
    }

    /// Returns the offending or missing block address when known.
    #[must_use]
    pub const fn block_digest(&self) -> Option<MerkleDigest> {
        self.block_digest
    }

    /// Returns the diagnostic message.
    #[must_use]
    pub fn message(&self) -> &str {
        &self.message
    }
}

impl fmt::Display for MerkleVerificationError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.message)
    }
}

impl Error for MerkleVerificationError {}

/// A concurrent-safe immutable-block storage contract.
///
/// A second write of identical bytes under a digest is an idempotent no-op; different bytes under
/// an existing digest must produce [`MerkleVerificationFailureKind::ConflictingBlock`]. Stores do
/// not replace the format-aware verifier used by load/import/proof operations.
pub trait MerkleBlockStore: Send + Sync {
    /// Returns the number of stored addresses.
    fn len(&self) -> usize;

    /// Returns whether the store contains no blocks.
    fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns a sorted point-in-time address snapshot.
    fn digests(&self) -> Vec<MerkleDigest>;

    /// Returns whether an address is present.
    fn contains(&self, digest: MerkleDigest) -> bool;

    /// Returns an immutable block snapshot when present.
    fn get(&self, digest: MerkleDigest) -> Option<MerkleBlock>;

    /// Stores a block, returning `true` only for a newly added address.
    fn put(&self, block: MerkleBlock) -> Result<bool, MerkleVerificationError>;

    /// Removes an address when present.
    fn remove(&self, digest: MerkleDigest) -> bool;

    /// Removes all blocks.
    fn clear(&self);
}

/// A thread-safe ephemeral block store backed by a standard-library `RwLock`.
#[derive(Default)]
pub struct InMemoryMerkleBlockStore {
    blocks: RwLock<HashMap<MerkleDigest, MerkleBlock>>,
}

impl InMemoryMerkleBlockStore {
    /// Creates an empty store.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    fn read(&self) -> RwLockReadGuard<'_, HashMap<MerkleDigest, MerkleBlock>> {
        self.blocks
            .read()
            .unwrap_or_else(|error| error.into_inner())
    }

    fn write(&self) -> RwLockWriteGuard<'_, HashMap<MerkleDigest, MerkleBlock>> {
        self.blocks
            .write()
            .unwrap_or_else(|error| error.into_inner())
    }
}

impl MerkleBlockStore for InMemoryMerkleBlockStore {
    fn len(&self) -> usize {
        self.read().len()
    }

    fn digests(&self) -> Vec<MerkleDigest> {
        let mut result = self.read().keys().copied().collect::<Vec<_>>();
        result.sort_unstable();
        result
    }

    fn contains(&self, digest: MerkleDigest) -> bool {
        self.read().contains_key(&digest)
    }

    fn get(&self, digest: MerkleDigest) -> Option<MerkleBlock> {
        self.read().get(&digest).cloned()
    }

    fn put(&self, block: MerkleBlock) -> Result<bool, MerkleVerificationError> {
        let mut blocks = self.write();
        if let Some(existing) = blocks.get(&block.digest()) {
            if existing == &block {
                return Ok(false);
            }
            return Err(verification(
                MerkleVerificationFailureKind::ConflictingBlock,
                format!(
                    "digest '{}' is already associated with different block bytes",
                    block.digest()
                ),
                Some(block.digest()),
            ));
        }
        blocks.insert(block.digest(), block);
        Ok(true)
    }

    fn remove(&self, digest: MerkleDigest) -> bool {
        self.write().remove(&digest).is_some()
    }

    fn clear(&self) {
        self.write().clear();
    }
}

/// An immutable complete or partial transfer pack with unique addresses.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleBlockPack {
    algorithm_id: Arc<str>,
    domain_digest: MerkleDigest,
    root_hash: MerkleDigest,
    blocks: Arc<[MerkleBlock]>,
    total_byte_count: u64,
    contains_root_block: bool,
}

impl MerkleBlockPack {
    /// Validates and owns one ordered transfer pack.
    pub fn new<I>(
        algorithm_id: impl Into<String>,
        domain_digest: MerkleDigest,
        root_hash: MerkleDigest,
        blocks: I,
    ) -> Result<Self, MerkleVerificationError>
    where
        I: IntoIterator<Item = MerkleBlock>,
    {
        let algorithm_id = algorithm_id.into();
        if algorithm_id.trim().is_empty() {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle pack algorithm identifier must not be empty",
                None,
            ));
        }
        let blocks = blocks.into_iter().collect::<Vec<_>>();
        let mut unique = HashSet::with_capacity(blocks.len());
        let mut total_byte_count = 0_u64;
        let mut contains_root_block = false;
        for block in &blocks {
            if !unique.insert(block.digest()) {
                return Err(verification(
                    MerkleVerificationFailureKind::DuplicateBlock,
                    format!("the pack contains duplicate digest '{}'", block.digest()),
                    Some(block.digest()),
                ));
            }
            total_byte_count = total_byte_count
                .checked_add(u64_len(block.len())?)
                .ok_or_else(|| size_limit("a Merkle pack byte count overflowed", block.digest()))?;
            contains_root_block |= block.digest() == root_hash;
        }
        Ok(Self {
            algorithm_id: Arc::from(algorithm_id),
            domain_digest,
            root_hash,
            blocks: Arc::from(blocks),
            total_byte_count,
            contains_root_block,
        })
    }

    fn from_unique(
        algorithm_id: &'static str,
        domain_digest: MerkleDigest,
        root_hash: MerkleDigest,
        blocks: Vec<MerkleBlock>,
    ) -> Self {
        Self::new(algorithm_id, domain_digest, root_hash, blocks)
            .expect("a trusted tree emits unique blocks with bounded lengths")
    }

    /// Returns the algorithm/block-format ID.
    #[must_use]
    pub fn algorithm_id(&self) -> &str {
        &self.algorithm_id
    }

    /// Returns the policy and codec domain.
    #[must_use]
    pub const fn domain_digest(&self) -> MerkleDigest {
        self.domain_digest
    }

    /// Returns the root whose closure is described.
    #[must_use]
    pub const fn root_hash(&self) -> MerkleDigest {
        self.root_hash
    }

    /// Returns blocks in transfer order.
    #[must_use]
    pub fn blocks(&self) -> &[MerkleBlock] {
        &self.blocks
    }

    /// Returns the number of transferred blocks.
    #[must_use]
    pub fn block_count(&self) -> usize {
        self.blocks.len()
    }

    /// Returns the complete transferred byte count.
    #[must_use]
    pub const fn total_byte_count(&self) -> u64 {
        self.total_byte_count
    }

    /// Returns whether the pack itself contains its named root block.
    #[must_use]
    pub const fn contains_root_block(&self) -> bool {
        self.contains_root_block
    }
}

/// One immutable synchronization frontier plan.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleSyncPlan {
    algorithm_id: Arc<str>,
    domain_digest: MerkleDigest,
    local_root_hash: MerkleDigest,
    target_root_hash: MerkleDigest,
    requested_blocks: Arc<[MerkleDigest]>,
    examined_block_count: usize,
    examined_byte_count: u64,
}

impl MerkleSyncPlan {
    fn from_unique(
        algorithm_id: &'static str,
        domain_digest: MerkleDigest,
        local_root_hash: MerkleDigest,
        target_root_hash: MerkleDigest,
        requested_blocks: Vec<MerkleDigest>,
        examined_block_count: usize,
        examined_byte_count: u64,
    ) -> Self {
        debug_assert_eq!(
            requested_blocks
                .iter()
                .copied()
                .collect::<HashSet<_>>()
                .len(),
            requested_blocks.len()
        );
        Self {
            algorithm_id: Arc::from(algorithm_id),
            domain_digest,
            local_root_hash,
            target_root_hash,
            requested_blocks: Arc::from(requested_blocks),
            examined_block_count,
            examined_byte_count,
        }
    }

    /// Returns the algorithm/block-format ID.
    #[must_use]
    pub fn algorithm_id(&self) -> &str {
        &self.algorithm_id
    }

    /// Returns the policy and codec domain.
    #[must_use]
    pub const fn domain_digest(&self) -> MerkleDigest {
        self.domain_digest
    }

    /// Returns the currently published receiver root.
    #[must_use]
    pub const fn local_root_hash(&self) -> MerkleDigest {
        self.local_root_hash
    }

    /// Returns the target peer root.
    #[must_use]
    pub const fn target_root_hash(&self) -> MerkleDigest {
        self.target_root_hash
    }

    /// Returns unique missing frontier addresses in deterministic order.
    #[must_use]
    pub fn requested_blocks(&self) -> &[MerkleDigest] {
        &self.requested_blocks
    }

    /// Returns the number of in-memory target blocks examined.
    #[must_use]
    pub const fn examined_block_count(&self) -> usize {
        self.examined_block_count
    }

    /// Returns the number of in-memory target bytes examined.
    #[must_use]
    pub const fn examined_byte_count(&self) -> u64 {
        self.examined_byte_count
    }

    /// Returns whether the receiver and target roots already match.
    #[must_use]
    pub fn roots_match(&self) -> bool {
        self.local_root_hash == self.target_root_hash
    }

    /// Returns whether another transfer round is required.
    #[must_use]
    pub fn requires_blocks(&self) -> bool {
        !self.requested_blocks.is_empty()
    }
}

/// Identifies the claim encoded by an `MSP2` proof query.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MerkleProofKind {
    /// One encoded key/value is present.
    Membership = 0,
    /// One encoded key is absent.
    NonMembership = 1,
    /// An inclusive encoded-key range is complete.
    Range = 2,
}

/// One authenticated block and the child intervals expanded elsewhere in a proof.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleProofStep {
    block: MerkleBlock,
    expanded_child_indexes: Arc<[usize]>,
}

impl MerkleProofStep {
    /// Validates, sorts, and owns expanded child indexes.
    pub fn new<I>(
        block: MerkleBlock,
        expanded_child_indexes: I,
    ) -> Result<Self, MerkleVerificationError>
    where
        I: IntoIterator<Item = usize>,
    {
        let mut indexes = expanded_child_indexes.into_iter().collect::<Vec<_>>();
        indexes.sort_unstable();
        if indexes.windows(2).any(|window| window[0] == window[1]) {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a proof step expands one child index more than once",
                Some(block.digest()),
            ));
        }
        Ok(Self {
            block,
            expanded_child_indexes: Arc::from(indexes),
        })
    }

    fn from_unique(block: MerkleBlock, expanded_child_indexes: Vec<usize>) -> Self {
        Self::new(block, expanded_child_indexes)
            .expect("trusted proof construction emits unique child indexes")
    }

    /// Returns the complete canonical block.
    #[must_use]
    pub const fn block(&self) -> &MerkleBlock {
        &self.block
    }

    /// Returns sorted expanded child indexes.
    #[must_use]
    pub fn expanded_child_indexes(&self) -> &[usize] {
        &self.expanded_child_indexes
    }
}

/// An immutable membership, nonmembership, or inclusive-range proof.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleProof {
    algorithm_id: Arc<str>,
    domain_digest: MerkleDigest,
    root_hash: MerkleDigest,
    kind: MerkleProofKind,
    query: Arc<[u8]>,
    steps: Arc<[MerkleProofStep]>,
    total_byte_count: u64,
}

impl MerkleProof {
    /// Validates and owns one proof envelope.
    pub fn new<I>(
        algorithm_id: impl Into<String>,
        domain_digest: MerkleDigest,
        root_hash: MerkleDigest,
        kind: MerkleProofKind,
        query: impl AsRef<[u8]>,
        steps: I,
    ) -> Result<Self, MerkleVerificationError>
    where
        I: IntoIterator<Item = MerkleProofStep>,
    {
        let algorithm_id = algorithm_id.into();
        if algorithm_id.trim().is_empty() {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle proof algorithm identifier must not be empty",
                None,
            ));
        }
        let query = query.as_ref();
        let steps = steps.into_iter().collect::<Vec<_>>();
        let mut unique = HashSet::with_capacity(steps.len());
        let mut total_byte_count = u64_len(query.len())?;
        for step in &steps {
            if !unique.insert(step.block.digest()) {
                return Err(verification(
                    MerkleVerificationFailureKind::DuplicateBlock,
                    format!(
                        "the proof contains duplicate block '{}'",
                        step.block.digest()
                    ),
                    Some(step.block.digest()),
                ));
            }
            total_byte_count = total_byte_count
                .checked_add(u64_len(step.block.len())?)
                .ok_or_else(|| {
                    size_limit("a Merkle proof byte count overflowed", step.block.digest())
                })?;
        }
        Ok(Self {
            algorithm_id: Arc::from(algorithm_id),
            domain_digest,
            root_hash,
            kind,
            query: Arc::from(query),
            steps: Arc::from(steps),
            total_byte_count,
        })
    }

    fn from_validated(
        algorithm_id: &'static str,
        domain_digest: MerkleDigest,
        root_hash: MerkleDigest,
        kind: MerkleProofKind,
        query: Vec<u8>,
        steps: Vec<MerkleProofStep>,
    ) -> Self {
        Self::new(algorithm_id, domain_digest, root_hash, kind, query, steps)
            .expect("trusted proof construction emits a bounded unique envelope")
    }

    /// Returns the algorithm/block-format ID.
    #[must_use]
    pub fn algorithm_id(&self) -> &str {
        &self.algorithm_id
    }

    /// Returns the policy and codec domain.
    #[must_use]
    pub const fn domain_digest(&self) -> MerkleDigest {
        self.domain_digest
    }

    /// Returns the authenticated root.
    #[must_use]
    pub const fn root_hash(&self) -> MerkleDigest {
        self.root_hash
    }

    /// Returns the claim kind.
    #[must_use]
    pub const fn kind(&self) -> MerkleProofKind {
        self.kind
    }

    /// Returns the complete canonical `MSP2` query descriptor.
    #[must_use]
    pub fn query(&self) -> &[u8] {
        &self.query
    }

    /// Returns uniquely addressed proof steps.
    #[must_use]
    pub fn steps(&self) -> &[MerkleProofStep] {
        &self.steps
    }

    /// Returns query plus block bytes.
    #[must_use]
    pub const fn total_byte_count(&self) -> u64 {
        self.total_byte_count
    }
}

/// Immutable proof verification outcome.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MerkleProofVerificationResult {
    is_valid: bool,
    failure_kind: MerkleVerificationFailureKind,
    failure_message: Option<String>,
    computed_root_hash: Option<MerkleDigest>,
    verified_block_count: usize,
    verified_byte_count: u64,
}

impl MerkleProofVerificationResult {
    fn success(
        computed_root_hash: MerkleDigest,
        verified_block_count: usize,
        verified_byte_count: u64,
    ) -> Self {
        Self {
            is_valid: true,
            failure_kind: MerkleVerificationFailureKind::None,
            failure_message: None,
            computed_root_hash: Some(computed_root_hash),
            verified_block_count,
            verified_byte_count,
        }
    }

    fn failure(
        error: MerkleVerificationError,
        verified_block_count: usize,
        verified_byte_count: u64,
        computed_root_hash: Option<MerkleDigest>,
    ) -> Self {
        Self {
            is_valid: false,
            failure_kind: error.kind,
            failure_message: Some(error.message),
            computed_root_hash,
            verified_block_count,
            verified_byte_count,
        }
    }

    /// Returns whether every proof claim and byte was valid.
    #[must_use]
    pub const fn is_valid(&self) -> bool {
        self.is_valid
    }

    /// Returns `None` on success or the stable failure classification.
    #[must_use]
    pub const fn failure_kind(&self) -> MerkleVerificationFailureKind {
        self.failure_kind
    }

    /// Returns a diagnostic on failure.
    #[must_use]
    pub fn failure_message(&self) -> Option<&str> {
        self.failure_message.as_deref()
    }

    /// Returns the computed/declared proof root when available.
    #[must_use]
    pub const fn computed_root_hash(&self) -> Option<MerkleDigest> {
        self.computed_root_hash
    }

    /// Returns the number of uniquely accounted blocks.
    #[must_use]
    pub const fn verified_block_count(&self) -> usize {
        self.verified_block_count
    }

    /// Returns accounted query plus block bytes.
    #[must_use]
    pub const fn verified_byte_count(&self) -> u64 {
        self.verified_byte_count
    }
}

/// Invalid verification-budget construction.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum MerkleBudgetError {
    /// One named limit was zero.
    ZeroLimit(&'static str),
    /// Per-block bytes exceeded total bytes.
    BlockExceedsTotal,
    /// Proof-query bytes exceeded total bytes.
    QueryExceedsTotal,
}

impl fmt::Display for MerkleBudgetError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ZeroLimit(name) => {
                write!(formatter, "verification limit '{name}' must be positive")
            }
            Self::BlockExceedsTotal => {
                formatter.write_str("the per-block byte limit must not exceed the total byte limit")
            }
            Self::QueryExceedsTotal => formatter
                .write_str("the proof-query byte limit must not exceed the total byte limit"),
        }
    }
}

impl Error for MerkleBudgetError {}

/// Seven finite limits applied before allocation, decoding, and reference traversal.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct MerkleVerificationBudget {
    /// Maximum uniquely decoded blocks.
    pub max_block_count: usize,
    /// Maximum query plus uniquely decoded block bytes.
    pub max_total_byte_count: u64,
    /// Maximum bytes in one block.
    pub max_block_byte_count: usize,
    /// Maximum root-to-leaf expansion depth.
    pub max_depth: usize,
    /// Maximum cumulative entries across unique blocks.
    pub max_entry_count: u64,
    /// Maximum child references in one block.
    pub max_child_references_per_block: usize,
    /// Maximum bytes in one proof query descriptor.
    pub max_proof_query_byte_count: usize,
}

impl MerkleVerificationBudget {
    /// Validates seven explicit verification limits.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        max_block_count: usize,
        max_total_byte_count: u64,
        max_block_byte_count: usize,
        max_depth: usize,
        max_entry_count: u64,
        max_child_references_per_block: usize,
        max_proof_query_byte_count: usize,
    ) -> Result<Self, MerkleBudgetError> {
        for (name, value) in [
            ("max_block_count", max_block_count),
            ("max_block_byte_count", max_block_byte_count),
            ("max_depth", max_depth),
            (
                "max_child_references_per_block",
                max_child_references_per_block,
            ),
            ("max_proof_query_byte_count", max_proof_query_byte_count),
        ] {
            if value == 0 {
                return Err(MerkleBudgetError::ZeroLimit(name));
            }
        }
        if max_total_byte_count == 0 {
            return Err(MerkleBudgetError::ZeroLimit("max_total_byte_count"));
        }
        if max_entry_count == 0 {
            return Err(MerkleBudgetError::ZeroLimit("max_entry_count"));
        }
        if u64::try_from(max_block_byte_count).unwrap_or(u64::MAX) > max_total_byte_count {
            return Err(MerkleBudgetError::BlockExceedsTotal);
        }
        if u64::try_from(max_proof_query_byte_count).unwrap_or(u64::MAX) > max_total_byte_count {
            return Err(MerkleBudgetError::QueryExceedsTotal);
        }
        Ok(Self {
            max_block_count,
            max_total_byte_count,
            max_block_byte_count,
            max_depth,
            max_entry_count,
            max_child_references_per_block,
            max_proof_query_byte_count,
        })
    }

    /// Uses the per-block limit as the proof-query limit.
    pub fn with_six_limits(
        max_block_count: usize,
        max_total_byte_count: u64,
        max_block_byte_count: usize,
        max_depth: usize,
        max_entry_count: u64,
        max_child_references_per_block: usize,
    ) -> Result<Self, MerkleBudgetError> {
        Self::new(
            max_block_count,
            max_total_byte_count,
            max_block_byte_count,
            max_depth,
            max_entry_count,
            max_child_references_per_block,
            max_block_byte_count,
        )
    }
}

impl Default for MerkleVerificationBudget {
    fn default() -> Self {
        Self {
            max_block_count: 1_000_000,
            max_total_byte_count: 1_u64 << 30,
            max_block_byte_count: 16 << 20,
            max_depth: 256,
            max_entry_count: 100_000_000,
            max_child_references_per_block: 65_536,
            max_proof_query_byte_count: 16 << 20,
        }
    }
}

/// Local proof construction failure.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum MerkleProofCreationError {
    /// A local key or value codec rejected the value.
    Codec(MerkleCodecError),
    /// Inclusive range bounds were reversed.
    ReversedRange,
    /// A query field exceeded the signed 32-bit wire length.
    SizeOverflow,
}

impl From<MerkleCodecError> for MerkleProofCreationError {
    fn from(error: MerkleCodecError) -> Self {
        Self::Codec(error)
    }
}

impl fmt::Display for MerkleProofCreationError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Codec(error) => fmt::Display::fmt(error, formatter),
            Self::ReversedRange => {
                formatter.write_str("the minimum key must not follow the maximum key")
            }
            Self::SizeOverflow => formatter.write_str("a proof query exceeds its wire limits"),
        }
    }
}

impl Error for MerkleProofCreationError {}

/// Presence or absence of a merge side; `Present(None)` remains distinct from `Absent`.
#[derive(Debug, Eq, PartialEq)]
pub enum MerkleMergeValue<V> {
    /// The key is absent.
    Absent,
    /// The key is present with a shared immutable value.
    Present(Arc<V>),
}

impl<V> Clone for MerkleMergeValue<V> {
    fn clone(&self) -> Self {
        match self {
            Self::Absent => Self::Absent,
            Self::Present(value) => Self::Present(Arc::clone(value)),
        }
    }
}

impl<V> MerkleMergeValue<V> {
    /// Returns whether a value is present.
    #[must_use]
    pub const fn is_present(&self) -> bool {
        matches!(self, Self::Present(_))
    }

    /// Returns a present value.
    #[must_use]
    pub fn value(&self) -> Option<&V> {
        match self {
            Self::Absent => None,
            Self::Present(value) => Some(value),
        }
    }

    /// Returns a cloned shared handle when present.
    #[must_use]
    pub fn value_handle(&self) -> Option<Arc<V>> {
        match self {
            Self::Absent => None,
            Self::Present(value) => Some(Arc::clone(value)),
        }
    }
}

/// One true key-level three-way merge conflict.
#[derive(Debug, Eq, PartialEq)]
pub struct MerkleThreeWayMergeConflict<K, V> {
    /// Selected stored key representative.
    pub key: Arc<K>,
    /// Common-base state.
    pub base: MerkleMergeValue<V>,
    /// Left-descendant state.
    pub left: MerkleMergeValue<V>,
    /// Right-descendant state.
    pub right: MerkleMergeValue<V>,
}

impl<K, V> Clone for MerkleThreeWayMergeConflict<K, V> {
    fn clone(&self) -> Self {
        Self {
            key: Arc::clone(&self.key),
            base: self.base.clone(),
            left: self.left.clone(),
            right: self.right.clone(),
        }
    }
}

/// Typed merge resolver response.
#[derive(Debug, Default, Eq, PartialEq)]
pub enum MerkleMergeResolution<V> {
    /// Leave the conflict unresolved.
    #[default]
    Unresolved,
    /// Keep the base state.
    UseBase,
    /// Keep the left state.
    UseLeft,
    /// Keep the right state.
    UseRight,
    /// Store a resolver-supplied value.
    SetValue(V),
    /// Remove the key.
    Delete,
}

/// Resolver callback invoked only for true descendant conflicts.
pub type MerkleThreeWayMergeResolver<'a, K, V> =
    dyn FnMut(&MerkleThreeWayMergeConflict<K, V>) -> MerkleMergeResolution<V> + 'a;

/// Complete successful tree or all unresolved conflicts, never a partial tree.
pub struct MerkleThreeWayMergeResult<K, V> {
    merged_tree: Option<MerkleSearchTree<K, V>>,
    unresolved_conflicts: Arc<[MerkleThreeWayMergeConflict<K, V>]>,
}

impl<K, V> MerkleThreeWayMergeResult<K, V> {
    fn success(tree: MerkleSearchTree<K, V>) -> Self {
        Self {
            merged_tree: Some(tree),
            unresolved_conflicts: Arc::from([]),
        }
    }

    fn conflicted(conflicts: Vec<MerkleThreeWayMergeConflict<K, V>>) -> Self {
        debug_assert!(!conflicts.is_empty());
        Self {
            merged_tree: None,
            unresolved_conflicts: Arc::from(conflicts),
        }
    }

    /// Returns whether a complete publishable tree was produced.
    #[must_use]
    pub const fn is_success(&self) -> bool {
        self.merged_tree.is_some()
    }

    /// Returns the complete tree only on success.
    #[must_use]
    pub const fn merged_tree(&self) -> Option<&MerkleSearchTree<K, V>> {
        self.merged_tree.as_ref()
    }

    /// Consumes a successful result and returns the complete tree.
    #[must_use]
    pub fn into_merged_tree(self) -> Option<MerkleSearchTree<K, V>> {
        self.merged_tree
    }

    /// Returns all unresolved conflicts; this is empty on success.
    #[must_use]
    pub fn unresolved_conflicts(&self) -> &[MerkleThreeWayMergeConflict<K, V>] {
        &self.unresolved_conflicts
    }
}

struct DecodedBlock<K, V> {
    block: MerkleBlock,
    level: u8,
    subtree_count: usize,
    entries: Vec<MerkleEntry<K, V>>,
    child_digests: Vec<MerkleDigest>,
}

struct VerificationContext<'a> {
    budget: &'a MerkleVerificationBudget,
    block_count: usize,
    total_bytes: u64,
    entry_count: u64,
    blocks: HashSet<MerkleDigest>,
}

impl<'a> VerificationContext<'a> {
    fn new(budget: &'a MerkleVerificationBudget) -> Self {
        Self {
            budget,
            block_count: 0,
            total_bytes: 0,
            entry_count: 0,
            blocks: HashSet::new(),
        }
    }

    fn check_depth(
        &self,
        depth: usize,
        digest: Option<MerkleDigest>,
    ) -> Result<(), MerkleVerificationError> {
        if depth == 0 || depth > self.budget.max_depth {
            Err(verification(
                MerkleVerificationFailureKind::ResourceLimitExceeded,
                "Merkle verification exceeded the maximum reference depth",
                digest,
            ))
        } else {
            Ok(())
        }
    }

    fn account(
        &mut self,
        block: &MerkleBlock,
        depth: usize,
    ) -> Result<bool, MerkleVerificationError> {
        self.check_depth(depth, Some(block.digest()))?;
        if !self.blocks.insert(block.digest()) {
            return Ok(false);
        }
        if block.len() > self.budget.max_block_byte_count {
            return Err(size_limit(
                "a Merkle block exceeds the per-block byte budget",
                block.digest(),
            ));
        }
        let block_bytes = u64_len(block.len())?;
        if self.block_count >= self.budget.max_block_count
            || block_bytes > self.budget.max_total_byte_count - self.total_bytes
        {
            return Err(size_limit(
                "Merkle verification exceeded its block or total-byte budget",
                block.digest(),
            ));
        }
        self.block_count += 1;
        self.total_bytes += block_bytes;
        Ok(true)
    }

    fn account_entries(
        &mut self,
        count: usize,
        digest: MerkleDigest,
    ) -> Result<(), MerkleVerificationError> {
        let count = u64_len(count)?;
        if count > self.budget.max_entry_count - self.entry_count {
            return Err(size_limit(
                "Merkle verification exceeded its decoded-entry budget",
                digest,
            ));
        }
        self.entry_count += count;
        Ok(())
    }

    fn account_proof_query(
        &mut self,
        byte_count: usize,
        root_hash: MerkleDigest,
    ) -> Result<(), MerkleVerificationError> {
        let byte_count_u64 = u64_len(byte_count)?;
        if byte_count > self.budget.max_proof_query_byte_count
            || byte_count_u64 > self.budget.max_total_byte_count - self.total_bytes
        {
            return Err(size_limit(
                "a Merkle proof query exceeds its query or total-byte budget",
                root_hash,
            ));
        }
        self.total_bytes += byte_count_u64;
        Ok(())
    }
}

struct OverlayBlockStore<'a> {
    staged: &'a HashMap<MerkleDigest, MerkleBlock>,
    fallback: Option<&'a dyn MerkleBlockStore>,
}

impl MerkleBlockStore for OverlayBlockStore<'_> {
    fn len(&self) -> usize {
        self.digests().len()
    }

    fn digests(&self) -> Vec<MerkleDigest> {
        let mut result = self.staged.keys().copied().collect::<HashSet<_>>();
        if let Some(fallback) = self.fallback {
            result.extend(fallback.digests());
        }
        let mut result = result.into_iter().collect::<Vec<_>>();
        result.sort_unstable();
        result
    }

    fn contains(&self, digest: MerkleDigest) -> bool {
        self.staged.contains_key(&digest)
            || self
                .fallback
                .is_some_and(|fallback| fallback.contains(digest))
    }

    fn get(&self, digest: MerkleDigest) -> Option<MerkleBlock> {
        self.staged
            .get(&digest)
            .cloned()
            .or_else(|| self.fallback.and_then(|fallback| fallback.get(digest)))
    }

    fn put(&self, _block: MerkleBlock) -> Result<bool, MerkleVerificationError> {
        Err(verification(
            MerkleVerificationFailureKind::MalformedBlock,
            "the verification overlay is read-only",
            None,
        ))
    }

    fn remove(&self, _digest: MerkleDigest) -> bool {
        false
    }

    fn clear(&self) {}
}

impl<K, V> MerkleSearchTree<K, V> {
    /// Writes the complete closure after preflighting every destination address conflict.
    pub fn save(&self, store: &dyn MerkleBlockStore) -> Result<usize, MerkleVerificationError> {
        let pack = self.export_pack();
        preflight_store(pack.blocks(), store)?;
        let mut added = 0_usize;
        for block in pack.blocks() {
            added += usize::from(store.put(block.clone())?);
        }
        Ok(added)
    }

    /// Exports the complete closure in deterministic preorder.
    #[must_use]
    pub fn export_pack(&self) -> MerkleBlockPack {
        let blocks = enumerate_nodes_preorder(self.root.as_ref())
            .into_iter()
            .map(|node| MerkleBlock::new(node.digest, node.block_bytes.as_ref()))
            .collect();
        MerkleBlockPack::from_unique(
            MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
            self.policy.domain_digest(),
            self.root_hash(),
            blocks,
        )
    }

    /// Exports unique explicitly requested blocks in the supplied transfer order.
    pub fn export_pack_for<I>(&self, digests: I) -> Result<MerkleBlockPack, MerkleVerificationError>
    where
        I: IntoIterator<Item = MerkleDigest>,
    {
        let by_digest = enumerate_nodes_preorder(self.root.as_ref())
            .into_iter()
            .map(|node| (node.digest, node))
            .collect::<HashMap<_, _>>();
        let mut unique = HashSet::new();
        let mut blocks = Vec::new();
        for digest in digests {
            if !unique.insert(digest) {
                return Err(verification(
                    MerkleVerificationFailureKind::DuplicateBlock,
                    format!("digest '{digest}' was requested more than once"),
                    Some(digest),
                ));
            }
            let node = by_digest.get(&digest).ok_or_else(|| {
                verification(
                    MerkleVerificationFailureKind::MissingBlock,
                    format!("digest '{digest}' does not name a block in this tree"),
                    Some(digest),
                )
            })?;
            blocks.push(MerkleBlock::new(node.digest, node.block_bytes.as_ref()));
        }
        Ok(MerkleBlockPack::from_unique(
            MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
            self.policy.domain_digest(),
            self.root_hash(),
            blocks,
        ))
    }

    /// Exports target blocks absent from a receiver while pruning known verified closures.
    #[must_use]
    pub fn create_sync_pack(&self, receiver: &dyn MerkleBlockStore) -> MerkleBlockPack {
        let mut missing = Vec::new();
        let mut pending = self.root.as_ref().into_iter().cloned().collect::<Vec<_>>();
        while let Some(node) = pending.pop() {
            if receiver.contains(node.digest) {
                continue;
            }
            missing.push(MerkleBlock::new(node.digest, node.block_bytes.as_ref()));
            for child in node.children.iter().rev().flatten() {
                pending.push(Arc::clone(child));
            }
        }
        MerkleBlockPack::from_unique(
            MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
            self.policy.domain_digest(),
            self.root_hash(),
            missing,
        )
    }

    /// Plans one iterative frontier round toward this target tree.
    pub fn plan_sync(
        &self,
        local_tree: &Self,
        receiver: &dyn MerkleBlockStore,
    ) -> Result<MerkleSyncPlan, MerkleTreeError> {
        self.ensure_compatible(local_tree)?;
        if self.root_hash() == local_tree.root_hash() {
            return Ok(MerkleSyncPlan::from_unique(
                MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
                self.policy.domain_digest(),
                local_tree.root_hash(),
                self.root_hash(),
                Vec::new(),
                0,
                0,
            ));
        }

        let mut requested = Vec::new();
        let mut examined_block_count = 0_usize;
        let mut examined_byte_count = 0_u64;
        let mut pending = self.root.as_ref().into_iter().cloned().collect::<Vec<_>>();
        while let Some(node) = pending.pop() {
            examined_block_count = examined_block_count
                .checked_add(1)
                .ok_or(MerkleTreeError::SizeOverflow)?;
            examined_byte_count = examined_byte_count
                .checked_add(
                    u64::try_from(node.block_bytes.len())
                        .map_err(|_| MerkleTreeError::SizeOverflow)?,
                )
                .ok_or(MerkleTreeError::SizeOverflow)?;
            if !receiver.contains(node.digest) {
                requested.push(node.digest);
                continue;
            }
            for child in node.children.iter().rev().flatten() {
                pending.push(Arc::clone(child));
            }
        }
        Ok(MerkleSyncPlan::from_unique(
            MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
            self.policy.domain_digest(),
            local_tree.root_hash(),
            self.root_hash(),
            requested,
            examined_block_count,
            examined_byte_count,
        ))
    }

    /// Loads and fully verifies a closure with bounded default limits.
    pub fn load(
        root_hash: MerkleDigest,
        policy: &MerkleSearchTreePolicy<K, V>,
        store: &dyn MerkleBlockStore,
    ) -> Result<Self, MerkleVerificationError> {
        Self::load_with_budget(
            root_hash,
            policy,
            store,
            &MerkleVerificationBudget::default(),
        )
    }

    /// Loads and fully verifies a closure under explicit finite limits.
    pub fn load_with_budget(
        root_hash: MerkleDigest,
        policy: &MerkleSearchTreePolicy<K, V>,
        store: &dyn MerkleBlockStore,
        budget: &MerkleVerificationBudget,
    ) -> Result<Self, MerkleVerificationError> {
        let mut context = VerificationContext::new(budget);
        Self::load_with_context(root_hash, policy, store, &mut context)
    }

    fn load_with_context(
        root_hash: MerkleDigest,
        policy: &MerkleSearchTreePolicy<K, V>,
        store: &dyn MerkleBlockStore,
        context: &mut VerificationContext<'_>,
    ) -> Result<Self, MerkleVerificationError> {
        let verifier = Self::new(policy.clone());
        if root_hash == policy.empty_digest() {
            return Ok(verifier);
        }
        let mut cache = HashMap::new();
        let mut active = HashSet::new();
        let root = verifier.load_node(root_hash, store, context, &mut cache, &mut active, 1)?;
        if root.digest != root_hash {
            return Err(verification(
                MerkleVerificationFailureKind::RootMismatch,
                "the reconstructed root does not match the requested root hash",
                Some(root_hash),
            ));
        }
        let tree = Self::from_verified_root(Some(root), policy.clone());
        tree.validate_structure().map_err(|error| {
            verification(
                MerkleVerificationFailureKind::InvalidReference,
                format!("verified closure failed final structure validation: {error}"),
                Some(root_hash),
            )
        })?;
        Ok(tree)
    }

    /// Verifies a complete/partial pack against its root and atomically preflights destination
    /// conflicts before committing any supplied block.
    pub fn import(
        pack: &MerkleBlockPack,
        policy: &MerkleSearchTreePolicy<K, V>,
        destination: Option<&dyn MerkleBlockStore>,
    ) -> Result<Self, MerkleVerificationError> {
        Self::import_with_budget(
            pack,
            policy,
            destination,
            &MerkleVerificationBudget::default(),
        )
    }

    /// Verifies and imports a pack under explicit finite limits.
    pub fn import_with_budget(
        pack: &MerkleBlockPack,
        policy: &MerkleSearchTreePolicy<K, V>,
        destination: Option<&dyn MerkleBlockStore>,
        budget: &MerkleVerificationBudget,
    ) -> Result<Self, MerkleVerificationError> {
        verify_envelope::<K, V>(pack.algorithm_id(), pack.domain_digest(), policy)?;
        let verifier = Self::new(policy.clone());
        let mut context = VerificationContext::new(budget);
        for block in pack.blocks() {
            verifier.decode_block(block, &mut context, 1)?;
        }
        let mut staged = HashMap::with_capacity(pack.block_count());
        for block in pack.blocks() {
            if staged.insert(block.digest(), block.clone()).is_some() {
                return Err(verification(
                    MerkleVerificationFailureKind::DuplicateBlock,
                    format!("the pack repeats block '{}'", block.digest()),
                    Some(block.digest()),
                ));
            }
        }
        let overlay = OverlayBlockStore {
            staged: &staged,
            fallback: destination,
        };
        let loaded = Self::load_with_context(pack.root_hash(), policy, &overlay, &mut context)?;
        if let Some(destination) = destination {
            preflight_store(pack.blocks(), destination)?;
            for block in pack.blocks() {
                destination.put(block.clone())?;
            }
        }
        Ok(loaded)
    }

    fn load_node(
        &self,
        digest: MerkleDigest,
        store: &dyn MerkleBlockStore,
        context: &mut VerificationContext<'_>,
        cache: &mut HashMap<MerkleDigest, Arc<MerkleNode<K, V>>>,
        active: &mut HashSet<MerkleDigest>,
        depth: usize,
    ) -> Result<Arc<MerkleNode<K, V>>, MerkleVerificationError> {
        context.check_depth(depth, Some(digest))?;
        if let Some(cached) = cache.get(&digest) {
            return Ok(Arc::clone(cached));
        }
        if !active.insert(digest) {
            return Err(verification(
                MerkleVerificationFailureKind::CycleDetected,
                "the Merkle closure contains a reference cycle",
                Some(digest),
            ));
        }
        let result = (|| {
            let block = store.get(digest).ok_or_else(|| {
                verification(
                    MerkleVerificationFailureKind::MissingBlock,
                    format!("required Merkle block '{digest}' is absent"),
                    Some(digest),
                )
            })?;
            let decoded = self.decode_block(&block, context, depth)?;
            let mut children: Vec<MerkleNodeLink<K, V>> =
                Vec::with_capacity(decoded.child_digests.len());
            for (index, child_digest) in decoded.child_digests.iter().copied().enumerate() {
                if child_digest == self.policy.empty_digest() {
                    children.push(None);
                    continue;
                }
                let child = self.load_node(
                    child_digest,
                    store,
                    context,
                    cache,
                    active,
                    depth.checked_add(1).ok_or_else(|| {
                        size_limit("Merkle reference depth overflowed", child_digest)
                    })?,
                )?;
                self.validate_reference(&decoded, index, &child)?;
                children.push(Some(child));
            }

            let mut actual_count = decoded.entries.len();
            for child in children.iter().flatten() {
                actual_count = actual_count
                    .checked_add(child.count)
                    .ok_or_else(|| size_limit("Merkle subtree entry count overflowed", digest))?;
            }
            if actual_count != decoded.subtree_count {
                return Err(verification(
                    MerkleVerificationFailureKind::InvalidReference,
                    format!(
                        "block '{digest}' declares {} entries but its closure contains {actual_count}",
                        decoded.subtree_count
                    ),
                    Some(digest),
                ));
            }
            let node = self
                .new_node(decoded.level, decoded.entries, children)
                .map_err(|error| map_tree_build_error(error, digest))?;
            if node.digest != digest || node.block_bytes.as_ref() != decoded.block.content() {
                return Err(verification(
                    MerkleVerificationFailureKind::NonCanonicalBlock,
                    "decoded block bytes do not round-trip canonically",
                    Some(digest),
                ));
            }
            cache.insert(digest, Arc::clone(&node));
            Ok(node)
        })();
        active.remove(&digest);
        result
    }

    fn validate_reference(
        &self,
        parent: &DecodedBlock<K, V>,
        child_index: usize,
        child: &MerkleNode<K, V>,
    ) -> Result<(), MerkleVerificationError> {
        if child.level >= parent.level {
            return Err(verification(
                MerkleVerificationFailureKind::InvalidReference,
                "a child block is not below its parent layer",
                Some(child.digest),
            ));
        }
        if child_index != 0
            && !self
                .policy
                .compare(
                    child.minimum_key.as_ref(),
                    parent.entries[child_index - 1].key(),
                )
                .is_gt()
        {
            return Err(verification(
                MerkleVerificationFailureKind::InvalidReference,
                "a child block crosses its lower key separator",
                Some(child.digest),
            ));
        }
        if child_index != parent.entries.len()
            && !self
                .policy
                .compare(
                    child.maximum_key.as_ref(),
                    parent.entries[child_index].key(),
                )
                .is_lt()
        {
            return Err(verification(
                MerkleVerificationFailureKind::InvalidReference,
                "a child block crosses its upper key separator",
                Some(child.digest),
            ));
        }
        Ok(())
    }

    fn decode_block(
        &self,
        block: &MerkleBlock,
        context: &mut VerificationContext<'_>,
        depth: usize,
    ) -> Result<DecodedBlock<K, V>, MerkleVerificationError> {
        let first_visit = context.account(block, depth)?;
        if MerkleDigest::hash(block.content()) != block.digest() {
            return Err(verification(
                MerkleVerificationFailureKind::DigestMismatch,
                "a block's bytes do not match its claimed digest",
                Some(block.digest()),
            ));
        }

        let bytes = block.content();
        let mut offset = 0_usize;
        if bytes.len() < MERKLE_BLOCK_HEADER_LENGTH + MERKLE_DIGEST_LENGTH {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle block is shorter than the minimum encoding",
                Some(block.digest()),
            ));
        }
        if take(
            bytes,
            &mut offset,
            MERKLE_BLOCK_MAGIC.len(),
            Some(block.digest()),
        )? != MERKLE_BLOCK_MAGIC
        {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle block has the wrong magic",
                Some(block.digest()),
            ));
        }
        if take(bytes, &mut offset, 1, Some(block.digest()))?[0] != MERKLE_NODE_BLOCK_TAG {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle block has an unsupported tag",
                Some(block.digest()),
            ));
        }
        let domain = MerkleDigest::from_bytes(take(
            bytes,
            &mut offset,
            MERKLE_DIGEST_LENGTH,
            Some(block.digest()),
        )?)
        .expect("a fixed-length digest slice always parses");
        if domain != self.policy.domain_digest() {
            return Err(verification(
                MerkleVerificationFailureKind::DomainMismatch,
                "a Merkle block belongs to another policy domain",
                Some(block.digest()),
            ));
        }
        let level = take(bytes, &mut offset, 1, Some(block.digest()))?[0];
        if level > 64 {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle block layer exceeds the SHA-256 nibble range",
                Some(block.digest()),
            ));
        }
        let subtree_count = read_positive_i32(bytes, &mut offset, block.digest(), "subtree")?;
        let entry_count = read_positive_i32(bytes, &mut offset, block.digest(), "entry")?;
        if subtree_count < entry_count {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle block has an entry count greater than its subtree count",
                Some(block.digest()),
            ));
        }
        if entry_count >= context.budget.max_child_references_per_block {
            return Err(size_limit(
                "a Merkle block exceeds the child-reference budget",
                block.digest(),
            ));
        }
        let minimum_length = u64_len(MERKLE_BLOCK_HEADER_LENGTH)?
            .checked_add(
                u64_len(entry_count)?
                    .checked_mul(u64_len(8 + MERKLE_DIGEST_LENGTH)?)
                    .ok_or_else(|| {
                        size_limit("a Merkle block minimum length overflowed", block.digest())
                    })?,
            )
            .and_then(|length| length.checked_add(MERKLE_DIGEST_LENGTH as u64))
            .ok_or_else(|| {
                size_limit("a Merkle block minimum length overflowed", block.digest())
            })?;
        if minimum_length > u64_len(bytes.len())? {
            return Err(verification(
                MerkleVerificationFailureKind::MalformedBlock,
                "a Merkle block cannot contain its declared entries and child references",
                Some(block.digest()),
            ));
        }
        if first_visit {
            context.account_entries(entry_count, block.digest())?;
        }

        let mut entries: Vec<MerkleEntry<K, V>> = Vec::with_capacity(entry_count);
        for index in 0..entry_count {
            let key_bytes = read_length_prefixed(bytes, &mut offset, block.digest())?;
            let value_bytes = read_length_prefixed(bytes, &mut offset, block.digest())?;
            let key = self
                .policy
                .key_codec()
                .decode(&key_bytes)
                .map_err(|error| {
                    verification(
                        MerkleVerificationFailureKind::MalformedBlock,
                        format!("a Merkle key codec rejected serialized entry bytes: {error}"),
                        Some(block.digest()),
                    )
                })?;
            let value = self
                .policy
                .value_codec()
                .decode(&value_bytes)
                .map_err(|error| {
                    verification(
                        MerkleVerificationFailureKind::MalformedBlock,
                        format!("a Merkle value codec rejected serialized entry bytes: {error}"),
                        Some(block.digest()),
                    )
                })?;
            let canonical_key = self.policy.key_codec().encode(&key).map_err(|error| {
                verification(
                    MerkleVerificationFailureKind::NonCanonicalBlock,
                    format!("a decoded key could not be re-encoded: {error}"),
                    Some(block.digest()),
                )
            })?;
            let canonical_value = self.policy.value_codec().encode(&value).map_err(|error| {
                verification(
                    MerkleVerificationFailureKind::NonCanonicalBlock,
                    format!("a decoded value could not be re-encoded: {error}"),
                    Some(block.digest()),
                )
            })?;
            if canonical_key != key_bytes || canonical_value != value_bytes {
                return Err(verification(
                    MerkleVerificationFailureKind::NonCanonicalBlock,
                    "a decoded entry does not round-trip to its exact encoded bytes",
                    Some(block.digest()),
                ));
            }
            let entry_level =
                MerkleSearchTreePolicy::<K, V>::level(self.policy.hash_key_bytes(&key_bytes));
            if entry_level != level {
                return Err(verification(
                    MerkleVerificationFailureKind::NonCanonicalBlock,
                    "an entry is stored at a level other than its hash-derived level",
                    Some(block.digest()),
                ));
            }
            let entry = MerkleEntry::from_encoded(key, value, key_bytes, value_bytes, entry_level);
            if index != 0
                && !self
                    .policy
                    .compare(entries[index - 1].key(), entry.key())
                    .is_lt()
            {
                return Err(verification(
                    MerkleVerificationFailureKind::NonCanonicalBlock,
                    "Merkle block entries are not strictly comparer-ordered",
                    Some(block.digest()),
                ));
            }
            entries.push(entry);
        }
        let child_count = entry_count.checked_add(1).ok_or_else(|| {
            size_limit("a Merkle child-reference count overflowed", block.digest())
        })?;
        let mut child_digests = Vec::with_capacity(child_count);
        for _ in 0..child_count {
            child_digests.push(
                MerkleDigest::from_bytes(take(
                    bytes,
                    &mut offset,
                    MERKLE_DIGEST_LENGTH,
                    Some(block.digest()),
                )?)
                .expect("a fixed-length digest slice always parses"),
            );
        }
        if offset != bytes.len() {
            return Err(verification(
                MerkleVerificationFailureKind::NonCanonicalBlock,
                "a Merkle block has trailing bytes",
                Some(block.digest()),
            ));
        }
        let canonical = self.encode_raw_block(
            level,
            subtree_count,
            &entries,
            &child_digests,
            block.digest(),
        )?;
        if canonical != bytes {
            return Err(verification(
                MerkleVerificationFailureKind::NonCanonicalBlock,
                "a Merkle block is not the unique canonical encoding",
                Some(block.digest()),
            ));
        }
        Ok(DecodedBlock {
            block: block.clone(),
            level,
            subtree_count,
            entries,
            child_digests,
        })
    }

    fn encode_raw_block(
        &self,
        level: u8,
        subtree_count: usize,
        entries: &[MerkleEntry<K, V>],
        child_digests: &[MerkleDigest],
        digest: MerkleDigest,
    ) -> Result<Vec<u8>, MerkleVerificationError> {
        let subtree_count = i32::try_from(subtree_count)
            .map_err(|_| size_limit("a Merkle subtree count exceeds the wire limit", digest))?;
        let entry_count = i32::try_from(entries.len())
            .map_err(|_| size_limit("a Merkle entry count exceeds the wire limit", digest))?;
        let mut length = MERKLE_BLOCK_HEADER_LENGTH
            .checked_add(
                child_digests
                    .len()
                    .checked_mul(MERKLE_DIGEST_LENGTH)
                    .ok_or_else(|| size_limit("a Merkle block length overflowed", digest))?,
            )
            .ok_or_else(|| size_limit("a Merkle block length overflowed", digest))?;
        for entry in entries {
            i32::try_from(entry.key_bytes().len())
                .map_err(|_| size_limit("a Merkle key exceeds the wire limit", digest))?;
            i32::try_from(entry.value_bytes().len())
                .map_err(|_| size_limit("a Merkle value exceeds the wire limit", digest))?;
            length = length
                .checked_add(8)
                .and_then(|value| value.checked_add(entry.key_bytes().len()))
                .and_then(|value| value.checked_add(entry.value_bytes().len()))
                .ok_or_else(|| size_limit("a Merkle block length overflowed", digest))?;
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
        for child_digest in child_digests {
            result.extend_from_slice(child_digest.as_bytes());
        }
        Ok(result)
    }
}

impl<K, V> MerkleSearchTree<K, V> {
    /// Creates a canonical `MSP2` membership or nonmembership proof for one key.
    pub fn create_proof(&self, key: &K) -> Result<MerkleProof, MerkleProofCreationError> {
        let key_bytes = self.policy.key_codec().encode(key)?;
        let mut steps = Vec::new();
        let mut node = self.root.as_deref();
        let mut kind = MerkleProofKind::NonMembership;
        let mut value_bytes = None;
        while let Some(current) = node {
            let (position, found) = self.find_position(&current.entries, key);
            let expanded = if !found && current.children[position].is_some() {
                vec![position]
            } else {
                Vec::new()
            };
            steps.push(MerkleProofStep::from_unique(
                MerkleBlock::new(current.digest, current.block_bytes.as_ref()),
                expanded,
            ));
            if found {
                kind = MerkleProofKind::Membership;
                value_bytes = Some(current.entries[position].value_bytes().to_vec());
                break;
            }
            node = current.children[position].as_deref();
        }
        let query = encode_point_query(kind, &key_bytes, value_bytes.as_deref())?;
        Ok(MerkleProof::from_validated(
            MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
            self.policy.domain_digest(),
            self.root_hash(),
            kind,
            query,
            steps,
        ))
    }

    /// Creates a canonical `MSP2` completeness proof for an inclusive key range.
    pub fn create_range_proof(
        &self,
        minimum_key: &K,
        maximum_key: &K,
    ) -> Result<MerkleProof, MerkleProofCreationError> {
        if self.policy.compare(minimum_key, maximum_key).is_gt() {
            return Err(MerkleProofCreationError::ReversedRange);
        }
        let mut steps = Vec::new();
        self.collect_range_proof(self.root.as_deref(), minimum_key, maximum_key, &mut steps);
        let minimum_bytes = self.policy.key_codec().encode(minimum_key)?;
        let maximum_bytes = self.policy.key_codec().encode(maximum_key)?;
        let query = encode_range_query(&minimum_bytes, &maximum_bytes)?;
        Ok(MerkleProof::from_validated(
            MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID,
            self.policy.domain_digest(),
            self.root_hash(),
            MerkleProofKind::Range,
            query,
            steps,
        ))
    }

    /// Verifies an untrusted proof with bounded default limits.
    #[must_use]
    pub fn verify_proof(
        proof: &MerkleProof,
        policy: &MerkleSearchTreePolicy<K, V>,
    ) -> MerkleProofVerificationResult {
        Self::verify_proof_with_budget(proof, policy, &MerkleVerificationBudget::default())
    }

    /// Verifies an untrusted proof under explicit finite limits.
    #[must_use]
    pub fn verify_proof_with_budget(
        proof: &MerkleProof,
        policy: &MerkleSearchTreePolicy<K, V>,
        budget: &MerkleVerificationBudget,
    ) -> MerkleProofVerificationResult {
        let mut context = VerificationContext::new(budget);
        let result = (|| {
            // This is intentionally the first operation: oversized queries are rejected before
            // envelope checks, codec callbacks, block hashing, or block decoding.
            context.account_proof_query(proof.query.len(), proof.root_hash)?;
            verify_envelope::<K, V>(proof.algorithm_id(), proof.domain_digest(), policy)?;
            let verifier = Self::new(policy.clone());
            let mut decoded = HashMap::with_capacity(proof.steps.len());
            let mut steps = HashMap::with_capacity(proof.steps.len());
            for step in proof.steps.iter() {
                let block = verifier.decode_block(&step.block, &mut context, 1)?;
                if decoded.insert(step.block.digest(), block).is_some()
                    || steps.insert(step.block.digest(), step).is_some()
                {
                    return Err(verification(
                        MerkleVerificationFailureKind::DuplicateBlock,
                        "a proof repeats a block digest",
                        Some(step.block.digest()),
                    ));
                }
                let decoded_block = decoded
                    .get(&step.block.digest())
                    .expect("the decoded proof block was just inserted");
                for &index in step.expanded_child_indexes.iter() {
                    if index >= decoded_block.child_digests.len() {
                        return Err(verification(
                            MerkleVerificationFailureKind::ProofMismatch,
                            "a proof expands a child index outside its block",
                            Some(step.block.digest()),
                        ));
                    }
                    if decoded_block.child_digests[index] == policy.empty_digest() {
                        return Err(verification(
                            MerkleVerificationFailureKind::ProofMismatch,
                            "a proof expands an empty child interval",
                            Some(step.block.digest()),
                        ));
                    }
                }
            }

            if proof.root_hash == policy.empty_digest() {
                if !proof.steps.is_empty() {
                    return Err(verification(
                        MerkleVerificationFailureKind::ProofMismatch,
                        "an empty-root proof must not contain blocks",
                        Some(proof.root_hash),
                    ));
                }
                verifier.verify_empty_proof_query(proof)?;
                return Ok(());
            }
            if !decoded.contains_key(&proof.root_hash) {
                return Err(verification(
                    MerkleVerificationFailureKind::RootMismatch,
                    "the proof does not contain its declared root block",
                    Some(proof.root_hash),
                ));
            }
            let mut visited = HashSet::new();
            match proof.kind {
                MerkleProofKind::Membership | MerkleProofKind::NonMembership => {
                    verifier.verify_point_proof(proof, &decoded, &steps, &mut visited, &context)?
                }
                MerkleProofKind::Range => {
                    verifier.verify_range_proof(proof, &decoded, &steps, &mut visited, &context)?
                }
            }
            if visited.len() != proof.steps.len() {
                return Err(verification(
                    MerkleVerificationFailureKind::ProofMismatch,
                    "the proof contains blocks outside the canonical query expansion",
                    Some(proof.root_hash),
                ));
            }
            Ok(())
        })();
        match result {
            Ok(()) => MerkleProofVerificationResult::success(
                proof.root_hash,
                context.block_count,
                context.total_bytes,
            ),
            Err(error) => MerkleProofVerificationResult::failure(
                error,
                context.block_count,
                context.total_bytes,
                Some(proof.root_hash),
            ),
        }
    }

    fn collect_range_proof(
        &self,
        node: Option<&MerkleNode<K, V>>,
        minimum_key: &K,
        maximum_key: &K,
        steps: &mut Vec<MerkleProofStep>,
    ) {
        let Some(node) = node else {
            return;
        };
        let expanded = (0..node.children.len())
            .filter(|&index| {
                node.children[index].is_some()
                    && self.child_interval_intersects(
                        &node.entries,
                        index,
                        minimum_key,
                        maximum_key,
                    )
            })
            .collect::<Vec<_>>();
        steps.push(MerkleProofStep::from_unique(
            MerkleBlock::new(node.digest, node.block_bytes.as_ref()),
            expanded.clone(),
        ));
        for index in expanded {
            self.collect_range_proof(
                node.children[index].as_deref(),
                minimum_key,
                maximum_key,
                steps,
            );
        }
    }

    fn child_interval_intersects(
        &self,
        entries: &[MerkleEntry<K, V>],
        child_index: usize,
        minimum_key: &K,
        maximum_key: &K,
    ) -> bool {
        (child_index == 0
            || self
                .policy
                .compare(entries[child_index - 1].key(), maximum_key)
                .is_lt())
            && (child_index == entries.len()
                || self
                    .policy
                    .compare(entries[child_index].key(), minimum_key)
                    .is_gt())
    }

    fn verify_point_proof(
        &self,
        proof: &MerkleProof,
        decoded: &HashMap<MerkleDigest, DecodedBlock<K, V>>,
        steps: &HashMap<MerkleDigest, &MerkleProofStep>,
        visited: &mut HashSet<MerkleDigest>,
        context: &VerificationContext<'_>,
    ) -> Result<(), MerkleVerificationError> {
        let (key, value_bytes) = self.decode_point_query(proof)?;
        let mut digest = proof.root_hash;
        let mut depth = 1_usize;
        loop {
            if !visited.insert(digest) {
                return Err(verification(
                    MerkleVerificationFailureKind::CycleDetected,
                    "a proof expansion contains a cycle",
                    Some(digest),
                ));
            }
            let block = decoded.get(&digest).ok_or_else(|| {
                verification(
                    MerkleVerificationFailureKind::MissingBlock,
                    "a proof omits an expanded point-query block",
                    Some(digest),
                )
            })?;
            let step = steps.get(&digest).ok_or_else(|| {
                verification(
                    MerkleVerificationFailureKind::MissingBlock,
                    "a proof omits an expanded point-query step",
                    Some(digest),
                )
            })?;
            context.check_depth(depth, Some(digest))?;
            let (position, found) = self.find_position(&block.entries, &key);
            if found {
                require_expanded_exactly(step, &[])?;
                if proof.kind != MerkleProofKind::Membership {
                    return Err(verification(
                        MerkleVerificationFailureKind::ProofMismatch,
                        "the proof claims absence for a present key",
                        Some(digest),
                    ));
                }
                if value_bytes.as_deref() != Some(block.entries[position].value_bytes()) {
                    return Err(verification(
                        MerkleVerificationFailureKind::ProofMismatch,
                        "the membership value claim does not match the authenticated entry",
                        Some(digest),
                    ));
                }
                return Ok(());
            }
            let child_digest = block.child_digests[position];
            if child_digest == self.policy.empty_digest() {
                require_expanded_exactly(step, &[])?;
                if proof.kind != MerkleProofKind::NonMembership {
                    return Err(verification(
                        MerkleVerificationFailureKind::ProofMismatch,
                        "the proof claims membership for an absent key",
                        Some(digest),
                    ));
                }
                return Ok(());
            }
            require_expanded_exactly(step, &[position])?;
            let child = decoded.get(&child_digest).ok_or_else(|| {
                verification(
                    MerkleVerificationFailureKind::MissingBlock,
                    "a proof omits its expanded child block",
                    Some(child_digest),
                )
            })?;
            self.validate_proof_reference(block, position, child)?;
            digest = child_digest;
            depth = depth
                .checked_add(1)
                .ok_or_else(|| size_limit("a proof reference depth overflowed", child_digest))?;
        }
    }

    fn verify_range_proof(
        &self,
        proof: &MerkleProof,
        decoded: &HashMap<MerkleDigest, DecodedBlock<K, V>>,
        steps: &HashMap<MerkleDigest, &MerkleProofStep>,
        visited: &mut HashSet<MerkleDigest>,
        context: &VerificationContext<'_>,
    ) -> Result<(), MerkleVerificationError> {
        let (minimum_key, maximum_key) = self.decode_range_query(proof)?;
        self.verify_range_block(
            proof.root_hash,
            &minimum_key,
            &maximum_key,
            decoded,
            steps,
            visited,
            context,
            1,
        )
    }

    #[allow(clippy::too_many_arguments)]
    fn verify_range_block(
        &self,
        digest: MerkleDigest,
        minimum_key: &K,
        maximum_key: &K,
        decoded: &HashMap<MerkleDigest, DecodedBlock<K, V>>,
        steps: &HashMap<MerkleDigest, &MerkleProofStep>,
        visited: &mut HashSet<MerkleDigest>,
        context: &VerificationContext<'_>,
        depth: usize,
    ) -> Result<(), MerkleVerificationError> {
        context.check_depth(depth, Some(digest))?;
        if !visited.insert(digest) {
            return Err(verification(
                MerkleVerificationFailureKind::CycleDetected,
                "a range-proof expansion contains a repeated block",
                Some(digest),
            ));
        }
        let block = decoded.get(&digest).ok_or_else(|| {
            verification(
                MerkleVerificationFailureKind::MissingBlock,
                "a range proof omits an expanded block",
                Some(digest),
            )
        })?;
        let step = steps.get(&digest).ok_or_else(|| {
            verification(
                MerkleVerificationFailureKind::MissingBlock,
                "a range proof omits an expanded step",
                Some(digest),
            )
        })?;
        let expected = (0..block.child_digests.len())
            .filter(|&index| {
                block.child_digests[index] != self.policy.empty_digest()
                    && self.child_interval_intersects(
                        &block.entries,
                        index,
                        minimum_key,
                        maximum_key,
                    )
            })
            .collect::<Vec<_>>();
        require_expanded_exactly(step, &expected)?;
        for index in expected {
            let child_digest = block.child_digests[index];
            let child = decoded.get(&child_digest).ok_or_else(|| {
                verification(
                    MerkleVerificationFailureKind::MissingBlock,
                    "a range proof omits an expanded child block",
                    Some(child_digest),
                )
            })?;
            self.validate_proof_reference(block, index, child)?;
            self.verify_range_block(
                child_digest,
                minimum_key,
                maximum_key,
                decoded,
                steps,
                visited,
                context,
                depth
                    .checked_add(1)
                    .ok_or_else(|| size_limit("a range-proof depth overflowed", child_digest))?,
            )?;
        }
        Ok(())
    }

    fn validate_proof_reference(
        &self,
        parent: &DecodedBlock<K, V>,
        child_index: usize,
        child: &DecodedBlock<K, V>,
    ) -> Result<(), MerkleVerificationError> {
        if child.level >= parent.level {
            return Err(verification(
                MerkleVerificationFailureKind::InvalidReference,
                "an expanded proof child is not below its parent level",
                Some(child.block.digest()),
            ));
        }
        for entry in &child.entries {
            if child_index != 0
                && !self
                    .policy
                    .compare(entry.key(), parent.entries[child_index - 1].key())
                    .is_gt()
            {
                return Err(verification(
                    MerkleVerificationFailureKind::InvalidReference,
                    "an expanded proof child crosses its lower separator",
                    Some(child.block.digest()),
                ));
            }
            if child_index != parent.entries.len()
                && !self
                    .policy
                    .compare(entry.key(), parent.entries[child_index].key())
                    .is_lt()
            {
                return Err(verification(
                    MerkleVerificationFailureKind::InvalidReference,
                    "an expanded proof child crosses its upper separator",
                    Some(child.block.digest()),
                ));
            }
        }
        Ok(())
    }

    fn decode_point_query(
        &self,
        proof: &MerkleProof,
    ) -> Result<(K, Option<Vec<u8>>), MerkleVerificationError> {
        if !matches!(
            proof.kind,
            MerkleProofKind::Membership | MerkleProofKind::NonMembership
        ) {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a point proof has the wrong proof kind",
                Some(proof.root_hash),
            ));
        }
        let mut offset = read_query_header(&proof.query, proof.kind, proof.root_hash)?;
        let key_bytes = read_query_field(&proof.query, &mut offset, proof.root_hash)?;
        let value_bytes = if proof.kind == MerkleProofKind::Membership {
            let bytes = read_query_field(&proof.query, &mut offset, proof.root_hash)?.to_vec();
            let value = self.policy.value_codec().decode(&bytes).map_err(|error| {
                verification(
                    MerkleVerificationFailureKind::ProofMismatch,
                    format!("a membership query has an invalid value encoding: {error}"),
                    Some(proof.root_hash),
                )
            })?;
            let canonical = self.policy.value_codec().encode(&value).map_err(|error| {
                verification(
                    MerkleVerificationFailureKind::ProofMismatch,
                    format!("a membership query value could not be re-encoded: {error}"),
                    Some(proof.root_hash),
                )
            })?;
            if canonical != bytes {
                return Err(verification(
                    MerkleVerificationFailureKind::ProofMismatch,
                    "a membership query value is not canonically encoded",
                    Some(proof.root_hash),
                ));
            }
            Some(bytes)
        } else {
            None
        };
        if offset != proof.query.len() {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a point-proof query has trailing bytes",
                Some(proof.root_hash),
            ));
        }
        Ok((
            self.decode_canonical_query_key(key_bytes, proof.root_hash)?,
            value_bytes,
        ))
    }

    fn decode_range_query(&self, proof: &MerkleProof) -> Result<(K, K), MerkleVerificationError> {
        if proof.kind != MerkleProofKind::Range {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a range proof has the wrong proof kind",
                Some(proof.root_hash),
            ));
        }
        let mut offset = read_query_header(&proof.query, MerkleProofKind::Range, proof.root_hash)?;
        let minimum_bytes = read_query_field(&proof.query, &mut offset, proof.root_hash)?;
        let maximum_bytes = read_query_field(&proof.query, &mut offset, proof.root_hash)?;
        if offset != proof.query.len() {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a range-proof query has trailing bytes",
                Some(proof.root_hash),
            ));
        }
        let minimum = self.decode_canonical_query_key(minimum_bytes, proof.root_hash)?;
        let maximum = self.decode_canonical_query_key(maximum_bytes, proof.root_hash)?;
        if self.policy.compare(&minimum, &maximum).is_gt() {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a range-proof query has reversed bounds",
                Some(proof.root_hash),
            ));
        }
        Ok((minimum, maximum))
    }

    fn decode_canonical_query_key(
        &self,
        bytes: &[u8],
        root_hash: MerkleDigest,
    ) -> Result<K, MerkleVerificationError> {
        let key = self.policy.key_codec().decode(bytes).map_err(|error| {
            verification(
                MerkleVerificationFailureKind::ProofMismatch,
                format!("a proof query has an invalid key encoding: {error}"),
                Some(root_hash),
            )
        })?;
        let canonical = self.policy.key_codec().encode(&key).map_err(|error| {
            verification(
                MerkleVerificationFailureKind::ProofMismatch,
                format!("a proof query key could not be re-encoded: {error}"),
                Some(root_hash),
            )
        })?;
        if canonical != bytes {
            return Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "a proof query key is not canonically encoded",
                Some(root_hash),
            ));
        }
        Ok(key)
    }

    fn verify_empty_proof_query(&self, proof: &MerkleProof) -> Result<(), MerkleVerificationError> {
        match proof.kind {
            MerkleProofKind::NonMembership => self.decode_point_query(proof).map(|_| ()),
            MerkleProofKind::Range => self.decode_range_query(proof).map(|_| ()),
            MerkleProofKind::Membership => Err(verification(
                MerkleVerificationFailureKind::ProofMismatch,
                "an empty tree cannot prove membership",
                Some(proof.root_hash),
            )),
        }
    }
}

fn encode_point_query(
    kind: MerkleProofKind,
    key_bytes: &[u8],
    value_bytes: Option<&[u8]>,
) -> Result<Vec<u8>, MerkleProofCreationError> {
    match (kind, value_bytes) {
        (MerkleProofKind::Membership, None) | (MerkleProofKind::NonMembership, Some(_)) => {
            unreachable!("trusted point-proof construction supplies exactly the claimed value")
        }
        (MerkleProofKind::Range, _) => unreachable!("a range uses its dedicated query encoder"),
        _ => {}
    }
    let key_length =
        i32::try_from(key_bytes.len()).map_err(|_| MerkleProofCreationError::SizeOverflow)?;
    let value_length = value_bytes
        .map(|bytes| i32::try_from(bytes.len()).map_err(|_| MerkleProofCreationError::SizeOverflow))
        .transpose()?;
    let mut length = PROOF_MAGIC
        .len()
        .checked_add(1 + 4)
        .and_then(|length| length.checked_add(key_bytes.len()))
        .ok_or(MerkleProofCreationError::SizeOverflow)?;
    if let Some(value_bytes) = value_bytes {
        length = length
            .checked_add(4)
            .and_then(|length| length.checked_add(value_bytes.len()))
            .ok_or(MerkleProofCreationError::SizeOverflow)?;
    }
    let mut result = Vec::with_capacity(length);
    result.extend_from_slice(PROOF_MAGIC);
    result.push(kind as u8);
    result.extend_from_slice(&key_length.to_be_bytes());
    result.extend_from_slice(key_bytes);
    if let (Some(value_length), Some(value_bytes)) = (value_length, value_bytes) {
        result.extend_from_slice(&value_length.to_be_bytes());
        result.extend_from_slice(value_bytes);
    }
    Ok(result)
}

fn encode_range_query(
    minimum_bytes: &[u8],
    maximum_bytes: &[u8],
) -> Result<Vec<u8>, MerkleProofCreationError> {
    let minimum_length =
        i32::try_from(minimum_bytes.len()).map_err(|_| MerkleProofCreationError::SizeOverflow)?;
    let maximum_length =
        i32::try_from(maximum_bytes.len()).map_err(|_| MerkleProofCreationError::SizeOverflow)?;
    let length = PROOF_MAGIC
        .len()
        .checked_add(1 + 8)
        .and_then(|length| length.checked_add(minimum_bytes.len()))
        .and_then(|length| length.checked_add(maximum_bytes.len()))
        .ok_or(MerkleProofCreationError::SizeOverflow)?;
    let mut result = Vec::with_capacity(length);
    result.extend_from_slice(PROOF_MAGIC);
    result.push(MerkleProofKind::Range as u8);
    result.extend_from_slice(&minimum_length.to_be_bytes());
    result.extend_from_slice(minimum_bytes);
    result.extend_from_slice(&maximum_length.to_be_bytes());
    result.extend_from_slice(maximum_bytes);
    Ok(result)
}

fn read_query_header(
    query: &[u8],
    expected_kind: MerkleProofKind,
    root_hash: MerkleDigest,
) -> Result<usize, MerkleVerificationError> {
    let mut offset = 0_usize;
    if take(query, &mut offset, PROOF_MAGIC.len(), Some(root_hash))? != PROOF_MAGIC {
        return Err(verification(
            MerkleVerificationFailureKind::ProofMismatch,
            "a proof query has the wrong magic",
            Some(root_hash),
        ));
    }
    if take(query, &mut offset, 1, Some(root_hash))?[0] != expected_kind as u8 {
        return Err(verification(
            MerkleVerificationFailureKind::ProofMismatch,
            "a proof query kind disagrees with its envelope",
            Some(root_hash),
        ));
    }
    Ok(offset)
}

fn read_query_field<'a>(
    query: &'a [u8],
    offset: &mut usize,
    root_hash: MerkleDigest,
) -> Result<&'a [u8], MerkleVerificationError> {
    let length = read_i32(query, offset, Some(root_hash))?;
    if length < 0 {
        return Err(verification(
            MerkleVerificationFailureKind::ProofMismatch,
            "a proof query field has a negative length",
            Some(root_hash),
        ));
    }
    take(query, offset, length as usize, Some(root_hash))
}

fn require_expanded_exactly(
    step: &MerkleProofStep,
    expected: &[usize],
) -> Result<(), MerkleVerificationError> {
    if step.expanded_child_indexes.as_ref() != expected {
        Err(verification(
            MerkleVerificationFailureKind::ProofMismatch,
            "a proof step expands a noncanonical set of child intervals",
            Some(step.block.digest()),
        ))
    } else {
        Ok(())
    }
}

struct MergeSide<'a, K, V> {
    entry: Option<&'a MerkleEntry<K, V>>,
}

impl<K, V> Copy for MergeSide<'_, K, V> {}

impl<K, V> Clone for MergeSide<'_, K, V> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<'a, K, V> MergeSide<'a, K, V> {
    fn absent() -> Self {
        Self { entry: None }
    }

    fn present(entry: &'a MerkleEntry<K, V>) -> Self {
        Self { entry: Some(entry) }
    }

    fn merge_value(self) -> MerkleMergeValue<V> {
        self.entry.map_or(MerkleMergeValue::Absent, |entry| {
            MerkleMergeValue::Present(entry.value_handle())
        })
    }
}

impl<K, V> MerkleSearchTree<K, V> {
    /// Three-way merges descendants with ordinary value equality.
    pub fn merge(
        base: &Self,
        left: &Self,
        right: &Self,
        resolver: Option<&mut MerkleThreeWayMergeResolver<'_, K, V>>,
    ) -> Result<MerkleThreeWayMergeResult<K, V>, MerkleTreeError>
    where
        V: PartialEq,
    {
        Self::merge_by(base, left, right, resolver, PartialEq::eq)
    }

    /// Three-way merges descendants with a caller-supplied semantic value relation.
    pub fn merge_by<F>(
        base: &Self,
        left: &Self,
        right: &Self,
        mut resolver: Option<&mut MerkleThreeWayMergeResolver<'_, K, V>>,
        values_equal: F,
    ) -> Result<MerkleThreeWayMergeResult<K, V>, MerkleTreeError>
    where
        F: Fn(&V, &V) -> bool,
    {
        base.ensure_compatible(left)?;
        base.ensure_compatible(right)?;
        if left.root_hash() == base.root_hash() {
            return Ok(MerkleThreeWayMergeResult::success(right.clone()));
        }
        if right.root_hash() == base.root_hash() || left.root_hash() == right.root_hash() {
            return Ok(MerkleThreeWayMergeResult::success(left.clone()));
        }

        let base_entries = base.iter().collect::<Vec<_>>();
        let left_entries = left.iter().collect::<Vec<_>>();
        let right_entries = right.iter().collect::<Vec<_>>();
        let mut base_index = 0_usize;
        let mut left_index = 0_usize;
        let mut right_index = 0_usize;
        let mut output = Vec::new();
        let mut conflicts = Vec::new();
        while base_index < base_entries.len()
            || left_index < left_entries.len()
            || right_index < right_entries.len()
        {
            let key = minimum_merge_key(
                &base.policy,
                &base_entries,
                base_index,
                &left_entries,
                left_index,
                &right_entries,
                right_index,
            );
            let base_side = take_merge_side(&base.policy, &base_entries, &mut base_index, key);
            let left_side = take_merge_side(&base.policy, &left_entries, &mut left_index, key);
            let right_side = take_merge_side(&base.policy, &right_entries, &mut right_index, key);

            if merge_sides_equal(left_side, base_side, &values_equal) {
                append_merge_side(&mut output, right_side);
            } else if merge_sides_equal(right_side, base_side, &values_equal)
                || merge_sides_equal(left_side, right_side, &values_equal)
            {
                append_merge_side(&mut output, left_side);
            } else {
                let representative = left_side
                    .entry
                    .or(right_side.entry)
                    .or(base_side.entry)
                    .expect("at least one merge side supplies the selected key");
                let conflict = MerkleThreeWayMergeConflict {
                    key: representative.key_handle(),
                    base: base_side.merge_value(),
                    left: left_side.merge_value(),
                    right: right_side.merge_value(),
                };
                let resolution = resolver
                    .as_mut()
                    .map_or(MerkleMergeResolution::Unresolved, |resolver| {
                        resolver(&conflict)
                    });
                match resolution {
                    MerkleMergeResolution::Unresolved => conflicts.push(conflict),
                    MerkleMergeResolution::UseBase => {
                        append_merge_side(&mut output, base_side);
                    }
                    MerkleMergeResolution::UseLeft => {
                        append_merge_side(&mut output, left_side);
                    }
                    MerkleMergeResolution::UseRight => {
                        append_merge_side(&mut output, right_side);
                    }
                    MerkleMergeResolution::Delete => {}
                    MerkleMergeResolution::SetValue(value) => {
                        let value_bytes = base.policy.value_codec().encode(&value)?;
                        output.push(representative.replacing_value(value, value_bytes));
                    }
                }
            }
        }

        if !conflicts.is_empty() {
            return Ok(MerkleThreeWayMergeResult::conflicted(conflicts));
        }
        let root = base.build_canonical(&output)?;
        Ok(MerkleThreeWayMergeResult::success(
            Self::from_verified_root(root, base.policy.clone()),
        ))
    }
}

#[allow(clippy::too_many_arguments)]
fn minimum_merge_key<'a, K, V>(
    policy: &MerkleSearchTreePolicy<K, V>,
    base: &[&'a MerkleEntry<K, V>],
    base_index: usize,
    left: &[&'a MerkleEntry<K, V>],
    left_index: usize,
    right: &[&'a MerkleEntry<K, V>],
    right_index: usize,
) -> &'a K {
    let mut key = base.get(base_index).map(|entry| entry.key());
    if let Some(left) = left.get(left_index).map(|entry| entry.key())
        && key.is_none_or(|key| policy.compare(left, key) == Ordering::Less)
    {
        key = Some(left);
    }
    if let Some(right) = right.get(right_index).map(|entry| entry.key())
        && key.is_none_or(|key| policy.compare(right, key) == Ordering::Less)
    {
        key = Some(right);
    }
    key.expect("a nonempty merge frontier has a minimum key")
}

fn take_merge_side<'a, K, V>(
    policy: &MerkleSearchTreePolicy<K, V>,
    entries: &[&'a MerkleEntry<K, V>],
    index: &mut usize,
    key: &K,
) -> MergeSide<'a, K, V> {
    if *index >= entries.len() || !policy.compare(entries[*index].key(), key).is_eq() {
        MergeSide::absent()
    } else {
        let result = MergeSide::present(entries[*index]);
        *index += 1;
        result
    }
}

fn merge_sides_equal<K, V, F>(
    left: MergeSide<'_, K, V>,
    right: MergeSide<'_, K, V>,
    values_equal: &F,
) -> bool
where
    F: Fn(&V, &V) -> bool,
{
    match (left.entry, right.entry) {
        (None, None) => true,
        (Some(left), Some(right)) => values_equal(left.value(), right.value()),
        _ => false,
    }
}

fn append_merge_side<K, V>(output: &mut Vec<MerkleEntry<K, V>>, selected: MergeSide<'_, K, V>) {
    if let Some(entry) = selected.entry {
        output.push(entry.clone());
    }
}

fn map_tree_build_error(error: MerkleTreeError, digest: MerkleDigest) -> MerkleVerificationError {
    let kind = match error {
        MerkleTreeError::SizeOverflow => MerkleVerificationFailureKind::ResourceLimitExceeded,
        _ => MerkleVerificationFailureKind::NonCanonicalBlock,
    };
    verification(
        kind,
        format!("a decoded Merkle block could not be reconstructed: {error}"),
        Some(digest),
    )
}

fn preflight_store(
    blocks: &[MerkleBlock],
    store: &dyn MerkleBlockStore,
) -> Result<(), MerkleVerificationError> {
    for block in blocks {
        if let Some(existing) = store.get(block.digest())
            && existing != *block
        {
            return Err(verification(
                MerkleVerificationFailureKind::ConflictingBlock,
                format!(
                    "digest '{}' is already associated with different block bytes",
                    block.digest()
                ),
                Some(block.digest()),
            ));
        }
    }
    Ok(())
}

fn verify_envelope<K, V>(
    algorithm_id: &str,
    domain_digest: MerkleDigest,
    policy: &MerkleSearchTreePolicy<K, V>,
) -> Result<(), MerkleVerificationError> {
    if algorithm_id != MerkleSearchTreePolicy::<K, V>::ALGORITHM_ID {
        return Err(verification(
            MerkleVerificationFailureKind::UnsupportedAlgorithm,
            format!("Merkle algorithm '{algorithm_id}' is not supported"),
            None,
        ));
    }
    if domain_digest != policy.domain_digest() {
        return Err(verification(
            MerkleVerificationFailureKind::DomainMismatch,
            "the Merkle envelope belongs to another policy domain",
            None,
        ));
    }
    Ok(())
}

fn take<'a>(
    source: &'a [u8],
    offset: &mut usize,
    length: usize,
    digest: Option<MerkleDigest>,
) -> Result<&'a [u8], MerkleVerificationError> {
    let end = offset.checked_add(length).ok_or_else(|| {
        verification(
            MerkleVerificationFailureKind::MalformedBlock,
            "a serialized Merkle field offset overflowed",
            digest,
        )
    })?;
    let result = source.get(*offset..end).ok_or_else(|| {
        verification(
            MerkleVerificationFailureKind::MalformedBlock,
            "a serialized Merkle field is truncated",
            digest,
        )
    })?;
    *offset = end;
    Ok(result)
}

fn read_i32(
    source: &[u8],
    offset: &mut usize,
    digest: Option<MerkleDigest>,
) -> Result<i32, MerkleVerificationError> {
    let bytes: [u8; 4] = take(source, offset, 4, digest)?
        .try_into()
        .expect("a four-byte slice always converts");
    Ok(i32::from_be_bytes(bytes))
}

fn read_positive_i32(
    source: &[u8],
    offset: &mut usize,
    digest: MerkleDigest,
    field: &str,
) -> Result<usize, MerkleVerificationError> {
    let value = read_i32(source, offset, Some(digest))?;
    if value <= 0 {
        return Err(verification(
            MerkleVerificationFailureKind::MalformedBlock,
            format!("a Merkle block has an invalid {field} count"),
            Some(digest),
        ));
    }
    Ok(value as usize)
}

fn read_length_prefixed(
    source: &[u8],
    offset: &mut usize,
    digest: MerkleDigest,
) -> Result<Vec<u8>, MerkleVerificationError> {
    let length = read_i32(source, offset, Some(digest))?;
    if length < 0 {
        return Err(verification(
            MerkleVerificationFailureKind::MalformedBlock,
            "a length-prefixed Merkle field has a negative length",
            Some(digest),
        ));
    }
    Ok(take(source, offset, length as usize, Some(digest))?.to_vec())
}

fn verification(
    kind: MerkleVerificationFailureKind,
    message: impl Into<String>,
    digest: Option<MerkleDigest>,
) -> MerkleVerificationError {
    MerkleVerificationError::new(kind, message, digest)
}

fn size_limit(message: impl Into<String>, digest: MerkleDigest) -> MerkleVerificationError {
    verification(
        MerkleVerificationFailureKind::ResourceLimitExceeded,
        message,
        Some(digest),
    )
}

fn u64_len(length: usize) -> Result<u64, MerkleVerificationError> {
    u64::try_from(length).map_err(|_| {
        verification(
            MerkleVerificationFailureKind::ResourceLimitExceeded,
            "a serialized Merkle length exceeds 64-bit accounting",
            None,
        )
    })
}
