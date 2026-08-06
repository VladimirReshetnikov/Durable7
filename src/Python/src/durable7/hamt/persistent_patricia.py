"""Persistent big-endian Patricia maps and sets for signed integer keys."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, field
from typing import Generic, TypeVar

from .hash_policy import same_value

K = TypeVar("K")
V = TypeVar("V")
W = TypeVar("W")
C = TypeVar("C")


@dataclass(frozen=True, slots=True)
class PatriciaMapEntry(Generic[K, V]):
    """Presence-safe map cursor entry; ``value`` may itself be ``None``."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class PatriciaCursorSearch(Generic[C]):
    """Exact-search result whose cursor remains usable on a miss."""

    cursor: C
    found: bool


class _PatriciaNode(Generic[K, V]):
    __slots__ = ()
    count: int


@dataclass(frozen=True, slots=True)
class _PatriciaLeaf(_PatriciaNode[K, V]):
    path: int
    key: K
    value: V
    count: int = field(default=1, init=False)


@dataclass(frozen=True, slots=True)
class _PatriciaBranch(_PatriciaNode[K, V]):
    prefix: int
    mask: int
    left: _PatriciaNode[K, V]
    right: _PatriciaNode[K, V]
    count: int = field(init=False)

    def __post_init__(self) -> None:
        """Cache the subtree's entry count from its two children."""

        object.__setattr__(self, "count", self.left.count + self.right.count)


@dataclass(frozen=True, slots=True)
class _PatriciaChange(Generic[K, V]):
    node: _PatriciaNode[K, V] | None
    changed: bool
    added: bool


def _prefix_of(path: int, mask: int) -> int:
    return path & ~((mask << 1) - 1)


def _join(
    left_path: int,
    left: _PatriciaNode[K, V],
    right_path: int,
    right: _PatriciaNode[K, V],
) -> _PatriciaNode[K, V]:
    difference = left_path ^ right_path
    if difference == 0:
        raise RuntimeError("A nonzero Patricia path difference is required.")
    mask = 1 << (difference.bit_length() - 1)
    prefix = _prefix_of(left_path, mask)
    if left_path & mask == 0:
        return _PatriciaBranch(prefix, mask, left, right)
    return _PatriciaBranch(prefix, mask, right, left)


def _find_leaf(node: _PatriciaNode[K, V], path: int) -> _PatriciaLeaf[K, V] | None:
    current = node
    while isinstance(current, _PatriciaBranch):
        if _prefix_of(path, current.mask) != current.prefix:
            return None
        current = current.right if path & current.mask else current.left
    assert isinstance(current, _PatriciaLeaf)
    return current if current.path == path else None


def _put_node(
    node: _PatriciaNode[K, V] | None,
    path: int,
    key: K,
    value: V,
) -> _PatriciaChange[K, V]:
    if node is None:
        return _PatriciaChange(_PatriciaLeaf(path, key, value), True, True)
    if isinstance(node, _PatriciaLeaf):
        if node.path == path:
            if same_value(node.value, value):
                return _PatriciaChange(node, False, False)
            return _PatriciaChange(_PatriciaLeaf(path, node.key, value), True, False)
        return _PatriciaChange(
            _join(node.path, node, path, _PatriciaLeaf(path, key, value)), True, True
        )
    assert isinstance(node, _PatriciaBranch)
    if _prefix_of(path, node.mask) != node.prefix:
        return _PatriciaChange(
            _join(node.prefix, node, path, _PatriciaLeaf(path, key, value)), True, True
        )
    if path & node.mask == 0:
        child = _put_node(node.left, path, key, value)
        if not child.changed or child.node is None:
            return _PatriciaChange(node, False, False)
        return _PatriciaChange(
            _PatriciaBranch(node.prefix, node.mask, child.node, node.right),
            True,
            child.added,
        )
    child = _put_node(node.right, path, key, value)
    if not child.changed or child.node is None:
        return _PatriciaChange(node, False, False)
    return _PatriciaChange(
        _PatriciaBranch(node.prefix, node.mask, node.left, child.node), True, child.added
    )


def _remove_node(node: _PatriciaNode[K, V] | None, path: int) -> _PatriciaChange[K, V]:
    if node is None:
        return _PatriciaChange(None, False, False)
    if isinstance(node, _PatriciaLeaf):
        if node.path == path:
            return _PatriciaChange(None, True, False)
        return _PatriciaChange(node, False, False)
    assert isinstance(node, _PatriciaBranch)
    if _prefix_of(path, node.mask) != node.prefix:
        return _PatriciaChange(node, False, False)
    if path & node.mask == 0:
        child = _remove_node(node.left, path)
        if not child.changed:
            return _PatriciaChange(node, False, False)
        replacement = (
            node.right
            if child.node is None
            else _PatriciaBranch(node.prefix, node.mask, child.node, node.right)
        )
        return _PatriciaChange(replacement, True, False)
    child = _remove_node(node.right, path)
    if not child.changed:
        return _PatriciaChange(node, False, False)
    replacement = (
        node.left
        if child.node is None
        else _PatriciaBranch(node.prefix, node.mask, node.left, child.node)
    )
    return _PatriciaChange(replacement, True, False)


