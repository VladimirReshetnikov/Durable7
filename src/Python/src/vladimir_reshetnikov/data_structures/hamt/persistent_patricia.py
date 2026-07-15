"""Persistent big-endian Patricia maps and sets for signed integer keys."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, field
from typing import Generic, TypeVar

from .hash_policy import same_value

K = TypeVar("K")
V = TypeVar("V")
W = TypeVar("W")


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
        self._root = root
        self.size = size
        self._encode = encode

    def get(self, key: K) -> V | None:
        path = self._encode(key)
        if self._root is None:
            return None
        leaf = _find_leaf(self._root, path)
        return None if leaf is None else leaf.value

    def contains_key(self, key: K) -> bool:
        path = self._encode(key)
        return self._root is not None and _find_leaf(self._root, path) is not None

    def put(self, key: K, value: V) -> _PatriciaCore[K, V]:
        change = _put_node(self._root, self._encode(key), key, value)
        if not change.changed:
            return self
        return _PatriciaCore(change.node, self.size + int(change.added), self._encode)

    def remove(self, key: K) -> _PatriciaCore[K, V]:
        change = _remove_node(self._root, self._encode(key))
        return (
            self if not change.changed else _PatriciaCore(change.node, self.size - 1, self._encode)
        )

    def union(
        self,
        other: _PatriciaCore[K, V],
        combine: Callable[[K, V, V], V] | None = None,
    ) -> _PatriciaCore[K, V]:
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
        result = _PatriciaCore[K, V](None, 0, self._encode)
        for key, left in self:
            right = None if other._root is None else _find_leaf(other._root, other._encode(key))
            if right is not None:
                result = result.put(
                    key, left if combine is None else combine(key, left, right.value)
                )
        return self if result.content_equals(self) else result

    def except_(self, other: _PatriciaCore[K, W]) -> _PatriciaCore[K, V]:
        result = self
        for key, _value in other:
            result = result.remove(key)
        return result

    def content_equals(self, other: _PatriciaCore[K, V]) -> bool:
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
        return self._root is other._root

    def __iter__(self) -> Iterator[tuple[K, V]]:
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
        self._core = core

    @classmethod
    def empty(cls) -> PersistentIntMap[V]:
        return cls(_PatriciaCore(None, 0, _encode_int32))

    @classmethod
    def from_items(cls, items: Iterable[tuple[int, V]]) -> PersistentIntMap[V]:
        result = cls.empty()
        for key, value in items:
            result = result.put(key, value)
        return result

    @property
    def size(self) -> int:
        return self._core.size

    @property
    def is_empty(self) -> bool:
        return self.size == 0

    def __len__(self) -> int:
        return self.size

    def get(self, key: int) -> V | None:
        return self._core.get(key)

    def contains_key(self, key: int) -> bool:
        return self._core.contains_key(key)

    def put(self, key: int, value: V) -> PersistentIntMap[V]:
        return self._with_core(self._core.put(key, value))

    def remove(self, key: int) -> PersistentIntMap[V]:
        return self._with_core(self._core.remove(key))

    def clear(self) -> PersistentIntMap[V]:
        return self if self.is_empty else PersistentIntMap.empty()

    def union(
        self,
        other: PersistentIntMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentIntMap[V]:
        return self._with_core(self._core.union(other._core, combine))

    def intersect(
        self,
        other: PersistentIntMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentIntMap[V]:
        return self._with_core(self._core.intersect(other._core, combine))

    def except_(self, other: PersistentIntMap[W]) -> PersistentIntMap[V]:
        return self._with_core(self._core.except_(other._core))

    def shares_root_with(self, other: PersistentIntMap[V]) -> bool:
        return self._core.shares_root_with(other._core)

    def __iter__(self) -> Iterator[tuple[int, V]]:
        return iter(self._core)

    def _with_core(self, core: _PatriciaCore[int, V]) -> PersistentIntMap[V]:
        return self if core is self._core else PersistentIntMap(core)


class PersistentLongMap(Generic[V]):
    """Persistent big-endian Patricia map over signed 64-bit integer keys."""

    __slots__ = ("_core",)

    def __init__(self, core: _PatriciaCore[int, V]) -> None:
        self._core = core

    @classmethod
    def empty(cls) -> PersistentLongMap[V]:
        return cls(_PatriciaCore(None, 0, _encode_int64))

    @classmethod
    def from_items(cls, items: Iterable[tuple[int, V]]) -> PersistentLongMap[V]:
        result = cls.empty()
        for key, value in items:
            result = result.put(key, value)
        return result

    @property
    def size(self) -> int:
        return self._core.size

    @property
    def is_empty(self) -> bool:
        return self.size == 0

    def __len__(self) -> int:
        return self.size

    def get(self, key: int) -> V | None:
        return self._core.get(key)

    def contains_key(self, key: int) -> bool:
        return self._core.contains_key(key)

    def put(self, key: int, value: V) -> PersistentLongMap[V]:
        return self._with_core(self._core.put(key, value))

    def remove(self, key: int) -> PersistentLongMap[V]:
        return self._with_core(self._core.remove(key))

    def clear(self) -> PersistentLongMap[V]:
        return self if self.is_empty else PersistentLongMap.empty()

    def union(
        self,
        other: PersistentLongMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentLongMap[V]:
        return self._with_core(self._core.union(other._core, combine))

    def intersect(
        self,
        other: PersistentLongMap[V],
        combine: Callable[[int, V, V], V] | None = None,
    ) -> PersistentLongMap[V]:
        return self._with_core(self._core.intersect(other._core, combine))

    def except_(self, other: PersistentLongMap[W]) -> PersistentLongMap[V]:
        return self._with_core(self._core.except_(other._core))

    def shares_root_with(self, other: PersistentLongMap[V]) -> bool:
        return self._core.shares_root_with(other._core)

    def __iter__(self) -> Iterator[tuple[int, V]]:
        return iter(self._core)

    def _with_core(self, core: _PatriciaCore[int, V]) -> PersistentLongMap[V]:
        return self if core is self._core else PersistentLongMap(core)


class PersistentIntSet:
    """Persistent Patricia set over signed 32-bit integer values."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentIntMap[bool]) -> None:
        self._map = map_value

    @classmethod
    def empty(cls) -> PersistentIntSet:
        return cls(PersistentIntMap.empty())

    @classmethod
    def from_values(cls, values: Iterable[int]) -> PersistentIntSet:
        result = cls.empty()
        for value in values:
            result = result.add(value)
        return result

    @property
    def size(self) -> int:
        return self._map.size

    @property
    def is_empty(self) -> bool:
        return self._map.is_empty

    def __len__(self) -> int:
        return self.size

    def contains(self, value: int) -> bool:
        return self._map.contains_key(value)

    def add(self, value: int) -> PersistentIntSet:
        return self._with_map(self._map.put(value, True))

    def remove(self, value: int) -> PersistentIntSet:
        return self._with_map(self._map.remove(value))

    def union(self, other: PersistentIntSet) -> PersistentIntSet:
        return self._with_map(self._map.union(other._map))

    def intersect(self, other: PersistentIntSet) -> PersistentIntSet:
        return self._with_map(self._map.intersect(other._map))

    def except_(self, other: PersistentIntSet) -> PersistentIntSet:
        return self._with_map(self._map.except_(other._map))

    def __iter__(self) -> Iterator[int]:
        return (key for key, _value in self._map)

    def _with_map(self, map_value: PersistentIntMap[bool]) -> PersistentIntSet:
        return self if map_value is self._map else PersistentIntSet(map_value)


