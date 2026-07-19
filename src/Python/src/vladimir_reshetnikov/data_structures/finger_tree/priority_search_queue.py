"""Persistent key-ordered, winner-cached AVL priority-search queues."""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .ordering import Comparator, default_comparator

K = TypeVar("K")
P = TypeVar("P")
V = TypeVar("V")

_MISSING = object()


def _equal_value(left: object, right: object) -> bool:
    return left is right or left == right


@dataclass(frozen=True, slots=True)
class PrioritySearchEntry(Generic[K, P, V]):
    key: K
    priority: P
    value: V


@dataclass(frozen=True, slots=True)
class _Node(Generic[K, P, V]):
    entry: PrioritySearchEntry[K, P, V]
    left: _Node[K, P, V] | None
    right: _Node[K, P, V] | None
    winner: PrioritySearchEntry[K, P, V]
    height: int
    count: int

    def __init__(
        self,
        entry: PrioritySearchEntry[K, P, V],
        left: _Node[K, P, V] | None,
        right: _Node[K, P, V] | None,
        winner: PrioritySearchEntry[K, P, V],
    ) -> None:
        object.__setattr__(self, "entry", entry)
        object.__setattr__(self, "left", left)
        object.__setattr__(self, "right", right)
        object.__setattr__(self, "winner", winner)
        object.__setattr__(
            self,
            "height",
            1 + max(0 if left is None else left.height, 0 if right is None else right.height),
        )
        object.__setattr__(
            self,
            "count",
            1 + (0 if left is None else left.count) + (0 if right is None else right.count),
        )


@dataclass(frozen=True, slots=True)
class PrioritySearchQueueStatistics:
    count: int
    height: int
    maximum_absolute_balance_factor: int


@dataclass(frozen=True, slots=True)
class PrioritySearchCursorPeek(Generic[K, P, V]):
    value: PrioritySearchEntry[K, P, V]


@dataclass(frozen=True, slots=True)
class PrioritySearchCursorSearch(Generic[K, P, V]):
    found: bool
    cursor: PrioritySearchQueueCursor[K, P, V]


@dataclass(frozen=True, slots=True)
class PrioritySearchAddResult(Generic[K, P, V]):
    added: bool
    queue: PrioritySearchQueue[K, P, V]


@dataclass(frozen=True, slots=True)
class PrioritySearchRemoveResult(Generic[K, P, V]):
    removed: bool
    entry: PrioritySearchEntry[K, P, V] | None
    queue: PrioritySearchQueue[K, P, V]


@dataclass(frozen=True, slots=True)
class PrioritySearchMinimumView(Generic[K, P, V]):
    entry: PrioritySearchEntry[K, P, V]
    remainder: PrioritySearchQueue[K, P, V]


@dataclass(frozen=True, slots=True)
class _SetResult(Generic[K, P, V]):
    node: _Node[K, P, V]
    added: bool
    changed: bool


