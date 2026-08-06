"""Persistent sequence that ranks and selects events whose occurrence depends on left context.

An ordinary sum-measured sequence can rank elements that carry a fixed local weight.  It cannot
rank a comma that is a field separator in one prefix context and quoted data in another, because no
single weight is correct for both.  A streaming automaton keeps that context but has to replay a
prefix after every edit.

:class:`ContextualRankSequence` resolves this by lifting a finite deterministic machine into the
measure of a persistent measured sequence.  Every subtree caches, *for every possible incoming
state*, the outgoing state and the number of events emitted while consuming that subtree.  Ordered
composition feeds the left subtree's outgoing state into the right subtree's summary, which turns a
context-dependent scan into an associative measure::

    (A * B).next(q)   = B.next(A.next(q))
    (A * B).events(q) = A.events(q) + B.events(A.next(q))
    (A * B).count     = A.count + B.count

Composition is associative but **not** commutative: ``A * B`` and ``B * A`` generally disagree in
both components, so measures are only ever combined left to right.  The identity maps every state
to itself, emits zero events, and holds zero elements.  Because event counts are nonnegative, the
select predicate ``prefix.events(q0) > r`` is monotone, so one measure-directed descent finds the
element that emitted event ``r`` - no binary search and no prefix replay.

Bounds
------

Let ``n`` be the element count and ``s`` the machine's fixed state count.  Whole-sequence
evaluation is O(1): it reads the cached root summary.  Indexing is O(log n) and composes no
summary.  Prefix evaluation, contextual event rank and select, insertion, replacement, removal,
splitting, and slicing are O(s log n): a measure-directed descent over O(log n) levels, each level
composing one O(s) effect table.  Enumeration is O(n) and building from a source is O(s n).

Endpoint updates are **O(s) amortized** (O(s log n) worst case per call), matching the C#
reference.  This workspace's :class:`~durable7.finger_tree.measured_sequence.MeasuredSequence` is
a lazy Hinze-Paterson finger tree - digits at both ends, a memoized suspended middle - so an
endpoint push performs amortized O(1) node work, each node composing one O(s) effect table, and
the amortization is valid under persistent branching because forced suspensions memoize into
shared cells.  (An earlier revision of this workspace sat on an implicit AVL join tree and
honestly documented Theta(s log n) here; the substrate upgrade removed that divergence.)

Concatenation is **O(s log(min(n, m))) amortized**, the reference bound: the substrate suspends
its middle recursion and regroups only the boundary digits eagerly.

Each element stores its own O(s) effect row and every internal node stores one more, so the
structure costs O(s n) summary space rather than O(n).  When the machine is fixed, ``s`` is a
constant and the contextual queries are O(log n) instead of the Theta(n) replay a state-free
persistent sequence needs.  If ``s`` is treated as an input rather than a fixed policy, the
structure does not dominate a plain persistent sequence: its edits and its storage explicitly
carry the ``s`` factor.

Deliberate limits
-----------------

Context must be finite and must flow left to right.  Event weights must be nonnegative so select
stays monotone.  One input element occupies one measured leaf, so this is not a chunked text rope
and makes no cache-density claim.  There is no way to change the machine of an existing sequence;
rebuilding under another machine costs O(s n).
"""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Any, Generic, Protocol, TypeVar, cast

from .measured_sequence import MeasuredSequence
from .measures import MeasurePolicy

T = TypeVar("T")
T_contra = TypeVar("T_contra", contravariant=True)

_INT32_MAX = 2_147_483_647
_INT64_MAX = (1 << 63) - 1


@dataclass(frozen=True, slots=True)
class ContextualEventTransition:
    """One transition of a deterministic additive event machine.

    ``event_count`` is the nonnegative number of logical events the transition emits; a single
    transition may emit more than one.
    """

    next_state: int
    event_count: int