class PersistentLongSet:
    """Persistent Patricia set over signed 64-bit integer values."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentLongMap[bool]) -> None:
        self._map = map_value

    @classmethod
    def empty(cls) -> PersistentLongSet:
        return cls(PersistentLongMap.empty())

    @classmethod
    def from_values(cls, values: Iterable[int]) -> PersistentLongSet:
        result = cls.empty()
        for value in values:
            result = result.add(value)
        return result

    @property
    def size(self) -> int:
        return self._map.size

    @property
    def is_empty(self) -> bool:
        return self._map.is_empty

    def __len__(self) -> int:
        return self.size

    def contains(self, value: int) -> bool:
        return self._map.contains_key(value)

    def add(self, value: int) -> PersistentLongSet:
        return self._with_map(self._map.put(value, True))

    def remove(self, value: int) -> PersistentLongSet:
        return self._with_map(self._map.remove(value))

    def union(self, other: PersistentLongSet) -> PersistentLongSet:
        return self._with_map(self._map.union(other._map))

    def intersect(self, other: PersistentLongSet) -> PersistentLongSet:
        return self._with_map(self._map.intersect(other._map))

    def except_(self, other: PersistentLongSet) -> PersistentLongSet:
        return self._with_map(self._map.except_(other._map))

    def __iter__(self) -> Iterator[int]:
        return (key for key, _value in self._map)

    def _with_map(self, map_value: PersistentLongMap[bool]) -> PersistentLongSet:
        return self if map_value is self._map else PersistentLongSet(map_value)


__all__ = [
    "PersistentIntMap",
    "PersistentIntSet",
    "PersistentLongMap",
    "PersistentLongSet",
]
