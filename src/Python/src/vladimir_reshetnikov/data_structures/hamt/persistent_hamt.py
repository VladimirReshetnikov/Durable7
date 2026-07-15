"""Persistent 32-way CHAMP maps, sets, and one-way editing sessions."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, field
from typing import Generic, Literal, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy, same_value

K = TypeVar("K")
V = TypeVar("V")
W = TypeVar("W")
T = TypeVar("T")
R = TypeVar("R")

_BITS_PER_LEVEL = 5
_BRANCH_MASK = 0x1F


class _Node(Generic[K, V]):
    __slots__ = ()
    entry_count: int


@dataclass(frozen=True, slots=True)
class _Leaf(_Node[K, V]):
    hash: int
    key: K
    value: V
    entry_count: int = field(default=1, init=False)


@dataclass(frozen=True, slots=True)
class _Collision(_Node[K, V]):
    hash: int
    entries: tuple[_Leaf[K, V], ...]
    entry_count: int = field(init=False)

    def __post_init__(self) -> None:
        object.__setattr__(self, "entry_count", len(self.entries))


@dataclass(frozen=True, slots=True)
class _BitmapNode(_Node[K, V]):
    data_map: int
    node_map: int
    data: tuple[_Leaf[K, V], ...]
    nodes: tuple[_Node[K, V], ...]
    entry_count: int = field(init=False)

    def __post_init__(self) -> None:
        object.__setattr__(
            self,
            "entry_count",
            len(self.data) + sum(node.entry_count for node in self.nodes),
        )


@dataclass(frozen=True, slots=True)
class _InsertResult(Generic[K, V]):
    node: _Node[K, V]
    added: bool
    changed: bool
    duplicate: bool


@dataclass(frozen=True, slots=True)
class _RemoveResult(Generic[K, V]):
    node: _Node[K, V] | None
    removed: HamtEntry[K, V] | None
    changed: bool


@dataclass(frozen=True, slots=True)
class HamtEntry(Generic[K, V]):
    """A concrete stored key/value representative."""

    key: K
    value: V


@dataclass(frozen=True, slots=True)
class AddResult(Generic[R]):
    """Result of a non-throwing duplicate-rejecting insertion."""

    value: R
    added: bool


@dataclass(frozen=True, slots=True)
class MapRemoveResult(Generic[K, V]):
    """A map successor and the removed value."""

    map: PersistentHashMap[K, V]
    value: V


@dataclass(frozen=True, slots=True)
class MapRemoveEntryResult(Generic[K, V]):
    """A map successor and its removed stored representative."""

    map: PersistentHashMap[K, V]
    entry: HamtEntry[K, V]


@dataclass(frozen=True, slots=True)
class SetRemoveResult(Generic[T]):
    """A set successor and its removed stored representative."""

    set: PersistentHashSet[T]
    value: T


MapDifferenceKind = Literal["added", "removed", "changed"]


@dataclass(frozen=True, slots=True)
class MapDifference(Generic[K, V]):
    """A typed semantic difference; ``kind`` disambiguates absent ``None`` values."""

    kind: MapDifferenceKind
    key: K
    before: V | None
    after: V | None


class DuplicateKeyError(ValueError):
    """Raised by duplicate-rejecting insertion."""


class TransientConsumedError(RuntimeError):
    """Raised when an editing session is accessed after publication."""


def _leaf(hash_value: int, key: K, value: V) -> _Leaf[K, V]:
    return _Leaf(hash_value, key, value)


def _collision(hash_value: int, entries: tuple[_Leaf[K, V], ...]) -> _Collision[K, V]:
    return _Collision(hash_value, entries)


def _bitmap(
    data_map: int,
    node_map: int,
    data: tuple[_Leaf[K, V], ...],
    nodes: tuple[_Node[K, V], ...],
) -> _BitmapNode[K, V]:
    return _BitmapNode(data_map, node_map, data, nodes)


def _values_equal(left: V, right: V) -> bool:
    return same_value(left, right)


def _hash_fragment(hash_value: int, shift: int) -> int:
    return ((hash_value & 0xFFFF_FFFF) >> shift) & _BRANCH_MASK


def _bit_position(fragment: int) -> int:
    return 1 << fragment


def _sparse_index(bitmap_value: int, bit: int) -> int:
    return (bitmap_value & (bit - 1)).bit_count()


def _replace_at(values: tuple[T, ...], index: int, value: T) -> tuple[T, ...]:
    return (*values[:index], value, *values[index + 1 :])


def _insert_at(values: tuple[T, ...], index: int, value: T) -> tuple[T, ...]:
    return (*values[:index], value, *values[index:])


def _remove_at(values: tuple[T, ...], index: int) -> tuple[T, ...]:
    return values[:index] + values[index + 1 :]


def _get_in_node(
    node: _Node[K, V],
    hash_value: int,
    key: K,
    shift: int,
    policy: HashPolicy[K],
) -> HamtEntry[K, V] | None:
    if isinstance(node, _Leaf):
        if node.hash == hash_value and policy.equivalent(node.key, key):
            return HamtEntry(node.key, node.value)
        return None
    if isinstance(node, _Collision):
        if node.hash != hash_value:
            return None
        for entry in node.entries:
            if policy.equivalent(entry.key, key):
                return HamtEntry(entry.key, entry.value)
        return None
    assert isinstance(node, _BitmapNode)
    bit = _bit_position(_hash_fragment(hash_value, shift))
    if node.data_map & bit:
        entry = node.data[_sparse_index(node.data_map, bit)]
        if entry.hash == hash_value and policy.equivalent(entry.key, key):
            return HamtEntry(entry.key, entry.value)
        return None
    if node.node_map & bit:
        child = node.nodes[_sparse_index(node.node_map, bit)]
        return _get_in_node(child, hash_value, key, shift + _BITS_PER_LEVEL, policy)
    return None


def _collect_leaves(node: _Node[K, V]) -> tuple[_Leaf[K, V], ...]:
    if isinstance(node, _Leaf):
        return (node,)
    if isinstance(node, _Collision):
        return node.entries
    assert isinstance(node, _BitmapNode)
    result = list(node.data)
    for child in node.nodes:
        result.extend(_collect_leaves(child))
    return tuple(result)


def _merge_nodes(
    left: _Node[K, V],
    left_hash: int,
    right: _Node[K, V],
    right_hash: int,
    shift: int,
) -> _Node[K, V]:
    if left_hash == right_hash:
        return _collision(left_hash, _collect_leaves(left) + _collect_leaves(right))
    left_bit = _bit_position(_hash_fragment(left_hash, shift))
    right_bit = _bit_position(_hash_fragment(right_hash, shift))
    if left_bit == right_bit:
        child = _merge_nodes(left, left_hash, right, right_hash, shift + _BITS_PER_LEVEL)
        return _bitmap(0, left_bit, (), (child,))
    ordered = ((left_bit, left), (right_bit, right))
    if left_bit > right_bit:
        ordered = ((right_bit, right), (left_bit, left))
    data: list[_Leaf[K, V]] = []
    nodes: list[_Node[K, V]] = []
    data_map = 0
    node_map = 0
    for bit, node in ordered:
        if isinstance(node, _Leaf):
            data_map |= bit
            data.append(node)
        else:
            node_map |= bit
            nodes.append(node)
    return _bitmap(data_map, node_map, tuple(data), tuple(nodes))


def _insert_node(
    node: _Node[K, V],
    hash_value: int,
    key: K,
    value: V,
    shift: int,
    overwrite: bool,
    policy: HashPolicy[K],
) -> _InsertResult[K, V]:
    if isinstance(node, _Leaf):
        if node.hash == hash_value and policy.equivalent(node.key, key):
            if not overwrite:
                return _InsertResult(node, False, False, True)
            if _values_equal(node.value, value):
                return _InsertResult(node, False, False, False)
            return _InsertResult(_leaf(hash_value, node.key, value), False, True, False)
        merged = _merge_nodes(node, node.hash, _leaf(hash_value, key, value), hash_value, shift)
        return _InsertResult(merged, True, True, False)

    if isinstance(node, _Collision):
        if node.hash != hash_value:
            merged = _merge_nodes(node, node.hash, _leaf(hash_value, key, value), hash_value, shift)
            return _InsertResult(merged, True, True, False)
        for index, entry in enumerate(node.entries):
            if not policy.equivalent(entry.key, key):
                continue
            if not overwrite:
                return _InsertResult(node, False, False, True)
            if _values_equal(entry.value, value):
                return _InsertResult(node, False, False, False)
            entries = _replace_at(node.entries, index, _leaf(hash_value, entry.key, value))
            return _InsertResult(_collision(hash_value, entries), False, True, False)
        entries = (*node.entries, _leaf(hash_value, key, value))
        return _InsertResult(_collision(hash_value, entries), True, True, False)

    assert isinstance(node, _BitmapNode)
    bit = _bit_position(_hash_fragment(hash_value, shift))
    if node.data_map & bit:
        data_index = _sparse_index(node.data_map, bit)
        current = node.data[data_index]
        if current.hash == hash_value and policy.equivalent(current.key, key):
            if not overwrite:
                return _InsertResult(node, False, False, True)
            if _values_equal(current.value, value):
                return _InsertResult(node, False, False, False)
            data = _replace_at(node.data, data_index, _leaf(hash_value, current.key, value))
            return _InsertResult(
                _bitmap(node.data_map, node.node_map, data, node.nodes), False, True, False
            )
        merged_child = _merge_nodes(
            current,
            current.hash,
            _leaf(hash_value, key, value),
            hash_value,
            shift + _BITS_PER_LEVEL,
        )
        nodes = _insert_at(node.nodes, _sparse_index(node.node_map, bit), merged_child)
        return _InsertResult(
            _bitmap(
                node.data_map & ~bit, node.node_map | bit, _remove_at(node.data, data_index), nodes
            ),
            True,
            True,
            False,
        )
    if node.node_map & bit:
        index = _sparse_index(node.node_map, bit)
        child = _insert_node(
            node.nodes[index], hash_value, key, value, shift + _BITS_PER_LEVEL, overwrite, policy
        )
        if not child.changed:
            return _InsertResult(node, False, False, child.duplicate)
        return _InsertResult(
            _bitmap(
                node.data_map, node.node_map, node.data, _replace_at(node.nodes, index, child.node)
            ),
            child.added,
            True,
            False,
        )
    data = _insert_at(node.data, _sparse_index(node.data_map, bit), _leaf(hash_value, key, value))
    return _InsertResult(
        _bitmap(node.data_map | bit, node.node_map, data, node.nodes), True, True, False
    )


def _singleton_leaf(node: _Node[K, V]) -> _Leaf[K, V] | None:
    if isinstance(node, _Leaf):
        return node
    if isinstance(node, _Collision):
        return node.entries[0] if len(node.entries) == 1 else None
    assert isinstance(node, _BitmapNode)
    return node.data[0] if len(node.data) == 1 and not node.nodes else None


def _normalize(node: _BitmapNode[K, V]) -> _Node[K, V] | None:
    if not node.data and not node.nodes:
        return None
    if len(node.data) == 1 and not node.nodes:
        return node.data[0]
    if not node.data and len(node.nodes) == 1 and not isinstance(node.nodes[0], _BitmapNode):
        return node.nodes[0]
    return node


def _remove_node(
    node: _Node[K, V],
    hash_value: int,
    key: K,
    shift: int,
    policy: HashPolicy[K],
) -> _RemoveResult[K, V]:
    if isinstance(node, _Leaf):
        if node.hash == hash_value and policy.equivalent(node.key, key):
            return _RemoveResult(None, HamtEntry(node.key, node.value), True)
        return _RemoveResult(node, None, False)
    if isinstance(node, _Collision):
        if node.hash != hash_value:
            return _RemoveResult(node, None, False)
        for index, entry in enumerate(node.entries):
            if not policy.equivalent(entry.key, key):
                continue
            entries = _remove_at(node.entries, index)
            replacement: _Node[K, V] | None
            if not entries:
                replacement = None
            elif len(entries) == 1:
                replacement = entries[0]
            else:
                replacement = _collision(hash_value, entries)
            return _RemoveResult(replacement, HamtEntry(entry.key, entry.value), True)
        return _RemoveResult(node, None, False)

    assert isinstance(node, _BitmapNode)
    bit = _bit_position(_hash_fragment(hash_value, shift))
    if node.data_map & bit:
        index = _sparse_index(node.data_map, bit)
        current = node.data[index]
        if current.hash != hash_value or not policy.equivalent(current.key, key):
            return _RemoveResult(node, None, False)
        replacement = _bitmap(
            node.data_map & ~bit, node.node_map, _remove_at(node.data, index), node.nodes
        )
        return _RemoveResult(_normalize(replacement), HamtEntry(current.key, current.value), True)
    if not node.node_map & bit:
        return _RemoveResult(node, None, False)
    index = _sparse_index(node.node_map, bit)
    child = _remove_node(node.nodes[index], hash_value, key, shift + _BITS_PER_LEVEL, policy)
    if not child.changed:
        return _RemoveResult(node, None, False)
    promoted = None if child.node is None else _singleton_leaf(child.node)
    if child.node is None:
        replacement = _bitmap(
            node.data_map, node.node_map & ~bit, node.data, _remove_at(node.nodes, index)
        )
    elif promoted is not None:
        replacement = _bitmap(
            node.data_map | bit,
            node.node_map & ~bit,
            _insert_at(node.data, _sparse_index(node.data_map, bit), promoted),
            _remove_at(node.nodes, index),
        )
    else:
        replacement = _bitmap(
            node.data_map, node.node_map, node.data, _replace_at(node.nodes, index, child.node)
        )
    return _RemoveResult(_normalize(replacement), child.removed, True)


def _entries_of_node(root: _Node[K, V]) -> Iterator[HamtEntry[K, V]]:
    stack: list[_Node[K, V]] = [root]
    while stack:
        node = stack.pop()
        if isinstance(node, _Leaf):
            yield HamtEntry(node.key, node.value)
        elif isinstance(node, _Collision):
            for entry in node.entries:
                yield HamtEntry(entry.key, entry.value)
        else:
            assert isinstance(node, _BitmapNode)
            stack.extend(reversed(node.nodes))
            for entry in node.data:
                yield HamtEntry(entry.key, entry.value)


class PersistentHashMap(Generic[K, V]):
    """Immutable 32-way CHAMP map with collision buckets and structural sharing."""

    __slots__ = ("_root", "policy", "size")

    def __init__(
        self,
        root: _Node[K, V] | None,
        size: int,
        policy: HashPolicy[K],
    ) -> None:
        self._root = root
        self.size = size
        self.policy = policy

    @classmethod
    def empty(cls, policy: HashPolicy[K] | None = None) -> PersistentHashMap[K, V]:
        return cls(None, 0, default_hash_policy() if policy is None else policy)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        policy: HashPolicy[K] | None = None,
    ) -> PersistentHashMap[K, V]:
        return cls.empty(policy).set_items(items)

    @classmethod
    def create_transient(cls, policy: HashPolicy[K] | None = None) -> TransientHashMap[K, V]:
        return TransientHashMap(cls.empty(policy))

    def to_transient(self) -> TransientHashMap[K, V]:
        return TransientHashMap(self)

    @property
    def is_empty(self) -> bool:
        return self.size == 0

    def __len__(self) -> int:
        return self.size

    def __bool__(self) -> bool:
        return not self.is_empty

    def shares_root_with(self, other: PersistentHashMap[K, V]) -> bool:
        return self._root is other._root

    def contains_key(self, key: K) -> bool:
        return self.get_entry(key) is not None

    def __contains__(self, key: object) -> bool:
        return self.contains_key(cast("K", key))

    def get(self, key: K) -> V | None:
        entry = self.get_entry(key)
        return None if entry is None else entry.value

    def __getitem__(self, key: K) -> V:
        entry = self.get_entry(key)
        if entry is None:
            raise KeyError(key)
        return entry.value

    def get_entry(self, key: K) -> HamtEntry[K, V] | None:
        if self._root is None:
            return None
        return _get_in_node(self._root, self.policy.hash(key), key, 0, self.policy)

    def put(self, key: K, value: V) -> PersistentHashMap[K, V]:
        hash_value = self.policy.hash(key)
        if self._root is None:
            return PersistentHashMap(_leaf(hash_value, key, value), 1, self.policy)
        result = _insert_node(self._root, hash_value, key, value, 0, True, self.policy)
        if not result.changed:
            return self
        return PersistentHashMap(result.node, self.size + int(result.added), self.policy)

    def set(self, key: K, value: V) -> PersistentHashMap[K, V]:
        """Return a map with *key* associated with *value* (Python mapping vocabulary)."""

        return self.put(key, value)

    def add(self, key: K, value: V) -> PersistentHashMap[K, V]:
        result = self.try_add(key, value)
        if not result.added:
            raise DuplicateKeyError("An equivalent key is already present.")
        return result.value

    def try_add(self, key: K, value: V) -> AddResult[PersistentHashMap[K, V]]:
        hash_value = self.policy.hash(key)
        if self._root is None:
            return AddResult(PersistentHashMap(_leaf(hash_value, key, value), 1, self.policy), True)
        result = _insert_node(self._root, hash_value, key, value, 0, False, self.policy)
        if result.duplicate:
            return AddResult(self, False)
        return AddResult(
            PersistentHashMap(result.node, self.size + int(result.added), self.policy),
            result.added,
        )

    def set_items(self, items: Iterable[tuple[K, V]]) -> PersistentHashMap[K, V]:
        result = self
        for key, value in items:
            result = result.put(key, value)
        return result

    def remove(self, key: K) -> PersistentHashMap[K, V]:
        result = self.try_remove_entry(key)
        return self if result is None else result.map

    def try_remove(self, key: K) -> MapRemoveResult[K, V] | None:
        result = self.try_remove_entry(key)
        return None if result is None else MapRemoveResult(result.map, result.entry.value)

    def try_remove_entry(self, key: K) -> MapRemoveEntryResult[K, V] | None:
        if self._root is None:
            return None
        result = _remove_node(self._root, self.policy.hash(key), key, 0, self.policy)
        if not result.changed or result.removed is None:
            return None
        return MapRemoveEntryResult(
            PersistentHashMap(result.node, self.size - 1, self.policy), result.removed
        )

    def clear(self) -> PersistentHashMap[K, V]:
        return self if self.is_empty else PersistentHashMap(None, 0, self.policy)

    def _require_same_policy(self, other: PersistentHashMap[K, V]) -> None:
        if self.policy is not other.policy:
            raise TypeError("Maps must retain the same hash-policy object.")

    def union(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        self._require_same_policy(other)
        if self._root is other._root:
            return self
        return self.set_items((entry.key, entry.value) for entry in other)

    def intersect(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        self._require_same_policy(other)
        if self._root is other._root:
            return self
        result: PersistentHashMap[K, V] = PersistentHashMap.empty(self.policy)
        for entry in self:
            if other.contains_key(entry.key):
                result = result.put(entry.key, entry.value)
        return self if result.map_equals(self) else result

    def except_(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        self._require_same_policy(other)
        if self._root is other._root:
            return self.clear()
        result = self
        for entry in other:
            result = result.remove(entry.key)
        return result

    def symmetric_except(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        self._require_same_policy(other)
        if self._root is other._root:
            return self.clear()
        result = self
        for entry in other:
            result = (
                result.remove(entry.key)
                if result.contains_key(entry.key)
                else result.put(entry.key, entry.value)
            )
        return result

    def map_equals(
        self,
        other: PersistentHashMap[K, V],
        value_equals: Callable[[V, V], bool] = _values_equal,
    ) -> bool:
        self._require_same_policy(other)
        if self._root is other._root:
            return True
        if self.size != other.size:
            return False
        for entry in self:
            candidate = other.get_entry(entry.key)
            if candidate is None or not value_equals(entry.value, candidate.value):
                return False
        return True

    def diff(
        self,
        other: PersistentHashMap[K, V],
        value_equals: Callable[[V, V], bool] = _values_equal,
    ) -> Iterator[MapDifference[K, V]]:
        self._require_same_policy(other)
        if self._root is other._root:
            return
        for entry in self:
            after = other.get_entry(entry.key)
            if after is None:
                yield MapDifference("removed", entry.key, entry.value, None)
            elif not value_equals(entry.value, after.value):
                yield MapDifference("changed", entry.key, entry.value, after.value)
        for entry in other:
            if not self.contains_key(entry.key):
                yield MapDifference("added", entry.key, None, entry.value)

    def keys(self) -> Iterator[K]:
        for entry in self:
            yield entry.key

    def values(self) -> Iterator[V]:
        for entry in self:
            yield entry.value

    def entries(self) -> Iterator[HamtEntry[K, V]]:
        return iter(self)

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        return iter(()) if self._root is None else _entries_of_node(self._root)


class PersistentHashSet(Generic[T]):
    """Immutable CHAMP set preserving stored representatives and policy identity."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentHashMap[T, bool]) -> None:
        self._map = map_value

    @classmethod
    def empty(cls, policy: HashPolicy[T] | None = None) -> PersistentHashSet[T]:
        return cls(PersistentHashMap.empty(policy))

    @classmethod
    def from_values(
        cls, values: Iterable[T], policy: HashPolicy[T] | None = None
    ) -> PersistentHashSet[T]:
        return cls.empty(policy).union(values)

    @classmethod
    def create_transient(cls, policy: HashPolicy[T] | None = None) -> TransientHashSet[T]:
        return TransientHashSet(cls.empty(policy))

    def to_transient(self) -> TransientHashSet[T]:
        return TransientHashSet(self)

    @property
    def size(self) -> int:
        return self._map.size

    @property
    def is_empty(self) -> bool:
        return self._map.is_empty

    @property
    def policy(self) -> HashPolicy[T]:
        return self._map.policy

    def __len__(self) -> int:
        return self.size

    def __bool__(self) -> bool:
        return not self.is_empty

    def shares_root_with(self, other: PersistentHashSet[T]) -> bool:
        return self._map.shares_root_with(other._map)

    def contains(self, value: T) -> bool:
        return self._map.contains_key(value)

    def __contains__(self, value: object) -> bool:
        return self.contains(cast("T", value))

    def get(self, value: T) -> T | None:
        entry = self._map.get_entry(value)
        return None if entry is None else entry.key

    def _with_map(self, map_value: PersistentHashMap[T, bool]) -> PersistentHashSet[T]:
        return self if map_value is self._map else PersistentHashSet(map_value)

    def add(self, value: T) -> PersistentHashSet[T]:
        return self._with_map(self._map.add(value, True))

    def try_add(self, value: T) -> AddResult[PersistentHashSet[T]]:
        result = self._map.try_add(value, True)
        return AddResult(self._with_map(result.value), result.added)

    def put(self, value: T) -> PersistentHashSet[T]:
        return self._with_map(self._map.put(value, True))

    def remove(self, value: T) -> PersistentHashSet[T]:
        return self._with_map(self._map.remove(value))

    def try_remove(self, value: T) -> SetRemoveResult[T] | None:
        result = self._map.try_remove_entry(value)
        if result is None:
            return None
        return SetRemoveResult(self._with_map(result.map), result.entry.key)

    def clear(self) -> PersistentHashSet[T]:
        return self._with_map(self._map.clear())

    def union(self, values: Iterable[T]) -> PersistentHashSet[T]:
        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.union(values._map))
        result = self
        for value in values:
            result = result.put(value)
        return result

    def intersect(self, values: Iterable[T]) -> PersistentHashSet[T]:
        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.intersect(values._map))
        probe = PersistentHashSet.from_values(values, self.policy)
        result: PersistentHashSet[T] = PersistentHashSet.empty(self.policy)
        for value in self:
            if probe.contains(value):
                result = result.put(value)
        return self if result.set_equals(self) else result

    def except_(self, values: Iterable[T]) -> PersistentHashSet[T]:
        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.except_(values._map))
        result = self
        for value in values:
            result = result.remove(value)
        return result

    def symmetric_except(self, values: Iterable[T]) -> PersistentHashSet[T]:
        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.symmetric_except(values._map))
        distinct = PersistentHashSet.from_values(values, self.policy)
        result = self
        for value in distinct:
            result = result.remove(value) if result.contains(value) else result.put(value)
        return result

    def is_subset_of(self, values: Iterable[T]) -> bool:
        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size <= probe.size and all(probe.contains(value) for value in self)

    def is_proper_subset_of(self, values: Iterable[T]) -> bool:
        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size < probe.size and self.is_subset_of(probe)

    def is_superset_of(self, values: Iterable[T]) -> bool:
        return all(self.contains(value) for value in values)

    def is_proper_superset_of(self, values: Iterable[T]) -> bool:
        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size > probe.size and self.is_superset_of(probe)

    def overlaps(self, values: Iterable[T]) -> bool:
        return any(self.contains(value) for value in values)

    def set_equals(self, values: Iterable[T]) -> bool:
        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size == probe.size and self.is_subset_of(probe)

    def __iter__(self) -> Iterator[T]:
        return self._map.keys()


