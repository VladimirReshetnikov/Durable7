//! Fully branching persistent insertion-only connectivity forest with a first-connection query.
//!
//! [`PersistentAncestralConnectionForest`] is a persistent union-find over the fixed integer vertex
//! universe `0 .. vertex_count`. Each successful [`PersistentAncestralConnectionForest::link`]
//! performs union by size *without* path compression and labels the one new root-parent edge with
//! the child [`AncestralConnectionVersion`]. A redundant (already connected) link still creates a
//! child history version, but shares the complete connectivity index.
//!
//! The parent cells live sparsely in a [`PersistentHashMap`](crate::PersistentHashMap). An absent
//! cell denotes a singleton root, so [`PersistentAncestralConnectionForest::new`] is O(1) rather
//! than O(`vertex_count`). Union by size limits every parent path to O(log n) cells.
//! [`PersistentAncestralConnectionForest::first_connected`] compares the two current parent paths
//! and takes the latest edge-version below their forest lowest common ancestor, requiring
//! O(w log n) time, where w is the CHAMP path cost defined below. It does *not* search the version
//! history, so its cost is independent of history depth.
//!
//! # Cost of the CHAMP path
//!
//! Throughout this module w denotes the cost of one [`PersistentHashMap`](crate::PersistentHashMap)
//! cell lookup or insertion, and it is bounded **unconditionally**, matching the managed reference:
//! the cell map is pinned to [`VertexHashState`], whose 32-bit output is a MurmurHash3 `fmix32`
//! bijection of the vertex. Every step of that finalizer — two xor-shifts by at least half the
//! width around multiplications by odd constants — is invertible modulo 2^32, so distinct `i32`
//! vertices can never share a hash, no collision bucket ever holds two distinct vertices, and any
//! two vertices diverge within ceil(32/5) = 7 trie levels. w is therefore O(1) worst-case and
//! every per-operation bound stated here is worst-case in both factors. Pinning is what earns the
//! claim: the standard library's `RandomState`, which this map previously retained, genuinely
//! collides after 32-bit truncation and could only support an expected bound.
//!
//! Instances and version tokens are immutable snapshots safe for concurrent readers. Deletion,
//! confluent merging, retroactive updates, and path compression are deliberately outside this
//! type's contract. The O(log n) parent-path factor above is worst-case logarithmic in component
//! size rather than the inverse-Ackermann amortized bound of an ephemeral linear-history
//! union-find.
//!
//! [`PersistentAncestralConnectionForest::validate_structure`] audits sparse-cell canonicality,
//! current root sizes, the resulting union-by-size height ceiling, union-tag ancestry and order,
//! and the cached component count.

use crate::PersistentHashMap;
use std::collections::{HashMap, HashSet};
use std::error::Error;
use std::fmt;
use std::hash::{BuildHasher, Hash, Hasher};
use std::sync::Arc;

/// A recoverable failure reported by a connection-forest operation.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AncestralConnectionError {
    /// A forest was requested over a negative vertex universe.
    NegativeVertexCount,
    /// A vertex argument lies outside the forest's fixed universe `0 .. vertex_count`.
    VertexOutOfRange,
    /// The history depth would exceed [`u64::MAX`].
    HistoryDepthOverflow,
}

impl fmt::Display for AncestralConnectionError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::NegativeVertexCount => "vertex count cannot be negative",
            Self::VertexOutOfRange => "vertex must lie within the forest's fixed universe",
            Self::HistoryDepthOverflow => "the connection-forest history depth would overflow",
        })
    }
}

impl Error for AncestralConnectionError {}

/// Statistics returned by a complete connection-forest representation audit.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct AncestralConnectionForestStatistics {
    /// The fixed vertex-universe size.
    pub vertex_count: i32,
    /// The number of connected components, recounted from the parent forest.
    pub component_count: i32,
    /// The number of explicitly stored parent cells; absent cells are singleton roots.
    pub stored_cell_count: usize,
    /// The longest parent path found, in edges. Bounded by `floor(log2(vertex_count))`.
    pub maximum_parent_path_len: usize,
    /// The number of link events from the history root to the validated version.
    pub history_depth: u64,
}

/// A structural invariant violation found by
/// [`PersistentAncestralConnectionForest::validate_structure`].
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AncestralConnectionForestInvariantError {
    /// The cached vertex or component counts are not in their permitted range.
    InvalidCounts,
    /// The history-token parent chain has inconsistent depths or root identities.
    InconsistentVersionChain,
    /// The history root has a parent, a nonzero depth, or is not on its own lineage.
    InconsistentHistoryRoot,
    /// A sparse parent cell addresses a vertex outside the universe.
    VertexOutsideUniverse,
    /// A stored root cell is not a canonical non-singleton root.
    NonCanonicalRootCell,
    /// A non-root cell has an invalid size, an absent parent, or an invalid union-version tag.
    InvalidChildCell,
    /// Union-edge versions do not strictly increase toward the root.
    TagsNotStrictlyIncreasing,
    /// A parent path exceeds the union-by-size height bound.
    HeightBoundExceeded,
    /// The cached component count disagrees with the parent forest.
    ComponentCountMismatch,
    /// A root's cached union size is incorrect.
    RootSizeMismatch,
}

impl fmt::Display for AncestralConnectionForestInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InvalidCounts => "connection-forest counts are invalid",
            Self::InconsistentVersionChain => "the history-token parent chain is inconsistent",
            Self::InconsistentHistoryRoot => "the history root is inconsistent",
            Self::VertexOutsideUniverse => {
                "a sparse parent cell addresses a vertex outside the universe"
            }
            Self::NonCanonicalRootCell => {
                "a stored root cell is not a canonical non-singleton root"
            }
            Self::InvalidChildCell => "a non-root cell has an invalid size or union-version tag",
            Self::TagsNotStrictlyIncreasing => {
                "union-edge versions do not strictly increase toward the root"
            }
            Self::HeightBoundExceeded => "a parent path exceeds the union-by-size height bound",
            Self::ComponentCountMismatch => {
                "the cached component count disagrees with the parent forest"
            }
            Self::RootSizeMismatch => "a root's cached union size is incorrect",
        })
    }
}

