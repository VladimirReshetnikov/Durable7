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
        self._vertices = vertices
        self._edges = edges
        self._reverse_lock = Lock()
        self._reverse_view: PersistentDirectedGraph[V] | None = None

    @classmethod
    def empty(cls, policy: HashPolicy[V] | None = None) -> PersistentDirectedGraph[V]:
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
        return self._vertices.size

    @property
    def edge_count(self) -> int:
        return self._edges.pair_count

    @property
    def is_empty(self) -> bool:
        return self._vertices.is_empty

    @property
    def policy(self) -> HashPolicy[V]:
        return self._vertices.policy

    @property
    def reversed(self) -> PersistentDirectedGraph[V]:
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
        return self.edge_count

    def __bool__(self) -> bool:
        return not self.is_empty

    def contains_vertex(self, vertex: V) -> bool:
        return self._vertices.contains(vertex)

    def contains_edge(self, from_vertex: V, to_vertex: V) -> bool:
        return self._edges.contains(from_vertex, to_vertex)

    def get_stored_vertex(self, vertex: V) -> V | None:
        return self._vertices.get(vertex)

    def outgoing(self, vertex: V) -> PersistentHashSet[V]:
        return self._edges.get_rights(vertex)

    def incoming(self, vertex: V) -> PersistentHashSet[V]:
        return self._edges.get_lefts(vertex)

    def vertices(self) -> Iterator[V]:
        return iter(self._vertices)

    def add_vertex(self, vertex: V) -> PersistentDirectedGraph[V]:
        vertices = self._vertices.put(vertex)
        return (
            self if vertices is self._vertices else PersistentDirectedGraph(vertices, self._edges)
        )

    def add_edge(self, from_vertex: V, to_vertex: V) -> PersistentDirectedGraph[V]:
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
        edges = self._edges.remove(from_vertex, to_vertex)
        return self if edges is self._edges else PersistentDirectedGraph(self._vertices, edges)

    def remove_vertex(self, vertex: V) -> PersistentDirectedGraph[V]:
        if not self._vertices.contains(vertex):
            return self
        stored = cast("V", self._vertices.get(vertex))
        edges = self._edges.remove_left(stored).remove_right(stored)
        return PersistentDirectedGraph(self._vertices.remove(stored), edges)

    def clear(self) -> PersistentDirectedGraph[V]:
        return self if self.is_empty else self.empty(self.policy)

    def __iter__(self) -> Iterator[DirectedEdge[V]]:
        for edge in self._edges:
            yield DirectedEdge(edge.left, edge.right)

    def shares_roots_with(self, other: PersistentDirectedGraph[V]) -> bool:
        return self._vertices.shares_root_with(other._vertices) and self._edges.shares_roots_with(
            other._edges
        )

    def validate_structure(self) -> bool:
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