class _VersionedMapIterator(Iterator[HamtEntry[K, V]]):
    __slots__ = ("_expected", "_iterator", "_owner")

    def __init__(self, owner: TransientHashMap[K, V]) -> None:
        owner._ensure_active()
        self._owner = owner
        self._expected = owner._version
        self._iterator = iter(owner._current)

    def __next__(self) -> HamtEntry[K, V]:
        self._owner._validate_version(self._expected)
        return next(self._iterator)


class _VersionedSetIterator(Iterator[T]):
    __slots__ = ("_expected", "_iterator", "_owner")

    def __init__(self, owner: TransientHashSet[T]) -> None:
        owner._ensure_active()
        self._owner = owner
        self._expected = owner._version
        self._iterator = iter(owner._current)

    def __next__(self) -> T:
        self._owner._validate_version(self._expected)
        return next(self._iterator)


class TransientHashMap(Generic[K, V]):
    """Unsynchronized single-owner edit-then-publish CHAMP session."""

    __slots__ = ("_active", "_current", "_version")

    def __init__(self, source: PersistentHashMap[K, V]) -> None:
        self._current = source
        self._active = True
        self._version = 0

    @property
    def size(self) -> int:
        self._ensure_active()
        return self._current.size

    @property
    def is_empty(self) -> bool:
        return self.size == 0

    @property
    def policy(self) -> HashPolicy[K]:
        self._ensure_active()
        return self._current.policy

    def contains_key(self, key: K) -> bool:
        self._ensure_active()
        return self._current.contains_key(key)

    def get(self, key: K) -> V | None:
        self._ensure_active()
        return self._current.get(key)

    def get_entry(self, key: K) -> HamtEntry[K, V] | None:
        self._ensure_active()
        return self._current.get_entry(key)

    def set(self, key: K, value: V) -> None:
        self._publish_mutation(self._current.put(key, value))

    def try_add(self, key: K, value: V) -> bool:
        self._ensure_active()
        result = self._current.try_add(key, value)
        if result.added:
            self._publish_mutation(result.value)
        return result.added

    def add(self, key: K, value: V) -> None:
        if not self.try_add(key, value):
            raise DuplicateKeyError("An equivalent key is already present.")

    def remove(self, key: K) -> bool:
        self._ensure_active()
        result = self._current.try_remove_entry(key)
        if result is None:
            return False
        self._publish_mutation(result.map)
        return True

    def clear(self) -> None:
        self._publish_mutation(self._current.clear())

    def persist(self) -> PersistentHashMap[K, V]:
        self._ensure_active()
        self._active = False
        return self._current

    def keys(self) -> Iterator[K]:
        entries = iter(self)
        return (entry.key for entry in entries)

    def values(self) -> Iterator[V]:
        entries = iter(self)
        return (entry.value for entry in entries)

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        return _VersionedMapIterator(self)

    def _publish_mutation(self, next_value: PersistentHashMap[K, V]) -> None:
        self._ensure_active()
        if next_value is not self._current:
            self._current = next_value
            self._version += 1

    def _validate_version(self, expected: int) -> None:
        self._ensure_active()
        if self._version != expected:
            raise RuntimeError("The transient was modified during iteration.")

    def _ensure_active(self) -> None:
        if not self._active:
            raise TransientConsumedError("The transient session has already been published.")