impl Error for AncestralConnectionForestInvariantError {}

/// Identifies one immutable version in a [`PersistentAncestralConnectionForest`] history tree.
///
/// Tokens use *identity*, not structural equality: [`PartialEq`] and [`Hash`] compare the retained
/// [`Arc`] pointer, so two versions at equal depths in sibling branches are never equal. This is
/// the Rust equivalent of the reference identity the C# contract relies on. Cloning a token is
/// O(1) and yields the same identity. The parent chain contains history identities, not mutable
/// connectivity state.
pub struct AncestralConnectionVersion(Arc<VersionNode>);

struct VersionNode {
    parent: Option<AncestralConnectionVersion>,
    depth: u64,
    /// The history root, or `None` when this node *is* the root. Storing the root as an `Option`
    /// rather than as a self-reference keeps [`Self::root`] O(1) without an `Arc` cycle.
    root: Option<AncestralConnectionVersion>,
}

impl Drop for VersionNode {
    /// Releases the parent chain iteratively.
    ///
    /// A history is a linked list that can be hundreds of thousands of links deep, so the derived
    /// recursive drop glue would overflow the stack. Each unlinked node's own drop then has nothing
    /// left to follow, making the release O(1) per node.
    fn drop(&mut self) {
        self.root = None;
        let mut current = self.parent.take();
        while let Some(version) = current {
            let Ok(mut node) = Arc::try_unwrap(version.0) else {
                break;
            };
            current = node.parent.take();
        }
    }
}

impl AncestralConnectionVersion {
    fn new_root() -> Self {
        Self(Arc::new(VersionNode {
            parent: None,
            depth: 0,
            root: None,
        }))
    }

    fn new_child(parent: &Self) -> Result<Self, AncestralConnectionError> {
        let depth = parent
            .depth()
            .checked_add(1)
            .ok_or(AncestralConnectionError::HistoryDepthOverflow)?;
        Ok(Self(Arc::new(VersionNode {
            parent: Some(parent.clone()),
            depth,
            root: Some(parent.root().clone()),
        })))
    }

    /// Borrows this version's parent, or `None` at the history root. O(1).
    #[must_use]
    pub fn parent(&self) -> Option<&Self> {
        self.0.parent.as_ref()
    }

    /// Returns the number of link events from the history root. O(1).
    #[must_use]
    pub fn depth(&self) -> u64 {
        self.0.depth
    }

    /// Borrows the root identity of this version tree. O(1).
    #[must_use]
    pub fn root(&self) -> &Self {
        self.0.root.as_ref().unwrap_or(self)
    }

    /// Reports whether two handles denote the very same version. O(1).
    #[must_use]
    pub fn is_same_version(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

impl Clone for AncestralConnectionVersion {
    fn clone(&self) -> Self {
        Self(Arc::clone(&self.0))
    }
}

impl PartialEq for AncestralConnectionVersion {
    fn eq(&self, other: &Self) -> bool {
        self.is_same_version(other)
    }
}

impl Eq for AncestralConnectionVersion {}

impl Hash for AncestralConnectionVersion {
    fn hash<H: Hasher>(&self, state: &mut H) {
        Arc::as_ptr(&self.0).hash(state);
    }
}

impl fmt::Debug for AncestralConnectionVersion {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("AncestralConnectionVersion")
            .field("depth", &self.depth())
            .finish()
    }
}

/// One sparse parent record.
///
/// `size` is meaningful only for a root (`parent` equal to the vertex itself), and `joined_at` is
/// present only on a non-root edge. An absent cell canonically means "singleton root of size 1".
#[derive(Clone, Debug, PartialEq, Eq)]
struct Cell {
    parent: i32,
    size: i32,
    joined_at: Option<AncestralConnectionVersion>,
}

impl Cell {
    fn root(vertex: i32, size: i32) -> Self {
        Self {
            parent: vertex,
            size,
            joined_at: None,
        }
    }

    fn child(parent: i32, joined_at: AncestralConnectionVersion) -> Self {
        Self {
            parent,
            size: 0,
            joined_at: Some(joined_at),
        }
    }
}

struct PathStep<'a> {
    vertex: i32,
    joined_at: Option<&'a AncestralConnectionVersion>,
}

/// A fully branching persistent insertion-only connectivity forest that can report the first
/// ancestor version in which two vertices became connected.

/// The pinned injective vertex-hash policy behind the forest's cell map.
///
/// `fmix32` (MurmurHash3's finalizer) composes xor-shifts by at least half the word with
/// multiplications by odd constants; each step is a bijection of the 32-bit word, so the whole is
/// injective over every `i32` vertex. That injectivity is what upgrades the CHAMP factor from
/// expected to worst-case: no collision bucket can ever hold two distinct vertices. The same
/// finalizer is pinned by the C++ and Python ports. Only injectivity is promised — the value may
/// differ across platforms — and only 32-bit lanes are supported, which covers the one key type
/// the forest stores.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct VertexHashState;

impl BuildHasher for VertexHashState {
    type Hasher = VertexHasher;

    fn build_hasher(&self) -> VertexHasher {
        VertexHasher(0)
    }
}

/// The single-lane hasher produced by [`VertexHashState`].
#[derive(Clone, Copy, Debug)]
pub struct VertexHasher(u64);

fn fmix32(mut value: u32) -> u32 {
    value ^= value >> 16;
    value = value.wrapping_mul(0x85EB_CA6B);
    value ^= value >> 13;
    value = value.wrapping_mul(0xC2B2_AE35);
    value ^ (value >> 16)
}

impl Hasher for VertexHasher {
    fn finish(&self) -> u64 {
        self.0
    }

    fn write(&mut self, bytes: &[u8]) {
        // Fallback for the byte-slice route some `Hash` impls take: fold at most one 32-bit lane.
        // Reading native-endian bytes as little-endian is itself a bijection of the lane, so
        // injectivity does not depend on endianness.
        let mut lane = [0u8; 4];
        for (index, byte) in bytes.iter().take(4).enumerate() {
            lane[index] = *byte;
        }
        self.write_u32(u32::from_le_bytes(lane));
    }

