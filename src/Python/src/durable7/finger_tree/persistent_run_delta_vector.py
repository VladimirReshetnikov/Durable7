"""Fixed-length persistent vector carrying a checkpoint and an exact maximal-run change index.

:class:`PersistentRunDeltaVector` keeps two logical views of one fixed-length sequence -- the
current values and a checkpoint -- plus a persistent ordered map holding one
``start -> end_exclusive`` record for every *maximal* run of positions whose values differ.
Dirtiness is relative to a retained :data:`~durable7.finger_tree.equality.ValueEquality`, not to
write history: writing a relation-equal value is a semantic no-op, and returning a position to its
checkpoint equivalence class cancels that position's delta and restores the *exact* checkpoint
representative.

Let ``n`` be the fixed length, ``k`` the number of dirty positions, and ``r`` the number of maximal
dirty runs, so ``0 <= r <= k <= n``. Against an unindexed pair of current/checkpoint roots the one
strict result is that exact dirty-run descriptor discovery is output-optimal ``Theta(r)`` rather
than worst-case ``Theta(n)``, and no common operation loses the baseline's asymptotic time or
space. The gap is unbounded: one contiguous changed block gives ``k = n`` and ``r = 1``. Emitting
the changed payload *values* is still ``Omega(k)`` and is not claimed to be faster. This is not a
new interval algorithm nor a new persistent-array technique; the run index is an application of the
Discrete Interval Encoding Tree idea over an ordinary persistent vector.

Length is fixed at construction. There is deliberately no insertion, deletion, concatenation, split,
range assignment, rebase, or merge of unrelated branches, because checkpoint correspondence would
need a position-transformation policy this abstract data type does not define. There is one
checkpoint per version.

Bounds
------

Both roots are this package's :class:`~durable7.finger_tree.rrb_vector.RrbVector`; the run index is
its :class:`~durable7.finger_tree.sorted.SortedMap` over a persistent monoid-measured balanced
sequence. Neither substrate defers work: ``RrbVector`` copies exactly the path it touches on every
``set_item``, ``split_at``, and ``concat``, and a ``SortedMap`` write is a split plus two joins
with path copying. The bounds below are therefore **worst case, not amortized**. The C# and Rust
baselines say "amortized" because their RRB substrates defer rebalancing; nothing in this
workspace's substrate does, so the stronger form is the one stated here.

The bound this port does *not* inherit is a global height guarantee. ``RrbVector.concat`` rebalances
only the seam between its operands rather than imposing a global occupancy rule, so a root's height
is ``O(log n)`` for radix-built roots and grows by at most one per concatenation. Every ``O(log n)``
below is really ``O(h)`` in that height, which is the substrate's own claim and not a stronger one.

======================================================  ==========================================
Operation                                               Bound
======================================================  ==========================================
Build ``n`` values                                      ``Theta(n)``
``len``, dirty and run cardinalities, ``has_changes``   ``O(1)``
Read a current or checkpoint value                      ``O(log n)``
Point edit (:meth:`~PersistentRunDeltaVector.set_item`) ``O(log n)`` time and fresh nodes
Whole :meth:`~PersistentRunDeltaVector.checkpoint`      ``O(1)``
Whole :meth:`~PersistentRunDeltaVector.rollback`        ``O(1)``
Dirty membership, run containing a position, run rank   ``O(log(r + 1))``
Exact dirty-run descriptors                             ``Theta(r)``, output-optimal
Accept or revert one indexed run                        ``O(log n + log(r + 1))``
Enumerate current values                                ``Theta(n)``
:meth:`~PersistentRunDeltaVector.validate_structure`    ``O(n log n)``
======================================================  ==========================================

Accepting or reverting one run is four ``split_at`` calls and two ``concat`` calls over the RRB
roots. It never walks the run's positions and makes **no** value-relation call at all, which is what
makes it independent of the run's length. That splice bound is inherited from the substrate and is
not itself a novelty claim.

Reference identity
------------------

The cleanliness invariant is "a clean position reuses its *exact* checkpoint representative", which
needs object identity rather than value equality: the retained relation may be coarser or finer than
``==``, so it cannot decide representative identity. C# uses a private ``Cell`` class compared with
``ReferenceEquals``, Rust retains an ``Arc`` compared with ``Arc::ptr_eq``, and the Kotlin, C++, and
OCaml ports box values in an identity-bearing cell. Python has ``is``, but ``is`` degenerates on
small integers, interned strings, ``True``/``False``/``None``, and other cached immortals -- exactly
the payload types a test naturally reaches for -- so an identity invariant stated over raw payloads
would be silently vacuous. This port therefore boxes every element in a private ``_Cell`` whose
``__eq__`` is object identity, and states the invariant over cells.

That choice also interlocks with the substrate. ``RrbVector.set_item`` short-circuits on ``is``,
not on ``==``: its leaf writer keeps the existing node when ``node.items[index] is value``. It
applies no equality no-op rule, so it can never silently keep an *equal but different* object in
place of the one written -- but with raw payloads that identity test would fire for interned values
and make the diagnostic meaningless. Boxing turns that short circuit into precisely the identity
test this structure wants, and :meth:`~PersistentRunDeltaVector.shares_representative_at` exposes
the resulting cell identity so a test can prove restoration is by identity, not merely by value.

Value equivalence
-----------------

C# takes an ``IEqualityComparer<T>``; the relation is retained here as a plain
:data:`~durable7.finger_tree.equality.ValueEquality` callable, the value-side counterpart of
:data:`~durable7.finger_tree.ordering.Comparator`. The caller's obligation is that the relation be a
genuine **equivalence relation** -- reflexive, symmetric, and transitive -- and stable for every
retained version's lifetime. A non-reflexive relation silently corrupts run accounting: a position
rewritten with its own value would be recorded as changed, leaving a phantom run that no invariant
check can detect as false.

Floats are the inherited trap. .NET's default comparer is reflexive on ``NaN`` (``double.Equals``
treats any two ``NaN`` as equal), while a naive ``left == right`` in Python is not:
``float("nan") == float("nan")`` is ``False``. Python's own containers hide this, because they
compare elementwise by identity first -- ``[nan] == [nan]`` is ``True`` for the *same* ``nan``
object while ``nan == nan`` is ``False`` -- so a relation that looks reflexive inside a list is not
reflexive on the bare payload.

:mod:`durable7.finger_tree.equality` supplies both relations this port needs:

* :func:`~durable7.finger_tree.equality.natural_value_equality`, the default, is
  ``left is right or left == right``. Trying identity first makes it reflexive on *every* object
  including ``NaN``, so it is always a true equivalence relation. Two *distinct* ``NaN`` objects
  still land in different classes, a coarser answer than .NET gives but a consistent one.
* :func:`~durable7.finger_tree.equality.reflexive_ieee_equality` is the canonical float relation,
  the counterpart of Rust's ``EqualityPolicy::reflexive_ieee``. It puts every ``NaN`` in one class
  and ``-0.0`` with ``0.0``, reproducing .NET's default comparer exactly. Pass it for ``float``
  payloads.

Neither helper rescues a ``NaN`` nested inside a payload it cannot see into; a caller whose values
contain floats must supply a relation that is reflexive on those values.

Every relation call in an edit happens before any successor root is built, so a raising relation
publishes no partial version: the receiver and every retained branch stay valid and unchanged.
:meth:`~PersistentRunDeltaVector.checkpoint`, :meth:`~PersistentRunDeltaVector.rollback`,
:meth:`~PersistentRunDeltaVector.dirty_runs`, and every accept/revert of an indexed run invoke no
relation callback at all.

Divergences from the C# baseline
--------------------------------

* Values are boxed in an identity-bearing private cell, as in every sibling port. See "Reference
  identity" above for why Python's ``is`` on raw payloads cannot carry the invariant.
* Positions and run ranks accept negative indices, counting from the end as Python sequences do, and
  raise :class:`IndexError` outside range where C# raises ``ArgumentOutOfRangeException``. Published
  run descriptors always carry non-negative absolute positions.
* ``TryGetDirtyRunContaining`` becomes :meth:`~PersistentRunDeltaVector.dirty_run_containing`
  returning ``PersistentDirtyRun | None``, since a run is never the zero-length default and Python
  has no ``out`` parameter.
* :meth:`~PersistentRunDeltaVector.reset_item` compares the current value against the checkpoint
  value directly instead of delegating to ``SetItem``, following the Rust port. C#'s delegation
  makes a second relation call that asks whether the checkpoint value equals itself; under the
  required reflexive relation the results are identical, and the direct form does not depend on
  reflexivity to terminate a reset.
* C# documents ``OverflowException`` past ``int.MaxValue`` positions. Python integers have no such
  ceiling and neither substrate imposes one, so that contract is vacuous here and is not reproduced.
* ``EnumerateDirtyRuns`` returns a lazy single-pass iterator rather than a repeatable
  ``IEnumerable``. Every version is immutable, so calling it again yields the same sequence.
* ``ValidateInvariants`` becomes the public :meth:`~PersistentRunDeltaVector.validate_structure`
  raising :class:`AssertionError`, matching this workspace's error convention.
* The relation and its two canonical helpers live in :mod:`durable7.finger_tree.equality`, the
  package's value-side counterpart of :mod:`durable7.finger_tree.ordering`, rather than being
  restated here. :class:`PersistentDeltaMap` retains the same relation from the same module, so the
  two delta-tracking collections decide "the same value" by one shared rule.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar

from .equality import ValueEquality, natural_value_equality
from .ordering import default_comparator
from .rrb_vector import RrbVector
from .sorted import SortedMap

T = TypeVar("T")


@dataclass(frozen=True, slots=True, order=True)
class PersistentDirtyRun:
    """One maximal half-open run of checkpoint-relative changes.

    A run covers the positions ``start .. start + length`` and is never empty. The runs published by
    one version are ordered, non-overlapping, non-adjacent, and maximal.
    """

    start: int
    length: int

    def __post_init__(self) -> None:
        """Reject a negative start or a non-positive length, so every run names real positions."""

        if self.start < 0 or self.length <= 0:
            raise ValueError("A dirty run starts at a non-negative position and is never empty.")

    @property
    def end_exclusive(self) -> int:
        """The exclusive end position."""

        return self.start + self.length

    def contains(self, index: int) -> bool:
        """Whether ``index`` lies inside this run."""

        return self.start <= index < self.end_exclusive

    def __str__(self) -> str:
        """The half-open interval in mathematical notation."""

        return f"[{self.start}, {self.end_exclusive})"


@dataclass(frozen=True, slots=True)
class RunDeltaVectorStatistics:
    """Representation measurements returned by a successful structural audit."""

    count: int
    dirty_count: int
    dirty_run_count: int
    shared_representative_count: int
    is_clean: bool


@dataclass(frozen=True, slots=True, eq=False)
class _Cell(Generic[T]):
    """One boxed element slot, identified by object identity rather than by value.

    ``eq=False`` keeps the inherited identity ``__eq__`` and ``__hash__``. That is deliberate: the
    retained value relation may be coarser or finer than ``==``, so only reference identity can
    decide whether a position reuses its exact checkpoint representative, and only boxing keeps that
    question meaningful for payloads Python interns.
    """

    value: T


def _to_run(start: int, end_exclusive: int) -> PersistentDirtyRun:
    return PersistentDirtyRun(start, end_exclusive - start)


def _add_dirty_position(runs: SortedMap[int, int], index: int) -> SortedMap[int, int]:
    """Record ``index`` as dirty, merging both neighbours, extending one, or inserting a singleton.

    The result stays ordered, non-overlapping, non-adjacent, and maximal, and at most two records
    change. The left record is overwritten in place rather than removed and reinserted, because
    ``set_item`` replaces a present key.
    """

    adjacent_left = runs.floor_entry(index)
    adjacent_left = (
        adjacent_left if adjacent_left is not None and adjacent_left.value == index else None
    )
    adjacent_right = runs.ceiling_entry(index)
    adjacent_right = (
        adjacent_right if adjacent_right is not None and adjacent_right.key == index + 1 else None
    )
    if adjacent_left is not None and adjacent_right is not None:
        return runs.remove(adjacent_right.key).set_item(adjacent_left.key, adjacent_right.value)
    if adjacent_left is not None:
        return runs.set_item(adjacent_left.key, index + 1)
    if adjacent_right is not None:
        return runs.remove(adjacent_right.key).set_item(index, adjacent_right.value)
    return runs.set_item(index, index + 1)


def _remove_dirty_position(runs: SortedMap[int, int], index: int) -> SortedMap[int, int]:
    """Clear ``index``, deleting, shrinking at either edge, or splitting its containing run.

    Raises :class:`AssertionError` when ``index`` is absent from the run index, which would mean the
    maintained invariant had already been broken.
    """

    containing = runs.floor_entry(index)
    if containing is None or containing.value <= index:
        raise AssertionError("The cleared dirty position is absent from the run index.")

    start, end = containing.key, containing.value
    if start == index and end == index + 1:
        return runs.remove(start)
    if start == index:
        return runs.remove(start).set_item(index + 1, end)
    if end == index + 1:
        return runs.set_item(start, index)
    return runs.set_item(start, index).set_item(index + 1, end)


def _replace_range(
    destination: RrbVector[_Cell[T]],
    source: RrbVector[_Cell[T]],
    run: PersistentDirtyRun,
) -> RrbVector[_Cell[T]]:
    """Splice ``source``'s ``run`` slice over ``destination``'s, sharing every untouched subtree.

    Four splits and two seam concatenations, so the cost is ``O(log n)`` regardless of the run's
    length, and no element is inspected or compared on the way.
    """

    if run.start == 0 and run.length == len(destination):
        return source

    destination_split = destination.split_at(run.start)
    source_split = source.split_at(run.start)
    if destination_split is None or source_split is None:
        raise AssertionError("A run start is not a valid split boundary.")
    destination_tail = destination_split.right.split_at(run.length)
    source_tail = source_split.right.split_at(run.length)
    if destination_tail is None or source_tail is None:
        raise AssertionError("A run end is not a valid split boundary.")
    return destination_split.left.concat(source_tail.left).concat(destination_tail.right)


class PersistentRunDeltaVector(Generic[T]):
    """A fixed-length persistent vector with a checkpoint and an exact maximal-run change index.

    See the module documentation for the representation, the shipped bounds, the reference-identity
    rule, and the equivalence-relation contract. Every version is immutable and every producing
    operation leaves its receiver -- and every other retained version -- valid and unchanged. A
    semantic no-op returns the receiver itself.
    """

    __slots__ = ("_checkpoint", "_current", "_dirty_count", "_dirty_runs", "value_equals")

    def __init__(
        self,
        current: RrbVector[_Cell[T]],
        checkpoint: RrbVector[_Cell[T]],
        dirty_runs: SortedMap[int, int],
        dirty_count: int,
        value_equals: ValueEquality[T],
    ) -> None:
        """Wrap already-built roots; use :meth:`empty` or :meth:`from_values` instead."""

        self._current = current
        self._checkpoint = checkpoint
        self._dirty_runs = dirty_runs
        self._dirty_count = dirty_count
        self.value_equals = value_equals

    @classmethod
    def empty(
        cls, value_equals: ValueEquality[T] = natural_value_equality
    ) -> PersistentRunDeltaVector[T]:
        """Return the clean empty vector retaining ``value_equals`` by identity. O(1)."""

        root: RrbVector[_Cell[T]] = RrbVector.empty()
        return cls(root, root, _empty_runs(), 0, value_equals)

    @classmethod
    def from_values(
        cls,
        values: Iterable[T],
        value_equals: ValueEquality[T] = natural_value_equality,
    ) -> PersistentRunDeltaVector[T]:
        """Build a clean fixed-length vector from one pass over ``values``. Theta(n).

        Both roots start as the same root, so the vector begins with no dirty runs and no relation
        callback is invoked.
        """

        root: RrbVector[_Cell[T]] = RrbVector.from_iterable(_Cell(value) for value in values)
        return cls(root, root, _empty_runs(), 0, value_equals)

    def __len__(self) -> int:
        """The fixed number of positions. O(1)."""

        return len(self._current)

    @property
    def size(self) -> int:
        """The fixed number of positions. O(1)."""

        return len(self._current)

    @property
    def is_empty(self) -> bool:
        """Whether the vector has no positions. O(1)."""

        return self._current.is_empty

    def __bool__(self) -> bool:
        """Whether the vector holds at least one position."""

        return not self._current.is_empty

    @property
    def dirty_count(self) -> int:
        """The number of positions that differ from the checkpoint. O(1)."""

        return self._dirty_count

    @property
    def dirty_run_count(self) -> int:
        """The number of maximal dirty runs. O(1)."""

        return len(self._dirty_runs)

    @property
    def has_changes(self) -> bool:
        """Whether at least one current value differs from its checkpoint value. O(1)."""

        return self._dirty_count != 0

    @property
    def is_clean(self) -> bool:
        """Whether this version records no change *and* reuses the exact checkpoint root. O(1).

        The two halves cannot come apart: a version whose last dirty position clears snaps its
        current root back to the checkpoint root, and :meth:`validate_structure` rejects any version
        where they have.
        """

        return self._dirty_runs.is_empty and self._current.shares_root_with(self._checkpoint)

    def __getitem__(self, index: int) -> T:
        """The current value at ``index``. O(log n).

        Negative indices count from the end. Raises :class:`IndexError` outside the vector.
        """

        return self._cell_at(self._current, self._position(index)).value

    def checkpoint_value(self, index: int) -> T:
        """The checkpoint value at ``index``. O(log n).

        Negative indices count from the end. Raises :class:`IndexError` outside the vector.
        """

        return self._cell_at(self._checkpoint, self._position(index)).value

    def to_list(self) -> list[T]:
        """Copy the current values into a list, in positional order. Theta(n)."""

        return [cell.value for cell in self._current]

    def checkpoint_to_list(self) -> list[T]:
        """Copy the checkpoint values into a list, in positional order. Theta(n)."""

        return [cell.value for cell in self._checkpoint]

    def __iter__(self) -> Iterator[T]:
        """Iterate the current values in positional order. Theta(n)."""

        for cell in self._current:
            yield cell.value

    def is_dirty(self, index: int) -> bool:
        """Whether ``index`` differs from its checkpoint value. O(log(r + 1)).

        Raises :class:`IndexError` outside the vector.
        """

        return self._find_run(self._position(index)) is not None

    def dirty_run_containing(self, index: int) -> PersistentDirtyRun | None:
        """The maximal dirty run containing ``index``, or ``None`` when that position is clean.

        O(log(r + 1)). Raises :class:`IndexError` outside the vector, so a clean position stays
        distinguishable from an invalid one.
        """

        return self._find_run(self._position(index))

    def dirty_run_at(self, rank: int) -> PersistentDirtyRun:
        """The maximal dirty run at zero-based ascending-start ``rank``. O(log(r + 1)).

        Negative ranks count from the last run. Raises :class:`IndexError` outside the run index.
        """

        entry = self._dirty_runs.entry_at(self._run_rank(rank))
        if entry is None:
            raise AssertionError("A validated dirty-run rank is absent from the run index.")
        return _to_run(entry.key, entry.value)

    def dirty_runs(self) -> Iterator[PersistentDirtyRun]:
        """Iterate the maximal dirty runs in ascending position order.

        Output-optimal ``Theta(r)``: the cost depends on the number of runs, not on how many
        positions they cover. Invokes no value-relation callback. The walk is lazy, so abandoning it
        early skips the rest.
        """

        for entry in self._dirty_runs:
            yield _to_run(entry.key, entry.value)

    def set_item(self, index: int, value: T) -> PersistentRunDeltaVector[T]:
        """Return a version with the current value at ``index`` replaced by ``value``. O(log n).

        A relation-equal replacement is a semantic no-op and returns **the receiver itself**,
        sharing every root and changing no dirty or run accounting. A replacement landing back in
        the checkpoint's equivalence class cancels that position's delta and restores the exact
        checkpoint representative; when the last dirty position clears, the current root snaps back
        to the checkpoint root. Otherwise the position becomes or stays dirty and at most two
        neighbouring run records change.

        Negative indices count from the end. Raises :class:`IndexError` outside the vector. Every
        relation call happens before any successor root is built, so a raising relation publishes no
        partial version.
        """

        position = self._position(index)
        current = self._cell_at(self._current, position)
        if self.value_equals(current.value, value):
            return self

        checkpoint = self._cell_at(self._checkpoint, position)
        remains_dirty = not self.value_equals(checkpoint.value, value)
        replacement = _Cell(value) if remains_dirty else checkpoint
        return self._replace_cell(position, replacement, remains_dirty)

    def reset_item(self, index: int) -> PersistentRunDeltaVector[T]:
        """Return a version whose value at ``index`` is reset to its checkpoint value. O(log n).

        An already clean position is a no-op returning the receiver itself. A cancelled position
        recovers the exact checkpoint representative, not merely a relation-equal value.

        Negative indices count from the end. Raises :class:`IndexError` outside the vector.
        """

        position = self._position(index)
        current = self._cell_at(self._current, position)
        checkpoint = self._cell_at(self._checkpoint, position)
        if self.value_equals(current.value, checkpoint.value):
            return self
        return self._replace_cell(position, checkpoint, False)

    def checkpoint(self) -> PersistentRunDeltaVector[T]:
        """Accept every current value as a new checkpoint. O(1).

        An already clean version returns the receiver itself; otherwise the result is a distinct
        clean version sharing the receiver's current root. No relation callback is invoked either
        way.
        """

        if not self.has_changes:
            return self
        return PersistentRunDeltaVector(
            self._current, self._current, _empty_runs(), 0, self.value_equals
        )

    def rollback(self) -> PersistentRunDeltaVector[T]:
        """Revert every current value to the checkpoint. O(1).

        An already clean version returns the receiver itself; otherwise the result is a distinct
        clean version sharing the receiver's checkpoint root. No relation callback is invoked either
        way.
        """

        if not self.has_changes:
            return self
        return PersistentRunDeltaVector(
            self._checkpoint, self._checkpoint, _empty_runs(), 0, self.value_equals
        )

    def accept_dirty_run_at(self, rank: int) -> PersistentRunDeltaVector[T]:
        """Accept the maximal dirty run at ``rank`` into the checkpoint. O(log n + log(r + 1)).

        Only the selected run becomes clean; every other run is untouched. Implemented by structural
        splitting and concatenation of the RRB roots, so the splice costs ``O(log n)``
        *independently of that run's length* and makes no value-relation call at all.

        Negative ranks count from the last run. Raises :class:`IndexError` outside the run index.
        """

        return self._accept_run(self.dirty_run_at(rank))

    def revert_dirty_run_at(self, rank: int) -> PersistentRunDeltaVector[T]:
        """Revert the maximal dirty run at ``rank`` to the checkpoint. O(log n + log(r + 1)).

        The mirror of :meth:`accept_dirty_run_at`, with the same splice cost and the same absence of
        value-relation calls.
        """

        return self._revert_run(self.dirty_run_at(rank))

    def accept_dirty_run_containing(self, index: int) -> PersistentRunDeltaVector[T]:
        """Accept the maximal dirty run *containing the position* ``index``. O(log n + log(r + 1)).

        The argument is a zero-based position, not a run rank, so a descriptor obtained from
        :meth:`dirty_run_containing` can be acted on without rediscovering its rank. A clean
        position is vacuous rather than an error: the receiver itself comes back, sharing roots and
        consulting no relation. A dirty position behaves exactly as :meth:`accept_dirty_run_at` on
        the containing run.

        Negative indices count from the end. Raises :class:`IndexError` outside the vector.
        """

        run = self._find_run(self._position(index))
        return self if run is None else self._accept_run(run)

    def revert_dirty_run_containing(self, index: int) -> PersistentRunDeltaVector[T]:
        """Revert the maximal dirty run *containing the position* ``index``. O(log n + log(r + 1)).

        The mirror of :meth:`accept_dirty_run_containing`, with the same addressing rule, the same
        splice cost, and the same absence of value-relation calls.
        """

        run = self._find_run(self._position(index))
        return self if run is None else self._revert_run(run)

    def shares_current_root_with(self, other: PersistentRunDeltaVector[T]) -> bool:
        """Whether two versions are backed by the same current root. O(1).

        A representation test, not an equality test. Two empty vectors always share, so a negative
        control needs non-empty roots, and comparing a version with itself proves nothing.
        """

        return self._current.shares_root_with(other._current)

    def shares_checkpoint_root_with(self, other: PersistentRunDeltaVector[T]) -> bool:
        """Whether two versions are backed by the same checkpoint root. O(1).

        A representation test, not an equality test; see :meth:`shares_current_root_with`.
        """

        return self._checkpoint.shares_root_with(other._checkpoint)

    def shares_representative_at(self, index: int) -> bool:
        """Whether this version's current and checkpoint roots hold the *same cell* at ``index``.

        O(log n). This is the diagnostic behind the cleanliness invariant, and the only way to see
        the difference between restoring the exact checkpoint representative and storing a merely
        relation-equal value: Python's ``is`` on the payloads themselves degenerates for interned
        objects, but cells are never interned.

        Negative indices count from the end. Raises :class:`IndexError` outside the vector.
        """

        position = self._position(index)
        return self._cell_at(self._current, position) is self._cell_at(self._checkpoint, position)

    def shared_leaf_count(self) -> int:
        """How many leaf nodes this version's two roots have in common by object identity.

        A representation measurement used to show that a run splice reused untouched subtrees rather
        than rebuilding them. Theta(n) in the number of nodes, so it is a diagnostic and not part of
        any operation's cost.
        """

        return self._current.shared_leaf_count(self._checkpoint)

    def validate_structure(self) -> RunDeltaVectorStatistics:
        """Walk the whole representation independently and return its measurements.

        Checks both RRB roots against their own structural audit, that a version with no runs is
        canonicalized onto its checkpoint root, that the run records are ordered, non-overlapping,
        non-adjacent, in range, and non-empty, that their lengths sum to the cached dirty
        cardinality, that every position outside every run reuses its exact checkpoint
        representative, and that every position inside a run really is relation-unequal to its
        checkpoint value. The last two together establish maximality: a dirty position left outside
        the runs, or a clean one swept inside them, is rejected.

        Raises :class:`AssertionError` on the first violation. ``O(n log n)``: an auditor, not a
        hot-path operation, and it does invoke the retained relation.
        """

        count = len(self._current)
        if count != len(self._checkpoint):
            raise AssertionError("The current and checkpoint roots have different lengths.")
        if self._dirty_count < 0 or self._dirty_count > count:
            raise AssertionError("The dirty cardinality is outside the position count.")
        try:
            self._current.validate_structure()
        except ValueError as error:
            raise AssertionError("The current RRB root is structurally invalid.") from error
        try:
            self._checkpoint.validate_structure()
        except ValueError as error:
            raise AssertionError("The checkpoint RRB root is structurally invalid.") from error
        if self._dirty_runs.is_empty and not self.is_clean:
            raise AssertionError("A clean version is not canonicalized onto its checkpoint root.")
        if self._dirty_runs.is_empty and self._dirty_count != 0:
            raise AssertionError("An empty run index carries a non-zero dirty cardinality.")

        previous_end = -1
        counted_dirty = 0
        for entry in self._dirty_runs:
            start, end = entry.key, entry.value
            if start < 0 or start >= end or end > count or start <= previous_end:
                raise AssertionError(
                    "Dirty runs are empty, out of range, overlapping, adjacent, or unordered."
                )
            counted_dirty += end - start
            previous_end = end
        if counted_dirty != self._dirty_count:
            raise AssertionError("Dirty run lengths do not sum to the dirty cardinality.")

        shared = 0
        for position in range(count):
            dirty = self._find_run(position) is not None
            current = self._cell_at(self._current, position)
            checkpoint = self._cell_at(self._checkpoint, position)
            if current is checkpoint:
                shared += 1
            elif not dirty:
                raise AssertionError(
                    "A clean position does not reuse its exact checkpoint representative."
                )
            if dirty and self.value_equals(current.value, checkpoint.value):
                raise AssertionError("A dirty position is relation-equal to its checkpoint value.")

        return RunDeltaVectorStatistics(
            count=count,
            dirty_count=self._dirty_count,
            dirty_run_count=len(self._dirty_runs),
            shared_representative_count=shared,
            is_clean=self.is_clean,
        )

    def __repr__(self) -> str:
        """A diagnostic summary naming the length and the dirty and run cardinalities."""

        return (
            f"PersistentRunDeltaVector(size={len(self._current)}, "
            f"dirty_count={self._dirty_count}, dirty_run_count={len(self._dirty_runs)})"
        )

    def _position(self, index: int) -> int:
        count = len(self._current)
        position = index + count if index < 0 else index
        if position < 0 or position >= count:
            raise IndexError("Position is outside the run-delta vector.")
        return position

    def _run_rank(self, rank: int) -> int:
        count = len(self._dirty_runs)
        normalized = rank + count if rank < 0 else rank
        if normalized < 0 or normalized >= count:
            raise IndexError("Dirty-run rank is outside the run index.")
        return normalized

    def _cell_at(self, root: RrbVector[_Cell[T]], position: int) -> _Cell[T]:
        cell = root.get(position)
        if cell is None:
            raise AssertionError("A validated position is absent from an RRB root.")
        return cell

    def _find_run(self, position: int) -> PersistentDirtyRun | None:
        entry = self._dirty_runs.floor_entry(position)
        if entry is None or entry.value <= position:
            return None
        return _to_run(entry.key, entry.value)

    def _replace_cell(
        self, position: int, replacement: _Cell[T], remains_dirty: bool
    ) -> PersistentRunDeltaVector[T]:
        was_dirty = self._find_run(position) is not None
        next_current = self._current.set_item(position, replacement)
        if next_current is None:
            raise AssertionError("A validated position rejected an RRB write.")
        next_runs = self._dirty_runs
        next_dirty_count = self._dirty_count

        if not was_dirty and remains_dirty:
            next_runs = _add_dirty_position(next_runs, position)
            next_dirty_count += 1
        elif was_dirty and not remains_dirty:
            next_runs = _remove_dirty_position(next_runs, position)
            next_dirty_count -= 1

        # A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
        # canonical and later checkpoint, rollback, and cleanliness queries stay O(1).
        if next_runs.is_empty:
            next_current = self._checkpoint
        return PersistentRunDeltaVector(
            next_current, self._checkpoint, next_runs, next_dirty_count, self.value_equals
        )

    def _accept_run(self, run: PersistentDirtyRun) -> PersistentRunDeltaVector[T]:
        """Splice one indexed run's current values into the checkpoint, leaving every other run.

        Comparison-free; ``run`` must be a record of this version's own index.
        """

        if len(self._dirty_runs) == 1:
            return self.checkpoint()
        return PersistentRunDeltaVector(
            self._current,
            _replace_range(self._checkpoint, self._current, run),
            self._dirty_runs.remove(run.start),
            self._dirty_count - run.length,
            self.value_equals,
        )

    def _revert_run(self, run: PersistentDirtyRun) -> PersistentRunDeltaVector[T]:
        """Splice one indexed run's checkpoint values back over the current root.

        Comparison-free; ``run`` must be a record of this version's own index.
        """

        if len(self._dirty_runs) == 1:
            return self.rollback()
        return PersistentRunDeltaVector(
            _replace_range(self._current, self._checkpoint, run),
            self._checkpoint,
            self._dirty_runs.remove(run.start),
            self._dirty_count - run.length,
            self.value_equals,
        )


def _empty_runs() -> SortedMap[int, int]:
    """A fresh empty run index ordered by ascending start position."""

    return SortedMap.empty(default_comparator)


__all__ = [
    "PersistentDirtyRun",
    "PersistentRunDeltaVector",
    "RunDeltaVectorStatistics",
]
