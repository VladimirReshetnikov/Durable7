"""Append-only incremental level-ancestor arenas shared by the ancestry-backed collections."""

from __future__ import annotations

import math
import threading
from dataclasses import dataclass
from typing import Generic, Protocol, TypeVar, cast

T = TypeVar("T")


class IncrementalAncestorArena(Protocol[T]):
    """The append-only incremental level-ancestor service.

    An incremental level-ancestor arena is a monotone forest that grows one leaf at a time and
    answers "which ancestor of this node sits at absolute depth d?".  Node handles are stable
    integers owned by one arena.  The distinguished :attr:`bottom` node has depth ``-1`` and no
    value; every labelled node has a depth of zero or more.  A leaf may be added below any
    previously published node, not only below the most recently added one, so old consumer versions
    grow into independent branches.

    An implementation must guarantee that a successful :meth:`add_leaf` publishes a fresh,
    never-recycled handle whose immutable parent is the supplied handle and whose depth is exactly
    one greater; that :meth:`depth_of`, :meth:`parent_of`, :meth:`value_at`, and ancestor answers
    for a published node never change; and that a failed addition publishes no partially
    initialized node.

    :meth:`depth_of`, :meth:`parent_of`, and :meth:`value_at` must take O(1) time: the
    parameterized bounds documented by the consuming collections assume those primitive reads are
    constant time.  Let ``U`` be leaf-add time and ``Q`` be level-ancestor query time after ``M``
    published nodes.  A backend with ``U = Q = O(1)`` worst case would give the consumers their
    optimal bound; neither logarithmic binary lifting nor the shipped
    :class:`MyersIncrementalAncestorArena` satisfies that stronger backend bound.

    Integer handles are arena-relative capabilities.  Presenting a handle obtained from a different
    arena violates this protocol's precondition even when the same integer happens to be valid
    locally, because range validation cannot identify an in-range integer copied from elsewhere.
    Callers must preserve arena ownership themselves.

    Statistics are deliberately absent here.  They describe one backend's representation, so they
    belong to the implementation rather than the seam; a test that wants a query profile holds its
    own :class:`MyersIncrementalAncestorArena` reference.
    """

    @property
    def bottom(self) -> int:
        """The distinguished unlabelled node at depth ``-1``."""
        ...

    @property
    def published_node_count(self) -> int:
        """The number of labelled nodes published by this arena."""
        ...

    def add_leaf(self, parent: int, value: T) -> int:
        """Add a labelled leaf below a previously published node and return its stable handle."""
        ...

    def depth_of(self, node: int) -> int:
        """Return a node's immutable absolute depth; the bottom node has depth ``-1``."""
        ...

    def parent_of(self, node: int) -> int:
        """Return a labelled node's parent, or :attr:`bottom` when the node has depth zero."""
        ...

    def ancestor_at_depth(self, node: int, depth: int) -> int:
        """Return the unique ancestor of ``node`` at absolute ``depth``."""
        ...

    def value_at(self, node: int) -> T:
        """Return the immutable value associated with a labelled node."""
        ...


@dataclass(frozen=True, slots=True)
class MyersIncrementalAncestorStatistics:
    """One snapshot of a Myers incremental-ancestor arena.

    ``block_count`` and ``allocated_slot_count`` describe the odd-sized block store; the hop
    counters describe the parent and jump links that ancestor queries actually followed.
    """

    published_node_count: int
    block_count: int
    allocated_slot_count: int
    add_leaf_count: int
    ancestor_query_count: int
    last_ancestor_hop_count: int
    maximum_ancestor_hop_count: int
    total_ancestor_hop_count: int


@dataclass(frozen=True, slots=True)
class _Node(Generic[T]):
    """One published node: a value, a parent link, and one coalesced jump link."""

    value: T
    parent: int
    depth: int
    skip: int
    skip_distance: int