    fn write_u32(&mut self, value: u32) {
        self.0 = u64::from(fmix32(value));
    }

    fn write_i32(&mut self, value: i32) {
        self.write_u32(value as u32);
    }
}

///
/// Cloning is O(1) and yields the same version; a clone and its source are indistinguishable.
#[derive(Clone)]
pub struct PersistentAncestralConnectionForest {
    vertex_count: i32,
    component_count: i32,
    cells: PersistentHashMap<i32, Cell, VertexHashState>,
    version: AncestralConnectionVersion,
}

impl PersistentAncestralConnectionForest {
    /// Creates an edgeless root version over vertices `0 .. vertex_count`. O(1).
    ///
    /// Every vertex starts as its own component. Because a singleton is represented by the *absence*
    /// of a cell, construction costs the same for a universe of two vertices and for one of
    /// [`i32::MAX`].
    ///
    /// Fails with [`AncestralConnectionError::NegativeVertexCount`] when `vertex_count` is negative.
    pub fn new(vertex_count: i32) -> Result<Self, AncestralConnectionError> {
        if vertex_count < 0 {
            return Err(AncestralConnectionError::NegativeVertexCount);
        }

        Ok(Self {
            vertex_count,
            component_count: vertex_count,
            cells: PersistentHashMap::with_hasher(VertexHashState),
            version: AncestralConnectionVersion::new_root(),
        })
    }

    /// Returns the fixed number of vertices in this forest. O(1).
    #[must_use]
    pub fn vertex_count(&self) -> i32 {
        self.vertex_count
    }

    /// Returns the number of connected components in this version. O(1).
    #[must_use]
    pub fn component_count(&self) -> i32 {
        self.component_count
    }

    /// Borrows the identity token for this history version. O(1).
    #[must_use]
    pub fn version(&self) -> &AncestralConnectionVersion {
        &self.version
    }

    /// Returns the current union-find representative of `vertex`. O(w log n), worst-case in the
    /// O(log n) path factor and in the CHAMP factor w alike (see the module-level note).
    ///
    /// The representative is selected by deterministic union-by-size history and is an
    /// implementation artifact: sibling versions need not agree on it.
    pub fn find(&self, vertex: i32) -> Result<i32, AncestralConnectionError> {
        self.check_vertex(vertex)?;
        Ok(self.find_root(vertex))
    }

    /// Reports whether two vertices are connected, that is, whether they have the same current
    /// root. O(w log n), worst-case in the O(log n) path factor and in the CHAMP factor w alike
    /// (see the module-level note).
    pub fn connected(&self, left: i32, right: i32) -> Result<bool, AncestralConnectionError> {
        self.check_vertices(left, right)?;
        Ok(self.find_root(left) == self.find_root(right))
    }

    /// Returns the number of vertices in the connected component containing `vertex`. O(w log n),
    /// worst-case in the O(log n) path factor and in the CHAMP factor w alike (see the
    /// module-level note).
    ///
    /// An isolated vertex is still a singleton root and reports 1. The answer is the union-by-size
    /// count cached at the component's current root, so the walk reads the structure without
    /// compressing it; this forest deliberately has no path compression.
    pub fn component_size(&self, vertex: i32) -> Result<i32, AncestralConnectionError> {
        self.check_vertex(vertex)?;
        Ok(self.size_of(self.find_root(vertex)))
    }

    /// Creates a child history version containing an undirected connection between two vertices.
    ///
    /// The returned version is always distinct. When the endpoints were already connected its
    /// connectivity cells are shared unchanged, which
    /// [`Self::shares_connectivity_root_with`] can confirm; otherwise exactly one union-by-size
    /// root edge is added and labelled with the child version. On a size tie the first endpoint's
    /// root stays the representative.
    ///
    /// O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1)
    /// space when redundant. The O(log n) path factor and the CHAMP factor w are both worst-case
    /// rather than worst-case (see the module-level note). The receiver is left untouched,
    /// including on failure.
    pub fn link(&self, left: i32, right: i32) -> Result<Self, AncestralConnectionError> {
        self.check_vertices(left, right)?;
        let mut left_root = self.find_root(left);
        let mut right_root = self.find_root(right);
        let child_version = AncestralConnectionVersion::new_child(&self.version)?;

        if left_root == right_root {
            return Ok(Self {
                vertex_count: self.vertex_count,
                component_count: self.component_count,
                cells: self.cells.clone(),
                version: child_version,
            });
        }

        let mut left_size = self.size_of(left_root);
        let mut right_size = self.size_of(right_root);
        if left_size < right_size {
            std::mem::swap(&mut left_root, &mut right_root);
            std::mem::swap(&mut left_size, &mut right_size);
        }

        // On a size tie the first endpoint's root remains the representative, making the result
        // deterministic without changing the logarithmic-height proof.
        let combined_size = left_size
            .checked_add(right_size)
            .expect("component sizes are bounded by the vertex universe");
        let cells = self
            .cells
            .insert(left_root, Cell::root(left_root, combined_size))
            .insert(right_root, Cell::child(left_root, child_version.clone()));
        Ok(Self {
            vertex_count: self.vertex_count,
            component_count: self.component_count - 1,
            cells,
            version: child_version,
        })
    }

