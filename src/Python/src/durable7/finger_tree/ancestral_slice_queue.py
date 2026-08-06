"""Persistent queue whose every version is one interval of an append-only ancestry path.

A value is the constant-sized tuple ``(arena, tail, low_depth, count)`` naming an interval of the
unique bottom-to-``tail`` path inside a shared incremental level-ancestor arena.  The visible values
are the labels of the ancestors of ``tail`` at absolute depths ``low_depth`` through
``depth(tail)``, so ``count == depth(tail) - low_depth + 1``.  Appending adds one child below
``tail``; removing or slicing moves only the two interval boundaries.

Because the arena is append-only and its published nodes are immutable, every retained handle keeps
denoting exactly the same sequence forever, and adding below an *old* handle simply starts a sibling
branch.  That is what makes the structure fully persistent for its restricted algebra: any
historical version can be extended, not only the newest one.  It is not confluently persistent,
because two unrelated versions cannot be merged.

The anchored-empty rule
-----------------------

An empty value is not "nothing".  It retains the node immediately *before* its window as an anchor,
so ``low_depth == depth(tail) + 1`` holds exactly when the value is empty.  Appending to any empty
slice therefore yields exactly the new value and never resurrects a discarded prefix.  A queue
drained from the front and one drained from the back keep *different* anchors while denoting the
same empty sequence, so there is no canonical empty value and no process-global empty arena.

Bounds
------

The bounds are parameterized by the backend.  Let ``M`` be the number of labelled nodes the arena
has ever published, ``U(M)`` the cost of adding a leaf, and ``Q(M)`` the cost of one level-ancestor
query.  Then :meth:`AncestralSliceQueue.add_last` costs ``U(M)``; :attr:`AncestralSliceQueue.first`,
:meth:`AncestralSliceQueue.get_at`, :meth:`AncestralSliceQueue.take`,
:meth:`AncestralSliceQueue.get_range`, :meth:`AncestralSliceQueue.try_remove_first`, and a
nontrivial :meth:`AncestralSliceQueue.split_at` cost ``Q(M)``; and ``len``,
:attr:`AncestralSliceQueue.is_empty`, :attr:`AncestralSliceQueue.last`,
:meth:`AncestralSliceQueue.remove_first`, :meth:`AncestralSliceQueue.remove_last`,
:meth:`AncestralSliceQueue.try_remove_last`, and :meth:`AncestralSliceQueue.drop` are O(1).

Instantiating the backend with Alstrup-Holm incremental level ancestry would give
``U(M) = Q(M) = O(1)`` *worst case* in linear space.  **That backend is not implemented here**; the
statement is a reduction to a published result, not a property of anything in this package.  The
shipped :class:`~durable7.finger_tree.incremental_ancestor.MyersIncrementalAncestorArena` instead
has O(1) *amortized* leaf addition and O(log M) worst-case ancestor queries, so the shipped bounds
are ``add_last`` O(1) amortized and the ``Q(M)`` group O(log M) worst case.  No amortized bound
above may be read as a worst-case bound.

Boundary-specialized calls make no ancestor query at all: ``take(len(queue))``, ``drop(0)``, the
whole-range ``get_range(0, len(queue))``, every suffix ``get_range(index, len(queue) - index)``
including the empty suffix ``get_range(len(queue), 0)``, a split at ``len(queue)``, and indexed
access at the last visible index.  A split at ``0`` of a *non-empty* queue is not query-free here:
the anchored-empty rule requires the empty prefix to retain the node at depth ``low_depth - 1``, and
this port always names that node with an ancestor query.  When ``low_depth > 0`` no cheaper route
exists, since only an ancestor query can name that node from the retained tail; when ``low_depth ==
0`` it is the arena's bottom node, which the seam also names in O(1), so the uniform query there is
a choice rather than a necessity.  On an *empty* queue the two split boundaries coincide, so a split
at ``0`` is query-free.

Deliberate limits
-----------------

The algebra intentionally excludes prepending, point replacement, arbitrary middle edits, and
concatenation of unrelated histories; omitting them is precisely why the indexed and slicing bounds
are possible.  Arena space is charged to *all* successful historical appends, not to the visible
length of one handle: dropping a prefix or releasing a handle reclaims nothing.  Two handles may
enumerate equal values while having different tails, anchors, or arenas, so this type promises no
O(1) sequence equality or hashing and implements neither; equality and hashing stay by identity.

The managed baseline raises when the visible count or the ancestry depth reaches ``Int32.MaxValue``.
Python integers are arbitrary precision and the arena this queue is built on deliberately
synthesizes no artificial ceiling of its own, so neither condition can arise here and this port does
not imitate them.  Growth is bounded by memory, which the runtime reports as :exc:`MemoryError`.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .incremental_ancestor import IncrementalAncestorArena, MyersIncrementalAncestorArena

T = TypeVar("T")


def _window_end_depth(low_depth: int, count: int) -> int:
    """The absolute depth of the last value of a ``count``-long window starting at ``low_depth``.

    An empty window yields ``low_depth - 1``, which is exactly the anchor depth the anchored-empty
    rule requires.
    """

    return low_depth + count - 1


@dataclass(frozen=True, slots=True)
class AncestralSliceQueueStatistics:
    """Window, anchor, and arena measurements returned by a successful structural audit.

    The audit uses only the arena's O(1) primitive reads, so it performs no level-ancestor query and
    cannot perturb a query-count measurement taken around it.
    """

    count: int
    low_depth: int
    tail_depth: int
    anchor_depth: int
    anchored_at_bottom: bool
    arena_published_node_count: int


@dataclass(frozen=True, slots=True)
class AncestralSliceQueuePop(Generic[T]):
    """One removed endpoint value and the persistent queue that remains without it."""

    value: T
    rest: AncestralSliceQueue[T]


@dataclass(frozen=True, slots=True)
class AncestralSliceQueueSplit(Generic[T]):
    """The two appendable ancestry intervals produced by :meth:`AncestralSliceQueue.split_at`."""

    left: AncestralSliceQueue[T]
    right: AncestralSliceQueue[T]


@dataclass(frozen=True, slots=True, eq=False, repr=False)
class AncestralSliceQueue(Generic[T]):
    """Immutable, fully persistent queue slice denoting one interval of an append ancestry path.

    A value retains its arena, a tail node, the absolute depth of its first visible value, and a
    redundant count cache.  Empty values retain the node immediately before their window as an
    anchor, so appending to any empty slice yields exactly the new value without exposing a
    discarded prefix.  Adding below an old handle creates a sibling branch in the shared monotone
    arena rather than disturbing that handle.

    Build values through :meth:`create`, :meth:`create_myers`, or :meth:`from_iterable`; the
    generated constructor takes an already-validated window and checks nothing.  Equality and
    hashing are by identity, because two handles may enumerate equal values while retaining
    different tails, anchors, or arenas.

    See the module documentation for the parameterized and shipped complexity tables and for the
    operations this algebra deliberately omits.
    """

    _arena: IncrementalAncestorArena[T]
    _tail: int
    _low_depth: int
    _count: int

    @classmethod
    def create(cls, arena: IncrementalAncestorArena[T]) -> AncestralSliceQueue[T]:
        """An empty queue owned by an explicit incremental-ancestor arena.

        This is the extension seam: supplying a backend with different ``U(M)``/``Q(M)`` costs
        changes every parameterized bound the module documents.  The arena retains and navigates
        every appended node.

        Raises:
            ValueError: the arena does not report depth ``-1`` for its bottom node, which the
                anchored-empty rule needs so that a queue anchored there begins at depth zero.
        """

        bottom = arena.bottom
        if arena.depth_of(bottom) != -1:
            raise ValueError(
                "An ancestral slice queue arena must report depth -1 for its bottom node."
            )
        return cls(arena, bottom, 0, 0)

    @classmethod
    def create_myers(cls) -> AncestralSliceQueue[T]:
        """An empty queue backed by a fresh, independently collectible Myers jump-link arena.

        There is deliberately no shared global empty value: each call owns its own arena, so
        unrelated workloads never share retained history or lock contention.
        """

        arena: IncrementalAncestorArena[T] = MyersIncrementalAncestorArena()
        return cls.create(arena)

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> AncestralSliceQueue[T]:
        """A Myers-backed queue holding ``values`` in enumeration order.

        Publishes one arena node per value, for Theta(k) total leaf-add work.  An existing queue is
        deliberately *not* returned unchanged: the result always owns a private arena, so it shares
        no retained history with its source.
        """

        result = cls.create_myers()
        for value in values:
            result = result.add_last(value)
        return result

    @property
    def count(self) -> int:
        """The number of visible values, read from the cache in O(1)."""

        return self._count

    def __len__(self) -> int:
        """The number of visible values, read from the cache in O(1)."""

        return self._count

    @property
    def is_empty(self) -> bool:
        """Whether this handle denotes an empty ancestry interval.  This read is O(1)."""

        return self._count == 0

    @property
    def first(self) -> T:
        """The first visible value.

        Costs one ancestor query ``Q(M)``: the front is selected jointly by the tail and the low
        boundary, so it is not an O(1) endpoint read under a logarithmic backend.  A singleton
        reuses its retained tail and makes no query.

        Raises:
            IndexError: the queue is empty.
        """

        self._ensure_not_empty()
        node = (
            self._tail
            if self._count == 1
            else self._arena.ancestor_at_depth(self._tail, self._low_depth)
        )
        return self._arena.value_at(node)

    @property
    def last(self) -> T:
        """The last visible value, read in O(1) because the handle retains the tail directly.

        Raises:
            IndexError: the queue is empty.
        """

        self._ensure_not_empty()
        return self._arena.value_at(self._tail)

    def get_at(self, index: int) -> T:
        """The visible value at a nonnegative zero-based index.

        Costs one ancestor query ``Q(M)``; the last visible index reuses the retained tail and makes
        no query.

        Raises:
            IndexError: the index is outside the queue.
        """

        if index < 0 or index >= self._count:
            raise IndexError("index is outside the ancestral slice queue")
        node = (
            self._tail
            if index == self._count - 1
            else self._arena.ancestor_at_depth(self._tail, self._low_depth + index)
        )
        return self._arena.value_at(node)

    def __getitem__(self, index: int) -> T:
        """The visible value at ``index``, validated exactly as :meth:`get_at`."""

        return self.get_at(index)

    def add_last(self, value: T) -> AncestralSliceQueue[T]:
        """A queue with ``value`` appended below this handle's tail or empty anchor.

        Costs ``U(M)`` and publishes exactly one arena node.  This handle and every other retained
        version are unaffected; appending below an old handle starts a sibling branch.  An arena
        that rejects the addition publishes nothing and leaves this handle valid.
        """

        tail = self._arena.add_leaf(self._tail, value)
        return AncestralSliceQueue(self._arena, tail, self._low_depth, self._count + 1)

    def remove_first(self) -> AncestralSliceQueue[T]:
        """The queue without its first visible value.  This update is O(1).

        Only the low boundary moves, so no ancestor query is made.  Emptying a singleton this way
        leaves the old tail as the result's anchor.

        Raises:
            IndexError: the queue is empty.
        """

        self._ensure_not_empty()
        return AncestralSliceQueue(self._arena, self._tail, self._low_depth + 1, self._count - 1)

    def remove_last(self) -> AncestralSliceQueue[T]:
        """The queue without its last visible value.  This update is O(1).

        The new tail is the old tail's parent, which the arena reads in constant time.  Emptying a
        singleton this way anchors the result immediately before the window.

        Raises:
            IndexError: the queue is empty.
        """

        self._ensure_not_empty()
        return AncestralSliceQueue(
            self._arena, self._arena.parent_of(self._tail), self._low_depth, self._count - 1
        )

    def try_remove_first(self) -> AncestralSliceQueuePop[T] | None:
        """The first visible value with the remainder, or ``None`` when the queue is empty.

        Costs ``Q(M)`` because it also returns the front value.  Nothing is published on the empty
        path and this handle stays valid either way.  A stored ``None`` is a present value: absence
        is reported only by the ``None`` result.
        """

        if self._count == 0:
            return None
        return AncestralSliceQueuePop(self.first, self.remove_first())

    def try_remove_last(self) -> AncestralSliceQueuePop[T] | None:
        """The last visible value with the remainder, or ``None`` when empty.

        This update is O(1): the tail is retained directly, so no ancestor query is made.
        """

        if self._count == 0:
            return None
        return AncestralSliceQueuePop(self.last, self.remove_last())

    def take(self, count: int) -> AncestralSliceQueue[T]:
        """The first ``count`` visible values, as an appendable ancestry prefix.

        Costs ``Q(M)``; ``take(len(queue))`` returns this handle unchanged and makes no query.  The
        result is a real ancestry slice and remains appendable, and ``take(0)`` on a non-empty queue
        is anchored immediately before the window.

        Raises:
            ValueError: ``count`` is negative or exceeds the visible length.
        """

        self._check_count(count)
        return self if count == self._count else self.get_range(0, count)

    def drop(self, count: int) -> AncestralSliceQueue[T]:
        """The queue after omitting its first ``count`` visible values.  This update is O(1).

        Only the low boundary moves, so no ancestor query is ever made.  ``drop(0)`` returns this
        handle unchanged, and ``drop(len(queue))`` keeps the old tail as the empty result's anchor.
        This is the reference contract's ``Drop``.

        Raises:
            ValueError: ``count`` is negative or exceeds the visible length.
        """

        self._check_count(count)
        if count == 0:
            return self
        return AncestralSliceQueue(
            self._arena,
            self._tail,
            self._low_depth + count,
            self._count - count,
        )

    def get_range(self, index: int, count: int) -> AncestralSliceQueue[T]:
        """One contiguous, appendable ancestry slice.  This is the reference contract's ``Slice``.

        Costs ``Q(M)`` in general.  A range whose result ends at the current tail -- the whole range
        and every suffix, including the empty suffix ``get_range(len(queue), 0)`` -- reuses that
        tail and makes no query.  A zero-length range elsewhere selects the ancestor immediately
        before its start as an anchor, which is the arena's bottom node when the start is the very
        front of a queue grown from an empty one.

        The count is validated as ``count > len(queue) - index``, so an index plus count that would
        run away is rejected rather than wrapped.

        Raises:
            IndexError: ``index`` is outside ``0..len(queue)``.
            ValueError: ``count`` is negative or runs past the end of the queue.
        """

        self._check_boundary(index)
        if count < 0:
            raise ValueError("count must be nonnegative")
        if count > self._count - index:
            raise ValueError("the range extends past the end of the ancestral slice queue")
        if index == 0 and count == self._count:
            return self

        low_depth = self._low_depth + index
        target_depth = _window_end_depth(low_depth, count)
        current_tail_depth = _window_end_depth(self._low_depth, self._count)
        tail = (
            self._tail
            if target_depth == current_tail_depth
            else self._arena.ancestor_at_depth(self._tail, target_depth)
        )
        return AncestralSliceQueue(self._arena, tail, low_depth, count)

    def split_at(self, index: int) -> AncestralSliceQueueSplit[T]:
        """This queue split at a zero-based boundary into an appendable prefix and suffix.

        Costs ``Q(M)``: the prefix performs the one ancestor-seeking query and the suffix only moves
        the low boundary.  Both results are real ancestry slices and can independently start new
        branches.  A split at ``len(queue)`` makes no query.  A split at ``0`` of a non-empty queue
        still costs ``Q(M)`` here, because the anchored-empty rule requires the empty prefix to
        retain the node at depth ``low_depth - 1``; that is forced when ``low_depth > 0``, while at
        ``low_depth == 0`` the node is the arena's bottom and the query is a uniformity choice.  On
        an empty queue the two boundaries coincide, so the split is query-free there.

        Raises:
            IndexError: the boundary is outside ``0..len(queue)``.
        """

        self._check_boundary(index)
        return AncestralSliceQueueSplit(self.take(index), self.drop(index))

    def to_list(self) -> list[T]:
        """The visible values in front-to-back order.

        Follows parent links from the tail rather than performing ``len(queue)`` ancestor queries,
        taking Theta(len) time and Theta(len) transient slots.  A concurrent append cannot enter the
        captured path.
        """

        values: list[T] = []
        node = self._tail
        for remaining in range(self._count, 0, -1):
            values.append(self._arena.value_at(node))
            if remaining != 1:
                node = self._arena.parent_of(node)
        values.reverse()
        return values

    def __iter__(self) -> Iterator[T]:
        """Iterate the visible values front to back.

        The whole path is captured up front, so the iterator observes exactly the handle it was
        obtained from and never sees a later append.
        """

        return iter(self.to_list())

    def __repr__(self) -> str:
        """Render the visible interval, as the sibling ports' debug formatting does."""

        return f"AncestralSliceQueue({self.to_list()!r})"

    @property
    def arena(self) -> IncrementalAncestorArena[T]:
        """The shared arena this handle navigates.  Exposed for diagnostics, not for mutation."""

        return self._arena

    @property
    def tail_handle(self) -> int:
        """The arena handle of the retained tail, or of the anchor when the queue is empty."""

        return self._tail

    @property
    def low_depth(self) -> int:
        """The absolute ancestry depth of the first visible value."""

        return self._low_depth

    def shares_arena_with(self, other: AncestralSliceQueue[T]) -> bool:
        """Whether both handles navigate the same arena, so their histories can interleave."""

        return self._arena is other._arena

    def shares_tail_with(self, other: AncestralSliceQueue[T]) -> bool:
        """Whether both handles retain the same arena node as their tail.

        Used by the tests to show that a whole-window operation returned a value sharing the source
        tail rather than one rebuilt through an ancestor query.
        """

        return self._arena is other._arena and self._tail == other._tail

    def validate_structure(self) -> AncestralSliceQueueStatistics:
        """Audit the window against the arena and report what it measured.

        Checks that the low boundary is a real depth, that the cached count agrees with the tail and
        low depths, that every visible node carries a value, that each parent hop from the tail
        drops the depth by exactly one, and that the anchor sits at ``low_depth - 1``.  Runs in
        Theta(len) time using only the arena's O(1) primitive reads, so it makes no level-ancestor
        query and cannot perturb a query-count measurement taken around it.  A defensive audit, not
        part of normal use.

        Raises:
            ValueError: on the first violated invariant.
        """

        if self._count < 0:
            raise ValueError("The ancestral slice queue count is negative.")
        if self._low_depth < 0:
            raise ValueError("The ancestral slice queue low boundary is not a real depth.")

        bottom = self._arena.bottom
        tail_depth = self._arena.depth_of(self._tail)
        if tail_depth != _window_end_depth(self._low_depth, self._count):
            raise ValueError(
                "The ancestral slice queue count disagrees with its tail and low depths."
            )
        if (self._tail == bottom) != (tail_depth == -1):
            raise ValueError("Only the ancestral slice queue arena bottom may sit at depth -1.")

        node = self._tail
        depth = tail_depth
        for _ in range(self._count):
            self._arena.value_at(node)
            parent = self._arena.parent_of(node)
            if self._arena.depth_of(parent) != depth - 1:
                raise ValueError("An ancestral slice queue parent hop skipped a depth.")
            node = parent
            depth -= 1

        if depth != self._low_depth - 1:
            raise ValueError("The ancestral slice queue anchor is not below its window.")
        anchored_at_bottom = node == bottom
        if anchored_at_bottom != (depth == -1):
            raise ValueError("The ancestral slice queue anchor depth contradicts its identity.")

        return AncestralSliceQueueStatistics(
            self._count,
            self._low_depth,
            tail_depth,
            depth,
            anchored_at_bottom,
            self._arena.published_node_count,
        )

    def _ensure_not_empty(self) -> None:
        if self._count == 0:
            raise IndexError("The ancestral slice queue is empty.")

    def _check_boundary(self, index: int) -> None:
        if index < 0 or index > self._count:
            raise IndexError("index is outside the ancestral slice queue boundary range")

    def _check_count(self, count: int) -> None:
        if count < 0:
            raise ValueError("count must be nonnegative")
        if count > self._count:
            raise ValueError("count exceeds the ancestral slice queue length")


__all__ = [
    "AncestralSliceQueue",
    "AncestralSliceQueuePop",
    "AncestralSliceQueueSplit",
    "AncestralSliceQueueStatistics",
]