class MyersIncrementalAncestorArena(Generic[T]):
    """An incremental ancestor arena using the two-link applicative-stack scheme of Myers.

    Each immutable node keeps one parent link and one coalesced jump link, so adding a leaf
    performs O(1) link work.  Nodes are retained in blocks of sizes 1, 3, 5, ..., whose square
    boundaries give O(1) addressing and O(sqrt(M)) unused slots after M published nodes.  Managed
    block allocation is what makes insertion O(1) amortized rather than worst case:
    a single addition at a square boundary pays Θ(√M) to allocate the next odd block, the accepted
    contract of the shared odd-block representation (deamortizing it was considered and
    declined; Haskell's arena-free port exceeds this profile at O(1) worst case).  An ancestor
    query follows O(log M) parent/jump links in the worst case.  Total storage is O(M), and every
    published value stays reachable until the arena itself is collected.

    Operations are serialized by one private lock, so an arena shared between collection values is
    safe for concurrent use.  Published nodes are immutable, so retained collection handles stay
    semantically stable; this implementation makes no lock-free progress guarantee.  Handle
    validation is range-based and cannot identify an in-range integer copied from another arena.

    The managed baseline raises on three arithmetic ceilings: an ancestry depth or node count that
    reaches ``Int32.MaxValue``, and a coalesced jump distance that overflows.  Python integers are
    arbitrary precision, so none of those conditions can arise and this port does not synthesize an
    artificial limit to imitate them.  Growth here is bounded by memory, which the runtime reports
    as :exc:`MemoryError`.  For the same reason the baseline's saturating counter arithmetic is
    absent and the counters below are exact rather than clamped.
    """

    __slots__ = (
        "_add_leaf_count",
        "_allocated_slot_count",
        "_ancestor_query_count",
        "_blocks",
        "_count",
        "_gate",
        "_last_ancestor_hop_count",
        "_maximum_ancestor_hop_count",
        "_total_ancestor_hop_count",
    )

    def __init__(self) -> None:
        """Initialize an empty arena containing only its unlabelled bottom node."""

        self._gate = threading.Lock()
        self._blocks: list[list[_Node[T]]] = []
        self._count = 0
        self._allocated_slot_count = 0
        self._add_leaf_count = 0
        self._ancestor_query_count = 0
        self._last_ancestor_hop_count = 0
        self._maximum_ancestor_hop_count = 0
        self._total_ancestor_hop_count = 0
        # The bottom node carries no value; every read of it is rejected before the field is
        # touched, which is what lets the field stay typed as T instead of infecting the public
        # signature with an optional that callers would have to unwrap.
        self._append(_Node(cast("T", None), 0, -1, 0, 0))

    @property
    def bottom(self) -> int:
        """The distinguished unlabelled node at depth ``-1``.  This read is O(1)."""

        return 0

    @property
    def published_node_count(self) -> int:
        """The number of labelled nodes published by this arena.  This read is O(1)."""

        with self._gate:
            return self._count - 1

    def add_leaf(self, parent: int, value: T) -> int:
        """Add a labelled leaf below ``parent`` and return its stable handle.

        This addition is O(1) amortized over the block allocations; a block-boundary call
        pays Θ(√M) for the new odd block.

        Raises:
            IndexError: ``parent`` was never published by this arena.
        """

        with self._gate:
            parent_node = self._node_at(parent)
            if parent == 0 or parent_node.skip == 0:
                skip = parent
                skip_distance = 1
            else:
                skipped = self._node_at(parent_node.skip)
                if skipped.skip_distance <= parent_node.skip_distance:
                    skip = skipped.skip
                    skip_distance = 1 + parent_node.skip_distance + skipped.skip_distance
                else:
                    skip = parent
                    skip_distance = 1
            handle = self._append(_Node(value, parent, parent_node.depth + 1, skip, skip_distance))
            self._add_leaf_count += 1
            return handle

    def depth_of(self, node: int) -> int:
        """Return ``node``'s immutable absolute depth; the bottom node has depth ``-1``.

        This read is O(1).

        Raises:
            IndexError: ``node`` was never published by this arena.
        """

        with self._gate:
            return self._node_at(node).depth

    def parent_of(self, node: int) -> int:
        """Return ``node``'s parent, or :attr:`bottom` when the node has depth zero.

        This read is O(1).

        Raises:
            IndexError: ``node`` is the bottom node, which has no parent, or was never published by
                this arena.
        """

        with self._gate:
            if node == 0:
                raise IndexError("The bottom node has no parent.")
            return self._node_at(node).parent

    def value_at(self, node: int) -> T:
        """Return the immutable value associated with the labelled ``node``.

        This read is O(1).

        Raises:
            IndexError: ``node`` is the bottom node, which has no value, or was never published by
                this arena.
        """

        with self._gate:
            if node == 0:
                raise IndexError("The bottom node has no value.")
            return self._node_at(node).value

    def ancestor_at_depth(self, node: int, depth: int) -> int:
        """Return the unique ancestor of ``node`` at absolute ``depth``.

        The walk follows O(log M) parent/jump links in the worst case after M published nodes.

        Raises:
            IndexError: ``node`` was never published by this arena, or ``depth`` is outside ``-1``
                through the node's own depth.
        """

        with self._gate:
            current = self._node_at(node)
            if depth < -1 or depth > current.depth:
                raise IndexError("The depth is outside the node's ancestry.")

            hops = 0
            while current.depth > depth:
                remaining = current.depth - depth
                # Take the jump only when it lands at or below the target, so the walk never
                # overshoots and never needs to back up.
                node = current.skip if 0 < current.skip_distance <= remaining else current.parent
                current = self._blocks_get(node)
                hops += 1

            self._ancestor_query_count += 1
            self._last_ancestor_hop_count = hops
            self._total_ancestor_hop_count += hops
            self._maximum_ancestor_hop_count = max(self._maximum_ancestor_hop_count, hops)
            return node

    def statistics(self) -> MyersIncrementalAncestorStatistics:
        """Return deterministic representation and traversal counters.  This read is O(1)."""

        with self._gate:
            return MyersIncrementalAncestorStatistics(
                self._count - 1,
                len(self._blocks),
                self._allocated_slot_count,
                self._add_leaf_count,
                self._ancestor_query_count,
                self._last_ancestor_hop_count,
                self._maximum_ancestor_hop_count,
                self._total_ancestor_hop_count,
            )

    def validate_ancestry(self, node: int) -> None:
        """Audit one node's whole root path against the Myers invariants.

        Checks that depths decrease by exactly one per parent link, that the bottom node terminates
        the path at depth ``-1``, and that every jump link lands exactly its recorded distance above
        its owner.  This is O(d) for a node at depth ``d``.

        Raises:
            IndexError: ``node`` was never published by this arena.
            AssertionError: the arena's representation invariants do not hold.
        """

        with self._gate:
            current = self._node_at(node)
            while current.depth >= 0:
                parent_node = self._blocks_get(current.parent)
                if parent_node.depth != current.depth - 1:
                    raise AssertionError("A parent link does not descend exactly one level.")
                if current.skip_distance < 1:
                    raise AssertionError("A jump link has a non-positive distance.")
                skipped = self._blocks_get(current.skip)
                if skipped.depth != current.depth - current.skip_distance:
                    raise AssertionError("A jump link does not match its recorded distance.")
                current = parent_node
            if current.parent != 0 or current.skip != 0 or current.depth != -1:
                raise AssertionError("The bottom node is malformed.")

    def _node_at(self, node: int) -> _Node[T]:
        """Return a published node, rejecting a handle this arena never issued."""

        if not 0 <= node < self._count:
            raise IndexError("The node was never published by this arena.")
        return self._blocks_get(node)

    def _blocks_get(self, index: int) -> _Node[T]:
        """Read an already-validated slot from the odd-sized block store in O(1)."""

        block = math.isqrt(index)
        return self._blocks[block][index - block * block]

    def _append(self, item: _Node[T]) -> int:
        """Publish one node and return its handle, allocating a block only at a square boundary."""

        index = self._count
        block = math.isqrt(index)
        if block == len(self._blocks):
            block_length = 2 * block + 1
            # Seed the block with the incoming node so every slot holds a well-formed value; the
            # tail slots are overwritten before any handle addressing them is ever issued.
            self._blocks.append([item] * block_length)
            self._allocated_slot_count += block_length
        self._blocks[block][index - block * block] = item
        self._count = index + 1
        return index


__all__ = [
    "IncrementalAncestorArena",
    "MyersIncrementalAncestorArena",
    "MyersIncrementalAncestorStatistics",
]