class PrioritySearchQueue(Generic[K, P, V]):
    """Persistent key-ordered AVL tree caching the priority-then-key winner."""

    __slots__ = ("_root", "key_comparator", "priority_comparator")

    def __init__(
        self,
        root: _Node[K, P, V] | None,
        key_comparator: Comparator[K],
        priority_comparator: Comparator[P],
    ) -> None:
        self._root = root
        self.key_comparator = key_comparator
        self.priority_comparator = priority_comparator

    @classmethod
    def empty(
        cls,
        key_comparator: Comparator[K] = default_comparator,
        priority_comparator: Comparator[P] = default_comparator,
    ) -> PrioritySearchQueue[K, P, V]:
        return cls(None, key_comparator, priority_comparator)

    @classmethod
    def from_iterable(
        cls,
        entries: Iterable[PrioritySearchEntry[K, P, V]],
        key_comparator: Comparator[K] = default_comparator,
        priority_comparator: Comparator[P] = default_comparator,
    ) -> PrioritySearchQueue[K, P, V]:
        result = cls.empty(key_comparator, priority_comparator)
        for entry in entries:
            result = result.set_item(entry.key, entry.priority, entry.value)
        return result

    def __len__(self) -> int:
        return 0 if self._root is None else self._root.count

    @property
    def count(self) -> int:
        return len(self)

    @property
    def size(self) -> int:
        return len(self)

    @property
    def is_empty(self) -> bool:
        return self._root is None

    @property
    def height(self) -> int:
        return 0 if self._root is None else self._root.height

    @property
    def minimum(self) -> PrioritySearchEntry[K, P, V]:
        if self._root is None:
            raise IndexError("The priority search queue is empty.")
        return self._root.winner

    def minimum_or_none(self) -> PrioritySearchEntry[K, P, V] | None:
        return None if self._root is None else self._root.winner

    def _find(self, key: K) -> _Node[K, P, V] | None:
        node = self._root
        while node is not None:
            comparison = self.key_comparator(key, node.entry.key)
            if comparison == 0:
                return node
            node = node.left if comparison < 0 else node.right
        return None

    def contains_key(self, key: K) -> bool:
        return self._find(key) is not None

    def __contains__(self, key: object) -> bool:
        return self.contains_key(cast(K, key))

    def get_entry(self, key: K) -> PrioritySearchEntry[K, P, V] | None:
        found = self._find(key)
        return None if found is None else found.entry

    def _before(
        self,
        left: PrioritySearchEntry[K, P, V],
        right: PrioritySearchEntry[K, P, V],
    ) -> bool:
        priority = self.priority_comparator(left.priority, right.priority)
        return priority < 0 or (priority == 0 and self.key_comparator(left.key, right.key) < 0)

    def _winner(
        self,
        entry: PrioritySearchEntry[K, P, V],
        left: _Node[K, P, V] | None,
        right: _Node[K, P, V] | None,
    ) -> PrioritySearchEntry[K, P, V]:
        winner = entry
        if left is not None and self._before(left.winner, winner):
            winner = left.winner
        if right is not None and self._before(right.winner, winner):
            winner = right.winner
        return winner

    def _node(
        self,
        entry: PrioritySearchEntry[K, P, V],
        left: _Node[K, P, V] | None,
        right: _Node[K, P, V] | None,
    ) -> _Node[K, P, V]:
        return _Node(entry, left, right, self._winner(entry, left, right))

    def _rotate_left(self, node: _Node[K, P, V]) -> _Node[K, P, V]:
        pivot = node.right
        if pivot is None:
            raise AssertionError("A left rotation requires a right child.")
        return self._node(
            pivot.entry,
            self._node(node.entry, node.left, pivot.left),
            pivot.right,
        )

    def _rotate_right(self, node: _Node[K, P, V]) -> _Node[K, P, V]:
        pivot = node.left
        if pivot is None:
            raise AssertionError("A right rotation requires a left child.")
        return self._node(
            pivot.entry,
            pivot.left,
            self._node(node.entry, pivot.right, node.right),
        )

    def _balance(self, node: _Node[K, P, V]) -> _Node[K, P, V]:
        factor = (0 if node.left is None else node.left.height) - (
            0 if node.right is None else node.right.height
        )
        if factor > 1:
            left = node.left
            if left is None:
                raise AssertionError("Invalid AVL left imbalance.")
            if (0 if left.left is None else left.left.height) < (
                0 if left.right is None else left.right.height
            ):
                return self._rotate_right(
                    self._node(node.entry, self._rotate_left(left), node.right)
                )
            return self._rotate_right(node)
        if factor < -1:
            right = node.right
            if right is None:
                raise AssertionError("Invalid AVL right imbalance.")
            if (0 if right.right is None else right.right.height) < (
                0 if right.left is None else right.left.height
            ):
                return self._rotate_left(
                    self._node(node.entry, node.left, self._rotate_right(right))
                )
            return self._rotate_left(node)
        return node

    def _set(
        self,
        node: _Node[K, P, V] | None,
        entry: PrioritySearchEntry[K, P, V],
        overwrite: bool,
    ) -> _SetResult[K, P, V]:
        if node is None:
            return _SetResult(self._node(entry, None, None), True, True)
        comparison = self.key_comparator(entry.key, node.entry.key)
        if comparison == 0:
            unchanged = not overwrite or (
                self.priority_comparator(entry.priority, node.entry.priority) == 0
                and _equal_value(entry.priority, node.entry.priority)
                and _equal_value(entry.value, node.entry.value)
            )
            if unchanged:
                return _SetResult(node, False, False)
            replacement = PrioritySearchEntry(node.entry.key, entry.priority, entry.value)
            return _SetResult(self._node(replacement, node.left, node.right), False, True)
        if comparison < 0:
            result = self._set(node.left, entry, overwrite)
            if not result.changed:
                return _SetResult(node, result.added, False)
            return _SetResult(
                self._balance(self._node(node.entry, result.node, node.right)),
                result.added,
                True,
            )
        result = self._set(node.right, entry, overwrite)
        if not result.changed:
            return _SetResult(node, result.added, False)
        return _SetResult(
            self._balance(self._node(node.entry, node.left, result.node)),
            result.added,
            True,
        )

    def set_item(self, key: K, priority: P, value: V) -> PrioritySearchQueue[K, P, V]:
        result = self._set(self._root, PrioritySearchEntry(key, priority, value), True)
        if result.node is self._root:
            return self
        return PrioritySearchQueue(result.node, self.key_comparator, self.priority_comparator)

    def try_add(self, key: K, priority: P, value: V) -> PrioritySearchAddResult[K, P, V]:
        result = self._set(self._root, PrioritySearchEntry(key, priority, value), False)
        queue = (
            PrioritySearchQueue(result.node, self.key_comparator, self.priority_comparator)
            if result.added
            else self
        )
        return PrioritySearchAddResult(result.added, queue)

    @staticmethod
    def _minimum_key(node: _Node[K, P, V]) -> _Node[K, P, V]:
        current = node
        while current.left is not None:
            current = current.left
        return current

    def _remove(
        self, node: _Node[K, P, V] | None, key: K
    ) -> tuple[_Node[K, P, V] | None, PrioritySearchEntry[K, P, V] | None]:
        if node is None:
            return None, None
        comparison = self.key_comparator(key, node.entry.key)
        if comparison == 0:
            if node.left is None:
                return node.right, node.entry
            if node.right is None:
                return node.left, node.entry
            successor = self._minimum_key(node.right)
            right, removed = self._remove(node.right, successor.entry.key)
            if removed is None:
                raise AssertionError("The selected successor must be removable.")
            return self._balance(self._node(successor.entry, node.left, right)), node.entry
        if comparison < 0:
            left, removed = self._remove(node.left, key)
            if removed is None:
                return node, None
            return self._balance(self._node(node.entry, left, node.right)), removed
        right, removed = self._remove(node.right, key)
        if removed is None:
            return node, None
        return self._balance(self._node(node.entry, node.left, right)), removed

    def remove(self, key: K) -> PrioritySearchQueue[K, P, V]:
        node, entry = self._remove(self._root, key)
        return (
            self
            if entry is None
            else PrioritySearchQueue(node, self.key_comparator, self.priority_comparator)
        )

    def try_remove(self, key: K) -> PrioritySearchRemoveResult[K, P, V]:
        node, entry = self._remove(self._root, key)
        queue = (
            self
            if entry is None
            else PrioritySearchQueue(node, self.key_comparator, self.priority_comparator)
        )
        return PrioritySearchRemoveResult(entry is not None, entry, queue)

    def delete_minimum(self) -> PrioritySearchMinimumView[K, P, V]:
        entry = self.minimum
        node, removed = self._remove(self._root, entry.key)
        if removed is None:
            raise AssertionError("Cached winner is missing by key.")
        return PrioritySearchMinimumView(
            entry, PrioritySearchQueue(node, self.key_comparator, self.priority_comparator)
        )

    def minimum_view(self) -> PrioritySearchMinimumView[K, P, V] | None:
        return None if self.is_empty else self.delete_minimum()

    def enumerate_at_most(
        self, minimum_key: K, maximum_key: K, maximum_priority: P
    ) -> Iterator[PrioritySearchEntry[K, P, V]]:
        if self.key_comparator(minimum_key, maximum_key) > 0:
            raise ValueError("The minimum key must not follow the maximum key.")
        if self._root is None:
            return
        pending: list[tuple[_Node[K, P, V], bool]] = [(self._root, False)]
        while pending:
            node, emit = pending.pop()
            if emit:
                yield node.entry
                continue
            if self.priority_comparator(node.winner.priority, maximum_priority) > 0:
                continue
            lower = self.key_comparator(node.entry.key, minimum_key)
            upper = self.key_comparator(node.entry.key, maximum_key)
            if upper < 0 and node.right is not None:
                pending.append((node.right, False))
            if (
                lower >= 0
                and upper <= 0
                and self.priority_comparator(node.entry.priority, maximum_priority) <= 0
            ):
                pending.append((node, True))
            if lower > 0 and node.left is not None:
                pending.append((node.left, False))

    def validate_structure(self) -> PrioritySearchQueueStatistics:
        if self._root is None:
            return PrioritySearchQueueStatistics(0, 0, 0)
        pending: list[tuple[_Node[K, P, V], object, object]] = [(self._root, _MISSING, _MISSING)]
        count = maximum_balance = 0
        while pending:
            node, lower, upper = pending.pop()
            if lower is not _MISSING and self.key_comparator(node.entry.key, cast(K, lower)) <= 0:
                raise ValueError("PSQ lower key-order invariant failed.")
            if upper is not _MISSING and self.key_comparator(node.entry.key, cast(K, upper)) >= 0:
                raise ValueError("PSQ upper key-order invariant failed.")
            left_height = 0 if node.left is None else node.left.height
            right_height = 0 if node.right is None else node.right.height
            balance = left_height - right_height
            expected_count = (
                1
                + (0 if node.left is None else node.left.count)
                + (0 if node.right is None else node.right.count)
            )
            if abs(balance) > 1 or node.height != 1 + max(left_height, right_height):
                raise ValueError("PSQ AVL height invariant failed.")
            if node.count != expected_count:
                raise ValueError("PSQ AVL count invariant failed.")
            winner = self._winner(node.entry, node.left, node.right)
            if winner is not node.winner:
                raise ValueError("PSQ winner cache invariant failed.")
            count += 1
            maximum_balance = max(maximum_balance, abs(balance))
            if node.right is not None:
                pending.append((node.right, node.entry.key, upper))
            if node.left is not None:
                pending.append((node.left, lower, node.entry.key))
        if count != len(self):
            raise ValueError("PSQ count invariant failed.")
        return PrioritySearchQueueStatistics(count, self.height, maximum_balance)

    def shared_node_count(self, other: PrioritySearchQueue[K, P, V]) -> int:
        def collect(root: _Node[K, P, V] | None) -> set[int]:
            destination: set[int] = set()
            pending = [] if root is None else [root]
            while pending:
                node = pending.pop()
                identity = id(node)
                if identity in destination:
                    continue
                destination.add(identity)
                if node.left is not None:
                    pending.append(node.left)
                if node.right is not None:
                    pending.append(node.right)
            return destination

        return len(collect(self._root) & collect(other._root))

    def _bound_rank(self, key: K, upper: bool) -> int:
        rank = 0
        node = self._root
        while node is not None:
            comparison = self.key_comparator(node.entry.key, key)
            if comparison < 0 or (upper and comparison == 0):
                rank += (0 if node.left is None else node.left.count) + 1
                node = node.right
            else:
                node = node.left
        return rank

    def _entry_at(self, rank: int) -> PrioritySearchEntry[K, P, V]:
        node = self._root
        while node is not None:
            left_count = 0 if node.left is None else node.left.count
            if rank < left_count:
                node = node.left
            elif rank == left_count:
                return node.entry
            else:
                rank -= left_count + 1
                node = node.right
        raise IndexError("Rank is outside the priority-search queue.")

    def cursor_at(self, position: int = 0) -> PrioritySearchQueueCursor[K, P, V]:
        return PrioritySearchQueueCursor(self, position)

    def cursor_at_lower_bound(self, key: K) -> PrioritySearchQueueCursor[K, P, V]:
        return self.cursor_at(self._bound_rank(key, False))

    def cursor_at_upper_bound(self, key: K) -> PrioritySearchQueueCursor[K, P, V]:
        return self.cursor_at(self._bound_rank(key, True))

    def find_cursor(self, key: K) -> PrioritySearchCursorSearch[K, P, V]:
        cursor = self.cursor_at_lower_bound(key)
        candidate = cursor.peek_next()
        return PrioritySearchCursorSearch(
            candidate is not None and self.key_comparator(candidate.value.key, key) == 0,
            cursor,
        )

    def cursor_at_minimum_priority(self) -> PrioritySearchQueueCursor[K, P, V]:
        return (
            self.cursor_at()
            if self._root is None
            else self.cursor_at_lower_bound(self._root.winner.key)
        )

    def __iter__(self) -> Iterator[PrioritySearchEntry[K, P, V]]:
        pending: list[_Node[K, P, V]] = []
        current = self._root
        while current is not None or pending:
            while current is not None:
                pending.append(current)
                current = current.left
            node = pending.pop()
            yield node.entry
            current = node.right


