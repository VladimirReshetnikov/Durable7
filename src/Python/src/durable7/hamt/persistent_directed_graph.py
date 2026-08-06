"""Persistent directed graph over the public hash set and relation."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from threading import Lock
from typing import Generic, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy
from .persistent_hamt import PersistentHashSet
from .persistent_relation import PersistentRelation

V = TypeVar("V")


@dataclass(frozen=True, slots=True)
class DirectedEdge(Generic[V]):
    """One directed edge using stored vertex representatives."""

    from_vertex: V
    to_vertex: V


class PersistentDirectedGraph(Generic[V]):
    """Immutable directed graph with explicit isolated vertices and set-valued edges."""

    __slots__ = ("_edges", "_reverse_lock", "_reverse_view", "_vertices")

    def __init__(self, vertices: PersistentHashSet[V], edges: PersistentRelation[V, V]) -> None:
        """Wrap an already-built vertex set and edge relation; use :meth:`empty` or
        :meth:`from_items`.

        The caller is responsible for every edge endpoint being present in ``vertices``.
        """

        self._vertices = vertices
        self._edges = edges
        self._reverse_lock = Lock()
        self._reverse_view: PersistentDirectedGraph[V] | None = None

    @classmethod
    def empty(cls, policy: HashPolicy[V] | None = None) -> PersistentDirectedGraph[V]:
        """Return an empty graph retaining the exact policy object for vertices and edges alike."""

        effective = default_hash_policy() if policy is None else policy
        return cls(
            PersistentHashSet.empty(effective), PersistentRelation.empty(effective, effective)
        )

    @classmethod
    def from_items(
        cls,
        vertices: Iterable[V],
        edges: Iterable[tuple[V, V]],
        policy: HashPolicy[V] | None = None,
    ) -> PersistentDirectedGraph[V]:
        """Build a graph from explicit vertices and edges.

        An edge endpoint not listed in ``vertices`` is added as a vertex, so the result is always
        internally consistent.
        """

        if vertices is None or edges is None:
            raise TypeError("vertices and edges must be iterable.")
        result = cls.empty(policy)
        for vertex in vertices:
            result = result.add_vertex(vertex)
        for from_vertex, to_vertex in edges:
            result = result.add_edge(from_vertex, to_vertex)
        return result

    @property
    def vertex_count(self) -> int:
        """Number of vertices, including isolated ones."""

        return self._vertices.size

    @property
    def edge_count(self) -> int:
        """Number of directed edges."""

        return self._edges.pair_count

    @property
    def is_empty(self) -> bool:
        """Whether the graph has no vertices, and therefore no edges."""

        return self._vertices.is_empty

    @property
    def policy(self) -> HashPolicy[V]:
        """The retained hash policy defining vertex equivalence."""

        return self._vertices.policy

    @property
    def reversed(self) -> PersistentDirectedGraph[V]:
        """Return this graph with every edge direction flipped.

        The vertex set is shared and the edge relation's two indexes are swapped, so this is O(1)
        rather than a rebuild. The result is cached, and reversing twice returns the original
        object.
        """

        current = self._reverse_view
        if current is not None:
            return current
        with self._reverse_lock:
            current = self._reverse_view
            if current is None:
                current = PersistentDirectedGraph(self._vertices, self._edges.inverse)
                current._reverse_view = self
                self._reverse_view = current
            return current

    def __len__(self) -> int:
        """Number of edges, matching :attr:`edge_count` rather than the vertex count."""

        return self.edge_count

    def __bool__(self) -> bool:
        """Whether the graph has at least one vertex."""

        return not self.is_empty

    def contains_vertex(self, vertex: V) -> bool:
        """Whether ``vertex`` is present, isolated or not."""

        return self._vertices.contains(vertex)

    def contains_edge(self, from_vertex: V, to_vertex: V) -> bool:
        """Whether the directed edge from ``from_vertex`` to ``to_vertex`` is present."""

        return self._edges.contains(from_vertex, to_vertex)

    def get_stored_vertex(self, vertex: V) -> V | None:
        """Return the stored representative equivalent to ``vertex``, or ``None`` when absent.

        Edge endpoints reuse this representative, so a graph never holds two equivalent vertices.
        """

        return self._vertices.get(vertex)

    def outgoing(self, vertex: V) -> PersistentHashSet[V]:
        """Return the vertices ``vertex`` points to, or a policy-preserving empty set when it has
        none.
        """

        return self._edges.get_rights(vertex)

    def incoming(self, vertex: V) -> PersistentHashSet[V]:
        """Return the vertices pointing to ``vertex``, or a policy-preserving empty set when it has
        none.

        As cheap as :meth:`outgoing`, because the edge relation materializes both directions.
        """

        return self._edges.get_lefts(vertex)

    def vertices(self) -> Iterator[V]:
        """Iterate the vertices, isolated ones included."""

        return iter(self._vertices)

    def add_vertex(self, vertex: V) -> PersistentDirectedGraph[V]:
        """Return a graph containing ``vertex`` as an isolated vertex.

        Adding a vertex that is already present returns the receiver and retains the existing
        representative.
        """

        vertices = self._vertices.put(vertex)
        return (
            self if vertices is self._vertices else PersistentDirectedGraph(vertices, self._edges)
        )

    def add_edge(self, from_vertex: V, to_vertex: V) -> PersistentDirectedGraph[V]:
        """Return a graph containing the directed edge, adding either endpoint that is not yet a
        vertex.

        Endpoints are normalized to the vertex set's stored representatives. Self loops are
        permitted; adding an edge that is already present returns the receiver.
        """

        vertices = self._vertices.put(from_vertex).put(to_vertex)
        stored_from = cast("V", vertices.get(from_vertex))
        stored_to = cast("V", vertices.get(to_vertex))
        edges = self._edges.add(stored_from, stored_to)
        return (
            self
            if vertices is self._vertices and edges is self._edges
            else PersistentDirectedGraph(vertices, edges)
        )

    def remove_edge(self, from_vertex: V, to_vertex: V) -> PersistentDirectedGraph[V]:
        """Return a graph without that directed edge, leaving both vertices in place.

        Removing an absent edge returns the receiver.
        """

        edges = self._edges.remove(from_vertex, to_vertex)
        return self if edges is self._edges else PersistentDirectedGraph(self._vertices, edges)

    def remove_vertex(self, vertex: V) -> PersistentDirectedGraph[V]:
        """Return a graph without ``vertex`` and without any edge incident to it, in either
        direction.

        Removing the incident edges is what keeps the edge relation from naming a vertex that no
        longer exists. Removing an absent vertex returns the receiver.
        """

        if not self._vertices.contains(vertex):
            return self
        stored = cast("V", self._vertices.get(vertex))
        edges = self._edges.remove_left(stored).remove_right(stored)
        return PersistentDirectedGraph(self._vertices.remove(stored), edges)

    def clear(self) -> PersistentDirectedGraph[V]:
        """Return an empty graph retaining the policy; a no-op when already empty."""

        return self if self.is_empty else self.empty(self.policy)

    def __iter__(self) -> Iterator[DirectedEdge[V]]:
        """Iterate the directed edges. Isolated vertices do not appear; use :meth:`vertices`."""

        for edge in self._edges:
            yield DirectedEdge(edge.left, edge.right)

    def shares_roots_with(self, other: PersistentDirectedGraph[V]) -> bool:
        """Whether both graphs reference the same vertex set and edge relation roots.

        A representation test used to confirm that a no-op avoided copying, not an equality test.
        """

        return self._vertices.shares_root_with(other._vertices) and self._edges.shares_roots_with(
            other._edges
        )

    def validate_structure(self) -> bool:
        """Check that the edge relation is internally consistent and that every endpoint is a known
        vertex, stored as the very same representative object. A defensive audit over the whole
        graph; ordinary operations maintain these invariants.
        """

        if not self._edges.validate_structure():
            return False
        for edge in self._edges:
            if not self._vertices.contains(edge.left) or not self._vertices.contains(edge.right):
                return False
            if self._vertices.get(edge.left) is not edge.left:
                return False
            if self._vertices.get(edge.right) is not edge.right:
                return False
        return True


__all__ = ["DirectedEdge", "PersistentDirectedGraph"]
