"""Tests for the append-only incremental level-ancestor seam.

The load-bearing case is the hop bound.  Myers' coalesced jump links are the entire reason this
backend exists over a plain parent array, and removing them leaves every ancestor *answer* correct
while turning each query into an O(depth) walk.  Only a hop-count assertion can catch that, so the
bound here is stated so that a coalescing-free arena fails it.
"""

from __future__ import annotations

import pytest

from durable7.finger_tree.incremental_ancestor import (
    IncrementalAncestorArena,
    MyersIncrementalAncestorArena,
)


def _ceiling_log2(value: int) -> int:
    """The depth in bits: what an ideal halving walk would need."""

    bits = 0
    while value > 1:
        value = (value + 1) // 2
        bits += 1
    return bits


def _reference_integer_square_root(value: int) -> int:
    """A multiply-only oracle, deliberately not :func:`math.isqrt`.

    The two disagree if the store's block arithmetic is off by one at a perfect square, which is
    exactly where an odd-block layout breaks.
    """

    root = 0
    while (root + 1) * (root + 1) <= value:
        root += 1
    return root


def _build_chain(arena: MyersIncrementalAncestorArena[int], length: int) -> int:
    tip = arena.bottom
    for index in range(1, length + 1):
        tip = arena.add_leaf(tip, index)
    return tip


def test_deep_chain_queries_stay_logarithmic() -> None:
    node_count = 32_768
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    tip = _build_chain(arena, node_count)

    before = arena.statistics()
    assert before.ancestor_query_count == 0, "additions must not move the query counter"
    assert before.maximum_ancestor_hop_count == 0, "no hops may be charged before any query"

    for depth in range(-1, node_count):
        arena.ancestor_at_depth(tip, depth)

    after = arena.statistics()
    # The factor four is headroom for the constant in Myers' coalescing schedule, whose jumps do
    # not close a full remaining half per hop. It stays Theta(log M), so no linear-hop
    # implementation can satisfy it at this scale: a plain parent walk would need 32_768 hops
    # against a bound of 64.
    bound = 4 * _ceiling_log2(node_count + 1)
    assert 1 <= after.maximum_ancestor_hop_count <= bound
    assert after.ancestor_query_count == node_count + 1
    assert after.total_ancestor_hop_count <= (node_count + 1) * bound


def test_last_hop_count_reports_one_query_rather_than_accumulating() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    tip = _build_chain(arena, 512)
    depth = arena.depth_of(tip)

    arena.ancestor_at_depth(tip, depth)
    assert arena.statistics().last_ancestor_hop_count == 0, "a self query costs no hop"

    arena.ancestor_at_depth(tip, -1)
    assert arena.statistics().last_ancestor_hop_count > 1, "a full walk costs hops"

    arena.ancestor_at_depth(tip, depth)
    assert arena.statistics().last_ancestor_hop_count == 0, "the last hop count is replaced"


def test_odd_block_layout_tracks_the_square_boundaries() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    tip = arena.bottom
    for published in range(2_049):
        current = arena.statistics()
        expected_blocks = _reference_integer_square_root(published) + 1
        assert current.published_node_count == published
        assert current.block_count == expected_blocks
        assert current.allocated_slot_count == expected_blocks * expected_blocks
        tip = arena.add_leaf(tip, published)


def test_error_contracts() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    node = arena.add_leaf(arena.bottom, 1)

    with pytest.raises(IndexError):
        arena.depth_of(99)
    with pytest.raises(IndexError):
        arena.depth_of(-1)
    with pytest.raises(IndexError):
        arena.parent_of(arena.bottom)
    with pytest.raises(IndexError):
        arena.value_at(arena.bottom)
    with pytest.raises(IndexError):
        arena.value_at(99)
    with pytest.raises(IndexError):
        arena.add_leaf(99, 2)
    with pytest.raises(IndexError):
        arena.ancestor_at_depth(node, 5)
    with pytest.raises(IndexError):
        arena.ancestor_at_depth(node, -2)

    assert arena.value_at(node) == 1
    assert arena.depth_of(arena.bottom) == -1
    arena.validate_ancestry(node)