def _iterate(root: _PatriciaNode[K, V]) -> Iterator[tuple[K, V]]:
    stack: list[_PatriciaNode[K, V]] = [root]
    while stack:
        node = stack.pop()
        if isinstance(node, _PatriciaLeaf):
            yield node.key, node.value
        else:
            assert isinstance(node, _PatriciaBranch)
            stack.append(node.right)
            stack.append(node.left)


class _PatriciaCore(Generic[K, V]):
    __slots__ = ("_encode", "_root", "size")

    def __init__(
        self,
        root: _PatriciaNode[K, V] | None,
        size: int,
        encode: Callable[[K], int],
    ) -> None:
        """Wrap an already-built trie root; the public families construct these."""

        self._root = root
        self.size = size
        self._encode = encode

    def get(self, key: K) -> V | None:
        """The value stored for ``key``, or ``None`` when absent."""

        path = self._encode(key)
        if self._root is None:
            return None
        leaf = _find_leaf(self._root, path)
        return None if leaf is None else leaf.value

    def contains_key(self, key: K) -> bool:
        """Whether ``key`` is present."""

        path = self._encode(key)
        return self._root is not None and _find_leaf(self._root, path) is not None

    def entry_at(self, index: int) -> tuple[K, V] | None:
        """The entry at ordinal ``index`` in ascending key order, or ``None`` when out of range.
        Descends by cached subtree counts rather than walking the entries.
        """

        if index < 0 or index >= self.size or self._root is None:
            return None
        current = self._root
        remaining = index
        while isinstance(current, _PatriciaBranch):
            if remaining < current.left.count:
                current = current.left
            else:
                remaining -= current.left.count
                current = current.right
        assert isinstance(current, _PatriciaLeaf)
        return current.key, current.value

    def lower_bound_rank(self, key: K) -> tuple[int, bool]:
        """The rank where ``key`` belongs, and whether it is actually present. Branch prefixes let
        whole subtrees be skipped, so the descent is bounded by the key width rather than by the
        entry count.
        """

        path = self._encode(key)
        current = self._root
        rank = 0
        while isinstance(current, _PatriciaBranch):
            if _prefix_of(path, current.mask) != current.prefix:
                return (rank, False) if path < current.prefix else (rank + current.count, False)
            if path & current.mask:
                rank += current.left.count
                current = current.right
            else:
                current = current.left
        if current is None:
            return 0, False
        assert isinstance(current, _PatriciaLeaf)
        return (rank + 1, False) if current.path < path else (rank, current.path == path)

    def put(self, key: K, value: V) -> _PatriciaCore[K, V]:
        """Return a trie with ``key`` mapped to ``value``; a write that changes nothing returns the
        receiver.
        """

        change = _put_node(self._root, self._encode(key), key, value)
        if not change.changed:
            return self
        return _PatriciaCore(change.node, self.size + int(change.added), self._encode)

    def remove(self, key: K) -> _PatriciaCore[K, V]:
        """Return a trie without ``key``; a no-op when the key is absent."""

        change = _remove_node(self._root, self._encode(key))
        return (
            self if not change.changed else _PatriciaCore(change.node, self.size - 1, self._encode)
        )

    def union(
        self,
        other: _PatriciaCore[K, V],
        combine: Callable[[K, V, V], V] | None = None,
    ) -> _PatriciaCore[K, V]:
        """Merge ``other`` into this trie, resolving shared keys with ``combine``. Without
        ``combine``, ``other``'s value wins.
        """

        result = self
        for key, right in other:
            found = None if result._root is None else _find_leaf(result._root, result._encode(key))
            value = (
                right
                if found is None or combine is None
                else combine(found.key, found.value, right)
            )
            result = result.put(key, value)
        return result

    def intersect(
        self,
        other: _PatriciaCore[K, V],
        combine: Callable[[K, V, V], V] | None = None,
    ) -> _PatriciaCore[K, V]:
        """Keep only the keys present in both, resolving values with ``combine``. Without
        ``combine``, this trie's value is kept.
        """

        result = _PatriciaCore[K, V](None, 0, self._encode)
        for key, left in self:
            right = None if other._root is None else _find_leaf(other._root, other._encode(key))
            if right is not None:
                result = result.put(
                    key, left if combine is None else combine(key, left, right.value)
                )
        return self if result.content_equals(self) else result

    def except_(self, other: _PatriciaCore[K, W]) -> _PatriciaCore[K, V]:
        """Remove every key that occurs in ``other``."""

        result = self
        for key, _value in other:
            result = result.remove(key)
        return result

    def content_equals(self, other: _PatriciaCore[K, V]) -> bool:
        """Whether both tries hold the same entries."""

        if self is other:
            return True
        if self.size != other.size:
            return False
        for key, value in self:
            found = None if other._root is None else _find_leaf(other._root, other._encode(key))
            if found is None or not same_value(value, found.value):
                return False
        return True

    def shares_root_with(self, other: _PatriciaCore[K, V]) -> bool:
        """Whether both tries reference the same root, so neither can observe an edit made to the
        other. A representation test, not an equality test.
        """

        return self._root is other._root

    def __iter__(self) -> Iterator[tuple[K, V]]:
        """Iterate the entries in ascending unsigned key order."""

        return iter(()) if self._root is None else _iterate(self._root)


