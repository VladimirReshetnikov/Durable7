use crate::{HashMultimapIter, PersistentHashSet, PersistentRelation};
use std::collections::hash_map::RandomState;
use std::fmt;
use std::hash::{BuildHasher, Hash};

/// Successful graph invariant statistics.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DirectedGraphStatistics {
    pub vertex_count: usize,
    pub edge_count: usize,
}

/// A disagreement between the graph's explicit vertex set and adjacency relation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DirectedGraphInvariantError {
    Relation,
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
    #[must_use]
    pub fn new() -> Self {
        Self::with_hasher(RandomState::new())
    }
}

impl<T, S> PersistentDirectedGraph<T, S>
where
    S: Clone,
{
    #[must_use]
    pub fn with_hasher(hasher: S) -> Self {
        Self {
            vertices: PersistentHashSet::with_hasher(hasher.clone()),
            edges: PersistentRelation::with_hashers(hasher.clone(), hasher),
        }
    }

    #[must_use]
    pub fn vertex_count(&self) -> usize {
        self.vertices.len()
    }

    #[must_use]
    pub fn edge_count(&self) -> usize {
        self.edges.pair_count()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.vertices.is_empty()
    }

    #[must_use]
    pub fn hasher(&self) -> &S {
        self.vertices.hasher()
    }

    pub fn vertices(&self) -> impl ExactSizeIterator<Item = &T> {
        self.vertices.iter()
    }

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
    #[must_use]
    pub fn contains_vertex(&self, vertex: &T) -> bool {
        self.vertices.contains(vertex)
    }

    #[must_use]
    pub fn get_vertex(&self, equal_vertex: &T) -> Option<&T> {
        self.vertices.get(equal_vertex)
    }

    #[must_use]
    pub fn contains_edge(&self, source: &T, target: &T) -> bool {
        self.edges.contains(source, target)
    }

    #[must_use]
    pub fn successors(&self, vertex: &T) -> Option<&PersistentHashSet<T, S>> {
        self.edges.get_rights(vertex)
    }

    #[must_use]
    pub fn predecessors(&self, vertex: &T) -> Option<&PersistentHashSet<T, S>> {
        self.edges.get_lefts(vertex)
    }

    #[must_use]
    pub fn out_degree(&self, vertex: &T) -> usize {
        self.edges.count_rights(vertex)
    }

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
    pub fn edges(&self) -> HashMultimapIter<'_, T, T, S> {
        self.edges.iter()
    }

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

    #[must_use]
    pub fn try_insert_edge(&self, source: T, target: T) -> (Self, bool) {
        let result = self.insert_edge(source, target);
        let changed = result.edge_count() != self.edge_count();
        (result, changed)
    }

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

    #[must_use]
    pub fn clear(&self) -> Self {
        if self.is_empty() {
            self.clone()
        } else {
            Self::with_hasher(self.hasher().clone())
        }
    }

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