    /// Returns the earliest version on this version's root-to-current history in which two vertices
    /// were connected, or `None` when they are disconnected in this version.
    ///
    /// A vertex is connected to itself at the history root. The answer is computed from the two
    /// current parent paths as the latest union-edge version strictly below their forest lowest
    /// common ancestor; the version history is never searched. O(w log n) time — worst-case in the
    /// O(log n) path factor and in the CHAMP factor w alike (see the module-level note) — and
    /// O(log n) worst-case temporary path space, independent of history depth.
    ///
    /// The result is never a version on another branch, and a later merge of the pair's component
    /// into a bigger one never replaces the pair's earlier witness.
    pub fn first_connected(
        &self,
        left: i32,
        right: i32,
    ) -> Result<Option<AncestralConnectionVersion>, AncestralConnectionError> {
        self.check_vertices(left, right)?;
        if left == right {
            return Ok(Some(self.version.root().clone()));
        }

        let left_path = self.build_path(left);
        let right_path = self.build_path(right);
        let left_root = left_path
            .last()
            .expect("a parent path is never empty")
            .vertex;
        let right_root = right_path
            .last()
            .expect("a parent path is never empty")
            .vertex;
        if left_root != right_root {
            return Ok(None);
        }

        // Delete the common rootward suffix. The remaining steps are precisely the two paths
        // strictly below the forest LCA, and every one of those steps owns a successful-union tag.
        let mut left_len = left_path.len();
        let mut right_len = right_path.len();
        while left_len > 0
            && right_len > 0
            && left_path[left_len - 1].vertex == right_path[right_len - 1].vertex
        {
            left_len -= 1;
            right_len -= 1;
        }

        let mut latest: Option<&AncestralConnectionVersion> = None;
        for step in &left_path[..left_len] {
            latest = later(latest, step.joined_at);
        }
        for step in &right_path[..right_len] {
            latest = later(latest, step.joined_at);
        }

        // Distinct connected vertices have at least one edge strictly below their LCA.
        Ok(Some(
            latest
                .expect("a connected pair has a union-edge witness")
                .clone(),
        ))
    }

    /// Reports whether two versions retain the exact same sparse connectivity index, so neither can
    /// observe a union recorded in the other. A representation test, not an equality test. O(1).
    #[must_use]
    pub fn shares_connectivity_root_with(&self, other: &Self) -> bool {
        self.cells.shares_root_with(&other.cells)
    }

    /// Audits sparse-cell canonicality, current root sizes, the resulting union-by-size height
    /// ceiling, union-tag ancestry and order, and the cached component count.
    ///
    /// O(H + m w log n), where H is the history depth, m is the explicit-cell count, and w is the
    /// CHAMP path cost, which is bounded in expectation rather than unconditionally (see the
    /// module-level note). A defensive audit; ordinary operations maintain these invariants.
    pub fn validate_structure(
        &self,
    ) -> Result<AncestralConnectionForestStatistics, AncestralConnectionForestInvariantError> {
        use AncestralConnectionForestInvariantError as Violation;

        if self.vertex_count < 0
            || self.component_count < 0
            || self.component_count > self.vertex_count
        {
            return Err(Violation::InvalidCounts);
        }

        let mut ancestors = HashSet::new();
        let mut expected_depth = Some(self.version.depth());
        let mut current = Some(&self.version);
        while let Some(version) = current {
            let Some(depth) = expected_depth else {
                return Err(Violation::InconsistentVersionChain);
            };
            if version.depth() != depth || !version.root().is_same_version(self.version.root()) {
                return Err(Violation::InconsistentVersionChain);
            }
            expected_depth = depth.checked_sub(1);
            ancestors.insert(version.clone());
            current = version.parent();
        }
        let history_root = self.version.root();
        if expected_depth.is_some()
            || !ancestors.contains(history_root)
            || history_root.parent().is_some()
            || history_root.depth() != 0
        {
            return Err(Violation::InconsistentHistoryRoot);
        }

        for (&vertex, cell) in &self.cells {
            if !self.is_vertex(vertex) || !self.is_vertex(cell.parent) {
                return Err(Violation::VertexOutsideUniverse);
            }

            if cell.parent == vertex {
                if cell.size <= 1 || cell.joined_at.is_some() {
                    return Err(Violation::NonCanonicalRootCell);
                }
            } else {
                let tagged = cell
                    .joined_at
                    .as_ref()
                    .is_some_and(|joined_at| ancestors.contains(joined_at));
                if cell.size != 0 || !tagged || !self.cells.contains_key(&cell.parent) {
                    return Err(Violation::InvalidChildCell);
                }
            }
        }

        let mut component_sizes: HashMap<i32, i32> = HashMap::new();
        let mut maximum_parent_path_len = 0usize;
        let maximum_height = if self.vertex_count <= 1 {
            0
        } else {
            (self.vertex_count as u32).ilog2() as usize
        };
        for (&vertex, _) in &self.cells {
            let mut current = vertex;
            let mut height = 0usize;
            let mut previous_depth: Option<u64> = None;
            while let Some(cell) = self.cells.get(&current) {
                if cell.parent == current {
                    break;
                }
                let Some(joined_at) = cell.joined_at.as_ref() else {
                    return Err(Violation::TagsNotStrictlyIncreasing);
                };
                if previous_depth.is_some_and(|previous| joined_at.depth() <= previous)
                    || !ancestors.contains(joined_at)
                {
                    return Err(Violation::TagsNotStrictlyIncreasing);
                }

                previous_depth = Some(joined_at.depth());
                current = cell.parent;
                height += 1;
                if height > maximum_height {
                    return Err(Violation::HeightBoundExceeded);
                }
            }

            maximum_parent_path_len = maximum_parent_path_len.max(height);
            *component_sizes.entry(current).or_insert(0) += 1;
        }

        let stored_cell_count = self.cells.len();
        let absent_singletons = i64::from(self.vertex_count) - stored_cell_count as i64;
        if absent_singletons < 0
            || absent_singletons + component_sizes.len() as i64 != i64::from(self.component_count)
        {
            return Err(Violation::ComponentCountMismatch);
        }
        for (&root, &size) in &component_sizes {
            if self.size_of(root) != size {
                return Err(Violation::RootSizeMismatch);
            }
        }

        Ok(AncestralConnectionForestStatistics {
            vertex_count: self.vertex_count,
            component_count: self.component_count,
            stored_cell_count,
            maximum_parent_path_len,
            history_depth: self.version.depth(),
        })
    }

    fn find_root(&self, vertex: i32) -> i32 {
        let mut current = vertex;
        loop {
            let parent = self.parent_of(current);
            if parent == current {
                return current;
            }
            current = parent;
        }
    }