@dataclass(frozen=True, slots=True)
class PrioritySearchQueueCursor(Generic[K, P, V]):
    """Immutable key-order root-plus-rank cursor over a priority-search queue."""

    queue: PrioritySearchQueue[K, P, V]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > len(self.queue):
            raise IndexError("Cursor position is outside the priority-search queue.")

    @property
    def count(self) -> int:
        return len(self.queue)

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> PrioritySearchCursorPeek[K, P, V] | None:
        if self.is_at_start:
            return None
        return PrioritySearchCursorPeek(self.queue._entry_at(self.position - 1))

    def peek_next(self) -> PrioritySearchCursorPeek[K, P, V] | None:
        if self.is_at_end:
            return None
        return PrioritySearchCursorPeek(self.queue._entry_at(self.position))

    def move_previous(self) -> PrioritySearchQueueCursor[K, P, V]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PrioritySearchQueueCursor(self.queue, self.position - 1)

    def move_next(self) -> PrioritySearchQueueCursor[K, P, V]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PrioritySearchQueueCursor(self.queue, self.position + 1)

    def seek_rank(self, position: int) -> PrioritySearchQueueCursor[K, P, V]:
        return (
            self if position == self.position else PrioritySearchQueueCursor(self.queue, position)
        )

    def insert(self, key: K, priority: P, value: V) -> PrioritySearchQueueCursor[K, P, V]:
        position = self.queue._bound_rank(key, False)
        result = self.queue.try_add(key, priority, value)
        if not result.added:
            raise ValueError("An equivalent key is already present.")
        return PrioritySearchQueueCursor(result.queue, position + 1)

    def try_insert(self, key: K, priority: P, value: V) -> PrioritySearchCursorSearch[K, P, V]:
        position = self.queue._bound_rank(key, False)
        result = self.queue.try_add(key, priority, value)
        cursor = (
            PrioritySearchQueueCursor(result.queue, position + 1)
            if result.added
            else PrioritySearchQueueCursor(self.queue, position)
        )
        return PrioritySearchCursorSearch(result.added, cursor)

    def set_item(self, key: K, priority: P, value: V) -> PrioritySearchQueueCursor[K, P, V]:
        location = self.queue.find_cursor(key)
        return PrioritySearchQueueCursor(
            self.queue.set_item(key, priority, value),
            location.cursor.position if location.found else location.cursor.position + 1,
        )

    def set_next(self, priority: P, value: V) -> PrioritySearchQueueCursor[K, P, V]:
        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return PrioritySearchQueueCursor(
            self.queue.set_item(entry.value.key, priority, value), self.position
        )

    def delete_previous(self) -> PrioritySearchQueueCursor[K, P, V]:
        entry = self.peek_previous()
        if entry is None:
            raise IndexError("No entry precedes the cursor.")
        return PrioritySearchQueueCursor(self.queue.remove(entry.value.key), self.position - 1)

    def delete_next(self) -> PrioritySearchQueueCursor[K, P, V]:
        entry = self.peek_next()
        if entry is None:
            raise IndexError("No entry follows the cursor.")
        return PrioritySearchQueueCursor(self.queue.remove(entry.value.key), self.position)

    def snapshot(self) -> PrioritySearchQueue[K, P, V]:
        return self.queue


__all__ = [
    "PrioritySearchAddResult",
    "PrioritySearchCursorPeek",
    "PrioritySearchCursorSearch",
    "PrioritySearchEntry",
    "PrioritySearchMinimumView",
    "PrioritySearchQueue",
    "PrioritySearchQueueCursor",
    "PrioritySearchQueueStatistics",
    "PrioritySearchRemoveResult",
]