def _encode_int32(key: int) -> int:
    if not isinstance(key, int) or key < -(1 << 31) or key >= 1 << 31:
        raise ValueError("PersistentIntMap keys must be signed 32-bit integers.")
    return (key & 0xFFFF_FFFF) ^ 0x8000_0000


def _encode_int64(key: int) -> int:
    if not isinstance(key, int) or key < -(1 << 63) or key >= 1 << 63:
        raise ValueError("PersistentLongMap keys must be signed 64-bit integers.")
    return (key & 0xFFFF_FFFF_FFFF_FFFF) ^ 0x8000_0000_0000_0000


class PersistentIntMap(Generic[V]):
    """Persistent big-endian Patricia map over signed 32-bit integer keys."""

    __slots__ = ("_core",)

    def __init__(self, core: _PatriciaCore[int, V]) -> None:
        """Wrap an already-built core; use :meth:`empty` or the ``from_`` factory."""

        self._core = core

    @classmethod
    def empty(cls) -> PersistentIntMap[V]:
        """Return the empty map."""

        return cls(_PatriciaCore(None, 0, _encode_int32))

    @classmethod
    def from_items(cls, items: Iterable[tuple[int, V]]) -> PersistentIntMap[V]:
        """Build a map from pairs; a repeated key keeps the last value."""

        result = cls.empty()
        for key, value in items:
            result = result.put(key, value)
        return result

    @property
    def size(self) -> int:
        """Number of entries."""

        return self._core.size

    @property
    def is_empty(self) -> bool:
        """Whether the map holds no entries."""

        return self.size == 0

    def __len__(self) -> int:
        """Number of entries, matching :attr:`size`."""

        return self.size

    def get(self, key: int) -> V | None:
        """The value stored for ``key``, or ``None`` when absent."""

        return self._core.get(key)

    def contains_key(self, key: int) -> bool:
        """Whether ``key`` is present."""

        return self._core.contains_key(key)

    def cursor(self, position: int = 0) -> PersistentIntMapCursor[V]:
        """Return an immutable ordered cursor at a rank gap."""
        return PersistentIntMapCursor(self, position)

    def cursor_at_end(self) -> PersistentIntMapCursor[V]:
        """Return an immutable ordered cursor after the final entry."""
        return PersistentIntMapCursor(self, self.size)

    def lower_bound_cursor(self, key: int) -> PersistentIntMapCursor[V]:
        """Return the gap before the first key not less than ``key``."""
        rank, _found = self._core.lower_bound_rank(key)
        return PersistentIntMapCursor(self, rank)

    def upper_bound_cursor(self, key: int) -> PersistentIntMapCursor[V]:
        """Return the gap before the first key greater than ``key``."""
        rank, found = self._core.lower_bound_rank(key)
        return PersistentIntMapCursor(self, rank + int(found))

    def cursor_at_key(self, key: int) -> PatriciaCursorSearch[PersistentIntMapCursor[V]]:
        """Return a usable lower-bound cursor and exact-key discriminator."""
        rank, found = self._core.lower_bound_rank(key)
        return PatriciaCursorSearch(PersistentIntMapCursor(self, rank), found)

    def put(self, key: int, value: V) -> PersistentIntMap[V]:
        """Return a map with ``key`` mapped to ``value``, adding or replacing as needed. Only the
        path from the root to the affected branch is copied.
        """

        return self._with_core(self._core.put(key, value))

    def remove(self, key: int) -> PersistentIntMap[V]:
        """Return a map without that key; a no-op when it is absent."""

        return self._with_core(self._core.remove(key))

    def clear(self) -> PersistentIntMap[V]:
        """Return the empty map; a no-op when already empty."""

        return self if self.is_empty else PersistentIntMap.empty()

    def union(
        self,
        other: PersistentIntMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentIntMap[V]:
        """Return the entries of both maps. Subtrees the operands already share are adopted whole
        rather than re-entered.
        """

        return self._with_core(self._core.union(other._core, combine))

    def intersect(
        self,
        other: PersistentIntMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentIntMap[V]:
        """Return the entries present in both maps."""

        return self._with_core(self._core.intersect(other._core, combine))

    def except_(self, other: PersistentIntMap[W]) -> PersistentIntMap[V]:
        """Return this map's entries that are absent from ``other``. Named with a trailing
        underscore because ``except`` is a Python keyword.
        """

        return self._with_core(self._core.except_(other._core))

    def shares_root_with(self, other: PersistentIntMap[V]) -> bool:
        """Whether both maps reference the same trie root. A representation test, not an equality
        test.
        """

        return self._core.shares_root_with(other._core)

    def __iter__(self) -> Iterator[tuple[int, V]]:
        """Iterate the entries in ascending key order."""

        return iter(self._core)

    def _entry_at(self, index: int) -> PatriciaMapEntry[int, V] | None:
        entry = self._core.entry_at(index)
        return None if entry is None else PatriciaMapEntry(*entry)

    def _lower_bound_rank(self, key: int) -> tuple[int, bool]:
        return self._core.lower_bound_rank(key)

    def _with_core(self, core: _PatriciaCore[int, V]) -> PersistentIntMap[V]:
        return self if core is self._core else PersistentIntMap(core)


class PersistentLongMap(Generic[V]):
    """Persistent big-endian Patricia map over signed 64-bit integer keys."""

    __slots__ = ("_core",)

    def __init__(self, core: _PatriciaCore[int, V]) -> None:
        """Wrap an already-built core; use :meth:`empty` or the ``from_`` factory."""

        self._core = core

    @classmethod
    def empty(cls) -> PersistentLongMap[V]:
        """Return the empty map."""

        return cls(_PatriciaCore(None, 0, _encode_int64))

    @classmethod
    def from_items(cls, items: Iterable[tuple[int, V]]) -> PersistentLongMap[V]:
        """Build a map from pairs; a repeated key keeps the last value."""

        result = cls.empty()
        for key, value in items:
            result = result.put(key, value)
        return result

    @property
    def size(self) -> int:
        """Number of entries."""

        return self._core.size

    @property
    def is_empty(self) -> bool:
        """Whether the map holds no entries."""

        return self.size == 0

    def __len__(self) -> int:
        """Number of entries, matching :attr:`size`."""

        return self.size

    def get(self, key: int) -> V | None:
        """The value stored for ``key``, or ``None`` when absent."""

        return self._core.get(key)

    def contains_key(self, key: int) -> bool:
        """Whether ``key`` is present."""

        return self._core.contains_key(key)

    def cursor(self, position: int = 0) -> PersistentLongMapCursor[V]:
        """Return an immutable ordered cursor at a rank gap."""
        return PersistentLongMapCursor(self, position)

    def cursor_at_end(self) -> PersistentLongMapCursor[V]:
        """Return an immutable ordered cursor after the final entry."""
        return PersistentLongMapCursor(self, self.size)

    def lower_bound_cursor(self, key: int) -> PersistentLongMapCursor[V]:
        """Return the gap before the first key not less than ``key``."""
        rank, _found = self._core.lower_bound_rank(key)
        return PersistentLongMapCursor(self, rank)

    def upper_bound_cursor(self, key: int) -> PersistentLongMapCursor[V]:
        """Return the gap before the first key greater than ``key``."""
        rank, found = self._core.lower_bound_rank(key)
        return PersistentLongMapCursor(self, rank + int(found))

    def cursor_at_key(self, key: int) -> PatriciaCursorSearch[PersistentLongMapCursor[V]]:
        """Return a usable lower-bound cursor and exact-key discriminator."""
        rank, found = self._core.lower_bound_rank(key)
        return PatriciaCursorSearch(PersistentLongMapCursor(self, rank), found)

    def put(self, key: int, value: V) -> PersistentLongMap[V]:
        """Return a map with ``key`` mapped to ``value``, adding or replacing as needed. Only the
        path from the root to the affected branch is copied.
        """

        return self._with_core(self._core.put(key, value))

    def remove(self, key: int) -> PersistentLongMap[V]:
        """Return a map without that key; a no-op when it is absent."""

        return self._with_core(self._core.remove(key))

    def clear(self) -> PersistentLongMap[V]:
        """Return the empty map; a no-op when already empty."""

        return self if self.is_empty else PersistentLongMap.empty()

    def union(
        self,
        other: PersistentLongMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentLongMap[V]:
        """Return the entries of both maps. Subtrees the operands already share are adopted whole
        rather than re-entered.
        """

        return self._with_core(self._core.union(other._core, combine))

    def intersect(
        self,
        other: PersistentLongMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentLongMap[V]:
        """Return the entries present in both maps."""

        return self._with_core(self._core.intersect(other._core, combine))

    def except_(self, other: PersistentLongMap[W]) -> PersistentLongMap[V]:
        """Return this map's entries that are absent from ``other``. Named with a trailing
        underscore because ``except`` is a Python keyword.
        """

        return self._with_core(self._core.except_(other._core))

    def shares_root_with(self, other: PersistentLongMap[V]) -> bool:
        """Whether both maps reference the same trie root. A representation test, not an equality
        test.
        """

        return self._core.shares_root_with(other._core)

    def __iter__(self) -> Iterator[tuple[int, V]]:
        """Iterate the entries in ascending key order."""

        return iter(self._core)

    def _entry_at(self, index: int) -> PatriciaMapEntry[int, V] | None:
        entry = self._core.entry_at(index)
        return None if entry is None else PatriciaMapEntry(*entry)

    def _lower_bound_rank(self, key: int) -> tuple[int, bool]:
        return self._core.lower_bound_rank(key)

    def _with_core(self, core: _PatriciaCore[int, V]) -> PersistentLongMap[V]:
        return self if core is self._core else PersistentLongMap(core)


@dataclass(frozen=True, slots=True)
class PersistentIntMapCursor(Generic[V]):
    """Immutable root-plus-rank gap cursor over a signed 32-bit Patricia map."""

    map: PersistentIntMap[V]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        _validate_cursor_position(self.position, self.map.size)

    @property
    def count(self) -> int:
        """Number of entries in the map version this cursor is positioned in."""

        return self.map.size

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first entry."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last entry."""

        return self.position == self.count

    def peek_previous(self) -> PatriciaMapEntry[int, V] | None:
        """The entry immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else self.map._entry_at(self.position - 1)

    def peek_next(self) -> PatriciaMapEntry[int, V] | None:
        """The entry immediately after the gap, or ``None`` at the end."""

        return self.map._entry_at(self.position)

    def move_previous(self) -> PersistentIntMapCursor[V]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PersistentIntMapCursor(self.map, self.position - 1)

    def move_next(self) -> PersistentIntMapCursor[V]:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PersistentIntMapCursor(self.map, self.position + 1)

    def seek(self, position: int) -> PersistentIntMapCursor[V]:
        """A cursor at ``position`` within the same map version, raising when out of range."""

        return self if position == self.position else PersistentIntMapCursor(self.map, position)

    def insert(self, key: int, value: V) -> PersistentIntMapCursor[V]:
        """Strictly insert at a missing lower-bound gap and return the gap after it."""
        rank, found = self.map._lower_bound_rank(key)
        if found:
            raise KeyError(f"The key {key!r} is already present.")
        self._ensure_current_gap(rank, key)
        return PersistentIntMapCursor(self.map.put(key, value), self.position + 1)

    def put(self, key: int, value: V) -> PersistentIntMapCursor[V]:
        """Update an exact next entry or insert at a missing lower-bound gap."""
        rank, found = self.map._lower_bound_rank(key)
        self._ensure_current_gap(rank, key)
        edited = self.map.put(key, value)
        if edited is self.map:
            return self
        return PersistentIntMapCursor(edited, self.position if found else self.position + 1)

    def set_next_value(self, value: V) -> PersistentIntMapCursor[V]:
        """Replace the value of the entry after the gap, keeping its key and position. Raises
        :class:`IndexError` at the end.
        """

        next_entry = self.peek_next()
        if next_entry is None:
            raise IndexError("No entry follows the cursor gap.")
        edited = self.map.put(next_entry.key, value)
        return self if edited is self.map else PersistentIntMapCursor(edited, self.position)

    def delete_previous(self) -> PersistentIntMapCursor[V]:
        """Remove the entry before the gap, returning a cursor there; raises at the start."""

        previous = self.peek_previous()
        if previous is None:
            raise IndexError("No entry precedes the cursor gap.")
        return PersistentIntMapCursor(self.map.remove(previous.key), self.position - 1)

    def delete_next(self) -> PersistentIntMapCursor[V]:
        """Remove the entry after the gap and return a cursor in its place, raising at the end."""

        next_entry = self.peek_next()
        if next_entry is None:
            raise IndexError("No entry follows the cursor gap.")
        return PersistentIntMapCursor(self.map.remove(next_entry.key), self.position)

    def snapshot(self) -> PersistentIntMap[V]:
        """The map version this cursor is positioned in."""

        return self.map

    def _ensure_current_gap(self, rank: int, key: int) -> None:
        if rank != self.position:
            raise ValueError(
                f"Key {key!r} belongs at gap {rank}, not at the current gap {self.position}."
            )


@dataclass(frozen=True, slots=True)
class PersistentLongMapCursor(Generic[V]):
    """Immutable root-plus-rank gap cursor over a signed 64-bit Patricia map."""

    map: PersistentLongMap[V]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        _validate_cursor_position(self.position, self.map.size)

    @property
    def count(self) -> int:
        """Number of entries in the map version this cursor is positioned in."""

        return self.map.size

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first entry."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last entry."""

        return self.position == self.count

    def peek_previous(self) -> PatriciaMapEntry[int, V] | None:
        """The entry immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else self.map._entry_at(self.position - 1)

    def peek_next(self) -> PatriciaMapEntry[int, V] | None:
        """The entry immediately after the gap, or ``None`` at the end."""

        return self.map._entry_at(self.position)

    def move_previous(self) -> PersistentLongMapCursor[V]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PersistentLongMapCursor(self.map, self.position - 1)

    def move_next(self) -> PersistentLongMapCursor[V]:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PersistentLongMapCursor(self.map, self.position + 1)

    def seek(self, position: int) -> PersistentLongMapCursor[V]:
        """A cursor at ``position`` within the same map version, raising when out of range."""

        return self if position == self.position else PersistentLongMapCursor(self.map, position)

    def insert(self, key: int, value: V) -> PersistentLongMapCursor[V]:
        """Strictly insert a new entry at the gap and return the gap after it. Raises
        :class:`KeyError` when the key is already present, and rejects a key that does not belong
        at this gap rather than silently relocating the cursor.
        """

        rank, found = self.map._lower_bound_rank(key)
        if found:
            raise KeyError(f"The key {key!r} is already present.")
        self._ensure_current_gap(rank, key)
        return PersistentLongMapCursor(self.map.put(key, value), self.position + 1)

    def put(self, key: int, value: V) -> PersistentLongMapCursor[V]:
        """Update the entry at the gap, or insert one there when the key is absent. Rejects a key
        that does not belong at this gap.
        """

        rank, found = self.map._lower_bound_rank(key)
        self._ensure_current_gap(rank, key)
        edited = self.map.put(key, value)
        if edited is self.map:
            return self
        return PersistentLongMapCursor(edited, self.position if found else self.position + 1)

    def set_next_value(self, value: V) -> PersistentLongMapCursor[V]:
        """Replace the value of the entry after the gap, keeping its key and position. Raises
        :class:`IndexError` at the end.
        """

        next_entry = self.peek_next()
        if next_entry is None:
            raise IndexError("No entry follows the cursor gap.")
        edited = self.map.put(next_entry.key, value)
        return self if edited is self.map else PersistentLongMapCursor(edited, self.position)

    def delete_previous(self) -> PersistentLongMapCursor[V]:
        """Remove the entry before the gap, returning a cursor there; raises at the start."""

        previous = self.peek_previous()
        if previous is None:
            raise IndexError("No entry precedes the cursor gap.")
        return PersistentLongMapCursor(self.map.remove(previous.key), self.position - 1)

    def delete_next(self) -> PersistentLongMapCursor[V]:
        """Remove the entry after the gap and return a cursor in its place, raising at the end."""

        next_entry = self.peek_next()
        if next_entry is None:
            raise IndexError("No entry follows the cursor gap.")
        return PersistentLongMapCursor(self.map.remove(next_entry.key), self.position)

    def snapshot(self) -> PersistentLongMap[V]:
        """The map version this cursor is positioned in."""

        return self.map

    def _ensure_current_gap(self, rank: int, key: int) -> None:
        if rank != self.position:
            raise ValueError(
                f"Key {key!r} belongs at gap {rank}, not at the current gap {self.position}."
            )


class PersistentIntSet:
    """Persistent Patricia set over signed 32-bit integer values."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentIntMap[bool]) -> None:
        """Wrap an already-built core; use :meth:`empty` or the ``from_`` factory."""

        self._map = map_value

    @classmethod
    def empty(cls) -> PersistentIntSet:
        """Return the empty set."""

        return cls(PersistentIntMap.empty())

    @classmethod
    def from_values(cls, values: Iterable[int]) -> PersistentIntSet:
        """Build a set from values, ignoring repeats."""

        result = cls.empty()
        for value in values:
            result = result.add(value)
        return result

    @property
    def size(self) -> int:
        """Number of elements."""

        return self._map.size

    @property
    def is_empty(self) -> bool:
        """Whether the set holds no elements."""

        return self._map.is_empty

    def __len__(self) -> int:
        """Number of elements, matching :attr:`size`."""

        return self.size

    def contains(self, value: int) -> bool:
        """Whether ``value`` is present."""

        return self._map.contains_key(value)

    def cursor(self, position: int = 0) -> PersistentIntSetCursor:
        """A cursor at gap ``position`` of the ascending element sequence."""

        return PersistentIntSetCursor(self, position)

    def cursor_at_end(self) -> PersistentIntSetCursor:
        """A cursor after the last element."""

        return PersistentIntSetCursor(self, self.size)

    def lower_bound_cursor(self, value: int) -> PersistentIntSetCursor:
        """A cursor before the first key not less than ``key``."""

        rank, _found = self._map._lower_bound_rank(value)
        return PersistentIntSetCursor(self, rank)

    def upper_bound_cursor(self, value: int) -> PersistentIntSetCursor:
        """A cursor before the first key greater than ``key``."""

        rank, found = self._map._lower_bound_rank(value)
        return PersistentIntSetCursor(self, rank + int(found))

    def cursor_at_item(self, value: int) -> PatriciaCursorSearch[PersistentIntSetCursor]:
        """A usable lower-bound cursor together with an exact-match discriminator. On a miss the
        cursor still sits at the insertion point.
        """

        rank, found = self._map._lower_bound_rank(value)
        return PatriciaCursorSearch(PersistentIntSetCursor(self, rank), found)

    def add(self, value: int) -> PersistentIntSet:
        """Return a set containing ``value``; a no-op when it is already present."""

        return self._with_map(self._map.put(value, True))

    def remove(self, value: int) -> PersistentIntSet:
        """Return a set without that key; a no-op when it is absent."""

        return self._with_map(self._map.remove(value))

    def union(self, other: PersistentIntSet) -> PersistentIntSet:
        """Return the elements of both sets. Subtrees the operands already share are adopted whole
        rather than re-entered.
        """

        return self._with_map(self._map.union(other._map))

    def intersect(self, other: PersistentIntSet) -> PersistentIntSet:
        """Return the elements present in both sets."""

        return self._with_map(self._map.intersect(other._map))

    def except_(self, other: PersistentIntSet) -> PersistentIntSet:
        """Return this set's elements that are absent from ``other``. Named with a trailing
        underscore because ``except`` is a Python keyword.
        """

        return self._with_map(self._map.except_(other._map))

    def __iter__(self) -> Iterator[int]:
        """Iterate the elements in ascending key order."""

        return (key for key, _value in self._map)

    def _item_at(self, index: int) -> int | None:
        entry = self._map._entry_at(index)
        return None if entry is None else entry.key

    def _lower_bound_rank(self, value: int) -> tuple[int, bool]:
        return self._map._lower_bound_rank(value)

    def _with_map(self, map_value: PersistentIntMap[bool]) -> PersistentIntSet:
        return self if map_value is self._map else PersistentIntSet(map_value)


class PersistentLongSet:
    """Persistent Patricia set over signed 64-bit integer values."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentLongMap[bool]) -> None:
        """Wrap an already-built core; use :meth:`empty` or the ``from_`` factory."""

        self._map = map_value

    @classmethod
    def empty(cls) -> PersistentLongSet:
        """Return the empty set."""

        return cls(PersistentLongMap.empty())

    @classmethod
    def from_values(cls, values: Iterable[int]) -> PersistentLongSet:
        """Build a set from values, ignoring repeats."""

        result = cls.empty()
        for value in values:
            result = result.add(value)
        return result

    @property
    def size(self) -> int:
        """Number of elements."""

        return self._map.size

    @property
    def is_empty(self) -> bool:
        """Whether the set holds no elements."""

        return self._map.is_empty

    def __len__(self) -> int:
        """Number of elements, matching :attr:`size`."""

        return self.size

    def contains(self, value: int) -> bool:
        """Whether ``value`` is present."""

        return self._map.contains_key(value)

    def cursor(self, position: int = 0) -> PersistentLongSetCursor:
        """A cursor at gap ``position`` of the ascending element sequence."""

        return PersistentLongSetCursor(self, position)

    def cursor_at_end(self) -> PersistentLongSetCursor:
        """A cursor after the last element."""

        return PersistentLongSetCursor(self, self.size)

    def lower_bound_cursor(self, value: int) -> PersistentLongSetCursor:
        """A cursor before the first key not less than ``key``."""

        rank, _found = self._map._lower_bound_rank(value)
        return PersistentLongSetCursor(self, rank)

    def upper_bound_cursor(self, value: int) -> PersistentLongSetCursor:
        """A cursor before the first key greater than ``key``."""

        rank, found = self._map._lower_bound_rank(value)
        return PersistentLongSetCursor(self, rank + int(found))

    def cursor_at_item(self, value: int) -> PatriciaCursorSearch[PersistentLongSetCursor]:
        """A usable lower-bound cursor together with an exact-match discriminator. On a miss the
        cursor still sits at the insertion point.
        """

        rank, found = self._map._lower_bound_rank(value)
        return PatriciaCursorSearch(PersistentLongSetCursor(self, rank), found)

    def add(self, value: int) -> PersistentLongSet:
        """Return a set containing ``value``; a no-op when it is already present."""

        return self._with_map(self._map.put(value, True))

    def remove(self, value: int) -> PersistentLongSet:
        """Return a set without that key; a no-op when it is absent."""

        return self._with_map(self._map.remove(value))

    def union(self, other: PersistentLongSet) -> PersistentLongSet:
        """Return the elements of both sets. Subtrees the operands already share are adopted whole
        rather than re-entered.
        """

        return self._with_map(self._map.union(other._map))

    def intersect(self, other: PersistentLongSet) -> PersistentLongSet:
        """Return the elements present in both sets."""

        return self._with_map(self._map.intersect(other._map))

    def except_(self, other: PersistentLongSet) -> PersistentLongSet:
        """Return this set's elements that are absent from ``other``. Named with a trailing
        underscore because ``except`` is a Python keyword.
        """

        return self._with_map(self._map.except_(other._map))

    def __iter__(self) -> Iterator[int]:
        """Iterate the elements in ascending key order."""

        return (key for key, _value in self._map)

    def _item_at(self, index: int) -> int | None:
        entry = self._map._entry_at(index)
        return None if entry is None else entry.key

    def _lower_bound_rank(self, value: int) -> tuple[int, bool]:
        return self._map._lower_bound_rank(value)

    def _with_map(self, map_value: PersistentLongMap[bool]) -> PersistentLongSet:
        return self if map_value is self._map else PersistentLongSet(map_value)


@dataclass(frozen=True, slots=True)
class PersistentIntSetCursor:
    """Immutable root-plus-rank gap cursor over a signed 32-bit Patricia set."""

    set: PersistentIntSet
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        _validate_cursor_position(self.position, self.set.size)

    @property
    def count(self) -> int:
        """Number of elements in the set version this cursor is positioned in."""

        return self.set.size

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> int | None:
        """The element immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else self.set._item_at(self.position - 1)

    def peek_next(self) -> int | None:
        """The element immediately after the gap, or ``None`` at the end."""

        return self.set._item_at(self.position)

    def move_previous(self) -> PersistentIntSetCursor:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PersistentIntSetCursor(self.set, self.position - 1)

    def move_next(self) -> PersistentIntSetCursor:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PersistentIntSetCursor(self.set, self.position + 1)

    def seek(self, position: int) -> PersistentIntSetCursor:
        """A cursor at ``position`` within the same set version, raising when out of range."""

        return self if position == self.position else PersistentIntSetCursor(self.set, position)

    def add(self, value: int) -> PersistentIntSetCursor:
        """Insert ``value`` and return a cursor just after it. The gap moves to the value's
        ascending position, since placement is decided by the key ordering.
        """

        rank, found = self.set._lower_bound_rank(value)
        self._ensure_current_gap(rank, value)
        return self if found else PersistentIntSetCursor(self.set.add(value), self.position + 1)

    def delete_previous(self) -> PersistentIntSetCursor:
        """Remove the element before the gap, returning a cursor there; raises at the start."""

        previous = self.peek_previous()
        if previous is None:
            raise IndexError("No item precedes the cursor gap.")
        return PersistentIntSetCursor(self.set.remove(previous), self.position - 1)

    def delete_next(self) -> PersistentIntSetCursor:
        """Remove the element after the gap and return a cursor in its place, raising at the end."""

        next_item = self.peek_next()
        if next_item is None:
            raise IndexError("No item follows the cursor gap.")
        return PersistentIntSetCursor(self.set.remove(next_item), self.position)

    def snapshot(self) -> PersistentIntSet:
        """The set version this cursor is positioned in."""

        return self.set

    def _ensure_current_gap(self, rank: int, value: int) -> None:
        if rank != self.position:
            raise ValueError(
                f"Item {value!r} belongs at gap {rank}, not at the current gap {self.position}."
            )


@dataclass(frozen=True, slots=True)
class PersistentLongSetCursor:
    """Immutable root-plus-rank gap cursor over a signed 64-bit Patricia set."""

    set: PersistentLongSet
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        _validate_cursor_position(self.position, self.set.size)

    @property
    def count(self) -> int:
        """Number of elements in the set version this cursor is positioned in."""

        return self.set.size

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> int | None:
        """The element immediately before the gap, or ``None`` at the start."""

        return None if self.is_at_start else self.set._item_at(self.position - 1)

    def peek_next(self) -> int | None:
        """The element immediately after the gap, or ``None`` at the end."""

        return self.set._item_at(self.position)

    def move_previous(self) -> PersistentLongSetCursor:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return PersistentLongSetCursor(self.set, self.position - 1)

    def move_next(self) -> PersistentLongSetCursor:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return PersistentLongSetCursor(self.set, self.position + 1)

    def seek(self, position: int) -> PersistentLongSetCursor:
        """A cursor at ``position`` within the same set version, raising when out of range."""

        return self if position == self.position else PersistentLongSetCursor(self.set, position)

    def add(self, value: int) -> PersistentLongSetCursor:
        """Insert ``value`` and return a cursor just after it. The gap moves to the value's
        ascending position, since placement is decided by the key ordering.
        """

        rank, found = self.set._lower_bound_rank(value)
        self._ensure_current_gap(rank, value)
        return self if found else PersistentLongSetCursor(self.set.add(value), self.position + 1)

    def delete_previous(self) -> PersistentLongSetCursor:
        """Remove the element before the gap, returning a cursor there; raises at the start."""

        previous = self.peek_previous()
        if previous is None:
            raise IndexError("No item precedes the cursor gap.")
        return PersistentLongSetCursor(self.set.remove(previous), self.position - 1)

    def delete_next(self) -> PersistentLongSetCursor:
        """Remove the element after the gap and return a cursor in its place, raising at the end."""

        next_item = self.peek_next()
        if next_item is None:
            raise IndexError("No item follows the cursor gap.")
        return PersistentLongSetCursor(self.set.remove(next_item), self.position)

    def snapshot(self) -> PersistentLongSet:
        """The set version this cursor is positioned in."""

        return self.set

    def _ensure_current_gap(self, rank: int, value: int) -> None:
        if rank != self.position:
            raise ValueError(
                f"Item {value!r} belongs at gap {rank}, not at the current gap {self.position}."
            )


def _validate_cursor_position(position: int, size: int) -> None:
    if position < 0 or position > size:
        raise ValueError(f"Cursor position must be in 0 .. {size}.")


__all__ = [
    "PatriciaCursorSearch",
    "PatriciaMapEntry",
    "PersistentIntMap",
    "PersistentIntMapCursor",
    "PersistentIntSet",
    "PersistentIntSetCursor",
    "PersistentLongMap",
    "PersistentLongMapCursor",
    "PersistentLongSet",
    "PersistentLongSetCursor",
]