    fn build_path(&self, vertex: i32) -> Vec<PathStep<'_>> {
        let mut path = Vec::new();
        let mut current = vertex;
        loop {
            let cell = self.cells.get(&current);
            let parent = cell.map_or(current, |cell| cell.parent);
            path.push(PathStep {
                vertex: current,
                joined_at: cell.and_then(|cell| cell.joined_at.as_ref()),
            });
            if parent == current {
                return path;
            }
            current = parent;
        }
    }

    /// Reads the parent of `vertex`; an absent cell is canonically its own singleton root.
    fn parent_of(&self, vertex: i32) -> i32 {
        self.cells.get(&vertex).map_or(vertex, |cell| cell.parent)
    }

    /// Reads the component size cached at the root `vertex`; an absent cell is a singleton.
    fn size_of(&self, vertex: i32) -> i32 {
        self.cells.get(&vertex).map_or(1, |cell| cell.size)
    }

    fn is_vertex(&self, vertex: i32) -> bool {
        vertex >= 0 && vertex < self.vertex_count
    }

    fn check_vertex(&self, vertex: i32) -> Result<(), AncestralConnectionError> {
        if self.is_vertex(vertex) {
            Ok(())
        } else {
            Err(AncestralConnectionError::VertexOutOfRange)
        }
    }

    fn check_vertices(&self, left: i32, right: i32) -> Result<(), AncestralConnectionError> {
        self.check_vertex(left)?;
        self.check_vertex(right)
    }
}

fn later<'a>(
    left: Option<&'a AncestralConnectionVersion>,
    right: Option<&'a AncestralConnectionVersion>,
) -> Option<&'a AncestralConnectionVersion> {
    match (left, right) {
        (_, None) => left,
        (None, Some(_)) => right,
        (Some(current), Some(candidate)) => {
            if candidate.depth() > current.depth() {
                right
            } else {
                left
            }
        }
    }
}

impl fmt::Debug for PersistentAncestralConnectionForest {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PersistentAncestralConnectionForest")
            .field("vertex_count", &self.vertex_count)
            .field("component_count", &self.component_count)
            .field("depth", &self.version.depth())
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    type Forest = PersistentAncestralConnectionForest;

    fn forest(vertex_count: i32) -> Forest {
        Forest::new(vertex_count).expect("a nonnegative universe is accepted")
    }

    fn first_connected(
        forest: &Forest,
        left: i32,
        right: i32,
    ) -> Option<AncestralConnectionVersion> {
        forest
            .first_connected(left, right)
            .expect("valid vertices are accepted")
    }


    #[test]
    fn the_pinned_vertex_hash_is_injective_by_inverse_round_trip() {
        // Invert fmix32 step by step: an xor-shift by >= 16 is self-inverse, the 13-bit xor-shift
        // inverts by applying its expansion twice, and each odd multiplier inverts by its
        // Newton-iterated modular inverse. A successful round trip proves injectivity on the
        // tested values by exhibiting the inverse, which no sampling-only test can.
        fn modular_inverse(value: u32) -> u32 {
            let mut inverse = value;
            for _ in 0..5 {
                inverse = inverse.wrapping_mul(2u32.wrapping_sub(value.wrapping_mul(inverse)));
            }
            inverse
        }
        let first_inverse = modular_inverse(0x85EB_CA6B);
        let second_inverse = modular_inverse(0xC2B2_AE35);
        assert_eq!(first_inverse.wrapping_mul(0x85EB_CA6B), 1);
        assert_eq!(second_inverse.wrapping_mul(0xC2B2_AE35), 1);

        let unmix = |mut value: u32| -> u32 {
            value ^= value >> 16;
            value = value.wrapping_mul(second_inverse);
            value ^= (value >> 13) ^ (value >> 26);
            value = value.wrapping_mul(first_inverse);
            value ^ (value >> 16)
        };

        let hash_of = |vertex: i32| -> u32 {
            let mut hasher = VertexHashState.build_hasher();
            vertex.hash(&mut hasher);
            hasher.finish() as u32
        };

        let samples = (0i32..8_192)
            .map(|index| index.wrapping_mul(524_287))
            .chain([i32::MIN, -1, 0, 1, i32::MAX]);
        for vertex in samples {
            let hashed = hash_of(vertex);
            assert_eq!(unmix(hashed) as i32, vertex, "fmix32 must round-trip {vertex}");
        }
    }
    #[test]
    fn new_produces_singleton_components_and_root_history() {
        let root = forest(5);

        assert_eq!(root.vertex_count(), 5);
        assert_eq!(root.component_count(), 5);
        assert_eq!(root.version().depth(), 0);
        assert!(root.version().parent().is_none());
        assert!(root.version().is_same_version(root.version().root()));
        for vertex in 0..root.vertex_count() {
            assert_eq!(root.find(vertex).unwrap(), vertex);
            assert!(root.connected(vertex, vertex).unwrap());
            assert_eq!(
                first_connected(&root, vertex, vertex).as_ref(),
                Some(root.version())
            );
        }

        assert!(!root.connected(0, 1).unwrap());
        assert_eq!(first_connected(&root, 0, 1), None);
        let statistics = root.validate_structure().unwrap();
        assert_eq!(statistics.stored_cell_count, 0);
        assert_eq!(statistics.component_count, 5);
        assert_eq!(statistics.maximum_parent_path_len, 0);

        let empty = forest(0);
        assert_eq!(empty.vertex_count(), 0);
        assert_eq!(empty.component_count(), 0);
        empty.validate_structure().unwrap();
        assert_eq!(
            Forest::new(-1).err(),
            Some(AncestralConnectionError::NegativeVertexCount)
        );
    }

