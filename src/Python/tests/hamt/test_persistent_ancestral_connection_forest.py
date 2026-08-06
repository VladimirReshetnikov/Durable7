"""Tests for the persistent ancestral connection forest.

Covers the sparse singleton representation, union by size with first-endpoint tie-break, redundant
links publishing a distinct version while sharing connectivity, the lowest-common-ancestor rule
behind the first-connection witness, persistence across branching histories, reference identity of
version tokens, rejection of vertices outside the fixed universe, the injectivity that earns this
port its unconditional CHAMP factor, deep histories that would overflow the interpreter stack if
any walk recursed, and every violation the structural audit reports.
"""

from __future__ import annotations

import random
import sys
from dataclasses import dataclass, is_dataclass

import pytest

from durable7.hamt.persistent_ancestral_connection_forest import (
    MAXIMUM_VERTEX_COUNT,
    VERTEX_HASH_POLICY,
    AncestralConnectionForestStatistics,
    AncestralConnectionVersion,
    PersistentAncestralConnectionForest,
    _Cell,
    _vertex_hash,
)
from durable7.hamt.persistent_hamt import PersistentHashMap

Forest = PersistentAncestralConnectionForest


def _cells(*items: tuple[int, _Cell]) -> PersistentHashMap[int, _Cell]:
    """Build a parent-cell map directly, for the audit's corruption cases."""

    result: PersistentHashMap[int, _Cell] = PersistentHashMap.empty(VERTEX_HASH_POLICY)
    for vertex, cell in items:
        result = result.put(vertex, cell)
    return result