class ContextualEventMachine(Protocol[T_contra]):
    """Finite deterministic machine whose transitions emit nonnegative event counts.

    A machine is a retained *value*, not a type parameter: the sequence holds the exact object it
    was built with, so an empty result stays usable and two sequences can be compared for
    compatibility by identity.  :attr:`state_count` must be positive and must not change while any
    sequence built from the machine is alive.  For every state in ``0..state_count - 1``,
    :meth:`transition` must return a state in the same range and a nonnegative event count, and it
    must be deterministic and free of side effects.  Violating any of those is a policy bug and
    raises :class:`ValueError`.
    """

    @property
    def state_count(self) -> int:
        """The finite number of machine states.  Must be greater than zero."""
        ...

    def transition(self, state: int, element: T_contra) -> ContextualEventTransition:
        """Consume one element from one valid state, reporting the outgoing state and count."""
        ...


@dataclass(frozen=True, slots=True)
class _FunctionalContextualEventMachine(Generic[T]):
    state_count: int
    _transition: Callable[[int, T], ContextualEventTransition]

    def transition(self, state: int, element: T) -> ContextualEventTransition:
        """Delegate to the caller-supplied transition function."""

        return self._transition(state, element)


def create_contextual_event_machine(
    state_count: int,
    transition: Callable[[int, T], ContextualEventTransition],
) -> ContextualEventMachine[T]:
    """Build a contextual event machine from a plain function instead of a class.

    The caller is responsible for the machine contract: ``state_count`` must be positive and
    ``transition`` must stay inside ``0..state_count - 1`` and emit nonnegative counts, since
    contextual rank and select rely on both.
    """

    return _FunctionalContextualEventMachine(state_count, transition)


@dataclass(frozen=True, slots=True)
class ContextualPrefixSummary:
    """The result of running a contextual event machine over a prefix."""

    final_state: int
    event_count: int


@dataclass(frozen=True, slots=True)
class ContextualEventLocation:
    """Identifies one contextual event and the input element that emitted it."""

    element_index: int
    event_index_in_element: int
    state_before: int
    state_after: int
    element_event_count: int


@dataclass(frozen=True, slots=True)
class ContextualRankSequenceSplit(Generic[T]):
    """The two sequences produced by a split; both share structure with the original."""

    left: ContextualRankSequence[T]
    right: ContextualRankSequence[T]


@dataclass(frozen=True, slots=True)
class ContextualRankSequenceStatistics:
    """Measurements returned by a successful structural audit, all recomputed by direct scan."""

    count: int
    state_count: int
    maximum_total_event_count: int


@dataclass(frozen=True, slots=True)
class _ContextualSummary:
    """A subtree summary: one outgoing state and event count per machine state, plus the count.

    The identity carries empty rows rather than a width-``s`` table, because the identity of the
    lifted monoid is the identity function on states with zero events for *every* machine.  That
    is what lets one shared measure policy serve every machine; :meth:`final_state` and
    :meth:`event_count` read the width-independent answer whenever ``count`` is zero.
    """

    count: int
    next_states: tuple[int, ...]
    event_counts: tuple[int, ...]

    def final_state(self, initial_state: int) -> int:
        """The state this subtree leaves the machine in, entered from ``initial_state``."""

        return initial_state if self.count == 0 else self.next_states[initial_state]

    def event_count(self, initial_state: int) -> int:
        """The number of events this subtree emits, entered from ``initial_state``."""

        return 0 if self.count == 0 else self.event_counts[initial_state]

    def evaluate(self, initial_state: int) -> ContextualPrefixSummary:
        """Both components at once, as one summary value."""

        return ContextualPrefixSummary(
            self.final_state(initial_state), self.event_count(initial_state)
        )


_IDENTITY_SUMMARY = _ContextualSummary(0, (), ())


def _combine_summaries(left: _ContextualSummary, right: _ContextualSummary) -> _ContextualSummary:
    """Compose two subtree summaries in sequence order, ``left`` preceding ``right``.

    Directional: the left summary's outgoing state selects the right summary's row, so swapping
    the operands generally changes both the outgoing state and the event count.
    """

    if left.count == 0:
        return right
    if right.count == 0:
        return left
    count = left.count + right.count
    if count > _INT32_MAX:
        raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")
    right_states = right.next_states
    right_events = right.event_counts
    next_states = tuple(right_states[middle] for middle in left.next_states)
    event_counts = tuple(
        events + right_events[middle]
        for events, middle in zip(left.event_counts, left.next_states, strict=True)
    )
    if max(event_counts) > _INT64_MAX:
        raise OverflowError("The composed contextual event count exceeds the signed 64-bit range.")
    return _ContextualSummary(count, next_states, event_counts)