    #[test]
    fn link_records_the_first_connection_version() {
        let root = forest(6);
        let first = root.link(0, 1).unwrap();
        let second = first.link(2, 3).unwrap();
        let bridge = second.link(0, 2).unwrap();
        let attach = bridge.link(4, 0).unwrap();

        assert_eq!(attach.component_count(), 2);
        assert_eq!(
            first_connected(&attach, 0, 1).as_ref(),
            Some(first.version())
        );
        assert_eq!(
            first_connected(&attach, 2, 3).as_ref(),
            Some(second.version())
        );
        assert_eq!(
            first_connected(&attach, 0, 3).as_ref(),
            Some(bridge.version())
        );
        assert_eq!(
            first_connected(&attach, 1, 2).as_ref(),
            Some(bridge.version())
        );
        assert_eq!(
            first_connected(&attach, 4, 1).as_ref(),
            Some(attach.version())
        );
        assert_eq!(
            first_connected(&attach, 4, 4).as_ref(),
            Some(root.version())
        );
        assert_eq!(first_connected(&attach, 0, 5), None);

        assert_eq!(attach.find(0).unwrap(), 0);
        assert_eq!(attach.find(4).unwrap(), 0);

        // Every retained version keeps its own connectivity, untouched by later descendants.
        assert_eq!(root.component_count(), 6);
        assert!(!root.connected(0, 1).unwrap());
        assert!(!first.connected(0, 2).unwrap());
        assert_eq!(first.component_count(), 5);
        for version in [&root, &first, &second, &bridge, &attach] {
            version.validate_structure().unwrap();
        }
    }

    #[test]
    fn redundant_links_create_versions_and_share_connectivity_state() {
        let root = forest(4);
        let linked = root.link(0, 1).unwrap();
        let reverse_duplicate = linked.link(1, 0).unwrap();
        let sibling_duplicate = linked.link(1, 0).unwrap();
        let self_duplicate = reverse_duplicate.link(0, 0).unwrap();

        assert_eq!(linked.version().depth(), 1);
        assert_eq!(reverse_duplicate.version().depth(), 2);
        assert_eq!(self_duplicate.version().depth(), 3);
        assert_eq!(reverse_duplicate.version().parent(), Some(linked.version()));
        assert_eq!(sibling_duplicate.version().parent(), Some(linked.version()));
        assert_eq!(
            self_duplicate.version().parent(),
            Some(reverse_duplicate.version())
        );
        assert_ne!(linked.version(), reverse_duplicate.version());
        assert_ne!(reverse_duplicate.version(), sibling_duplicate.version());
        assert_eq!(
            linked.component_count(),
            reverse_duplicate.component_count()
        );
        assert_eq!(linked.component_count(), self_duplicate.component_count());
        assert!(linked.shares_connectivity_root_with(&reverse_duplicate));
        assert!(reverse_duplicate.shares_connectivity_root_with(&self_duplicate));
        assert!(!root.shares_connectivity_root_with(&linked));
        assert_eq!(
            first_connected(&self_duplicate, 0, 1).as_ref(),
            Some(linked.version())
        );
        assert_eq!(
            first_connected(&self_duplicate, 2, 2).as_ref(),
            Some(root.version())
        );
        self_duplicate.validate_structure().unwrap();
    }

    #[test]
    fn sibling_branches_keep_equal_depth_version_tags_distinct() {
        let root = forest(5);
        let common = root.link(0, 1).unwrap();
        let left = common.link(0, 2).unwrap();
        let right = common.link(0, 3).unwrap();
        let left_later = left.link(2, 4).unwrap();
        let right_later = right.link(3, 4).unwrap();

        assert_eq!(left.version().depth(), right.version().depth());
        assert_ne!(left.version(), right.version());
        assert_eq!(
            first_connected(&left, 0, 1).as_ref(),
            Some(common.version())
        );
        assert_eq!(
            first_connected(&right, 0, 1).as_ref(),
            Some(common.version())
        );
        assert_eq!(
            first_connected(&left_later, 1, 2).as_ref(),
            Some(left.version())
        );
        assert_eq!(
            first_connected(&right_later, 1, 3).as_ref(),
            Some(right.version())
        );
        assert_eq!(
            first_connected(&left_later, 0, 4).as_ref(),
            Some(left_later.version())
        );
        assert_eq!(
            first_connected(&right_later, 0, 4).as_ref(),
            Some(right_later.version())
        );
        assert!(!left_later.connected(0, 3).unwrap());
        assert!(!right_later.connected(0, 2).unwrap());
        assert!(left_later.version().root().is_same_version(root.version()));
        assert!(right_later.version().root().is_same_version(root.version()));
        left_later.validate_structure().unwrap();
        right_later.validate_structure().unwrap();
    }

    #[test]
    fn first_connected_uses_pair_lca_rather_than_latest_component_birth() {
        let root = forest(8);
        let ab = root.link(0, 1).unwrap();
        let cd = ab.link(2, 3).unwrap();
        let abcd = cd.link(1, 3).unwrap();
        let ef = abcd.link(4, 5).unwrap();
        let gh = ef.link(6, 7).unwrap();
        let efgh = gh.link(4, 6).unwrap();
        let all = efgh.link(0, 4).unwrap();

        assert_eq!(first_connected(&all, 0, 1).as_ref(), Some(ab.version()));
        assert_eq!(first_connected(&all, 2, 3).as_ref(), Some(cd.version()));
        assert_eq!(first_connected(&all, 0, 2).as_ref(), Some(abcd.version()));
        assert_eq!(first_connected(&all, 4, 5).as_ref(), Some(ef.version()));
        assert_eq!(first_connected(&all, 6, 7).as_ref(), Some(gh.version()));
        assert_eq!(first_connected(&all, 5, 7).as_ref(), Some(efgh.version()));
        assert_eq!(first_connected(&all, 1, 6).as_ref(), Some(all.version()));
        all.validate_structure().unwrap();
    }

    #[test]
    fn first_connected_handles_one_endpoint_at_lca_after_that_lca_becomes_a_child() {
        let root = forest(5);
        let pair = root.link(0, 1).unwrap(); // 1 -> 0
        let other_pair = pair.link(2, 3).unwrap(); // 3 -> 2
        let larger = other_pair.link(2, 4).unwrap();
        let merged = larger.link(2, 0).unwrap(); // the component rooted at 0 becomes a child of 2

        assert_eq!(merged.find(0).unwrap(), 2);
        assert_eq!(
            first_connected(&merged, 0, 1).as_ref(),
            Some(pair.version())
        );
        assert_eq!(
            first_connected(&merged, 0, 2).as_ref(),
            Some(merged.version())
        );
        assert_eq!(
            first_connected(&merged, 1, 4).as_ref(),
            Some(merged.version())
        );
        merged.validate_structure().unwrap();
    }