def test_a_rejected_addition_publishes_nothing() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    tip = _build_chain(arena, 64)
    before = arena.statistics()

    with pytest.raises(IndexError):
        arena.add_leaf(4_096, 7)

    after = arena.statistics()
    assert after.published_node_count == before.published_node_count
    assert after.add_leaf_count == before.add_leaf_count
    assert after.block_count == before.block_count
    assert after.allocated_slot_count == before.allocated_slot_count

    # Pin the counter to a value, not merely to being unchanged: comparing two reads of the same
    # counter also holds for a counter that never advances at all.
    assert before.add_leaf_count == 64

    # The next successful addition still receives the handle that was pending.
    assert arena.add_leaf(tip, 65) == before.published_node_count + 1
    assert arena.statistics().add_leaf_count == 65


def test_branches_below_a_shared_parent_stay_independent() -> None:
    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    root = arena.add_leaf(arena.bottom, 0)
    left = arena.add_leaf(root, 1)
    right = arena.add_leaf(root, 2)

    assert left != right
    assert arena.ancestor_at_depth(left, 0) == root
    assert arena.ancestor_at_depth(right, 0) == root
    assert arena.parent_of(left) == root
    assert arena.parent_of(right) == root
    assert arena.value_at(left) == 1
    assert arena.value_at(right) == 2
    assert arena.depth_of(left) == arena.depth_of(right) == 1


def test_every_ancestor_answer_matches_the_naive_parent_walk() -> None:
    """The jump links are an optimization; they must not change a single answer.

    The shape has to be both branching and deep, and the assertions below pin that rather than
    trusting the construction. A chain cannot distinguish a jump that lands on the wrong branch from
    one that lands correctly, and a shallow shape - a star, say - never reaches ``add_leaf``'s
    coalescing arm at all, so an overshooting jump distance would pass unnoticed. An earlier version
    of this test used ``handles[(index * 7) % len(handles)]``, which is identically zero because
    ``len(handles) == index`` there, and so built exactly the depth-zero star that hides the bug.
    """

    arena: MyersIncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    handles = [arena.bottom]
    # Extend four independent frontiers round-robin, forking a fifth from an older node partway
    # through. Every path therefore grows deep enough to coalesce, while the arena still holds
    # several distinct branches for a mis-aimed jump to land on.
    frontiers = [arena.bottom] * 4
    seed = 12_345
    for index in range(1, 400):
        seed = (seed * 1_103_515_245 + 12_345) % 2**31
        slot = index % len(frontiers)
        node = arena.add_leaf(frontiers[slot], index)
        frontiers[slot] = node
        handles.append(node)
        if index % 97 == 0:
            frontiers.append(handles[seed % len(handles)])

    depths = [arena.depth_of(handle) for handle in handles]
    deepest_depth = max(depths)
    assert len({arena.parent_of(handle) for handle in handles[1:]}) > 1, "the shape must branch"
    assert deepest_depth >= 20, "the shape must be deep enough to coalesce jump links"

    # Coalescing must actually have happened: reaching bottom from the deepest node in fewer hops
    # than its depth is only possible if jump links were built and followed.
    deepest = handles[depths.index(deepest_depth)]
    assert arena.ancestor_at_depth(deepest, -1) == arena.bottom
    hops = arena.statistics().last_ancestor_hop_count
    assert hops < deepest_depth, (
        f"coalesced jumps must beat a parent walk: {hops} vs {deepest_depth}"
    )

    for handle in handles:
        arena.validate_ancestry(handle)
        # Materialize the root path once, so every depth is checked against an independently
        # derived answer without re-walking for each one.
        chain: list[int] = []
        walker = handle
        while walker != arena.bottom:
            chain.append(walker)
            walker = arena.parent_of(walker)
        chain.reverse()

        assert arena.ancestor_at_depth(handle, -1) == arena.bottom
        for depth, expected in enumerate(chain):
            assert arena.ancestor_at_depth(handle, depth) == expected


def test_the_shipped_arena_satisfies_the_seam() -> None:
    seam: IncrementalAncestorArena[int] = MyersIncrementalAncestorArena()
    node = seam.add_leaf(seam.bottom, 7)
    assert seam.published_node_count == 1
    assert seam.value_at(node) == 7
    assert seam.parent_of(node) == seam.bottom
    assert seam.ancestor_at_depth(node, -1) == seam.bottom
