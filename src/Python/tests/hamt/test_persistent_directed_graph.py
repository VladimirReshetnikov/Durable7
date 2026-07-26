"""Tests for the persistent directed graph.

Covers retention of explicitly added isolated vertices, edges implying their endpoints, self
loops, the absence of parallel edges, reuse of vertex-set representatives at endpoints, the
distinct semantics of edge versus vertex removal, reversal as an O(1) involutive facade, and
root sharing for no-ops.
"""

from __future__ import annotations

from dataclasses import dataclass

from durable7 import (
    HashPolicy,
    PersistentDirectedGraph,
    create_hash_policy,
)


@dataclass(frozen=True)
class Vertex:
    """A vertex wrapper with controlled equality, so the test can check which representative the
    graph retains.
    """

    identifier: int
    name: str


VERTICES: HashPolicy[Vertex] = create_hash_policy(
    lambda vertex: vertex.identifier,
    lambda left, right: left.identifier == right.identifier,
)


def test_explicit_isolated_vertices_are_retained() -> None:
    graph = PersistentDirectedGraph[str].empty().add_vertex("isolated")
    assert graph.vertex_count == 1
    assert graph.edge_count == 0
    assert list(graph.vertices()) == ["isolated"]


def test_edges_add_endpoints_self_loops_and_no_parallel_pairs() -> None:
    graph = PersistentDirectedGraph[str].empty().add_edge("a", "b").add_edge("a", "a")
    assert graph.vertex_count == 2
    assert graph.edge_count == 2
    assert graph.add_edge("a", "b") is graph
    assert graph.outgoing("a").set_equals(["a", "b"])
    assert graph.incoming("a").set_equals(["a"])


def test_edges_use_vertex_set_representatives() -> None:
    first = Vertex(1, "first")
    second = Vertex(2, "second")
    graph = (
        PersistentDirectedGraph[Vertex]
        .empty(VERTICES)
        .add_vertex(first)
        .add_edge(Vertex(1, "later"), second)
    )
    edge = next(iter(graph))
    assert edge.from_vertex is first
    assert edge.to_vertex is second
    assert graph.validate_structure()


def test_edge_and_vertex_removal_have_distinct_semantics() -> None:
    source = PersistentDirectedGraph.from_items([], [("a", "b"), ("b", "c"), ("c", "a")])
    edge_removed = source.remove_edge("a", "b")
    vertex_removed = source.remove_vertex("b")
    assert edge_removed.contains_vertex("a") and edge_removed.contains_vertex("b")
    assert not vertex_removed.contains_vertex("b")
    assert all(edge.from_vertex != "b" and edge.to_vertex != "b" for edge in vertex_removed)
    assert source.edge_count == 3


def test_reversed_is_an_o1_involutive_facade() -> None:
    graph = PersistentDirectedGraph.from_items([], [("a", "b"), ("a", "c")])
    assert graph.reversed.reversed is graph
    assert graph.reversed.contains_edge("b", "a")
    assert graph.reversed is graph.reversed


def test_no_ops_and_retained_branches() -> None:
    source = PersistentDirectedGraph.from_items(["isolated"], [("a", "b")])
    assert source.add_vertex("a") is source
    assert source.remove_edge("b", "a") is source
    assert source.remove_vertex("missing") is source
    branch = source.add_edge("b", "a")
    assert not source.contains_edge("b", "a")
    assert branch.validate_structure()