    #[test]
    fn balanced_unions_respect_union_by_size_height_and_retain_witnesses() {
        const COUNT: i32 = 1024;
        let root = forest(COUNT);
        let mut current = root.clone();
        let mut first_pair_version = None;
        let mut final_bridge_version = None;

        let mut width = 1;
        while width < COUNT {
            let mut start = 0;
            while start < COUNT {
                current = current.link(start, start + width).unwrap();
                if start == 0 && width == 1 {
                    first_pair_version = Some(current.version().clone());
                }
                if start == 0 && width == COUNT / 2 {
                    final_bridge_version = Some(current.version().clone());
                }
                start += width * 2;
            }
            width *= 2;
        }

        assert_eq!(current.component_count(), 1);
        assert_eq!(current.find(COUNT - 1).unwrap(), 0);
        assert_eq!(first_connected(&current, 0, 1), first_pair_version);
        assert_eq!(
            first_connected(&current, 0, COUNT - 1),
            final_bridge_version
        );
        assert_eq!(root.component_count(), COUNT);
        let statistics = current.validate_structure().unwrap();
        assert_eq!(statistics.stored_cell_count, COUNT as usize);
        assert_eq!(statistics.component_count, 1);
        assert_eq!(statistics.maximum_parent_path_len, 10);
        assert_eq!(statistics.history_depth, u64::from(COUNT as u32) - 1);
    }

    struct ModelVersion {
        actual: Forest,
        classes: Vec<i32>,
        parent: Option<usize>,
    }

    fn first_connected_by_scan(
        states: &[ModelVersion],
        index: usize,
        left: i32,
        right: i32,
    ) -> Option<AncestralConnectionVersion> {
        let mut lineage = Vec::new();
        let mut current = Some(index);
        while let Some(position) = current {
            lineage.push(position);
            current = states[position].parent;
        }

        for &position in lineage.iter().rev() {
            let candidate = &states[position];
            if candidate.classes[left as usize] == candidate.classes[right as usize] {
                return Some(candidate.actual.version().clone());
            }
        }

        None
    }

    #[test]
    fn random_branching_histories_match_naive_ancestor_scan() {
        const VERTEX_COUNT: i32 = 12;
        let mut state = 0x5EED_2026u64;
        let mut next = move || {
            state = state
                .wrapping_mul(6_364_136_223_846_793_005)
                .wrapping_add(1_442_695_040_888_963_407);
            (state >> 33) as usize
        };

        let mut states = vec![ModelVersion {
            actual: forest(VERTEX_COUNT),
            classes: (0..VERTEX_COUNT).collect(),
            parent: None,
        }];

        for operation in 0..350 {
            let parent_index = next() % states.len();
            let left = (next() % VERTEX_COUNT as usize) as i32;
            let right = (next() % VERTEX_COUNT as usize) as i32;
            let mut classes = states[parent_index].classes.clone();
            let left_class = classes[left as usize];
            let right_class = classes[right as usize];
            if left_class != right_class {
                for class in &mut classes {
                    if *class == right_class {
                        *class = left_class;
                    }
                }
            }

            let actual = states[parent_index].actual.link(left, right).unwrap();
            assert_eq!(
                actual.version().parent(),
                Some(states[parent_index].actual.version())
            );
            assert_eq!(
                actual.version().depth(),
                states[parent_index].actual.version().depth() + 1
            );
            let mut distinct = classes.clone();
            distinct.sort_unstable();
            distinct.dedup();
            assert_eq!(actual.component_count(), distinct.len() as i32);

            let index = states.len();
            states.push(ModelVersion {
                actual,
                classes,
                parent: Some(parent_index),
            });

            for query_left in 0..VERTEX_COUNT {
                for query_right in 0..VERTEX_COUNT {
                    let state = &states[index];
                    let expected_connected =
                        state.classes[query_left as usize] == state.classes[query_right as usize];
                    assert_eq!(
                        state.actual.connected(query_left, query_right).unwrap(),
                        expected_connected
                    );
                    assert_eq!(
                        first_connected(&state.actual, query_left, query_right),
                        first_connected_by_scan(&states, index, query_left, query_right)
                    );
                }
            }

            if operation % 25 == 0 {
                states[index].actual.validate_structure().unwrap();
            }
        }

        // Recheck retained versions after every sibling branch has been constructed.
        for _ in 0..80 {
            let index = next() % states.len();
            let left = (next() % VERTEX_COUNT as usize) as i32;
            let right = (next() % VERTEX_COUNT as usize) as i32;
            assert_eq!(
                first_connected(&states[index].actual, left, right),
                first_connected_by_scan(&states, index, left, right)
            );
        }
    }

    #[test]
    fn long_redundant_history_does_not_replace_the_successful_union_witness() {
        let root = forest(3);
        let first = root.link(0, 1).unwrap();
        let mut current = first.clone();
        for _ in 0..20_000 {
            current = current.link(2, 2).unwrap();
        }

        assert_eq!(current.version().depth(), 20_001);
        assert_eq!(
            first_connected(&current, 0, 1).as_ref(),
            Some(first.version())
        );
        assert_eq!(first_connected(&current, 0, 2), None);
        assert!(first.shares_connectivity_root_with(&current));
    }

    #[test]
    fn huge_sparse_universe_stores_only_vertices_touched_by_a_union() {
        let root = forest(i32::MAX);
        let statistics = root.validate_structure().unwrap();
        assert_eq!(statistics.stored_cell_count, 0);

        let linked = root.link(0, i32::MAX - 1).unwrap();
        assert_eq!(linked.component_count(), i32::MAX - 1);
        assert!(linked.connected(0, i32::MAX - 1).unwrap());
        assert!(!linked.connected(0, i32::MAX - 2).unwrap());
        assert_eq!(linked.find(i32::MAX - 2).unwrap(), i32::MAX - 2);
        assert_eq!(
            first_connected(&linked, 0, i32::MAX - 1).as_ref(),
            Some(linked.version())
        );
        let statistics = linked.validate_structure().unwrap();
        assert_eq!(statistics.stored_cell_count, 2);
        assert_eq!(statistics.maximum_parent_path_len, 1);
    }

