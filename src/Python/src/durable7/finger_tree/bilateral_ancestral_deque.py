"""Persistent deque held as two oppositely oriented intervals of an append-only ancestry arena.

This is not a balanced sequence tree.  A version is a constant-sized handle over one shared
:class:`~durable7.finger_tree.incremental_ancestor.IncrementalAncestorArena`: it retains a *left*
interval ``(l_anchor, l_tail]`` and a *right* interval ``(r_anchor, r_tail]``, and its logical value
is ``reverse(left)`` followed by ``right``.  Adding at the front publishes one leaf below the left
tail; adding at the back publishes one below the right tail.  Reversal merely exchanges the two
intervals, so it costs O(1) and needs neither a lazy flag nor a rebuild.

The load-bearing property is *slice closure*: every contiguous slice meets each interval at most
once, so a slice is again a bilateral handle rather than a rebuilt tree.

Each interval also caches ``base``, the first physical node after ``anchor``.  That cache is
redundant but load-bearing: it keeps both public endpoint reads query-free even when the opposite
arm is empty.  For the left arm the logical *first* value sits at ``tail`` and the logical *last* at
``base``; for the right arm those roles are exchanged.  Those four handles are the deque's cached
endpoints -- visible indices ``0`` and ``left_count - 1``, and ``left_count`` and ``count - 1`` --
and every one of them is read with no level-ancestor query at all.

Bounds
------

Two layers of claim must not be conflated.

1. **The reduction.**  Let ``M`` be the number of nodes the arena has ever published, ``U(M)`` the
   cost of adding a leaf, and ``Q(M)`` the cost of one level-ancestor query.  Then
   :meth:`BilateralAncestralDeque.add_first` and :meth:`BilateralAncestralDeque.add_last` cost
   ``U(M)`` and make no query; :meth:`BilateralAncestralDeque.get_at` costs at most **one** query;
   :meth:`BilateralAncestralDeque.get_range`, :meth:`BilateralAncestralDeque.take`,
   :meth:`BilateralAncestralDeque.drop`, and :meth:`BilateralAncestralDeque.split_at` cost at most
   **two**, including the ranges that cross the centre and touch both arms; end removal makes **no**
   query on its own nonempty arm and at most **one** when it crosses the centre; and
   :meth:`BilateralAncestralDeque.validate_structure` costs two queries per nonempty interval, a
   ceiling of four.  Endpoint reads, :meth:`len`, :meth:`BilateralAncestralDeque.reverse`, and
   :meth:`BilateralAncestralDeque.clear` are O(1) and query-free.  Enumeration is Theta(count).

2. **The optimal backend.**  An Alstrup-Holm incremental level-ancestor backend has
   ``U(M) = Q(M) = O(1)`` worst case in linear space, which would make every scalar operation of
   this restricted algebra O(1) worst case.  **No such backend ships here**; that statement is a
   reduction to a published result, not a property of anything in this package.

Backed by the shipped
:class:`~durable7.finger_tree.incremental_ancestor.MyersIncrementalAncestorArena`, end insertion is
O(1) *amortized*, endpoint reads, :meth:`len`, reversal, and clearing are O(1) worst case, and
indexing, removal, slicing, splitting, and the structural audit are O(log M) worst case after M
historical insertions.  Nothing here upgrades an amortized or logarithmic bound to a worst-case
constant one.

Deliberate limits
-----------------

The algebra is restricted on purpose: add or remove at either end, read either end or an indexed
value, reverse, take/drop/slice/split, and enumerate front to back.  Arbitrary concatenation of
unrelated versions, point replacement, and middle editing are absent, and no bound above may be read
as covering an omitted operation.  Arena space is charged to *all* successful historical end
insertions, not to the values visible from one handle: a handle retains its arena, and the arena
retains every node it ever published.  Enumeration is Theta(count) time and materializes O(count)
temporary values, because the forward-oriented right interval is reversed through a buffer to keep
enumeration query-free.

There is no process-global empty value: an empty deque belongs to an arena, and retaining it retains
that arena.  Two handles may enumerate equal values while retaining different intervals or arenas,
so this type promises no O(1) sequence equality or hashing and implements neither; equality and
hashing stay by identity.

Port notes
----------

The managed reference raises ``OverflowException`` once the visible count or an ancestry depth would
pass ``Int32.MaxValue``.  Python integers are arbitrary precision and the arena publishes Python
integer depths, so neither ceiling can be reached; following the stated policy of
:mod:`~durable7.finger_tree.incremental_ancestor`, this port synthesizes no artificial limit to
imitate them.  Growth is bounded by memory, which the runtime reports as :exc:`MemoryError`.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .incremental_ancestor import IncrementalAncestorArena, MyersIncrementalAncestorArena

T = TypeVar("T")


@dataclass(frozen=True, slots=True)
class BilateralAncestralDequeInterval:
    """One oriented ancestry interval ``(anchor, tail]`` named by arena handles.

    ``count`` is the number of visible values, which equals ``depth(tail) - depth(anchor)``.
    ``base`` caches the first physical node after ``anchor``, so the endpoint at the anchor side of
    the interval is read without a level-ancestor query.  An empty interval sets all three handles
    to the same node, which is the node the interval is anchored at.

    This descriptor is immutable and is exposed so that diagnostics and tests can inspect -- or
    deliberately corrupt -- a handle's representation.  Normal use never constructs one.
    """

    anchor: int
    base: int
    tail: int
    count: int

    @staticmethod
    def empty_at(node: int) -> BilateralAncestralDequeInterval:
        """Return the empty interval whose three endpoints all name ``node``."""

        return BilateralAncestralDequeInterval(node, node, node, 0)


@dataclass(frozen=True, slots=True)
class BilateralAncestralDequeStatistics:
    """Interval and arena measurements returned by a successful structural audit.

    ``left_anchor_depth`` and ``right_anchor_depth`` are the absolute depths of the two interval
    anchors; the arena's bottom node reports ``-1``.  They are read with the arena's O(1) primitives
    and cost no level-ancestor query.
    """

    count: int
    left_count: int
    right_count: int
    left_anchor_depth: int
    right_anchor_depth: int
    arena_published_node_count: int


@dataclass(frozen=True, slots=True)
class BilateralAncestralDequePop(Generic[T]):
    """One removed endpoint value and the persistent deque that remains without it."""

    value: T
    rest: BilateralAncestralDeque[T]


@dataclass(frozen=True, slots=True)
class BilateralAncestralDequeSplit(Generic[T]):
    """The prefix and suffix produced by :meth:`BilateralAncestralDeque.split_at`.

    Together they enumerate the value that was split, and each side stays independently growable.
    """

    left: BilateralAncestralDeque[T]
    right: BilateralAncestralDeque[T]


@dataclass(frozen=True, slots=True, eq=False, repr=False)
class BilateralAncestralDeque(Generic[T]):
    """Immutable deque held as two oppositely oriented intervals of an append-only ancestry arena.

    A value retains its arena, the two interval descriptors, and a redundant total-count cache.
    Every operation returns a new handle and leaves this one -- and every other retained version --
    denoting exactly the same sequence forever, because the arena is append-only and its published
    nodes are immutable.  Growing an *old* handle starts a sibling branch rather than disturbing the
    handles derived from it, which is what makes this fully persistent for its restricted algebra;
    it is not confluently persistent, since two unrelated versions cannot be merged.

    Construct values through :meth:`create`, :meth:`create_myers`, or :meth:`from_iterable`; the
    generated constructor takes already-validated intervals and performs no checking.  Equality and
    hashing are by identity.

    See the module documentation for the representation, the query ceilings, the separation between
    the optimal-backend bounds and the shipped Myers bounds, and the omitted operations.
    """

    _arena: IncrementalAncestorArena[T]
    _left: BilateralAncestralDequeInterval
    _right: BilateralAncestralDequeInterval
    _count: int

    @classmethod
    def create(cls, arena: IncrementalAncestorArena[T]) -> BilateralAncestralDeque[T]:
        """Return an empty deque owned by an explicit incremental-ancestor arena.

        This is the extension seam: supplying a backend with different ``U(M)``/``Q(M)`` costs
        changes every parameterized bound the module documents.  Sharing one arena between
        independent deque families is allowed; the arena is append-only, so their nodes interleave
        harmlessly.  Raises :class:`ValueError` when the arena does not report depth ``-1`` for its
        bottom node, because the interval arithmetic anchors an empty deque there.  A rejected arena
        is never asked to publish anything.
        """

        bottom = arena.bottom
        if arena.depth_of(bottom) != -1:
            raise ValueError(
                "A bilateral ancestral deque arena must report depth -1 for its bottom node."
            )
        empty = BilateralAncestralDequeInterval.empty_at(bottom)
        return cls(arena, empty, empty, 0)

    @classmethod
    def create_myers(cls) -> BilateralAncestralDeque[T]:
        """Return an empty deque backed by a fresh, independently collectible Myers arena.

        There is deliberately no shared global empty value: each call owns its own arena, so
        unrelated workloads never share retained history.
        """

        arena: IncrementalAncestorArena[T] = MyersIncrementalAncestorArena()
        return cls.create(arena)

    @classmethod
    def from_iterable(cls, values: Iterable[T]) -> BilateralAncestralDeque[T]:
        """Return a Myers-backed deque holding ``values`` in enumeration order.

        Each value is added at the back, publishing one arena node.  An existing deque passed here
        is *not* returned unchanged: the result always owns a private arena and shares no retained
        history with its source.
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
        """Whether this handle denotes an empty deque.  This read is O(1)."""

        return self._count == 0

    @property
    def first(self) -> T:
        """The first visible value, read from a cached endpoint with no ancestor query.

        Raises :class:`IndexError` when the deque is empty.
        """

        self._ensure_not_empty()
        node = self._left.tail if self._left.count != 0 else self._right.base
        return self._arena.value_at(node)

    @property
    def last(self) -> T:
        """The last visible value, read from a cached endpoint with no ancestor query.

        Raises :class:`IndexError` when the deque is empty.
        """

        self._ensure_not_empty()
        node = self._right.tail if self._right.count != 0 else self._left.base
        return self._arena.value_at(node)

    def get_at(self, index: int) -> T:
        """The visible value at a nonnegative zero-based index.

        Costs at most one level-ancestor query ``Q(M)``.  The four cached endpoints -- index ``0``,
        the end of the left arm, the start of the right arm, and the last index -- are answered from
        retained handles with no query at all.  Raises :class:`IndexError` when the index is outside
        the deque.
        """

        if index < 0 or index >= self._count:
            raise IndexError("index is outside the bilateral ancestral deque")

        left = self._left
        if index < left.count:
            if index == 0:
                node = left.tail
            elif index == left.count - 1:
                node = left.base
            else:
                node = self._arena.ancestor_at_depth(
                    left.tail, self._arena.depth_of(left.tail) - index
                )
        else:
            right = self._right
            right_index = index - left.count
            if right_index == 0:
                node = right.base
            elif right_index == right.count - 1:
                node = right.tail
            else:
                node = self._arena.ancestor_at_depth(
                    right.tail, self._arena.depth_of(right.anchor) + right_index + 1
                )
        return self._arena.value_at(node)

    def __getitem__(self, index: int) -> T:
        """The visible value at ``index``, validated exactly as :meth:`get_at`."""

        return self.get_at(index)

    def add_first(self, value: T) -> BilateralAncestralDeque[T]:
        """Return a deque with ``value`` added at the front.

        Publishes exactly one arena leaf below the left interval's tail and makes no ancestor query,
        so it costs ``U(M)``.  This handle and every other retained version are unaffected.
        """

        left = self._add_to_tail(self._left, value)
        return BilateralAncestralDeque(self._arena, left, self._right, self._count + 1)

    def add_last(self, value: T) -> BilateralAncestralDeque[T]:
        """Return a deque with ``value`` added at the back.

        Publishes exactly one arena leaf below the right interval's tail and makes no ancestor
        query, so it costs ``U(M)``.  This handle and every other retained version are unaffected.
        """

        right = self._add_to_tail(self._right, value)
        return BilateralAncestralDeque(self._arena, self._left, right, self._count + 1)

    def remove_first(self) -> BilateralAncestralDeque[T]:
        """Return the deque without its first visible value.

        Makes no ancestor query while the left arm is nonempty, because only that arm's tail moves
        to its parent.  A removal that crosses the centre into the right arm advances that arm's
        anchor to its cached base and costs at most one query, which selects the next cached base.
        Raises :class:`IndexError` when the deque is empty.
        """

        self._ensure_not_empty()
        if self._count == 1:
            return self._empty_handle()
        if self._left.count != 0:
            left = self._remove_tail(self._left)
            return BilateralAncestralDeque(self._arena, left, self._right, self._count - 1)
        right = self._remove_base(self._right)
        return BilateralAncestralDeque(self._arena, self._left, right, self._count - 1)

    def remove_last(self) -> BilateralAncestralDeque[T]:
        """Return the deque without its last visible value.

        Makes no ancestor query while the right arm is nonempty, and at most one when the removal
        crosses the centre into the left arm.  Raises :class:`IndexError` when the deque is empty.
        """

        self._ensure_not_empty()
        if self._count == 1:
            return self._empty_handle()
        if self._right.count != 0:
            right = self._remove_tail(self._right)
            return BilateralAncestralDeque(self._arena, self._left, right, self._count - 1)
        left = self._remove_base(self._left)
        return BilateralAncestralDeque(self._arena, left, self._right, self._count - 1)

    def try_remove_first(self) -> BilateralAncestralDequePop[T] | None:
        """The first visible value with the remainder, or ``None`` when the deque is empty.

        Nothing is published on the empty path and this handle stays valid either way.  A stored
        ``None`` value is a present value: absence is reported only by the returned ``None`` result.
        """

        if self._count == 0:
            return None
        return BilateralAncestralDequePop(self.first, self.remove_first())

    def try_remove_last(self) -> BilateralAncestralDequePop[T] | None:
        """The last visible value with the remainder, or ``None`` when the deque is empty."""

        if self._count == 0:
            return None
        return BilateralAncestralDequePop(self.last, self.remove_last())

    def take(self, count: int) -> BilateralAncestralDeque[T]:
        """Return the first ``count`` visible values as a growable bilateral slice.

        A prefix is a slice, so it costs at most two ancestor queries; ``take(len(deque))`` returns
        this handle unchanged and makes none.  Raises :class:`ValueError` when ``count`` is negative
        or exceeds the visible length.
        """

        self._check_count(count)
        return self if count == self._count else self.get_range(0, count)

    def drop(self, count: int) -> BilateralAncestralDeque[T]:
        """Return the deque after omitting its first ``count`` visible values.

        A suffix is a slice, so it costs at most two ancestor queries; ``drop(0)`` returns this
        handle unchanged and makes none.  Raises :class:`ValueError` when ``count`` is negative or
        exceeds the visible length.  This is the reference contract's ``Drop``.
        """

        self._check_count(count)
        return self if count == 0 else self.get_range(count, self._count - count)

    def get_range(self, index: int, count: int) -> BilateralAncestralDeque[T]:
        """Return one contiguous, growable bilateral slice.  This is the reference ``Slice``.

        Every contiguous range meets each interval at most once, so the result is again a bilateral
        handle and the operation costs **at most two** ancestor queries no matter how many values it
        selects -- including a range that crosses the centre, where the left part is necessarily a
        suffix of the left arm and the right part a prefix of the right arm, so each side spends at
        most one query.  The whole range returns this handle unchanged and an empty range returns a
        fresh empty handle; neither makes a query.

        Raises :class:`IndexError` when ``index`` is outside ``0..len(deque)`` and
        :class:`ValueError` when ``count`` is negative or runs past the end.  The count is validated
        as ``count > len(deque) - index``, so an index plus count that would run away is rejected
        rather than wrapped.
        """

        self._check_boundary(index)
        if count < 0:
            raise ValueError("count must be nonnegative")
        if count > self._count - index:
            raise ValueError("the range extends past the end of the bilateral ancestral deque")
        if index == 0 and count == self._count:
            return self
        if count == 0:
            return self._empty_handle()

        end = index + count
        left_count = self._left.count
        left_start = min(index, left_count)
        left_end = min(end, left_count)
        right_start = max(0, index - left_count)
        right_end = max(0, end - left_count)

        left = self._slice_left(self._left, left_start, left_end - left_start)
        right = self._slice_right(self._right, right_start, right_end - right_start)
        return BilateralAncestralDeque(self._arena, left, right, count)

    def split_at(self, index: int) -> BilateralAncestralDequeSplit[T]:
        """Split this deque at a zero-based boundary into a growable prefix and suffix.

        Although it is expressed as a prefix plus a suffix, the pair costs at most two ancestor
        queries in total: a prefix that stops inside an arm spends one and its complementary suffix
        spends one.  Both endpoint boundaries return values sharing this handle's storage and make
        no query.  Raises :class:`IndexError` when the boundary is outside ``0..len(deque)``.
        """

        self._check_boundary(index)
        return BilateralAncestralDequeSplit(self.take(index), self.drop(index))

    def reverse(self) -> BilateralAncestralDeque[T]:
        """Return the logical reversal by exchanging the two oriented intervals.

        This is O(1) and query-free: no node is published, copied, or navigated, and no lazy flag is
        recorded.  A deque of at most one value is its own reversal and returns a value sharing this
        handle's storage.
        """

        if self._count <= 1:
            return self
        return BilateralAncestralDeque(self._arena, self._right, self._left, self._count)

    def clear(self) -> BilateralAncestralDeque[T]:
        """Return an empty, independently growable handle in the same arena.

        This is O(1) and query-free; clearing an already-empty deque returns a value sharing this
        handle's storage.  Nothing is reclaimed: the arena keeps every node it ever published.
        """

        if self._count == 0:
            return self
        return self._empty_handle()

    def to_list(self) -> list[T]:
        """The visible values in front-to-back order.

        Follows parent links rather than performing ``len(deque)`` ancestor queries, so it takes
        Theta(count) time and makes no query.  The forward-oriented right interval is walked from
        its tail and reversed through a buffer, which is why enumeration retains O(count) temporary
        values.  A concurrent addition cannot enter the captured paths.
        """

        values: list[T] = []
        node = self._left.tail
        for remaining in range(self._left.count, 0, -1):
            values.append(self._arena.value_at(node))
            if remaining != 1:
                node = self._arena.parent_of(node)

        right_values: list[T] = []
        node = self._right.tail
        for remaining in range(self._right.count, 0, -1):
            right_values.append(self._arena.value_at(node))
            if remaining != 1:
                node = self._arena.parent_of(node)
        right_values.reverse()
        values.extend(right_values)
        return values

    def __iter__(self) -> Iterator[T]:
        """Iterate the visible values front to back.

        Both ancestry paths are captured up front, so the iterator observes exactly the handle it
        was obtained from and never sees a later branch.
        """

        return iter(self.to_list())

    def __repr__(self) -> str:
        """Render the visible values, matching how the sibling ports format this type."""

        return f"BilateralAncestralDeque({self.to_list()!r})"

    @property
    def arena(self) -> IncrementalAncestorArena[T]:
        """The shared arena this handle navigates.  Exposed for diagnostics, not for mutation."""

        return self._arena

    @property
    def left_interval(self) -> BilateralAncestralDequeInterval:
        """The reversed left interval, whose tail holds the first visible value."""

        return self._left

    @property
    def right_interval(self) -> BilateralAncestralDequeInterval:
        """The forward right interval, whose tail holds the last visible value."""

        return self._right

    def shares_arena_with(self, other: BilateralAncestralDeque[T]) -> bool:
        """Whether both handles navigate the same arena, so their histories can interleave."""

        return self._arena is other._arena

    def shares_storage_with(self, other: BilateralAncestralDeque[T]) -> bool:
        """Whether both handles denote the same arena nodes through identical intervals.

        A representation test, not an equality test.  The reference asserts reference identity for
        the operations that return their receiver; a Python handle produced by a no-op is literally
        ``self``, and this predicate additionally recognizes a handle rebuilt with the same cached
        interval descriptors as sharing exactly the same storage.
        """

        return (
            self._arena is other._arena
            and self._left == other._left
            and self._right == other._right
            and self._count == other._count
        )

    def validate_structure(self) -> BilateralAncestralDequeStatistics:
        """Audit the cached interval endpoints and counts, and report what was measured.

        Checks that the total count agrees with the two interval counts, that an empty interval has
        one shared endpoint, that each nonempty interval's count matches its endpoint depths, and
        that its cached base really is the first node after its anchor on the tail's ancestry path.
        Confirming that path costs exactly **two** ancestor queries per nonempty interval, a ceiling
        of four, so the audit is ``O(1 + Q(M))`` rather than O(1) work; an empty handle spends none.
        Ports the reference's internal ``ValidateInvariants``.  Raises :class:`ValueError` on the
        first violation.  A defensive audit, not part of normal use.
        """

        if self._count < 0 or self._left.count < 0 or self._right.count < 0:
            raise ValueError("A bilateral ancestral deque count is negative.")
        if self._count != self._left.count + self._right.count:
            raise ValueError("The total count disagrees with the interval counts.")

        left_anchor_depth = self._validate_interval(self._left)
        right_anchor_depth = self._validate_interval(self._right)
        return BilateralAncestralDequeStatistics(
            self._count,
            self._left.count,
            self._right.count,
            left_anchor_depth,
            right_anchor_depth,
            self._arena.published_node_count,
        )

    def _validate_interval(self, segment: BilateralAncestralDequeInterval) -> int:
        """Check one interval and return its anchor depth, spending two queries when nonempty."""

        if segment.count == 0:
            if segment.anchor != segment.base or segment.base != segment.tail:
                raise ValueError("An empty interval does not have one shared endpoint.")
            return self._arena.depth_of(segment.anchor)

        anchor_depth = self._arena.depth_of(segment.anchor)
        tail_depth = self._arena.depth_of(segment.tail)
        if tail_depth - anchor_depth != segment.count:
            raise ValueError("An interval count disagrees with its endpoint depths.")
        if self._arena.parent_of(segment.base) != segment.anchor:
            raise ValueError("The cached base is not the first node after the anchor.")
        if self._arena.ancestor_at_depth(segment.tail, anchor_depth) != segment.anchor:
            raise ValueError("The interval anchor is not an ancestor of its tail.")
        if self._arena.ancestor_at_depth(segment.tail, anchor_depth + 1) != segment.base:
            raise ValueError("The cached base is not on the tail's ancestry path.")
        return anchor_depth

    def _empty_handle(self) -> BilateralAncestralDeque[T]:
        """Return an empty handle anchored at this arena's bottom node."""

        empty = BilateralAncestralDequeInterval.empty_at(self._arena.bottom)
        return BilateralAncestralDeque(self._arena, empty, empty, 0)

    def _add_to_tail(
        self, segment: BilateralAncestralDequeInterval, value: T
    ) -> BilateralAncestralDequeInterval:
        """Publish one leaf below an interval's tail and extend the interval by one value."""

        tail = self._arena.add_leaf(segment.tail, value)
        base = tail if segment.count == 0 else segment.base
        return BilateralAncestralDequeInterval(segment.anchor, base, tail, segment.count + 1)

    def _remove_tail(
        self, segment: BilateralAncestralDequeInterval
    ) -> BilateralAncestralDequeInterval:
        """Drop an interval's newest label by moving its tail to the parent.  No queries."""

        if segment.count == 0:
            raise AssertionError("An empty interval has no tail label to drop.")
        tail = self._arena.parent_of(segment.tail)
        if segment.count == 1:
            return BilateralAncestralDequeInterval.empty_at(tail)
        return BilateralAncestralDequeInterval(
            segment.anchor, segment.base, tail, segment.count - 1
        )

    def _remove_base(
        self, segment: BilateralAncestralDequeInterval
    ) -> BilateralAncestralDequeInterval:
        """Drop an interval's oldest label by advancing its anchor to the cached base.

        Costs at most one ancestor query, which selects the next cached base.  A two-value interval
        needs no query because the surviving value is already the retained tail.
        """

        if segment.count == 0:
            raise AssertionError("An empty interval has no base label to drop.")
        if segment.count == 1:
            return BilateralAncestralDequeInterval.empty_at(segment.tail)

        base = (
            segment.tail
            if segment.count == 2
            else self._arena.ancestor_at_depth(
                segment.tail, self._arena.depth_of(segment.tail) - segment.count + 2
            )
        )
        return BilateralAncestralDequeInterval(segment.base, base, segment.tail, segment.count - 1)

    def _slice_left(
        self, segment: BilateralAncestralDequeInterval, start: int, count: int
    ) -> BilateralAncestralDequeInterval:
        """Select the sub-interval ``[start, start + count)`` of a reversed left arm.

        The reversed reading order means ``start`` counts down from the tail.  At most two queries,
        and fewer whenever a cached endpoint already answers.
        """

        if start < 0 or count < 0 or start > segment.count - count:
            raise AssertionError("A left sub-interval must lie inside its interval.")
        if count == 0:
            return BilateralAncestralDequeInterval.empty_at(self._arena.bottom)
        if start == 0 and count == segment.count:
            return segment

        tail_depth = self._arena.depth_of(segment.tail)
        base_depth = tail_depth - segment.count + 1
        sliced_tail_depth = tail_depth - start
        sliced_base_depth = sliced_tail_depth - count + 1

        tail = (
            segment.tail
            if sliced_tail_depth == tail_depth
            else self._arena.ancestor_at_depth(segment.tail, sliced_tail_depth)
        )
        if sliced_base_depth == sliced_tail_depth:
            base = tail
        elif sliced_base_depth == base_depth:
            base = segment.base
        else:
            base = self._arena.ancestor_at_depth(segment.tail, sliced_base_depth)

        anchor = segment.anchor if base == segment.base else self._arena.parent_of(base)
        return BilateralAncestralDequeInterval(anchor, base, tail, count)

    def _slice_right(
        self, segment: BilateralAncestralDequeInterval, start: int, count: int
    ) -> BilateralAncestralDequeInterval:
        """Select the sub-interval ``[start, start + count)`` of a forward right arm.

        At most two queries, and fewer whenever a cached endpoint already answers.
        """

        if start < 0 or count < 0 or start > segment.count - count:
            raise AssertionError("A right sub-interval must lie inside its interval.")
        if count == 0:
            return BilateralAncestralDequeInterval.empty_at(self._arena.bottom)
        if start == 0 and count == segment.count:
            return segment

        anchor_depth = self._arena.depth_of(segment.anchor)
        sliced_base_depth = anchor_depth + start + 1
        sliced_tail_depth = sliced_base_depth + count - 1
        end = start + count

        base = (
            segment.base
            if start == 0
            else self._arena.ancestor_at_depth(segment.tail, sliced_base_depth)
        )
        if sliced_tail_depth == sliced_base_depth:
            tail = base
        elif end == segment.count:
            tail = segment.tail
        else:
            tail = self._arena.ancestor_at_depth(segment.tail, sliced_tail_depth)

        anchor = segment.anchor if start == 0 else self._arena.parent_of(base)
        return BilateralAncestralDequeInterval(anchor, base, tail, count)

    def _ensure_not_empty(self) -> None:
        if self._count == 0:
            raise IndexError("The bilateral ancestral deque is empty.")

    def _check_boundary(self, index: int) -> None:
        if index < 0 or index > self._count:
            raise IndexError("index is outside the bilateral ancestral deque boundary range")

    def _check_count(self, count: int) -> None:
        if count < 0:
            raise ValueError("count must be nonnegative")
        if count > self._count:
            raise ValueError("count exceeds the bilateral ancestral deque length")


__all__ = [
    "BilateralAncestralDeque",
    "BilateralAncestralDequeInterval",
    "BilateralAncestralDequePop",
    "BilateralAncestralDequeSplit",
    "BilateralAncestralDequeStatistics",
]