class TransientHashSet(Generic[T]):
    """Unsynchronized single-owner wrapper for a persistent CHAMP set."""

    __slots__ = ("_active", "_current", "_version")

    def __init__(self, source: PersistentHashSet[T]) -> None:
        self._current = source
        self._active = True
        self._version = 0

    @property
    def size(self) -> int:
        self._ensure_active()
        return self._current.size

    @property
    def is_empty(self) -> bool:
        return self.size == 0

    @property
    def policy(self) -> HashPolicy[T]:
        self._ensure_active()
        return self._current.policy

    def contains(self, value: T) -> bool:
        self._ensure_active()
        return self._current.contains(value)

    def get(self, value: T) -> T | None:
        self._ensure_active()
        return self._current.get(value)

    def add(self, value: T) -> bool:
        self._ensure_active()
        result = self._current.try_add(value)
        if result.added:
            self._publish_mutation(result.value)
        return result.added

    def put(self, value: T) -> None:
        self._publish_mutation(self._current.put(value))

    def remove(self, value: T) -> bool:
        self._ensure_active()
        result = self._current.try_remove(value)
        if result is None:
            return False
        self._publish_mutation(result.set)
        return True

    def clear(self) -> None:
        self._publish_mutation(self._current.clear())

    def persist(self) -> PersistentHashSet[T]:
        self._ensure_active()
        self._active = False
        return self._current

    def __iter__(self) -> Iterator[T]:
        return _VersionedSetIterator(self)

    def _publish_mutation(self, next_value: PersistentHashSet[T]) -> None:
        self._ensure_active()
        if next_value is not self._current:
            self._current = next_value
            self._version += 1

    def _validate_version(self, expected: int) -> None:
        self._ensure_active()
        if self._version != expected:
            raise RuntimeError("The transient was modified during iteration.")

    def _ensure_active(self) -> None:
        if not self._active:
            raise TransientConsumedError("The transient session has already been published.")


__all__ = [
    "AddResult",
    "DuplicateKeyError",
    "HamtEntry",
    "MapDifference",
    "MapDifferenceKind",
    "MapRemoveEntryResult",
    "MapRemoveResult",
    "PersistentHashMap",
    "PersistentHashSet",
    "SetRemoveResult",
    "TransientConsumedError",
    "TransientHashMap",
    "TransientHashSet",
]