def _undo_xor_shift(value: int, shift: int) -> int:
    """Invert ``x ^= x >> shift`` over a 32-bit word."""

    result = value
    for _ in range(32 // shift + 1):
        result = value ^ (result >> shift)
    return result


def _undo_vertex_hash(hash_value: int) -> int:
    """Invert the pinned MurmurHash3 finalizer, witnessing that it is a bijection."""

    bits = _undo_xor_shift(hash_value & 0xFFFF_FFFF, 16)
    bits = (bits * pow(0xC2B2_AE35, -1, 1 << 32)) & 0xFFFF_FFFF
    bits = _undo_xor_shift(bits, 13)
    bits = (bits * pow(0x85EB_CA6B, -1, 1 << 32)) & 0xFFFF_FFFF
    return _undo_xor_shift(bits, 16)


def test_create_produces_singleton_components_and_root_history() -> None:
    root = Forest.create(5)

    assert root.vertex_count == 5
    assert root.component_count == 5
    assert root.version.depth == 0
    assert root.version.parent is None
    assert root.version.root is root.version
    for vertex in range(root.vertex_count):
        assert root.find(vertex) == vertex
        assert root.connected(vertex, vertex)
        assert root.component_size(vertex) == 1
        assert root.first_connected(vertex, vertex) is root.version

    assert not root.connected(0, 1)
    assert root.first_connected(0, 1) is None
    assert root.validate_structure() == AncestralConnectionForestStatistics(5, 5, 0, 0, 0)
    assert "vertex_count=5" in repr(root)

    empty = Forest.create(0)
    assert empty.vertex_count == 0
    assert empty.component_count == 0
    assert empty.validate_structure().stored_cell_count == 0


def test_create_rejects_invalid_vertex_counts() -> None:
    with pytest.raises(ValueError, match="cannot be negative"):
        Forest.create(-1)
    with pytest.raises(ValueError, match="cannot exceed"):
        Forest.create(MAXIMUM_VERTEX_COUNT + 1)
    assert Forest.create(MAXIMUM_VERTEX_COUNT).vertex_count == MAXIMUM_VERTEX_COUNT


def test_link_records_the_first_connection_version() -> None:
    root = Forest.create(6)
    first = root.link(0, 1)
    second = first.link(2, 3)
    bridge = second.link(0, 2)
    attach = bridge.link(4, 0)

    assert attach.component_count == 2
    assert attach.first_connected(0, 1) is first.version
    assert attach.first_connected(2, 3) is second.version
    assert attach.first_connected(0, 3) is bridge.version
    assert attach.first_connected(1, 2) is bridge.version
    assert attach.first_connected(4, 1) is attach.version
    assert attach.first_connected(4, 4) is root.version
    assert attach.first_connected(0, 5) is None

    assert attach.find(0) == 0
    assert attach.find(4) == 0

    # Every retained version keeps its own connectivity, untouched by later descendants.
    assert root.component_count == 6
    assert not root.connected(0, 1)
    assert not first.connected(0, 2)
    assert first.component_count == 5
    for version in (root, first, second, bridge, attach):
        version.validate_structure()


def test_redundant_links_create_versions_and_share_connectivity_state() -> None:
    root = Forest.create(4)
    linked = root.link(0, 1)
    reverse_duplicate = linked.link(1, 0)
    sibling_duplicate = linked.link(1, 0)
    self_duplicate = reverse_duplicate.link(0, 0)

    assert linked.version.depth == 1
    assert reverse_duplicate.version.depth == 2
    assert self_duplicate.version.depth == 3
    assert reverse_duplicate.version.parent is linked.version
    assert sibling_duplicate.version.parent is linked.version
    assert self_duplicate.version.parent is reverse_duplicate.version
    assert linked.version is not reverse_duplicate.version
    assert reverse_duplicate.version is not sibling_duplicate.version
    assert linked.component_count == reverse_duplicate.component_count
    assert linked.component_count == self_duplicate.component_count
    assert linked.shares_connectivity_root_with(reverse_duplicate)
    assert reverse_duplicate.shares_connectivity_root_with(self_duplicate)
    assert not root.shares_connectivity_root_with(linked)
    assert self_duplicate.first_connected(0, 1) is linked.version
    assert self_duplicate.first_connected(2, 2) is root.version
    self_duplicate.validate_structure()


def test_sibling_branches_keep_equal_depth_version_tags_distinct() -> None:
    root = Forest.create(5)
    common = root.link(0, 1)
    left = common.link(0, 2)
    right = common.link(0, 3)
    left_later = left.link(2, 4)
    right_later = right.link(3, 4)

    assert left.version.depth == right.version.depth
    assert left.version is not right.version
    assert left.first_connected(0, 1) is common.version
    assert right.first_connected(0, 1) is common.version
    assert left_later.first_connected(1, 2) is left.version
    assert right_later.first_connected(1, 3) is right.version
    assert left_later.first_connected(0, 4) is left_later.version
    assert right_later.first_connected(0, 4) is right_later.version
    assert not left_later.connected(0, 3)
    assert not right_later.connected(0, 2)
    assert left_later.version.root is root.version
    assert right_later.version.root is root.version
    left_later.validate_structure()
    right_later.validate_structure()


def test_version_tokens_use_reference_identity_rather_than_structure() -> None:
    root = Forest.create(5)
    common = root.link(0, 1)
    left = common.link(0, 2)
    right = common.link(0, 3)

    # A token records only a depth, a parent, and a root; these siblings agree on all three.
    assert left.version.depth == right.version.depth
    assert left.version.parent is right.version.parent
    assert left.version.root is right.version.root

    # Structural equality would therefore merge the branches, so the token is not a dataclass.
    assert not is_dataclass(AncestralConnectionVersion)
    assert left.version != right.version
    assert left.version == left.version
    assert not left.version.is_same_version(right.version)
    assert left.version.is_same_version(left.version)
    assert len({left.version, right.version, common.version, root.version}) == 4
    assert repr(left.version) == "AncestralConnectionVersion(depth=2)"


def test_separate_forests_have_separate_version_trees() -> None:
    left_root = Forest.create(2)
    right_root = Forest.create(2)
    left = left_root.link(0, 1)
    right = right_root.link(0, 1)

    assert left_root.version is not right_root.version
    assert left.version is not right.version
    assert left.version.depth == right.version.depth
    assert left.first_connected(0, 1) is left.version
    assert right.first_connected(0, 1) is right.version
    assert left.first_connected(0, 1) != right.first_connected(0, 1)


def test_first_connected_uses_pair_lca_rather_than_latest_component_birth() -> None:
    root = Forest.create(8)
    ab = root.link(0, 1)
    cd = ab.link(2, 3)
    abcd = cd.link(1, 3)
    ef = abcd.link(4, 5)
    gh = ef.link(6, 7)
    efgh = gh.link(4, 6)
    everything = efgh.link(0, 4)

    assert everything.first_connected(0, 1) is ab.version
    assert everything.first_connected(2, 3) is cd.version
    assert everything.first_connected(0, 2) is abcd.version
    assert everything.first_connected(4, 5) is ef.version
    assert everything.first_connected(6, 7) is gh.version
    assert everything.first_connected(5, 7) is efgh.version
    assert everything.first_connected(1, 6) is everything.version
    everything.validate_structure()


def test_first_connected_handles_one_endpoint_at_lca_after_that_lca_becomes_a_child() -> None:
    root = Forest.create(5)
    pair = root.link(0, 1)  # 1 -> 0
    other_pair = pair.link(2, 3)  # 3 -> 2
    larger = other_pair.link(2, 4)
    merged = larger.link(2, 0)  # the component rooted at 0 becomes a child of 2

    assert merged.find(0) == 2
    assert merged.first_connected(0, 1) is pair.version
    assert merged.first_connected(0, 2) is merged.version
    assert merged.first_connected(1, 4) is merged.version
    merged.validate_structure()


def test_first_connected_distinguishes_out_of_universe_from_disconnected() -> None:
    root = Forest.create(3)

    assert root.first_connected(0, 1) is None
    with pytest.raises(ValueError, match="fixed universe"):
        root.first_connected(0, 3)


def test_successful_union_rebuilds_only_the_touched_cells() -> None:
    root = Forest.create(16)
    first = root.link(0, 1)
    second = first.link(2, 3)

    # A successful union publishes a new connectivity index; a redundant one does not.
    assert not first.shares_connectivity_root_with(second)
    assert second.shares_connectivity_root_with(second.link(0, 1))
    assert second.shares_connectivity_root_with(second.link(3, 2))

    # Union by size: on a tie the first endpoint's root stays the representative.
    assert second.find(1) == 0
    assert second.find(3) == 2
    merged = second.link(3, 1)
    assert merged.find(0) == 2
    assert merged.component_count == 13
    merged.validate_structure()


def test_component_size_reports_the_cached_union_size() -> None:
    def total(forest: Forest) -> int:
        """Sum one cached size per distinct current root, recovering the whole universe."""

        roots = {forest.find(vertex): forest.component_size(vertex) for vertex in range(6)}
        assert len(roots) == forest.component_count
        return sum(roots.values())

    root = Forest.create(6)
    for vertex in range(root.vertex_count):
        assert root.component_size(vertex) == 1
    assert total(root) == 6

    pair = root.link(0, 1)
    triple = pair.link(1, 2)
    quad = triple.link(2, 3)

    assert pair.component_size(0) == 2
    assert pair.component_size(1) == 2
    assert pair.component_size(5) == 1
    assert triple.component_size(2) == 3
    for vertex in range(4):
        assert quad.component_size(vertex) == 4
    assert quad.component_size(4) == 1
    assert total(quad) == 6

    redundant = quad.link(3, 0)
    for vertex in range(quad.vertex_count):
        assert redundant.component_size(vertex) == quad.component_size(vertex)

    branch = quad.link(4, 5)
    assert branch.component_size(4) == 2
    assert quad.component_size(4) == 1
    assert pair.component_size(2) == 1
    assert total(branch) == 6
    assert total(pair) == 6

    merged = branch.link(5, 0)
    assert merged.component_count == 1
    for vertex in range(merged.vertex_count):
        assert merged.component_size(vertex) == 6
    merged.validate_structure()


def test_huge_sparse_universe_stores_only_vertices_touched_by_a_union() -> None:
    root = Forest.create(MAXIMUM_VERTEX_COUNT)
    assert root.validate_structure().stored_cell_count == 0

    linked = root.link(0, MAXIMUM_VERTEX_COUNT - 1)
    assert linked.component_count == MAXIMUM_VERTEX_COUNT - 1
    assert linked.connected(0, MAXIMUM_VERTEX_COUNT - 1)
    assert not linked.connected(0, MAXIMUM_VERTEX_COUNT - 2)
    assert linked.find(MAXIMUM_VERTEX_COUNT - 2) == MAXIMUM_VERTEX_COUNT - 2
    assert linked.component_size(MAXIMUM_VERTEX_COUNT - 2) == 1
    assert linked.first_connected(0, MAXIMUM_VERTEX_COUNT - 1) is linked.version
    statistics = linked.validate_structure()
    assert statistics.stored_cell_count == 2
    assert statistics.maximum_parent_path_length == 1


def test_operations_reject_vertices_outside_the_universe() -> None:
    root = Forest.create(3)

    for left, right in ((-1, 0), (0, 3)):
        with pytest.raises(ValueError, match="fixed universe"):
            root.connected(left, right)
        with pytest.raises(ValueError, match="fixed universe"):
            root.link(left, right)
        with pytest.raises(ValueError, match="fixed universe"):
            root.first_connected(left, right)
    for vertex in (-1, 3):
        with pytest.raises(ValueError, match="fixed universe"):
            root.find(vertex)
        with pytest.raises(ValueError, match="fixed universe"):
            root.component_size(vertex)

    # A rejected operation publishes nothing.
    assert root.version.root is root.version
    assert root.version.depth == 0
    assert root.component_count == 3
    root.validate_structure()


def test_balanced_unions_respect_union_by_size_height_and_retain_witnesses() -> None:
    count = 1024
    root = Forest.create(count)
    current = root
    first_pair_version: AncestralConnectionVersion | None = None
    final_bridge_version: AncestralConnectionVersion | None = None

    width = 1
    while width < count:
        for start in range(0, count, width * 2):
            current = current.link(start, start + width)
            if start == 0 and width == 1:
                first_pair_version = current.version
            if start == 0 and width == count // 2:
                final_bridge_version = current.version
        width *= 2

    assert current.component_count == 1
    assert current.find(count - 1) == 0
    assert current.first_connected(0, 1) is first_pair_version
    assert current.first_connected(0, count - 1) is final_bridge_version
    assert root.component_count == count
    statistics = current.validate_structure()
    assert statistics.stored_cell_count == count
    assert statistics.component_count == 1
    assert statistics.maximum_parent_path_length == 10
    assert statistics.history_depth == count - 1


def test_pinned_vertex_hash_is_an_injective_bijection_of_the_32_bit_word() -> None:
    samples = [
        *range(4096),
        *range(MAXIMUM_VERTEX_COUNT - 4095, MAXIMUM_VERTEX_COUNT + 1),
        1 << 16,
        (1 << 30) - 1,
        1 << 30,
    ]

    # Injectivity is what makes the CHAMP factor worst-case rather than expected: distinct vertices
    # never share a 32-bit hash, so no collision bucket can hold two of them.
    assert len({VERTEX_HASH_POLICY.hash(vertex) for vertex in samples}) == len(set(samples))
    for vertex in samples:
        assert _undo_vertex_hash(_vertex_hash(vertex)) == vertex
        assert _undo_vertex_hash(VERTEX_HASH_POLICY.hash(vertex)) == vertex
        assert -(1 << 31) <= VERTEX_HASH_POLICY.hash(vertex) < (1 << 31)


def test_long_redundant_history_does_not_replace_the_successful_union_witness() -> None:
    root = Forest.create(3)
    first = root.link(0, 1)
    current = first
    for _ in range(20_000):
        current = current.link(2, 2)

    # The history is twenty times the interpreter's recursion limit, so every version-chain walk
    # below would raise RecursionError if it were not an explicit loop.
    assert current.version.depth == 20_001
    assert current.version.depth > sys.getrecursionlimit()
    assert current.first_connected(0, 1) is first.version
    assert current.first_connected(0, 2) is None
    assert first.shares_connectivity_root_with(current)
    assert current.validate_structure().history_depth == 20_001


def test_deep_successful_history_keeps_paths_and_the_audit_iterative() -> None:
    count = 3000
    current = Forest.create(count)
    for vertex in range(count - 1):
        current = current.link(vertex, vertex + 1)

    assert current.version.depth == count - 1
    assert current.version.depth > sys.getrecursionlimit()
    assert current.component_count == 1
    assert current.component_size(0) == count
    assert current.find(count - 1) == 0
    statistics = current.validate_structure()
    assert statistics.history_depth == count - 1
    assert statistics.stored_cell_count == count
    assert statistics.maximum_parent_path_length == 1


@dataclass(slots=True)
class _ModelVersion:
    """One naive reference version: a forest, its class labels, and its history parent."""

    actual: PersistentAncestralConnectionForest
    classes: list[int]
    parent: int | None


def _first_connected_by_scan(
    states: list[_ModelVersion],
    index: int,
    left: int,
    right: int,
) -> AncestralConnectionVersion | None:
    """Answer the query by scanning the whole ancestor history, oldest first."""

    lineage: list[int] = []
    current: int | None = index
    while current is not None:
        lineage.append(current)
        current = states[current].parent
    for position in reversed(lineage):
        candidate = states[position]
        if candidate.classes[left] == candidate.classes[right]:
            return candidate.actual.version
    return None


def test_random_branching_histories_match_naive_ancestor_scan() -> None:
    vertex_count = 12
    rng = random.Random(0x5EED_2026)
    states = [_ModelVersion(Forest.create(vertex_count), list(range(vertex_count)), None)]

    for operation in range(350):
        parent_index = rng.randrange(len(states))
        left = rng.randrange(vertex_count)
        right = rng.randrange(vertex_count)
        classes = list(states[parent_index].classes)
        left_class = classes[left]
        right_class = classes[right]
        if left_class != right_class:
            classes = [left_class if label == right_class else label for label in classes]

        actual = states[parent_index].actual.link(left, right)
        assert actual.version.parent is states[parent_index].actual.version
        assert actual.version.depth == states[parent_index].actual.version.depth + 1
        assert actual.component_count == len(set(classes))

        index = len(states)
        states.append(_ModelVersion(actual, classes, parent_index))

        state = states[index]
        for query_left in range(vertex_count):
            for query_right in range(vertex_count):
                expected = state.classes[query_left] == state.classes[query_right]
                assert state.actual.connected(query_left, query_right) == expected
                assert state.actual.first_connected(query_left, query_right) is (
                    _first_connected_by_scan(states, index, query_left, query_right)
                )

        if operation % 25 == 0:
            state.actual.validate_structure()

    # Recheck retained versions after every sibling branch has been constructed.
    for _ in range(80):
        index = rng.randrange(len(states))
        left = rng.randrange(vertex_count)
        right = rng.randrange(vertex_count)
        assert states[index].actual.first_connected(left, right) is (
            _first_connected_by_scan(states, index, left, right)
        )


class _MisreportingVersion(AncestralConnectionVersion):
    """A token that lies about its depth, so the audit's chain checks can be exercised."""

    __slots__ = ()

    @property
    def depth(self) -> int:
        """Report a depth unrelated to the token's position in its chain."""

        return 99


def test_validate_structure_reports_count_and_history_violations() -> None:
    root = Forest.create(3)

    with pytest.raises(ValueError, match=r"Connection-forest counts are invalid\."):
        Forest(3, 4, _cells(), root.version).validate_structure()
    with pytest.raises(ValueError, match=r"Connection-forest counts are invalid\."):
        Forest(-1, 0, _cells(), root.version).validate_structure()
    with pytest.raises(ValueError, match="history-token parent chain is inconsistent"):
        Forest(
            3, 3, _cells(), _MisreportingVersion(AncestralConnectionVersion())
        ).validate_structure()
    with pytest.raises(ValueError, match="history root is inconsistent"):
        Forest(3, 3, _cells(), _MisreportingVersion()).validate_structure()


def test_validate_structure_reports_sparse_cell_violations() -> None:
    linked = Forest.create(4).link(0, 1)
    foreign = Forest.create(4).link(0, 1)

    with pytest.raises(ValueError, match="outside the universe"):
        Forest(4, 3, _cells((9, _Cell(9, 2, None))), linked.version).validate_structure()
    with pytest.raises(ValueError, match="canonical non-singleton root"):
        Forest(4, 4, _cells((0, _Cell(0, 1, None))), linked.version).validate_structure()
    with pytest.raises(ValueError, match="canonical non-singleton root"):
        Forest(4, 4, _cells((0, _Cell(0, 2, linked.version))), linked.version).validate_structure()

    # A tag borrowed from a sibling history is not an ancestor of this version.
    borrowed = _cells((0, _Cell(0, 2, None)), (1, _Cell(0, 0, foreign.version)))
    with pytest.raises(ValueError, match="invalid size or union-version tag"):
        Forest(4, 3, borrowed, linked.version).validate_structure()

    orphaned = _cells((0, _Cell(0, 2, None)), (1, _Cell(2, 0, linked.version)))
    with pytest.raises(ValueError, match="invalid size or union-version tag"):
        Forest(4, 3, orphaned, linked.version).validate_structure()


def test_validate_structure_reports_path_and_size_violations() -> None:
    root_version = AncestralConnectionVersion()
    first = AncestralConnectionVersion(root_version)
    second = AncestralConnectionVersion(first)
    third = AncestralConnectionVersion(second)

    decreasing = _cells(
        (0, _Cell(0, 3, None)),
        (1, _Cell(0, 0, first)),
        (2, _Cell(1, 0, second)),
    )
    with pytest.raises(ValueError, match="do not strictly increase toward the root"):
        Forest(4, 2, decreasing, third).validate_structure()

    too_tall = _cells(
        (0, _Cell(0, 4, None)),
        (1, _Cell(0, 0, third)),
        (2, _Cell(1, 0, second)),
        (3, _Cell(2, 0, first)),
    )
    with pytest.raises(ValueError, match="exceeds the union-by-size height bound"):
        Forest(4, 1, too_tall, third).validate_structure()

    linked = Forest.create(4).link(0, 1)
    consistent = _cells((0, _Cell(0, 2, None)), (1, _Cell(0, 0, linked.version)))
    with pytest.raises(ValueError, match="component count disagrees with the parent forest"):
        Forest(4, 2, consistent, linked.version).validate_structure()

    oversized = _cells((0, _Cell(0, 3, None)), (1, _Cell(0, 0, linked.version)))
    with pytest.raises(ValueError, match="cached union size is incorrect"):
        Forest(4, 3, oversized, linked.version).validate_structure()
