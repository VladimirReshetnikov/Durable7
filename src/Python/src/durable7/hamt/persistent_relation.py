"""Persistent many-to-many relation over mutually inverse hash multimaps."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from threading import Lock
from typing import Generic, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy
from .persistent_hamt import PersistentHashSet
from .persistent_hash_multimap import PersistentHashMultimap

L = TypeVar("L")
R = TypeVar("R")


@dataclass(frozen=True, slots=True)
class RelationEntry(Generic[L, R]):
    """One globally normalized representative pair."""

    left: L
    right: R


class PersistentRelation(Generic[L, R]):
    """Immutable many-to-many relation with mutually inverse adjacency indexes.

    Every equivalence class has one global first representative, even when it occurs in several
    groups. The cached ``inverse`` facade swaps the existing roots in O(1).
    """

    __slots__ = ("_forward", "_inverse_lock", "_inverse_view", "_reverse")

    def __init__(
        self,
        forward: PersistentHashMultimap[L, R],
        reverse: PersistentHashMultimap[R, L],
    ) -> None:
        """Wrap two already-built, mutually inverse indexes; use :meth:`empty` or
        :meth:`from_items`.

        The caller is responsible for ``forward`` and ``reverse`` describing the same pair set.
        """

        self._forward = forward
        self._reverse = reverse
        self._inverse_lock = Lock()
        self._inverse_view: PersistentRelation[R, L] | None = None

    @classmethod
    def empty(
        cls,
        left_policy: HashPolicy[L] | None = None,
        right_policy: HashPolicy[R] | None = None,
    ) -> PersistentRelation[L, R]:
        """Return an empty relation retaining both exact policy objects."""

        effective_left = default_hash_policy() if left_policy is None else left_policy
        effective_right = default_hash_policy() if right_policy is None else right_policy
        return cls(
            PersistentHashMultimap.empty(effective_left, effective_right),
            PersistentHashMultimap.empty(effective_right, effective_left),
        )

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[L, R]],
        left_policy: HashPolicy[L] | None = None,
        right_policy: HashPolicy[R] | None = None,
    ) -> PersistentRelation[L, R]:
        """Build a relation from pairs, collapsing duplicates under the retained policies."""

        if items is None:
            raise TypeError("items must be iterable.")
        result = cls.empty(left_policy, right_policy)
        for left, right in items:
            result = result.add(left, right)
        return result

    @property
    def pair_count(self) -> int:
        """Number of ``(left, right)`` pairs; both indexes agree on this."""

        return self._forward.pair_count

    @property
    def left_count(self) -> int:
        """Number of distinct left values participating in at least one pair."""

        return self._forward.key_count

    @property
    def right_count(self) -> int:
        """Number of distinct right values participating in at least one pair."""

        return self._reverse.key_count

    @property
    def is_empty(self) -> bool:
        """Whether the relation holds no pairs."""

        return self._forward.is_empty

    @property
    def left_policy(self) -> HashPolicy[L]:
        """The retained hash policy defining left-value equivalence."""

        return self._forward.key_policy

    @property
    def right_policy(self) -> HashPolicy[R]:
        """The retained hash policy defining right-value equivalence."""

        return self._reverse.key_policy

    @property
    def inverse(self) -> PersistentRelation[R, L]:
        """Return the cached inverse facade whose inverse is this object."""

        current = self._inverse_view
        if current is not None:
            return current
        with self._inverse_lock:
            current = self._inverse_view
            if current is None:
                current = PersistentRelation(self._reverse, self._forward)
                current._inverse_view = self
                self._inverse_view = current
            return current

    def __len__(self) -> int:
        """Number of pairs, matching :attr:`pair_count`."""

        return self.pair_count

    def __bool__(self) -> bool:
        """Whether the relation holds at least one pair."""

        return not self.is_empty

    def contains(self, left: L, right: R) -> bool:
        """Whether the pair ``(left, right)`` is present."""

        return self._forward.contains(left, right)

    def contains_left(self, left: L) -> bool:
        """Whether ``left`` participates in at least one pair."""

        return self._forward.contains_key(left)

    def contains_right(self, right: R) -> bool:
        """Whether ``right`` participates in at least one pair.

        As cheap as :meth:`contains_left`, because the reverse index is materialized.
        """

        return self._reverse.contains_key(right)

    def get_rights(self, left: L) -> PersistentHashSet[R]:
        """Return the values related to ``left``, or a policy-preserving empty set when absent."""

        return self._forward.get_values(left)

    def get_lefts(self, right: R) -> PersistentHashSet[L]:
        """Return the values related to ``right``, or a policy-preserving empty set when absent."""

        return self._reverse.get_values(right)

    def add(self, left: L, right: R) -> PersistentRelation[L, R]:
        """Add a pair after normalizing both domains to global first representatives."""

        left_lookup = self._forward.try_get_key(left)
        right_lookup = self._reverse.try_get_key(right)
        stored_left = cast("L", left_lookup.key) if left_lookup.found else left
        stored_right = cast("R", right_lookup.key) if right_lookup.found else right
        if self._forward.contains(stored_left, stored_right):
            return self
        return PersistentRelation(
            self._forward.add(stored_left, stored_right),
            self._reverse.add(stored_right, stored_left),
        )

    def remove(self, left: L, right: R) -> PersistentRelation[L, R]:
        """Remove the pair ``(left, right)`` from both indexes; a no-op when it is absent.

        A value that loses its last pair drops out of the relation, keeping every group non-empty.
        """

        left_lookup = self._forward.try_get_key(left)
        right_lookup = self._reverse.try_get_key(right)
        if not left_lookup.found or not right_lookup.found:
            return self
        stored_left = cast("L", left_lookup.key)
        stored_right = cast("R", right_lookup.key)
        if not self._forward.contains(stored_left, stored_right):
            return self
        return PersistentRelation(
            self._forward.remove(stored_left, stored_right),
            self._reverse.remove(stored_right, stored_left),
        )

    def remove_left(self, left: L) -> PersistentRelation[L, R]:
        """Remove a complete left adjacency group from both indexes."""

        lookup = self._forward.try_get_key(left)
        if not lookup.found:
            return self
        stored_left = cast("L", lookup.key)
        reverse = self._reverse
        for right in self._forward.get_values(stored_left):
            reverse = reverse.remove(right, stored_left)
        return PersistentRelation(self._forward.remove_key(stored_left), reverse)

    def remove_right(self, right: R) -> PersistentRelation[L, R]:
        """Remove a complete right adjacency group from both indexes."""

        lookup = self._reverse.try_get_key(right)
        if not lookup.found:
            return self
        stored_right = cast("R", lookup.key)
        forward = self._forward
        for left in self._reverse.get_values(stored_right):
            forward = forward.remove(left, stored_right)
        return PersistentRelation(forward, self._reverse.remove_key(stored_right))

    def clear(self) -> PersistentRelation[L, R]:
        """Return an empty relation retaining both policies; a no-op when already empty."""

        return self if self.is_empty else self.empty(self.left_policy, self.right_policy)

    def __iter__(self) -> Iterator[RelationEntry[L, R]]:
        """Iterate every ``(left, right)`` pair in the forward index's order."""

        for entry in self._forward:
            yield RelationEntry(entry.key, entry.value)

    def shares_roots_with(self, other: PersistentRelation[L, R]) -> bool:
        """Whether both relations reference the same forward and reverse roots.

        A representation test used to confirm that a no-op avoided copying, not an equality test.
        """

        return self._forward.shares_root_with(other._forward) and self._reverse.shares_root_with(
            other._reverse
        )

    def validate_structure(self) -> bool:
        """Check both indexes, pair counts, and global representative identity."""

        if (
            not self._forward.validate_structure()
            or not self._reverse.validate_structure()
            or self._forward.pair_count != self._reverse.pair_count
        ):
            return False
        for entry in self._forward:
            reverse_key = self._reverse.try_get_key(entry.value)
            reverse_values = self._reverse.get_values(entry.value)
            if (
                not reverse_key.found
                or reverse_key.key is not entry.value
                or not reverse_values.contains(entry.key)
                or reverse_values.get(entry.key) is not entry.key
            ):
                return False
        return all(self._forward.contains(entry.value, entry.key) for entry in self._reverse)


__all__ = ["PersistentRelation", "RelationEntry"]
