//! Persistent directed graph with explicit vertices and both adjacency directions.
//!
//! [`PersistentDirectedGraph`] composes a [`PersistentHashSet`] of vertices with a
//! [`PersistentRelation`] of edges. Keeping the vertex set explicit means an isolated vertex is
//! representable, and removing a vertex removes its incident edges rather than leaving the edge
//! relation referring to a vertex that no longer exists.
//!
//! Because the underlying relation maintains forward and reverse adjacency, both successors and
//! predecessors of a vertex are available directly; neither direction requires scanning the edge
//! set. [`PersistentDirectedGraph::validate`] cross-checks the vertex set against both adjacency
//! directions and returns [`DirectedGraphStatistics`] or the first
//! [`DirectedGraphInvariantError`].

use crate::{HashMultimapIter, PersistentHashSet, PersistentRelation};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

/// Successful graph invariant statistics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DirectedGraphStatistics {
    /// The number of vertices, including isolated ones.
    pub vertex_count: usize,
    /// The number of directed edges.
    pub edge_count: usize,
}

/// A disagreement between the graph's explicit vertex set and adjacency relation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DirectedGraphInvariantError {
    /// The underlying edge relation failed its own validation.
    Relation,
    /// A directed edge names an endpoint that is absent from the vertex set.
    MissingEndpoint,
}

impl fmt::Display for DirectedGraphInvariantError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::Relation => "the directed graph's relation indexes disagree",
            Self::MissingEndpoint => "a directed edge has an endpoint absent from the vertex set",
        })
    }
}

impl std::error::Error for DirectedGraphInvariantError {}

/// Immutable simple directed graph with explicit vertices and bidirectional adjacency indexes.
pub struct PersistentDirectedGraph<T, S = RandomState> {
    vertices: PersistentHashSet<T, S>,
    edges: PersistentRelation<T, T, S, S>,
}

impl<T, S> Clone for PersistentDirectedGraph<T, S>
where
    S: Clone,
{
    fn clone(&self) -> Self {
        Self {
            vertices: self.vertices.clone(),
            edges: self.edges.clone(),
        }
    }
}

impl<T> PersistentDirectedGraph<T, RandomState> {
    /// Creates an empty graph using a fresh default hash policy.
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<T, S> PersistentDirectedGraph<T, S>
where
    S: Clone,
{
    /// Creates an empty graph whose vertex equivalence is defined by `hasher`.
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            vertices: PersistentHashSet::with_hasher(hasher.clone()),
            edges: PersistentRelation::with_hashers(hasher.clone(), hasher),
        }
    }

    /// Returns the number of vertices, including isolated ones. O(1).
    #[must_use]
    pub fn vertex_count(&self) -> usize {
        self.vertices.len()
    }

    /// Returns the number of directed edges. O(1).
    #[must_use]
    pub fn edge_count(&self) -> usize {
        self.edges.pair_count()
    }

    /// Returns `true` when the graph has no vertices, and therefore no edges.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.vertices.is_empty()
    }

    /// Borrows the retained hash policy that defines vertex equivalence.
    #[must_use]
    pub fn hasher(&self) -> &S {
        self.vertices.hasher()
    }

    /// Iterates the vertices, in unspecified order.
    pub fn vertices(&self) -> impl ExactSizeIterator<Item = &T> {
        self.vertices.iter()
    }

    /// Reports whether two graphs share both the vertex set and the edge relation, so neither can
    /// observe an edit made to the other. A representation test, not an equality test.
    #[must_use]
    pub fn shares_roots_with(&self, other: &Self) -> bool {
        self.vertices.shares_root_with(&other.vertices)
            && self.edges.shares_indexes_with(&other.edges)
    }

    /// Produces an O(1) root-swapping reversed value. Reversing twice shares the original roots.
    #[must_use]
    pub fn reversed(&self) -> Self {
        Self {
            vertices: self.vertices.clone(),
            edges: self.edges.inverse(),
        }
    }
}

impl<T, S> PersistentDirectedGraph<T, S>
where
    T: Eq + Hash,
    S: BuildHasher,
{
    /// Reports whether `vertex` is present. O(1) expected.
    #[must_use]
    pub fn contains_vertex(&self, vertex: &T) -> bool {
        self.vertices.contains(vertex)
    }

    /// Borrows the stored vertex representative equivalent to `equal_vertex`, or `None` when absent.
    ///
    /// Edge endpoints reuse this representative, so a graph never stores two equivalent vertices.
    #[must_use]
    pub fn get_vertex(&self, equal_vertex: &T) -> Option<&T> {
        self.vertices.get(equal_vertex)
    }

    /// Reports whether the directed edge `source -> target` is present. O(1) expected.
    #[must_use]
    pub fn contains_edge(&self, source: &T, target: &T) -> bool {
        self.edges.contains(source, target)
    }

    /// Borrows the vertices `vertex` points to, or `None` when `vertex` has no outgoing edges.
    #[must_use]
    pub fn successors(&self, vertex: &T) -> Option<&PersistentHashSet<T, S>> {
        self.edges.get_rights(vertex)
    }

    /// Borrows the vertices pointing to `vertex`, or `None` when it has no incoming edges.
    ///
    /// As cheap as [`Self::successors`], because the edge relation materializes both directions.
    #[must_use]
    pub fn predecessors(&self, vertex: &T) -> Option<&PersistentHashSet<T, S>> {
        self.edges.get_lefts(vertex)
    }

    /// Returns the number of edges leaving `vertex`, or zero when it has none.
    #[must_use]
    pub fn out_degree(&self, vertex: &T) -> usize {
        self.edges.count_rights(vertex)
    }

    /// Returns the number of edges entering `vertex`, or zero when it has none.
    #[must_use]
    pub fn in_degree(&self, vertex: &T) -> usize {
        self.edges.count_lefts(vertex)
    }
}