@dataclass(frozen=True, slots=True)
class _Entry(Generic[T]):
    """One stored element together with its eagerly computed effect row.

    Wrapping the element does three things at once: the machine runs once per element rather than
    once per level of every later descent, the row is computed before any node is built so a
    rejected edit publishes nothing, and a located entry is never ``None``, which keeps a stored
    ``None`` element unambiguous in the substrate's locate result.
    """

    element: T
    summary: _ContextualSummary


class _ContextualMeasure:
    """Measure policy over pre-summarized entries.

    It never calls the machine: the effect row is computed once, when the entry is created, so a
    measure-directed descent composes cached tables only.  Because the summary carries its own
    width and the identity is width-independent, one instance serves every machine, which is what
    keeps the substrate's policy-identity check from partitioning sequences by lineage.
    """

    identity: _ContextualSummary = _IDENTITY_SUMMARY

    def combine(self, left: _ContextualSummary, right: _ContextualSummary) -> _ContextualSummary:
        """Compose two summaries in sequence order."""

        return _combine_summaries(left, right)

    def measure(self, element: _Entry[Any]) -> _ContextualSummary:
        """An entry measures as the row it already carries."""

        return element.summary


_SHARED_MEASURE = _ContextualMeasure()


def _summary_policy() -> MeasurePolicy[_Entry[T], _ContextualSummary]:
    """The single shared measure policy, viewed at one element type."""

    return cast(MeasurePolicy[_Entry[T], _ContextualSummary], _SHARED_MEASURE)


def _validated_state_count(machine: ContextualEventMachine[T]) -> int:
    """Read the machine's state count, rejecting a machine that declares none."""

    state_count = machine.state_count
    if state_count <= 0:
        raise ValueError("A contextual event machine must have at least one state.")
    return state_count


def _make_entry(machine: ContextualEventMachine[T], state_count: int, element: T) -> _Entry[T]:
    """Run the machine once from every incoming state, eagerly building the element's effect row.

    Every check happens here, before any tree node is built, which is what makes a rejected edit
    publish nothing and leave every retained version untouched.
    """

    next_states: list[int] = []
    event_counts: list[int] = []
    for state in range(state_count):
        transition = machine.transition(state, element)
        next_state = transition.next_state
        event_count = transition.event_count
        if next_state < 0 or next_state >= state_count:
            raise ValueError("The contextual event machine returned an invalid next state.")
        if event_count < 0:
            raise ValueError("The contextual event machine returned a negative event count.")
        if event_count > _INT64_MAX:
            raise OverflowError("A contextual event count exceeds the signed 64-bit range.")
        next_states.append(next_state)
        event_counts.append(event_count)
    return _Entry(element, _ContextualSummary(1, tuple(next_states), tuple(event_counts)))