    #[test]
    fn operations_reject_vertices_outside_the_universe() {
        let root = forest(3);
        let out_of_range = Some(AncestralConnectionError::VertexOutOfRange);

        assert_eq!(root.find(-1).err(), out_of_range);
        assert_eq!(root.find(3).err(), out_of_range);
        assert_eq!(root.connected(-1, 0).err(), out_of_range);
        assert_eq!(root.connected(0, 3).err(), out_of_range);
        assert_eq!(root.link(-1, 0).err(), out_of_range);
        assert_eq!(root.link(0, 3).err(), out_of_range);
        assert_eq!(root.first_connected(-1, 0).err(), out_of_range);
        assert_eq!(root.first_connected(0, 3).err(), out_of_range);
        assert_eq!(root.component_size(-1).err(), out_of_range);
        assert_eq!(root.component_size(3).err(), out_of_range);

        // A rejected operation publishes nothing.
        assert!(root.version().is_same_version(root.version().root()));
        assert_eq!(root.version().depth(), 0);
        assert_eq!(root.component_count(), 3);
        root.validate_structure().unwrap();
    }

    fn component_size(forest: &Forest, vertex: i32) -> i32 {
        forest
            .component_size(vertex)
            .expect("valid vertices are accepted")
    }

    /// Sums one cached size per distinct current root, which must recover the whole universe.
    fn total_component_size(forest: &Forest) -> i32 {
        let mut roots = HashMap::new();
        for vertex in 0..forest.vertex_count() {
            roots.insert(forest.find(vertex).unwrap(), component_size(forest, vertex));
        }

        assert_eq!(roots.len() as i32, forest.component_count());
        roots.values().sum()
    }

    #[test]
    fn component_size_reports_the_cached_union_size() {
        let root = forest(6);
        for vertex in 0..root.vertex_count() {
            assert_eq!(component_size(&root, vertex), 1);
        }
        assert_eq!(total_component_size(&root), 6);

        // A chain of unions grows exactly the touched component.
        let pair = root.link(0, 1).unwrap();
        let triple = pair.link(1, 2).unwrap();
        let quad = triple.link(2, 3).unwrap();

        assert_eq!(component_size(&pair, 0), 2);
        assert_eq!(component_size(&pair, 1), 2);
        assert_eq!(component_size(&pair, 5), 1);
        assert_eq!(component_size(&triple, 2), 3);
        for vertex in 0..4 {
            // Both endpoints of every union agree, and so does the whole component.
            assert_eq!(component_size(&quad, vertex), 4);
        }
        assert_eq!(component_size(&quad, 4), 1);
        assert_eq!(total_component_size(&quad), 6);

        // A redundant link changes nothing observable about sizes.
        let redundant = quad.link(3, 0).unwrap();
        for vertex in 0..quad.vertex_count() {
            assert_eq!(
                component_size(&redundant, vertex),
                component_size(&quad, vertex)
            );
        }
        assert_eq!(total_component_size(&redundant), 6);

        // Retained versions keep their own sizes across a branching history.
        let branch = quad.link(4, 5).unwrap();
        assert_eq!(component_size(&branch, 4), 2);
        assert_eq!(component_size(&quad, 4), 1);
        assert_eq!(component_size(&pair, 2), 1);
        assert_eq!(component_size(&root, 0), 1);
        assert_eq!(total_component_size(&branch), 6);
        assert_eq!(total_component_size(&pair), 6);

        let merged = branch.link(5, 0).unwrap();
        assert_eq!(merged.component_count(), 1);
        for vertex in 0..merged.vertex_count() {
            assert_eq!(component_size(&merged, vertex), 6);
        }
        assert_eq!(total_component_size(&merged), 6);
        merged.validate_structure().unwrap();

        // A huge sparse universe still reports singletons without storing them.
        let sparse = forest(i32::MAX).link(0, i32::MAX - 1).unwrap();
        assert_eq!(component_size(&sparse, 0), 2);
        assert_eq!(component_size(&sparse, i32::MAX - 1), 2);
        assert_eq!(component_size(&sparse, i32::MAX - 2), 1);
    }

    #[test]
    fn snapshots_and_version_tokens_are_send_and_sync() {
        fn assert_send_sync<T: Send + Sync>() {}

        assert_send_sync::<Forest>();
        assert_send_sync::<AncestralConnectionVersion>();
        assert_send_sync::<AncestralConnectionError>();
        assert_send_sync::<AncestralConnectionForestStatistics>();
        assert_send_sync::<AncestralConnectionForestInvariantError>();
    }

    #[test]
    fn separate_forests_have_separate_version_trees() {
        let left_root = forest(2);
        let right_root = forest(2);
        let left = left_root.link(0, 1).unwrap();
        let right = right_root.link(0, 1).unwrap();

        assert_ne!(left_root.version(), right_root.version());
        assert_ne!(left.version(), right.version());
        assert_eq!(left.version().depth(), right.version().depth());
        assert_eq!(first_connected(&left, 0, 1).as_ref(), Some(left.version()));
        assert_eq!(
            first_connected(&right, 0, 1).as_ref(),
            Some(right.version())
        );
        assert_ne!(first_connected(&left, 0, 1), first_connected(&right, 0, 1));
    }

    #[test]
    fn successful_union_rebuilds_only_the_touched_cells() {
        let root = forest(16);
        let first = root.link(0, 1).unwrap();
        let second = first.link(2, 3).unwrap();

        // A successful union publishes a new connectivity index; a redundant one does not.
        assert!(!first.shares_connectivity_root_with(&second));
        assert!(second.shares_connectivity_root_with(&second.link(0, 1).unwrap()));
        assert!(second.shares_connectivity_root_with(&second.link(3, 2).unwrap()));

        // Union by size: on a tie the first endpoint's root stays the representative.
        assert_eq!(second.find(1).unwrap(), 0);
        assert_eq!(second.find(3).unwrap(), 2);
        let merged = second.link(3, 1).unwrap();
        assert_eq!(merged.find(0).unwrap(), 2);
        assert_eq!(merged.component_count(), 13);
        merged.validate_structure().unwrap();
    }
}