impl<T, S> PersistentDirectedGraph<T, S>
where
    T: Eq + Hash + Clone,
    S: BuildHasher + Clone,
{
    /// Iterates the directed edges as `(source, target)` pairs, in unspecified order.
    pub fn edges(&self) -> HashMultimapIter<'_, T, T, S> {
        self.edges.iter()
    }

    /// Builds a graph from explicit vertices and edges under the supplied hash policy.
    ///
    /// An edge endpoint not listed in `vertices` is added as a vertex, so the result is always
    /// internally consistent.
    #[must_use]
    pub fn from_vertices_and_edges<VI, EI>(vertices: VI, edges: EI, hasher: S) -> Self
    where
        VI: IntoIterator<Item = T>,
        EI: IntoIterator<Item = (T, T)>,
    {
        let mut result = Self::with_hasher(hasher);
        for vertex in vertices {
            result = result.insert_vertex(vertex);
        }
        for (source, target) in edges {
            result = result.insert_edge(source, target);
        }
        result
    }

    /// Adds an isolated vertex. Adding a vertex that is already present is a no-op that shares the
    /// receiver's representation and retains the existing representative.
    #[must_use]
    pub fn insert_vertex(&self, vertex: T) -> Self {
        let vertices = self.vertices.insert(vertex);
        if vertices.shares_root_with(&self.vertices) {
            self.clone()
        } else {
            Self {
                vertices,
                edges: self.edges.clone(),
            }
        }
    }

    /// Adds the directed edge `source -> target`, adding either endpoint that is not yet a vertex.
    ///
    /// Adding an edge that is already present is a no-op. A self-loop is permitted.
    #[must_use]
    pub fn insert_edge(&self, source: T, target: T) -> Self {
        let vertices = self.vertices.insert(source.clone()).insert(target.clone());
        let actual_source = vertices
            .get(&source)
            .expect("inserted source must be present")
            .clone();
        let actual_target = vertices
            .get(&target)
            .expect("inserted target must be present")
            .clone();
        let edges = self.edges.insert(actual_source, actual_target);
        if vertices.shares_root_with(&self.vertices) && edges.shares_indexes_with(&self.edges) {
            self.clone()
        } else {
            Self { vertices, edges }
        }
    }

    /// Adds the directed edge `source -> target`, reporting whether the edge was new.
    #[must_use]
    pub fn try_insert_edge(&self, source: T, target: T) -> (Self, bool) {
        let result = self.insert_edge(source, target);
        let changed = result.edge_count() != self.edge_count();
        (result, changed)
    }

    /// Removes the directed edge `source -> target`, leaving both vertices in place. Removing an
    /// absent edge is a no-op.
    #[must_use]
    pub fn remove_edge(&self, source: &T, target: &T) -> Self {
        let edges = self.edges.remove(source, target);
        if edges.shares_indexes_with(&self.edges) {
            self.clone()
        } else {
            Self {
                vertices: self.vertices.clone(),
                edges,
            }
        }
    }

    /// Removes `vertex` together with every edge incident to it, in either direction.
    ///
    /// Removing the incident edges is what keeps the edge relation from referring to a vertex that no
    /// longer exists. Removing an absent vertex is a no-op.
    #[must_use]
    pub fn remove_vertex(&self, vertex: &T) -> Self {
        let Some(actual) = self.vertices.get(vertex).cloned() else {
            return self.clone();
        };
        let edges = self.edges.remove_left(&actual).remove_right(&actual);
        Self {
            vertices: self.vertices.remove(&actual),
            edges,
        }
    }

    /// Returns an empty graph retaining the hash policy. Clearing an empty graph is a no-op that
    /// shares the receiver's representation.
    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::with_hasher(self.hasher().clone())
        }
    }

    /// Cross-checks the vertex set against both adjacency directions, confirming that every edge
    /// endpoint is a known vertex and that the two directions agree.
    ///
    /// A defensive audit over the whole graph; ordinary operations maintain these invariants.
    pub fn validate(&self) -> Result<DirectedGraphStatistics, DirectedGraphInvariantError> {
        self.edges
            .validate()
            .map_err(|_| DirectedGraphInvariantError::Relation)?;
        for (source, target) in self.edges.iter() {
            if !self.vertices.contains(source) || !self.vertices.contains(target) {
                return Err(DirectedGraphInvariantError::MissingEndpoint);
            }
        }
        Ok(DirectedGraphStatistics {
            vertex_count: self.vertex_count(),
            edge_count: self.edge_count(),
        })
    }
}

impl<T> PersistentDirectedGraph<T, RandomState>
where
    T: Eq + Hash + Clone,
{
    /// Builds a graph from explicit vertices and edges under a fresh default hash policy.
    #[must_use]
    pub fn from_parts<VI, EI>(vertices: VI, edges: EI) -> Self
    where
        VI: IntoIterator<Item = T>,
        EI: IntoIterator<Item = (T, T)>,
    {
        Self::from_vertices_and_edges(vertices, edges, RandomState::new())
    }
}

impl<T, S> Default for PersistentDirectedGraph<T, S>
where
    S: Default + Clone,
{
    fn default() -> Self {
        Self::with_hasher(S::default())
    }
}

impl<T, S> fmt::Debug for PersistentDirectedGraph<T, S>
where
    T: Eq + Hash + Clone + fmt::Debug,
    S: BuildHasher + Clone,
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("PersistentDirectedGraph")
            .field("vertices", &self.vertices)
            .field("edges", &self.edges)
            .finish()
    }
}