class ContextualRankSequence(Generic[T]):
    """Immutable sequence indexed by events whose occurrence depends on finite left context."""

    __slots__ = ("_entries", "_machine", "_state_count")

    def __init__(
        self,
        entries: MeasuredSequence[_Entry[T], _ContextualSummary],
        machine: ContextualEventMachine[T],
        state_count: int,
    ) -> None:
        """Wrap an already-built representation; use :meth:`empty` or :meth:`from_iterable`
        instead.
        """

        self._entries = entries
        self._machine = machine
        self._state_count = state_count

    @classmethod
    def empty(cls, machine: ContextualEventMachine[T]) -> ContextualRankSequence[T]:
        """Return the empty sequence over ``machine``.

        The machine is retained by identity, and only sequences retaining that exact object may be
        concatenated.  Its summary maps every state to itself with zero events, so an empty result
        of a split or a range stays a usable sequence rather than a degenerate one.
        """

        state_count = _validated_state_count(machine)
        return cls(MeasuredSequence.empty(_summary_policy()), machine, state_count)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], machine: ContextualEventMachine[T]
    ) -> ContextualRankSequence[T]:
        """Build a sequence from ``values`` in input order, in one balanced pass.  O(s n).

        Passing a sequence that already retains this exact machine returns it unchanged.
        """

        if type(values) is cls and values._machine is machine:
            return values
        state_count = _validated_state_count(machine)
        entries = [_make_entry(machine, state_count, value) for value in values]
        if len(entries) > _INT32_MAX:
            raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")
        return cls(MeasuredSequence.from_iterable(entries, _summary_policy()), machine, state_count)

    @property
    def machine(self) -> ContextualEventMachine[T]:
        """The retained machine.  Only sequences retaining that exact object may be concatenated."""

        return self._machine

    @property
    def state_count(self) -> int:
        """The machine's finite state count, read once when the sequence lineage was created."""

        return self._state_count

    def __len__(self) -> int:
        """Number of stored elements, read from the cached subtree size."""

        return len(self._entries)

    @property
    def is_empty(self) -> bool:
        """Whether the sequence holds no elements."""

        return self._entries.is_empty

    def get_at(self, index: int) -> T:
        """The element at a zero-based ``index``.

        O(log n): a descent by cached subtree sizes that composes no summary and calls the machine
        not at all.  Raises :class:`IndexError` when ``index`` is outside the sequence.
        """

        self._check_element_index(index)
        entry = self._entries.at(index)
        if entry is None:
            raise AssertionError("A validated element lookup found no entry.")
        return entry.element

    def __getitem__(self, index: int) -> T:
        """The element at ``index``, raising for a non-integer or out-of-range index."""

        if not isinstance(index, int):
            raise TypeError("contextual rank sequence indices must be integers")
        return self.get_at(index)

    def evaluate(self, initial_state: int) -> ContextualPrefixSummary | None:
        """Run the machine over the whole sequence from ``initial_state``.

        O(1): the answer is the cached root summary.  Returns ``None`` when ``initial_state`` is
        outside ``0..state_count - 1``.
        """

        if not self._is_valid_state(initial_state):
            return None
        return self._entries.measure.evaluate(initial_state)

    def evaluate_prefix(
        self, element_count: int, initial_state: int
    ) -> ContextualPrefixSummary | None:
        """Run the machine over the first ``element_count`` elements from ``initial_state``.

        O(s log n), and O(1) at either endpoint, where the answer is either the identity or the
        cached root summary.  Raises :class:`IndexError` when ``element_count`` falls outside
        ``0..len(self)``, and returns ``None`` when ``initial_state`` is outside the machine range.
        """

        self._check_boundary_index(element_count)
        if not self._is_valid_state(initial_state):
            return None
        if element_count == 0:
            return ContextualPrefixSummary(initial_state, 0)
        if element_count == len(self):
            return self._entries.measure.evaluate(initial_state)
        measure = self._entries.prefix_measure(element_count)
        if measure is None:
            raise AssertionError("A validated prefix length has no measure.")
        return measure.evaluate(initial_state)

    def event_rank(self, element_count: int, initial_state: int) -> int | None:
        """The number of contextual events strictly before an element boundary.

        Fails on exactly the inputs :meth:`evaluate_prefix` fails on.  O(s log n).
        """

        summary = self.evaluate_prefix(element_count, initial_state)
        return None if summary is None else summary.event_count

    def try_select_event(
        self, event_index: int, initial_state: int
    ) -> ContextualEventLocation | None:
        """Locate the zero-based contextual event ``event_index``, counting from ``initial_state``.

        Because event counts are nonnegative, ``prefix.events(q0) > r`` is monotone, so this is a
        single measure-directed descent: no binary search over boundaries and no prefix replay.
        The located element's own effect row is already cached, so the machine is not called at
        all.  O(s log n).

        Returns ``None`` when ``initial_state`` is outside the machine range, or when
        ``event_index`` is negative or at or past the sequence's total event count.
        """

        if not self._is_valid_state(initial_state):
            return None
        if event_index < 0 or event_index >= self._entries.measure.event_count(initial_state):
            return None
        located = self._entries.locate(
            lambda summary: summary.event_count(initial_state) > event_index
        )
        if not located.found or located.value is None:
            raise AssertionError("The contextual event summary is inconsistent.")
        before = located.measure_before
        state_before = before.final_state(initial_state)
        row = located.value.summary
        return ContextualEventLocation(
            located.index,
            event_index - before.event_count(initial_state),
            state_before,
            row.final_state(state_before),
            row.event_count(state_before),
        )

    def prepend(self, item: T) -> ContextualRankSequence[T]:
        """Return a sequence with ``item`` added at the front.

        O(s) amortized, O(s log n) worst case per call: see the module docstring.
        """

        self._check_insertion_capacity()
        return self._wrap(self._entries.prepend(self._entry(item)))

    def append(self, item: T) -> ContextualRankSequence[T]:
        """Return a sequence with ``item`` added at the back.  O(s) amortized, as :meth:`prepend`
        is.
        """

        self._check_insertion_capacity()
        return self._wrap(self._entries.append(self._entry(item)))

    def concat(self, other: ContextualRankSequence[T]) -> ContextualRankSequence[T]:
        """Return this sequence's elements followed by ``other``'s.

        Both sequences must retain the same machine object, since their effect rows would
        otherwise be tables of different automata - and possibly different widths.  A mismatch
        raises :class:`TypeError`, following the algebra-identity precedent of
        :class:`~durable7.finger_tree.range_update_sequence.RangeUpdateSequence`.  An empty
        operand is not copied: the result is the other operand itself.

        O(s log(min(n, m))) amortized, the reference bound: the substrate suspends its middle
        recursion, so the descent terminates at the shallower operand rather than tracking the
        operands' height difference.
        """

        if not isinstance(other, ContextualRankSequence):
            raise TypeError("other must be a ContextualRankSequence")
        if self._machine is not other._machine:
            raise TypeError("Sequences must retain the same contextual event machine object.")
        if len(other) > _INT32_MAX - len(self):
            raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")
        if other.is_empty:
            return self
        if self.is_empty:
            return other
        return self._wrap(self._entries.concat(other._entries))

    def insert(self, index: int, item: T) -> ContextualRankSequence[T]:
        """Return a sequence with ``item`` inserted so that ``index`` old elements precede it.

        O(s log n).  Raises :class:`IndexError` when ``index`` falls outside ``0..len(self)``.
        """

        self._check_boundary_index(index)
        self._check_insertion_capacity()
        entries = self._entries.insert_at(index, self._entry(item))
        if entries is None:
            raise AssertionError("A validated insertion boundary was rejected.")
        return self._wrap(entries)

    def set_item(self, index: int, item: T) -> ContextualRankSequence[T]:
        """Return a sequence with the element at ``index`` replaced.  O(s log n)."""

        self._check_element_index(index)
        entries = self._entries.set_at(index, self._entry(item))
        if entries is None:
            raise AssertionError("A validated replacement index was rejected.")
        return self._wrap(entries)

    def remove_at(self, index: int) -> ContextualRankSequence[T]:
        """Return a sequence without the element at ``index``.  O(s log n)."""

        self._check_element_index(index)
        entries = self._entries.remove_at(index)
        if entries is None:
            raise AssertionError("A validated removal index was rejected.")
        return self._wrap(entries)

    def split_at(self, index: int) -> ContextualRankSequenceSplit[T]:
        """Split into the elements before ``index`` and those from ``index`` on.

        O(s log n).  Splitting at either endpoint shares the receiver's root with the nonempty
        half.  Raises :class:`IndexError` when ``index`` falls outside ``0..len(self)``.
        """

        self._check_boundary_index(index)
        split = self._entries.split_at(index)
        if split is None:
            raise AssertionError("A validated split boundary was rejected.")
        return ContextualRankSequenceSplit(self._wrap(split.left), self._wrap(split.right))

    def get_range(self, index: int, count: int) -> ContextualRankSequence[T]:
        """Return the ``count`` elements starting at ``index`` as a new sequence.  O(s log n).

        A range covering the whole sequence returns the receiver.  Raises :class:`IndexError` for
        a negative or past-the-end ``index`` and :class:`ValueError` for a negative ``count`` or
        one that runs past the end.
        """

        self._check_range(index, count)
        if count == 0:
            return self._empty()
        if count == len(self):
            return self
        head = self._entries.split_at(index)
        if head is None:
            raise AssertionError("A validated range start was rejected.")
        tail = head.right.split_at(count)
        if tail is None:
            raise AssertionError("A validated range length was rejected.")
        return self._wrap(tail.left)

    def to_list(self) -> list[T]:
        """Copy the elements into a list, in sequence order.  O(n)."""

        return [entry.element for entry in self._entries]

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in sequence order."""

        return (entry.element for entry in self._entries)

    def shares_storage_with(self, other: ContextualRankSequence[T]) -> bool:
        """Whether both versions share any node by object identity.

        A representation test used to confirm that a no-op avoided copying, not an equality test.
        """

        return self._entries.shares_structure_with(other._entries)

    def validate_structure(self) -> ContextualRankSequenceStatistics:
        """Recompute every cached summary by direct machine scan and compare it with the tree.

        Checks that the machine still declares the state count the lineage was built with, that
        each element's cached effect row matches a fresh scan, that the root summary is exactly
        the ordered composition of those rows, and that the cached element count agrees with
        enumeration.  Raises :class:`ValueError` on the first violation, or :class:`OverflowError`
        if rescanning overflows the signed 64-bit event total.  A defensive audit; ordinary
        operations maintain these invariants.  O(s n).
        """

        state_count = self._state_count
        if _validated_state_count(self._machine) != state_count:
            raise ValueError("The machine's state count changed after the sequence was built.")

        entries = list(self._entries)
        count = len(entries)
        measure = self._entries.measure
        if count != len(self) or count != measure.count:
            raise ValueError("The cached element count disagrees with enumeration.")
        expected_width = 0 if count == 0 else state_count
        if (
            len(measure.next_states) != expected_width
            or len(measure.event_counts) != expected_width
        ):
            raise ValueError("The cached contextual summary does not cover every machine state.")

        rows: list[_ContextualSummary] = []
        for entry in entries:
            row = _make_entry(self._machine, state_count, entry.element).summary
            if row != entry.summary:
                raise ValueError("A cached element summary disagrees with a direct machine scan.")
            rows.append(row)

        maximum_total_event_count = 0
        for initial_state in range(state_count):
            state = initial_state
            events = 0
            for row in rows:
                events += row.event_counts[state]
                state = row.next_states[state]
                if events > _INT64_MAX:
                    raise OverflowError("Rescanning the elements overflowed the event total.")
            if state != measure.final_state(initial_state):
                raise ValueError("A cached outgoing state disagrees with a direct scan.")
            if events != measure.event_count(initial_state):
                raise ValueError("A cached event count disagrees with a direct scan.")
            maximum_total_event_count = max(maximum_total_event_count, events)

        return ContextualRankSequenceStatistics(count, state_count, maximum_total_event_count)

    def _entry(self, item: T) -> _Entry[T]:
        return _make_entry(self._machine, self._state_count, item)

    def _wrap(
        self, entries: MeasuredSequence[_Entry[T], _ContextualSummary]
    ) -> ContextualRankSequence[T]:
        return ContextualRankSequence(entries, self._machine, self._state_count)

    def _empty(self) -> ContextualRankSequence[T]:
        return self._wrap(MeasuredSequence.empty(_summary_policy()))

    def _is_valid_state(self, state: int) -> bool:
        return 0 <= state < self._state_count

    def _check_insertion_capacity(self) -> None:
        if len(self) == _INT32_MAX:
            raise OverflowError("A sequence cannot contain more than Int32.MaxValue elements.")

    def _check_element_index(self, index: int) -> None:
        if index < 0 or index >= len(self):
            raise IndexError("index is outside the valid range of the sequence")

    def _check_boundary_index(self, index: int) -> None:
        if index < 0 or index > len(self):
            raise IndexError("index is outside the valid boundary range of the sequence")

    def _check_range(self, index: int, count: int) -> None:
        if index < 0:
            raise IndexError("index must be nonnegative")
        if count < 0:
            raise ValueError("count must be nonnegative")
        if index > len(self):
            raise IndexError("index is outside the valid boundary range of the sequence")
        if count > len(self) - index:
            raise ValueError("the range extends past the end of the sequence")


__all__ = [
    "ContextualEventLocation",
    "ContextualEventMachine",
    "ContextualEventTransition",
    "ContextualPrefixSummary",
    "ContextualRankSequence",
    "ContextualRankSequenceSplit",
    "ContextualRankSequenceStatistics",
    "create_contextual_event_machine",
]
