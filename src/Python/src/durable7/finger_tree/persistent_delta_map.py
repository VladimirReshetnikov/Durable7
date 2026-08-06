"""Checkpoint-differential persistent sorted map with an exact, coalesced change index.

:class:`PersistentDeltaMap` is a fully persistent sorted map that carries a designated *checkpoint*
plus an ordered index of the exact net changes from that checkpoint. It answers one narrow question
cheaply: which key classes differ from the last checkpoint, and what were their checkpoint and
current values? A plain persistent map makes versions cheap, but a root handle alone does not name
the changed keys, and a write log names operations while leaving repeated writes, cancellations, and
ordering to be resolved later. This structure maintains the exact net answer online, while edits
happen.

A version holds three immutable roots: the checkpoint state ``B``, the current state ``S``, and a
change index ``D`` carrying exactly one ``(before, after)`` record for every key class on which
``B`` and ``S`` differ. The first effective write to a class captures its checkpoint-relative
``before``; later writes replace only ``after``; returning a class to its checkpoint state deletes
its record outright. When ``D`` empties, the current root snaps back to the *exact* checkpoint root,
so a wholly cancelled epoch is storage-clean as well as extensionally clean and
:meth:`~PersistentDeltaMap.checkpoint` and :meth:`~PersistentDeltaMap.rollback` stay O(1) root
swaps.

Bounds
------

Let ``N = max(2, |dom(B) union dom(S)|)`` counting key-policy equivalence classes rather than key
objects, and let ``k = |D|``. The substrate is :class:`~durable7.finger_tree.sorted.SortedMap` over
a persistent monoid-measured lazy finger tree, so these are the bounds it actually delivers:

* Point lookup, point write, point removal, rank, and neighbour queries are O(log N): every one is a
  cached-size descent, and a write is a split plus two joins with path copying.
* ``len`` and :attr:`~PersistentDeltaMap.change_count` are O(1), read from cached subtree sizes.
* :meth:`~PersistentDeltaMap.checkpoint` and :meth:`~PersistentDeltaMap.rollback` are O(1).
* :meth:`~PersistentDeltaMap.get_change` is O(log(k + 1)), searching ``D`` rather than ``S``.
* :meth:`~PersistentDeltaMap.min_entry` and :meth:`~PersistentDeltaMap.max_entry` are **O(1)
  worst case**, matching the C# baseline: the substrate is a finger tree whose endpoint digits are
  direct reads. (An earlier revision sat on an extremes-free AVL and honestly documented Theta(log
  N) here; the substrate upgrade removed that divergence.)
* Fully consuming :meth:`~PersistentDeltaMap.get_changes` is Theta(k + 1) and output-optimal, which
  is the whole point of the design: it avoids the Theta(k log(N / k + 1)) adversarial divergent-path
  walk that a reference-pruned comparison of two path-copied balanced trees must perform. The
  in-order walk pushes a leftmost spine first, so the *first* element costs Theta(log(k + 1)) and
  abandoning the enumeration early still saves the rest.
* :meth:`~PersistentDeltaMap.changes_in_range` is a boundary seek plus a bounded walk over the
  in-range sub-index, never a filter over all ``k`` records: O(log(k + 1)) key comparisons to locate
  both boundaries, O(log(k + 1)) nodes copied by the two splits that isolate them, then Theta(1) per
  yielded change and no value comparison at all.
* :meth:`~PersistentDeltaMap.shares_storage_with` and the snapshot-level sharing test are O(1) when
  the compared roots are the same object, which is the case every canonicalisation check cares
  about; between two genuinely different roots the substrate walks both node sets, so a *negative*
  answer costs O(n + m).

A live version occupies O(N + k) nodes; retaining every successor adds O(log N) nodes per changed
point write, the same asymptotic persistence cost as an ordinary path-copied ordered map.

Policies
--------

Key identity and output order come from a retained
:data:`~durable7.finger_tree.ordering.Comparator`; two keys are the same class exactly when it
compares them equal. Semantic no-ops and change cancellation come from a retained
:data:`~durable7.finger_tree.equality.ValueEquality`. Both are retained on the map by identity
rather than passed per call, so an empty or wholly cancelled result stays usable and every successor
keeps deciding no-ops the same way. "Exact" therefore means exact *extensionally* under those two
policies, not by object identity of the retained key or value representatives. The comparator must
define a stable total preorder and the value relation a stable reflexive, symmetric, transitive
equivalence. Policy side effects cannot be undone, but a raising policy never publishes a partial
successor: the receiver and every retained branch stay valid and unchanged.
:meth:`~PersistentDeltaMap.checkpoint`, :meth:`~PersistentDeltaMap.rollback`, and
:meth:`~PersistentDeltaMap.get_changes` invoke no policy callback at all.

A baseline key representative survives point updates and delete/re-add round trips; a newly added
class keeps its first representative while its net-added record is active, and a later addition
after complete cancellation begins a new representative episode.

Divergences from the C# baseline
--------------------------------

* Presence is an explicit wrapper. ``None`` is a legal stored value in Python, so ``V | None``
  cannot say whether an endpoint is absent or present-and-``None``. An absent endpoint is ``None``
  in the endpoint slot and a present one is :class:`DeltaMapValue`, mirroring C#'s
  ``DeltaMapValue<T>`` and Rust's ``Option<Arc<V>>``. This is the workspace's established shape for
  the same problem; see ``OrderedCursorPeek``, ``IntervalMapLookup``, and ``OptionalValue``.
* Both policies are retained callables rather than :class:`typing.Protocol` objects. Each is a
  single operation, and the key half must be the workspace's existing
  :data:`~durable7.finger_tree.ordering.Comparator` alias, so a matching callable alias keeps the
  pair symmetric. Protocols in this package (``Monoid``, ``MeasurePolicy``, ``RangeUpdateAlgebra``)
  are the multi-member policies.
* There is no ``Empty`` singleton. :meth:`PersistentDeltaMap.empty` takes both policies with the
  module-level defaults, matching ``SortedMap.empty`` and ``PersistentIntervalMap.empty``.
* :meth:`PersistentDeltaMap.from_items` retains the **first** representative of each key class while
  the last value wins, because the substrate's insertion does. C#'s ``CreateRange`` lets the last
  representative win, which contradicts its own ``SetItem``; the rule used here is the one the point
  writes already follow.
* :meth:`PersistentDeltaMap.entry_at` raises :class:`IndexError` outside ``0..len`` rather than
  returning ``None``, matching C#'s throwing ``EntryAt`` and this port's error convention.
  :meth:`~PersistentDeltaMap.min_entry` and :meth:`~PersistentDeltaMap.max_entry` keep the
  substrate's presence-safe ``None`` for an empty map.
* C# documents ``OverflowException`` once a map reaches ``int.MaxValue`` entries. Python integers
  have no such ceiling and the substrate imposes none, so that contract is vacuous here and is not
  reproduced, as in the Rust port.
* :meth:`~PersistentDeltaMap.get_changes` returns a lazy single-pass iterator rather than a
  repeatable ``IEnumerable``. Every version is immutable, so calling it again yields the same
  sequence.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from enum import StrEnum
from itertools import pairwise
from typing import Generic, TypeVar, cast

from .equality import ValueEquality, natural_value_equality
from .ordering import Comparator, default_comparator
from .sorted import SortedMap, SortedMapEntry

K = TypeVar("K")
V = TypeVar("V")


@dataclass(frozen=True, slots=True)
class DeltaMapValue(Generic[V]):
    """A *present* endpoint value in a recorded change.

    An endpoint slot holds ``None`` when the key class is absent at that endpoint and a
    ``DeltaMapValue`` when it is present, so a stored ``None`` stays distinct from absence.
    Comparing two wrappers with ``==`` is ordinary Python equality and is *not* the map's retained
    value relation; ask the map's :attr:`PersistentDeltaMap.value_equals` when the relation is what
    you mean.
    """

    value: V


class PersistentMapChangeKind(StrEnum):
    """Classifies one net key change relative to a :class:`PersistentDeltaMap` checkpoint."""

    ADDED = "added"
    """The key class is absent at the checkpoint and present in the current map."""

    REMOVED = "removed"
    """The key class is present at the checkpoint and absent from the current map."""

    UPDATED = "updated"
    """The key class is present at both endpoints with non-equivalent values."""


@dataclass(frozen=True, slots=True)
class PersistentMapChange(Generic[K, V]):
    """One exact net change between a version's checkpoint and its current state.

    ``key`` is the checkpoint representative when the class was present at the checkpoint, and
    otherwise the current addition episode's first representative.
    """

    key: K
    before: DeltaMapValue[V] | None
    after: DeltaMapValue[V] | None

    @property
    def kind(self) -> PersistentMapChangeKind:
        """The classification implied by the two endpoints.

        Raises :class:`AssertionError` when both endpoints are absent, which is not a net change and
        is never recorded.
        """

        if self.before is not None:
            return (
                PersistentMapChangeKind.UPDATED
                if self.after is not None
                else PersistentMapChangeKind.REMOVED
            )
        if self.after is not None:
            return PersistentMapChangeKind.ADDED
        raise AssertionError("A recorded change is never absent at both endpoints.")


@dataclass(frozen=True, slots=True)
class DeltaMapStatistics:
    """Representation measurements returned by a successful structural audit."""

    count: int
    checkpoint_count: int
    change_count: int
    added_count: int
    removed_count: int
    updated_count: int
    is_clean: bool


@dataclass(frozen=True, slots=True, eq=False)
class _DeltaRecord(Generic[V]):
    """One ``(before, after)`` endpoint pair stored in the change index.

    Compared by identity on purpose. ``SortedMap.set_item`` skips a write whose value is ``==`` to
    the stored one, and two records may compare ``==`` while the retained value relation says their
    endpoints differ; identity comparison keeps that shortcut from vetoing a write the relation
    considers effective.
    """

    before: DeltaMapValue[V] | None
    after: DeltaMapValue[V] | None


def _forced_set(entries: SortedMap[K, V], representative: K, value: V) -> SortedMap[K, V]:
    """Store ``value`` under ``representative``'s class, keeping that exact key object.

    ``SortedMap.set_item`` returns its receiver when the new value is ``==`` to the stored one. That
    shortcut is the substrate's own equality, not the delta map's retained relation, so a relation
    stricter than ``==`` -- one distinguishing ``1`` from ``1.0``, say -- would otherwise leave the
    current state holding a value that contradicts the recorded ``after`` endpoint. Detecting the
    veto by result identity and reissuing the write as a removal plus an insertion costs a second
    O(log N) pass, and only in that rare case.
    """

    result = entries.set_item(representative, value)
    if result is not entries:
        return result
    return entries.remove(representative).set_item(representative, value)


class PersistentDeltaMap(Generic[K, V]):
    """An immutable sorted map carrying a checkpoint and the exact net changes from it.

    See the module documentation for the representation, the policy contract, and the complexity
    statements. Every operation returns a new version and leaves the receiver, and every other
    retained version, valid and unchanged. A semantic no-op returns the receiver itself.
    """

    __slots__ = ("_changes", "_checkpoint", "_current", "comparator", "value_equals")

    def __init__(
        self,
        current: SortedMap[K, V],
        checkpoint: SortedMap[K, V],
        changes: SortedMap[K, _DeltaRecord[V]],
        comparator: Comparator[K],
        value_equals: ValueEquality[V],
    ) -> None:
        """Wrap already-built roots; use :meth:`empty` or :meth:`from_items` instead."""

        self._current = current
        self._checkpoint = checkpoint
        self._changes = changes
        self.comparator = comparator
        self.value_equals = value_equals

    @classmethod
    def empty(
        cls,
        comparator: Comparator[K] = default_comparator,
        value_equals: ValueEquality[V] = natural_value_equality,
    ) -> PersistentDeltaMap[K, V]:
        """Return an empty map whose current state is also its checkpoint. O(1).

        Both policies are retained by identity. This is also the O(1) way to open an unrelated clean
        epoch; it is not the same as clearing while keeping the old checkpoint, which the
        deliberately point-only mutation surface omits.
        """

        state: SortedMap[K, V] = SortedMap.empty(comparator)
        changes: SortedMap[K, _DeltaRecord[V]] = SortedMap.empty(comparator)
        return cls(state, state, changes, comparator, value_equals)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V] | SortedMapEntry[K, V]],
        comparator: Comparator[K] = default_comparator,
        value_equals: ValueEquality[V] = natural_value_equality,
    ) -> PersistentDeltaMap[K, V]:
        """Build a clean checkpoint from ``items``. O(m log m) for ``m`` supplied entries.

        Within one key class the first representative is retained and the last value wins.
        """

        entries: SortedMap[K, V] = SortedMap.empty(comparator)
        for item in items:
            key, value = (item.key, item.value) if isinstance(item, SortedMapEntry) else item
            stored = entries.get_entry(key)
            entries = _forced_set(entries, key if stored is None else stored.key, value)
        changes: SortedMap[K, _DeltaRecord[V]] = SortedMap.empty(comparator)
        return cls(entries, entries, changes, comparator, value_equals)

    def __len__(self) -> int:
        """Number of current entries. O(1)."""

        return len(self._current)

    @property
    def size(self) -> int:
        """Number of current entries. O(1)."""

        return len(self._current)

    @property
    def is_empty(self) -> bool:
        """Whether the current map holds no entries. O(1)."""

        return self._current.is_empty

    def __bool__(self) -> bool:
        """Whether the current map holds at least one entry."""

        return not self._current.is_empty

    @property
    def change_count(self) -> int:
        """Number of exact net-changed key classes. O(1)."""

        return len(self._changes)

    @property
    def has_changes(self) -> bool:
        """Whether the current map differs semantically from its checkpoint. O(1)."""

        return not self._changes.is_empty

    @property
    def is_clean(self) -> bool:
        """Whether this version records no change *and* reuses the exact checkpoint root. O(1).

        The two halves cannot come apart: a version whose change index empties snaps its current
        root back to the checkpoint root, and :meth:`validate_structure` rejects any version where
        they have.
        """

        return self._changes.is_empty and self._current is self._checkpoint

    def current_snapshot(self) -> SortedMap[K, V]:
        """The current persistent ordered-map root. O(1)."""

        return self._current

    def checkpoint_snapshot(self) -> SortedMap[K, V]:
        """The checkpoint persistent ordered-map root. O(1)."""

        return self._checkpoint

    def contains_key(self, key: K) -> bool:
        """Whether a current entry exists for ``key``'s class. O(log N)."""

        return self._current.contains_key(key)

    def __contains__(self, key: object) -> bool:
        """Whether ``key``'s class is currently present, for the ``in`` operator."""

        return self._current.contains_key(cast(K, key))

    def get(self, key: K) -> V | None:
        """The current value for ``key``'s class, or ``None`` when absent. O(log N).

        Use :meth:`get_entry` when a stored ``None`` must stay distinct from absence.
        """

        return self._current.get(key)

    def get_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The retained current representative and value for ``key``'s class. O(log N)."""

        return self._current.get_entry(key)

    def __getitem__(self, key: K) -> V:
        """The current value for ``key``'s class, or :class:`KeyError` when absent. O(log N)."""

        entry = self._current.get_entry(key)
        if entry is None:
            raise KeyError(key)
        return entry.value

    def index_of_key(self, key: K) -> int | None:
        """The zero-based rank of ``key``'s class in the current map, or ``None``. O(log N)."""

        return self._current.index_of_key(key)

    def entry_at(self, rank: int) -> SortedMapEntry[K, V]:
        """The current entry at zero-based key rank. O(log N).

        Raises :class:`IndexError` when ``rank`` falls outside ``0..len - 1``.
        """

        entry = self._current.entry_at(rank)
        if entry is None:
            raise IndexError("Entry rank is outside the delta map.")
        return entry

    def min_entry(self) -> SortedMapEntry[K, V] | None:
        """The least current entry by key, or ``None`` when empty. O(1) worst case.

        An endpoint digit read on the finger-tree substrate, matching the reference baseline.
        """

        return self._current.min_entry()

    def max_entry(self) -> SortedMapEntry[K, V] | None:
        """The greatest current entry by key, or ``None`` when empty. O(1) worst case.

        An endpoint digit read; see :meth:`min_entry`.
        """

        return self._current.max_entry()

    def floor_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The greatest current entry whose key is at most ``key``. O(log N)."""

        return self._current.floor_entry(key)

    def ceiling_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The least current entry whose key is at least ``key``. O(log N)."""

        return self._current.ceiling_entry(key)

    def lower_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The greatest current entry whose key is strictly below ``key``. O(log N)."""

        return self._current.lower_entry(key)

    def higher_entry(self, key: K) -> SortedMapEntry[K, V] | None:
        """The least current entry whose key is strictly above ``key``. O(log N)."""

        return self._current.higher_entry(key)

    def get_key_range(self, low: K, high: K) -> SortedMap[K, V]:
        """The current entries whose keys lie in the inclusive range as a plain map. O(log N).

        An inverted range -- ``low`` comparing greater than ``high`` under the retained comparator
        -- yields an empty map. This is a read: the result is an ordinary ordered map and does not
        open a delta epoch of its own.
        """

        return self._current.get_key_range(low, high)

    def keys(self) -> list[K]:
        """Copy the current keys into a list, in ascending order. Theta(n)."""

        return self._current.keys()

    def values(self) -> list[V]:
        """Copy the current values into a list, in ascending key order. Theta(n)."""

        return self._current.values()

    def to_list(self) -> list[SortedMapEntry[K, V]]:
        """Copy the current entries into a list, in ascending key order. Theta(n)."""

        return self._current.to_list()

    def __iter__(self) -> Iterator[SortedMapEntry[K, V]]:
        """Iterate the current entries in ascending key order."""

        return iter(self._current)

    def get_change(self, key: K) -> PersistentMapChange[K, V] | None:
        """One exact checkpoint-relative net change, or ``None`` when the class is unchanged.

        O(log(k + 1)), searching the change index rather than the current state.
        """

        entry = self._changes.get_entry(key)
        return None if entry is None else _to_change(entry)

    def get_changes(self) -> Iterator[PersistentMapChange[K, V]]:
        """Iterate the exact net changes once each in ascending key order.

        Theta(k + 1) when fully consumed, which is output-optimal, and free of key and value policy
        callbacks. The walk is lazy, so abandoning it early skips the rest; the leftmost spine is
        pushed first, so the first element costs Theta(log(k + 1)).
        """

        for entry in self._changes:
            yield _to_change(entry)

    def changes_in_range(self, low: K, high: K) -> Iterator[PersistentMapChange[K, V]]:
        """Iterate the net changes whose keys lie in the inclusive range ``[low, high]``.

        Ordering, endpoints, kinds, and representatives are exactly those of
        :meth:`get_changes`. The change index is itself an ordered map under the same retained
        comparator, so this is a boundary seek plus a bounded walk over the in-range sub-index,
        never a filter over all ``k`` records: O(log(k + 1)) key comparisons locate both boundaries,
        O(log(k + 1)) nodes are copied by the two splits that isolate them, and each yielded change
        then costs Theta(1). No value comparison happens at all. The seek is deferred to the first
        step of the iteration.

        An inverted range yields no changes rather than failing, matching :meth:`get_key_range`.
        "Inverted" is decided by the retained comparator, so under a descending order the low
        endpoint is the numerically greater key.
        """

        for entry in self._changes.get_key_range(low, high):
            yield _to_change(entry)

    def set_item(self, key: K, value: V) -> PersistentDeltaMap[K, V]:
        """Add or replace one current entry and coalesce its checkpoint-relative change. O(log N).

        A write whose value the retained relation considers equal to the current value is a semantic
        no-op and returns the receiver itself, sharing all three roots. A write that returns a class
        to its checkpoint state deletes that class's record instead of recording an update.
        """

        current_entry = self._current.get_entry(key)
        if current_entry is not None and self.value_equals(current_entry.value, value):
            return self

        change_entry = self._changes.get_entry(key)
        # A class present at the checkpoint keeps its baseline representative; a still-active
        # addition episode keeps its first representative; only a genuinely new class takes the
        # supplied key.
        if current_entry is not None:
            representative = current_entry.key
        elif change_entry is not None:
            representative = change_entry.key
        else:
            representative = key
        # The first effective write captures ``before``; later writes preserve it.
        if change_entry is not None:
            before = change_entry.value.before
        elif current_entry is not None:
            before = DeltaMapValue(current_entry.value)
        else:
            before = None
        after = DeltaMapValue(value)

        next_current = _forced_set(self._current, representative, value)
        next_changes = (
            self._changes.remove(representative)
            if self._endpoints_equal(before, after)
            else self._changes.set_item(representative, _DeltaRecord(before, after))
        )
        return self._successor(next_current, next_changes)

    def set_items(
        self, items: Iterable[tuple[K, V] | SortedMapEntry[K, V]]
    ) -> PersistentDeltaMap[K, V]:
        """Apply ``items`` in iteration order, exactly as repeated :meth:`set_item` would.

        O(m log N) for ``m`` supplied entries. This is a convenience and an allocation saving over
        repeated single assignment, not a different algorithm: it *is* the left fold of
        :meth:`set_item`, so every coalescing, cancellation, representative-retention, and
        checkpoint-snap rule holds verbatim and no stronger bound is claimed. A later entry for the
        same class wins. An empty sequence, or one whose every entry is a semantic no-op, returns
        the receiver. Because each intermediate value is itself an immutable successor that
        published nothing, a policy raising partway leaves the receiver and every retained branch
        unchanged.
        """

        result = self
        for item in items:
            key, value = (item.key, item.value) if isinstance(item, SortedMapEntry) else item
            result = result.set_item(key, value)
        return result

    def remove(self, key: K) -> PersistentDeltaMap[K, V]:
        """Remove one current entry and coalesce its checkpoint-relative change. O(log N).

        Removing an absent key is a no-op that returns the receiver itself. Removing a class added
        since the checkpoint cancels its record instead of recording a removal.
        """

        current_entry = self._current.get_entry(key)
        if current_entry is None:
            return self

        change_entry = self._changes.get_entry(key)
        representative = current_entry.key if change_entry is None else change_entry.key
        before = (
            DeltaMapValue(current_entry.value)
            if change_entry is None
            else change_entry.value.before
        )
        after = None

        next_current = self._current.remove(current_entry.key)
        next_changes = (
            self._changes.remove(representative)
            if self._endpoints_equal(before, after)
            else self._changes.set_item(representative, _DeltaRecord(before, after))
        )
        return self._successor(next_current, next_changes)

    def checkpoint(self) -> PersistentDeltaMap[K, V]:
        """Make the current snapshot the new checkpoint and reset the change index. O(1).

        An already-clean version returns the receiver itself. No key or value policy callback is
        invoked either way.
        """

        if self.is_clean:
            return self
        changes: SortedMap[K, _DeltaRecord[V]] = SortedMap.empty(self.comparator)
        return PersistentDeltaMap(
            self._current, self._current, changes, self.comparator, self.value_equals
        )

    def rollback(self) -> PersistentDeltaMap[K, V]:
        """Return to this version's checkpoint and reset the change index. O(1).

        An already-clean version returns the receiver itself. No key or value policy callback is
        invoked either way.
        """

        if self.is_clean:
            return self
        changes: SortedMap[K, _DeltaRecord[V]] = SortedMap.empty(self.comparator)
        return PersistentDeltaMap(
            self._checkpoint, self._checkpoint, changes, self.comparator, self.value_equals
        )

    def shares_storage_with(self, other: PersistentDeltaMap[K, V]) -> bool:
        """Whether all three of both versions' roots share at least one node by object identity.

        A representation test used to show that a no-op or a cancellation avoided copying, not an
        equality test, and deliberately *not* a same-root test: the substrate answers node overlap,
        so two versions that merely descend from a common ancestor can report ``True`` while holding
        different entries. Use ``is`` on the snapshots when root identity is what you mean.
        O(1) when the compared roots are the same object; a negative answer between two different
        roots costs a walk of both node sets. Two empty roots always share, so a negative control
        needs non-empty states.
        """

        return (
            self._current.shares_storage_with(other._current)
            and self._checkpoint.shares_storage_with(other._checkpoint)
            and self._changes.shares_storage_with(other._changes)
        )

    def validate_structure(self) -> DeltaMapStatistics:
        """Walk the whole representation and return its measurements.

        Checks that all three roots are strictly ascending under the retained comparator, that a
        version without changes reuses the exact checkpoint root, that every record has
        non-equivalent endpoints matching the checkpoint and current states, and that the recorded
        change cardinality is exactly the number of differing classes. Raises
        :class:`AssertionError` on the first violation. A defensive audit, not part of any
        operation's cost: it is O((N + k) log N) and does invoke both policies.
        """

        self._require_ascending(
            self._current.keys(), "The current state is not strictly ascending by key."
        )
        self._require_ascending(
            self._checkpoint.keys(), "The checkpoint state is not strictly ascending by key."
        )
        self._require_ascending(
            self._changes.keys(), "The change index is not strictly ascending by key."
        )

        is_clean = self._current is self._checkpoint
        if self._changes.is_empty and not is_clean:
            raise AssertionError("A version without changes does not reuse its checkpoint root.")

        added = removed = updated = 0
        for change in self._changes:
            record = change.value
            if self._endpoints_equal(record.before, record.after):
                raise AssertionError("A recorded change has equivalent endpoints.")
            if not self._endpoint_matches(record.before, self._checkpoint.get_entry(change.key)):
                raise AssertionError(
                    "A change's before endpoint differs from the checkpoint state."
                )
            if not self._endpoint_matches(record.after, self._current.get_entry(change.key)):
                raise AssertionError("A change's after endpoint differs from the current state.")
            if record.before is None:
                added += 1
            elif record.after is None:
                removed += 1
            else:
                updated += 1

        expected = 0
        for baseline in self._checkpoint:
            current = self._current.get_entry(baseline.key)
            if current is None or not self.value_equals(baseline.value, current.value):
                expected += 1
                if self._changes.get_entry(baseline.key) is None:
                    raise AssertionError("A checkpoint difference has no recorded change.")
        for entry in self._current:
            if self._checkpoint.get_entry(entry.key) is None:
                expected += 1
                if self._changes.get_entry(entry.key) is None:
                    raise AssertionError("A current addition has no recorded change.")
        if expected != len(self._changes):
            raise AssertionError("The recorded change cardinality is not exact.")

        return DeltaMapStatistics(
            count=len(self._current),
            checkpoint_count=len(self._checkpoint),
            change_count=len(self._changes),
            added_count=added,
            removed_count=removed,
            updated_count=updated,
            is_clean=is_clean,
        )

    def __repr__(self) -> str:
        """A diagnostic summary naming the current size and the net-changed class count."""

        return (
            f"PersistentDeltaMap(size={len(self._current)}, change_count={len(self._changes)}, "
            f"is_clean={self.is_clean})"
        )

    def _successor(
        self, current: SortedMap[K, V], changes: SortedMap[K, _DeltaRecord[V]]
    ) -> PersistentDeltaMap[K, V]:
        # A wholly cancelled epoch snaps back to the exact checkpoint root, so clean versions are
        # canonical and later checkpoint, rollback, and cleanliness queries stay O(1).
        if changes.is_empty:
            current = self._checkpoint
        return PersistentDeltaMap(
            current, self._checkpoint, changes, self.comparator, self.value_equals
        )

    def _endpoints_equal(
        self, left: DeltaMapValue[V] | None, right: DeltaMapValue[V] | None
    ) -> bool:
        if left is None:
            return right is None
        if right is None:
            return False
        return self.value_equals(left.value, right.value)

    def _endpoint_matches(
        self, endpoint: DeltaMapValue[V] | None, stored: SortedMapEntry[K, V] | None
    ) -> bool:
        if endpoint is None:
            return stored is None
        if stored is None:
            return False
        return self.value_equals(endpoint.value, stored.value)

    def _require_ascending(self, keys: list[K], message: str) -> None:
        for previous, key in pairwise(keys):
            if self.comparator(previous, key) >= 0:
                raise AssertionError(message)


def _to_change(entry: SortedMapEntry[K, _DeltaRecord[V]]) -> PersistentMapChange[K, V]:
    return PersistentMapChange(entry.key, entry.value.before, entry.value.after)


__all__ = [
    "DeltaMapStatistics",
    "DeltaMapValue",
    "PersistentDeltaMap",
    "PersistentMapChange",
    "PersistentMapChangeKind",
]
