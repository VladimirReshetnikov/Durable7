"""Persistent 32-way CHAMP maps, sets, and one-way editing sessions."""

from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass, field
from typing import Generic, Literal, TypeVar, cast

from .hash_policy import HashPolicy, default_hash_policy

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
        """Cache the bucket's entry count, which never changes once the node is built."""

        object.__setattr__(self, "entry_count", len(self.entries))


@dataclass(frozen=True, slots=True)
class _BitmapNode(_Node[K, V]):
    data_map: int
    node_map: int
    data: tuple[_Leaf[K, V], ...]
    nodes: tuple[_Node[K, V], ...]
    entry_count: int = field(init=False)

    def __post_init__(self) -> None:
        """Cache the subtree's entry count from this node's inline entries and its children."""

        object.__setattr__(
            self,
            "entry_count",
            len(self.data) + sum(node.entry_count for node in self.nodes),
        )


class _MutableNode(Generic[K, V]):
    __slots__ = ()


@dataclass(slots=True)
class _MutableLeaf(_MutableNode[K, V]):
    hash: int
    key: K
    value: V


@dataclass(slots=True)
class _MutableCollision(_MutableNode[K, V]):
    hash: int
    entries: list[_MutableLeaf[K, V]]


@dataclass(slots=True)
class _MutableBitmap(_MutableNode[K, V]):
    data_map: int
    node_map: int
    data: list[_MutableLeaf[K, V]]
    nodes: list[_MutableNode[K, V]]


@dataclass(frozen=True, slots=True)
class _InsertResult(Generic[K, V]):
    node: _Node[K, V]
    added: bool
    changed: bool
    duplicate: bool


@dataclass(frozen=True, slots=True)
class _FactoryNodeUpdate(Generic[K, V]):
    node: _Node[K, V]
    value: V
    added: bool
    changed: bool


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
class MapUpdateResult(Generic[K, V]):
    """A persistent factory update and the concrete value it selected."""

    map: PersistentHashMap[K, V]
    value: V


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
    if left is right:
        return True
    result = left == right
    return result if isinstance(result, bool) else False


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


def _mutable_leaves(node: _MutableNode[K, V]) -> list[_MutableLeaf[K, V]]:
    if isinstance(node, _MutableLeaf):
        return [node]
    if isinstance(node, _MutableCollision):
        return list(node.entries)
    assert isinstance(node, _MutableBitmap)
    result = list(node.data)
    for child in node.nodes:
        result.extend(_mutable_leaves(child))
    return result


def _merge_mutable_nodes(
    left: _MutableNode[K, V],
    left_hash: int,
    right: _MutableNode[K, V],
    right_hash: int,
    shift: int,
) -> _MutableNode[K, V]:
    if left_hash == right_hash:
        return _MutableCollision(left_hash, _mutable_leaves(left) + _mutable_leaves(right))
    left_bit = _bit_position(_hash_fragment(left_hash, shift))
    right_bit = _bit_position(_hash_fragment(right_hash, shift))
    if left_bit == right_bit:
        child = _merge_mutable_nodes(left, left_hash, right, right_hash, shift + _BITS_PER_LEVEL)
        return _MutableBitmap(0, left_bit, [], [child])
    ordered = [(left_bit, left), (right_bit, right)]
    ordered.sort(key=lambda item: item[0])
    data: list[_MutableLeaf[K, V]] = []
    nodes: list[_MutableNode[K, V]] = []
    data_map = 0
    node_map = 0
    for bit, node in ordered:
        if isinstance(node, _MutableLeaf):
            data_map |= bit
            data.append(node)
        else:
            node_map |= bit
            nodes.append(node)
    return _MutableBitmap(data_map, node_map, data, nodes)


def _set_mutable_node(
    node: _MutableNode[K, V],
    hash_value: int,
    key: K,
    value: V,
    shift: int,
    policy: HashPolicy[K],
    update: Callable[[V, V], V] | None = None,
) -> tuple[_MutableNode[K, V], bool, V]:
    if isinstance(node, _MutableLeaf):
        if node.hash == hash_value and policy.equivalent(node.key, key):
            selected = value if update is None else update(node.value, value)
            if _values_equal(node.value, selected):
                return node, False, node.value
            node.value = selected
            return node, False, selected
        return (
            _merge_mutable_nodes(
                node,
                node.hash,
                _MutableLeaf(hash_value, key, value),
                hash_value,
                shift,
            ),
            True,
            value,
        )

    if isinstance(node, _MutableCollision):
        if node.hash != hash_value:
            return (
                _merge_mutable_nodes(
                    node,
                    node.hash,
                    _MutableLeaf(hash_value, key, value),
                    hash_value,
                    shift,
                ),
                True,
                value,
            )
        for entry in node.entries:
            if not policy.equivalent(entry.key, key):
                continue
            selected = value if update is None else update(entry.value, value)
            if _values_equal(entry.value, selected):
                return node, False, entry.value
            entry.value = selected
            return node, False, selected
        node.entries.append(_MutableLeaf(hash_value, key, value))
        return node, True, value

    assert isinstance(node, _MutableBitmap)
    bit = _bit_position(_hash_fragment(hash_value, shift))
    if node.data_map & bit:
        data_index = _sparse_index(node.data_map, bit)
        current = node.data[data_index]
        if current.hash == hash_value and policy.equivalent(current.key, key):
            selected = value if update is None else update(current.value, value)
            if _values_equal(current.value, selected):
                return node, False, current.value
            current.value = selected
            return node, False, selected
        child = _merge_mutable_nodes(
            current,
            current.hash,
            _MutableLeaf(hash_value, key, value),
            hash_value,
            shift + _BITS_PER_LEVEL,
        )
        node.data.pop(data_index)
        node.data_map &= ~bit
        node.nodes.insert(_sparse_index(node.node_map, bit), child)
        node.node_map |= bit
        return node, True, value
    if node.node_map & bit:
        node_index = _sparse_index(node.node_map, bit)
        child, added, selected = _set_mutable_node(
            node.nodes[node_index],
            hash_value,
            key,
            value,
            shift + _BITS_PER_LEVEL,
            policy,
            update,
        )
        node.nodes[node_index] = child
        return node, added, selected
    node.data.insert(_sparse_index(node.data_map, bit), _MutableLeaf(hash_value, key, value))
    node.data_map |= bit
    return node, True, value


def _freeze_mutable(node: _MutableNode[K, V]) -> _Node[K, V]:
    if isinstance(node, _MutableLeaf):
        return _leaf(node.hash, node.key, node.value)
    if isinstance(node, _MutableCollision):
        return _collision(
            node.hash,
            tuple(_leaf(entry.hash, entry.key, entry.value) for entry in node.entries),
        )
    assert isinstance(node, _MutableBitmap)
    return _bitmap(
        node.data_map,
        node.node_map,
        tuple(_leaf(entry.hash, entry.key, entry.value) for entry in node.data),
        tuple(_freeze_mutable(child) for child in node.nodes),
    )


def _factory_update_node(
    node: _Node[K, V],
    hash_value: int,
    key: K,
    shift: int,
    policy: HashPolicy[K],
    add_factory: Callable[[K], V],
    update_factory: Callable[[K, V], V] | None,
) -> _FactoryNodeUpdate[K, V]:
    if isinstance(node, _Leaf):
        if node.hash == hash_value and policy.equivalent(node.key, key):
            if update_factory is None:
                return _FactoryNodeUpdate(node, node.value, False, False)
            selected = update_factory(key, node.value)
            if _values_equal(node.value, selected):
                return _FactoryNodeUpdate(node, node.value, False, False)
            return _FactoryNodeUpdate(_leaf(hash_value, node.key, selected), selected, False, True)
        selected = add_factory(key)
        return _FactoryNodeUpdate(
            _merge_nodes(node, node.hash, _leaf(hash_value, key, selected), hash_value, shift),
            selected,
            True,
            True,
        )

    if isinstance(node, _Collision):
        if node.hash != hash_value:
            selected = add_factory(key)
            return _FactoryNodeUpdate(
                _merge_nodes(node, node.hash, _leaf(hash_value, key, selected), hash_value, shift),
                selected,
                True,
                True,
            )
        for index, entry in enumerate(node.entries):
            if not policy.equivalent(entry.key, key):
                continue
            if update_factory is None:
                return _FactoryNodeUpdate(node, entry.value, False, False)
            selected = update_factory(key, entry.value)
            if _values_equal(entry.value, selected):
                return _FactoryNodeUpdate(node, entry.value, False, False)
            entries = _replace_at(node.entries, index, _leaf(hash_value, entry.key, selected))
            return _FactoryNodeUpdate(_collision(hash_value, entries), selected, False, True)
        selected = add_factory(key)
        return _FactoryNodeUpdate(
            _collision(hash_value, (*node.entries, _leaf(hash_value, key, selected))),
            selected,
            True,
            True,
        )

    assert isinstance(node, _BitmapNode)
    bit = _bit_position(_hash_fragment(hash_value, shift))
    if node.data_map & bit:
        data_index = _sparse_index(node.data_map, bit)
        current = node.data[data_index]
        if current.hash == hash_value and policy.equivalent(current.key, key):
            if update_factory is None:
                return _FactoryNodeUpdate(node, current.value, False, False)
            selected = update_factory(key, current.value)
            if _values_equal(current.value, selected):
                return _FactoryNodeUpdate(node, current.value, False, False)
            data = _replace_at(node.data, data_index, _leaf(hash_value, current.key, selected))
            return _FactoryNodeUpdate(
                _bitmap(node.data_map, node.node_map, data, node.nodes),
                selected,
                False,
                True,
            )
        selected = add_factory(key)
        merged_child = _merge_nodes(
            current,
            current.hash,
            _leaf(hash_value, key, selected),
            hash_value,
            shift + _BITS_PER_LEVEL,
        )
        nodes = _insert_at(node.nodes, _sparse_index(node.node_map, bit), merged_child)
        return _FactoryNodeUpdate(
            _bitmap(
                node.data_map & ~bit,
                node.node_map | bit,
                _remove_at(node.data, data_index),
                nodes,
            ),
            selected,
            True,
            True,
        )
    if node.node_map & bit:
        index = _sparse_index(node.node_map, bit)
        child = _factory_update_node(
            node.nodes[index],
            hash_value,
            key,
            shift + _BITS_PER_LEVEL,
            policy,
            add_factory,
            update_factory,
        )
        if not child.changed:
            return _FactoryNodeUpdate(node, child.value, child.added, False)
        return _FactoryNodeUpdate(
            _bitmap(
                node.data_map,
                node.node_map,
                node.data,
                _replace_at(node.nodes, index, child.node),
            ),
            child.value,
            child.added,
            True,
        )
    selected = add_factory(key)
    data = _insert_at(
        node.data,
        _sparse_index(node.data_map, bit),
        _leaf(hash_value, key, selected),
    )
    return _FactoryNodeUpdate(
        _bitmap(node.data_map | bit, node.node_map, data, node.nodes),
        selected,
        True,
        True,
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
        """Wrap an already-built root; use :meth:`empty` or :meth:`from_items` instead."""

        self._root = root
        self.size = size
        self.policy = policy

    @classmethod
    def empty(cls, policy: HashPolicy[K] | None = None) -> PersistentHashMap[K, V]:
        """Return an empty map retaining the exact policy object. Set algebra between two maps
        requires that same object, since agreement between independently created policies cannot
        be proven.
        """

        return cls(None, 0, default_hash_policy() if policy is None else policy)

    @classmethod
    def from_items(
        cls,
        items: Iterable[tuple[K, V]],
        policy: HashPolicy[K] | None = None,
    ) -> PersistentHashMap[K, V]:
        """Build a map from pairs through a bulk builder, avoiding per-item path copies. A repeated
        key keeps the last value.
        """

        builder = HashMapBulkBuilder[K, V](policy)
        builder.set_items(items)
        return builder.to_immutable()

    @classmethod
    def create_bulk_builder(cls, policy: HashPolicy[K] | None = None) -> HashMapBulkBuilder[K, V]:
        """Return a scratch builder that assembles unpublished nodes, for bulk construction."""

        return HashMapBulkBuilder(policy)

    @classmethod
    def create_transient(cls, policy: HashPolicy[K] | None = None) -> TransientHashMap[K, V]:
        """Return an empty single-owner editing session under ``policy``."""

        return TransientHashMap(cls.empty(policy))

    def to_transient(self) -> TransientHashMap[K, V]:
        """Return a single-owner editing session starting from this map. The map is unaffected by
        the session's edits; the session shares its root until its first change.
        """

        return TransientHashMap(self)

    @property
    def is_empty(self) -> bool:
        """Whether the map holds no entries."""

        return self.size == 0

    def __len__(self) -> int:
        """Number of entries."""

        return self.size

    def __bool__(self) -> bool:
        """Whether the map holds at least one entry."""

        return not self.is_empty

    def shares_root_with(self, other: PersistentHashMap[K, V]) -> bool:
        """Whether both maps reference the same trie root, so neither can observe an edit made to
        the other. A representation test used to confirm that a no-op avoided copying, not an
        equality test.
        """

        return self._root is other._root

    def contains_key(self, key: K) -> bool:
        """Whether ``key``'s equivalence class is present."""

        return self.get_entry(key) is not None

    def __contains__(self, key: object) -> bool:
        """Whether ``key`` is present, for the ``in`` operator."""

        return self.contains_key(cast("K", key))

    def get(self, key: K) -> V | None:
        """The value stored for ``key``, or ``None`` when absent. Use :meth:`get_entry` when a
        stored ``None`` must stay distinct from absence.
        """

        entry = self.get_entry(key)
        return None if entry is None else entry.value

    def __getitem__(self, key: K) -> V:
        """The value stored for ``key``, raising :class:`KeyError` when absent."""

        entry = self.get_entry(key)
        if entry is None:
            raise KeyError(key)
        return entry.value

    def get_entry(self, key: K) -> HamtEntry[K, V] | None:
        """The stored key representative and value for ``key``, or ``None`` when absent. The stored
        representative is the first inserted for its class, which need not be the value passed
        in.
        """

        if self._root is None:
            return None
        return _get_in_node(self._root, self.policy.hash(key), key, 0, self.policy)

    def put(self, key: K, value: V) -> PersistentHashMap[K, V]:
        """Return a map with ``key`` mapped to ``value``, adding or replacing as needed. Replacing
        keeps the stored key representative. A write that changes nothing returns the receiver,
        so only the affected root-to-leaf path is ever copied.
        """

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
        """Add a new entry, raising :class:`DuplicateKeyError` when ``key`` is already present. The
        receiver is unchanged on failure.
        """

        result = self.try_add(key, value)
        if not result.added:
            raise DuplicateKeyError("An equivalent key is already present.")
        return result.value

    def try_add(self, key: K, value: V) -> AddResult[PersistentHashMap[K, V]]:
        """Add a new entry, reporting whether it was added rather than raising on a duplicate."""

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

    def get_or_add(
        self,
        key: K,
        add_factory: Callable[[K], V],
    ) -> MapUpdateResult[K, V]:
        """Get a stored value or add one from a factory in one trie descent."""

        if not callable(add_factory):
            raise TypeError("add_factory must be callable.")
        return self._apply_factory_update(key, add_factory, None)

    def add_or_update(
        self,
        key: K,
        add_factory: Callable[[K], V],
        update_factory: Callable[[K, V], V],
    ) -> MapUpdateResult[K, V]:
        """Add or update through exactly one selected factory and one trie descent."""

        if not callable(add_factory):
            raise TypeError("add_factory must be callable.")
        if not callable(update_factory):
            raise TypeError("update_factory must be callable.")
        return self._apply_factory_update(key, add_factory, update_factory)

    def _apply_factory_update(
        self,
        key: K,
        add_factory: Callable[[K], V],
        update_factory: Callable[[K, V], V] | None,
    ) -> MapUpdateResult[K, V]:
        hash_value = self.policy.hash(key)
        if self._root is None:
            value = add_factory(key)
            return MapUpdateResult(
                PersistentHashMap(_leaf(hash_value, key, value), 1, self.policy),
                value,
            )
        result = _factory_update_node(
            self._root,
            hash_value,
            key,
            0,
            self.policy,
            add_factory,
            update_factory,
        )
        if not result.changed:
            return MapUpdateResult(self, result.value)
        return MapUpdateResult(
            PersistentHashMap(result.node, self.size + int(result.added), self.policy),
            result.value,
        )

    def set_items(self, items: Iterable[tuple[K, V]]) -> PersistentHashMap[K, V]:
        """Apply :meth:`put` for each pair in turn, so later pairs overwrite earlier ones. Only the
        final map is observable. For building from scratch, :meth:`from_items` avoids the per-
        item path copies entirely.
        """

        result = self
        for key, value in items:
            result = result.put(key, value)
        return result

    def remove(self, key: K) -> PersistentHashMap[K, V]:
        """Return a map without ``key``; a no-op returning the receiver when the key is absent."""

        result = self.try_remove_entry(key)
        return self if result is None else result.map

    def try_remove(self, key: K) -> MapRemoveResult[K, V] | None:
        """Remove ``key`` and report the removed value, or ``None`` when the key was absent."""

        result = self.try_remove_entry(key)
        return None if result is None else MapRemoveResult(result.map, result.entry.value)

    def try_remove_entry(self, key: K) -> MapRemoveEntryResult[K, V] | None:
        """Remove ``key`` and report its stored representative and value, or ``None`` if absent."""

        if self._root is None:
            return None
        result = _remove_node(self._root, self.policy.hash(key), key, 0, self.policy)
        if not result.changed or result.removed is None:
            return None
        return MapRemoveEntryResult(
            PersistentHashMap(result.node, self.size - 1, self.policy), result.removed
        )

    def clear(self) -> PersistentHashMap[K, V]:
        """Return an empty map retaining the policy; a no-op when already empty."""

        return self if self.is_empty else PersistentHashMap(None, 0, self.policy)

    def _require_same_policy(self, other: PersistentHashMap[K, V]) -> None:
        if self.policy is not other.policy:
            raise TypeError("Maps must retain the same hash-policy object.")

    def union(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        """Return a map holding every entry of both, with ``other``'s value winning on shared keys.
        Both maps must retain the same policy object.
        """

        self._require_same_policy(other)
        if self._root is other._root:
            return self
        return self.set_items((entry.key, entry.value) for entry in other)

    def intersect(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        """Keep only the entries whose keys occur in both maps, taking values from the receiver."""

        self._require_same_policy(other)
        if self._root is other._root:
            return self
        result: PersistentHashMap[K, V] = PersistentHashMap.empty(self.policy)
        for entry in self:
            if other.contains_key(entry.key):
                result = result.put(entry.key, entry.value)
        return self if result.map_equals(self) else result

    def except_(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        """Remove every key that occurs in ``other``. Named with a trailing underscore because
        ``except`` is a Python keyword.
        """

        self._require_same_policy(other)
        if self._root is other._root:
            return self.clear()
        result = self
        for entry in other:
            result = result.remove(entry.key)
        return result

    def symmetric_except(self, other: PersistentHashMap[K, V]) -> PersistentHashMap[K, V]:
        """Keep the entries whose keys occur in exactly one of the two maps."""

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
        """Whether both maps hold the same entries, comparing values with ``value_equals``.
        Identical roots short-circuit.
        """

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
        """Return the entry-level differences between this map and ``other``."""

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
        """Iterate the keys in canonical CHAMP order."""

        for entry in self:
            yield entry.key

    def values(self) -> Iterator[V]:
        """Iterate the values in canonical CHAMP order."""

        for entry in self:
            yield entry.value

    def entries(self) -> Iterator[HamtEntry[K, V]]:
        """Iterate the entries in canonical CHAMP order."""

        return iter(self)

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        """Iterate the entries in canonical CHAMP order. The order depends only on the keys' hashes
        under this map's policy, so it is stable for a given version but is not insertion order.
        """

        return iter(()) if self._root is None else _entries_of_node(self._root)


class HashMapBulkBuilder(Generic[K, V]):
    """Mutable scratch constructor over unpublished CHAMP nodes."""

    __slots__ = ("_root", "policy", "size")

    def __init__(self, policy: HashPolicy[K] | None = None) -> None:
        """Start an empty builder under ``policy``."""

        self._root: _MutableNode[K, V] | None = None
        self.size = 0
        self.policy = default_hash_policy() if policy is None else policy

    @property
    def is_empty(self) -> bool:
        """Whether nothing has been accumulated yet."""

        return self.size == 0

    def set_item(self, key: K, value: V) -> None:
        """Add or replace one entry, mutating the builder's unpublished nodes in place. Nothing here
        is shared with any published map, so this avoids the path copy a persistent write would
        need.
        """

        self._set_item(key, value, None)

    def _add_or_update(
        self,
        key: K,
        add_value: V,
        update: Callable[[V, V], V],
    ) -> V:
        return self._set_item(key, add_value, update)

    def _set_item(
        self,
        key: K,
        value: V,
        update: Callable[[V, V], V] | None,
    ) -> V:
        hash_value = self.policy.hash(key)
        if self._root is None:
            self._root = _MutableLeaf(hash_value, key, value)
            self.size = 1
            return value
        self._root, added, selected = _set_mutable_node(
            self._root,
            hash_value,
            key,
            value,
            0,
            self.policy,
            update,
        )
        self.size += int(added)
        return selected

    def set_items(self, items: Iterable[tuple[K, V]]) -> None:
        """Apply :meth:`set_item` for each pair in turn."""

        for key, value in items:
            self.set_item(key, value)

    def to_immutable(self) -> PersistentHashMap[K, V]:
        """Freeze the accumulated nodes into a persistent map. The builder must not be used
        afterwards: the frozen nodes are now shared with the published map, so further mutation
        would corrupt it.
        """

        root = None if self._root is None else _freeze_mutable(self._root)
        return PersistentHashMap(root, self.size, self.policy)


class PersistentHashSet(Generic[T]):
    """Immutable CHAMP set preserving stored representatives and policy identity."""

    __slots__ = ("_map",)

    def __init__(self, map_value: PersistentHashMap[T, bool]) -> None:
        """Wrap an already-built backing map; use :meth:`empty` or :meth:`from_values` instead."""

        self._map = map_value

    @classmethod
    def empty(cls, policy: HashPolicy[T] | None = None) -> PersistentHashSet[T]:
        """Return an empty set retaining the exact policy object."""

        return cls(PersistentHashMap.empty(policy))

    @classmethod
    def from_values(
        cls, values: Iterable[T], policy: HashPolicy[T] | None = None
    ) -> PersistentHashSet[T]:
        """Build a set from ``values``, keeping the first representative of each class."""

        builder = HashMapBulkBuilder[T, bool](policy)
        for value in values:
            builder.set_item(value, True)
        return cls(builder.to_immutable())

    @classmethod
    def create_transient(cls, policy: HashPolicy[T] | None = None) -> TransientHashSet[T]:
        """Return an empty single-owner editing session under ``policy``."""

        return TransientHashSet(cls.empty(policy))

    def to_transient(self) -> TransientHashSet[T]:
        """Return a single-owner editing session from this set, which is itself unaffected."""

        return TransientHashSet(self)

    @property
    def size(self) -> int:
        """Number of distinct elements."""

        return self._map.size

    @property
    def is_empty(self) -> bool:
        """Whether the set holds no elements."""

        return self._map.is_empty

    @property
    def policy(self) -> HashPolicy[T]:
        """The retained hash policy defining equivalence classes."""

        return self._map.policy

    def __len__(self) -> int:
        """Number of distinct elements, matching :attr:`size`."""

        return self.size

    def __bool__(self) -> bool:
        """Whether the set holds at least one element."""

        return not self.is_empty

    def shares_root_with(self, other: PersistentHashSet[T]) -> bool:
        """Whether both sets reference the same trie root. A representation test, not an equality
        test.
        """

        return self._map.shares_root_with(other._map)

    def contains(self, value: T) -> bool:
        """Whether ``value``'s equivalence class is present."""

        return self._map.contains_key(value)

    def __contains__(self, value: object) -> bool:
        """Whether ``value`` is present, for the ``in`` operator."""

        return self.contains(cast("T", value))

    def get(self, value: T) -> T | None:
        """The stored representative equivalent to ``value``, or ``None`` when absent."""

        entry = self._map.get_entry(value)
        return None if entry is None else entry.key

    def _with_map(self, map_value: PersistentHashMap[T, bool]) -> PersistentHashSet[T]:
        return self if map_value is self._map else PersistentHashSet(map_value)

    def add(self, value: T) -> PersistentHashSet[T]:
        """Add ``value``, raising :class:`DuplicateKeyError` when its class is already present."""

        return self._with_map(self._map.add(value, True))

    def try_add(self, value: T) -> AddResult[PersistentHashSet[T]]:
        """Add ``value``, reporting whether it was added rather than raising."""

        result = self._map.try_add(value, True)
        return AddResult(self._with_map(result.value), result.added)

    def put(self, value: T) -> PersistentHashSet[T]:
        """Return a set containing ``value``. An already present class is a no-op that keeps the
        existing representative and returns the receiver.
        """

        return self._with_map(self._map.put(value, True))

    def remove(self, value: T) -> PersistentHashSet[T]:
        """Return a set without ``value``'s class; a no-op when absent."""

        return self._with_map(self._map.remove(value))

    def try_remove(self, value: T) -> SetRemoveResult[T] | None:
        """Remove ``value``'s class and report its representative, or ``None`` when absent."""

        result = self._map.try_remove_entry(value)
        if result is None:
            return None
        return SetRemoveResult(self._with_map(result.map), result.entry.key)

    def clear(self) -> PersistentHashSet[T]:
        """Return an empty set retaining the policy."""

        return self._with_map(self._map.clear())

    def union(self, values: Iterable[T]) -> PersistentHashSet[T]:
        """Return the elements of this set and ``values``, keeping the receiver's representatives. A
        set sharing this policy takes a structural path; any other iterable is added element by
        element.
        """

        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.union(values._map))
        result = self
        for value in values:
            result = result.put(value)
        return result

    def intersect(self, values: Iterable[T]) -> PersistentHashSet[T]:
        """Return the elements this set and ``values`` have in common, keeping the receiver's
        representatives.
        """

        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.intersect(values._map))
        probe = PersistentHashSet.from_values(values, self.policy)
        builder = HashMapBulkBuilder[T, bool](self.policy)
        for value in self:
            if probe.contains(value):
                builder.set_item(value, True)
        result = PersistentHashSet(builder.to_immutable())
        return self if result.set_equals(self) else result

    def except_(self, values: Iterable[T]) -> PersistentHashSet[T]:
        """Return this set's elements that do not occur in ``values``."""

        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.except_(values._map))
        result = self
        for value in values:
            result = result.remove(value)
        return result

    def symmetric_except(self, values: Iterable[T]) -> PersistentHashSet[T]:
        """Return the elements occurring in exactly one of this set and ``values``."""

        if isinstance(values, PersistentHashSet) and values.policy is self.policy:
            return self._with_map(self._map.symmetric_except(values._map))
        distinct = PersistentHashSet.from_values(values, self.policy)
        result = self
        for value in distinct:
            result = result.remove(value) if result.contains(value) else result.put(value)
        return result

    def is_subset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of this set also occurs in ``values``. A non-set iterable is
        normalized under this set's policy first, so repeats count once.
        """

        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size <= probe.size and all(probe.contains(value) for value in self)

    def is_proper_subset_of(self, values: Iterable[T]) -> bool:
        """Whether this set is a subset of ``values`` and ``values`` has an element it lacks."""

        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size < probe.size and self.is_subset_of(probe)

    def is_superset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of ``values`` occurs in this set."""

        return all(self.contains(value) for value in values)

    def is_proper_superset_of(self, values: Iterable[T]) -> bool:
        """Whether this set is a superset of ``values`` and holds an element they lack."""

        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size > probe.size and self.is_superset_of(probe)

    def overlaps(self, values: Iterable[T]) -> bool:
        """Whether this set shares at least one element with ``values``."""

        return any(self.contains(value) for value in values)

    def set_equals(self, values: Iterable[T]) -> bool:
        """Whether this set holds exactly the distinct elements of ``values``."""

        probe = (
            values
            if isinstance(values, PersistentHashSet) and values.policy is self.policy
            else PersistentHashSet.from_values(values, self.policy)
        )
        return self.size == probe.size and self.is_subset_of(probe)

    def __iter__(self) -> Iterator[T]:
        """Iterate the stored representatives in canonical CHAMP order."""

        return self._map.keys()


class _VersionedMapIterator(Iterator[HamtEntry[K, V]]):
    __slots__ = ("_expected", "_iterator", "_owner")

    def __init__(self, owner: TransientHashMap[K, V]) -> None:
        """Capture the session's current version, so later mutation invalidates this iterator."""

        owner._ensure_active()
        self._owner = owner
        self._expected = owner._version
        self._iterator = iter(owner._current)

    def __next__(self) -> HamtEntry[K, V]:
        """Yield the next entry, raising when the session was mutated since iteration began."""

        self._owner._validate_version(self._expected)
        return next(self._iterator)


class _VersionedSetIterator(Iterator[T]):
    __slots__ = ("_expected", "_iterator", "_owner")

    def __init__(self, owner: TransientHashSet[T]) -> None:
        """Capture the session's current version, so later mutation invalidates this iterator."""

        owner._ensure_active()
        self._owner = owner
        self._expected = owner._version
        self._iterator = iter(owner._current)

    def __next__(self) -> T:
        """Yield the next element, raising when the session was mutated since iteration began."""

        self._owner._validate_version(self._expected)
        return next(self._iterator)


class TransientHashMap(Generic[K, V]):
    """Unsynchronized single-owner edit-then-publish CHAMP session."""

    __slots__ = ("_active", "_current", "_version")

    def __init__(self, source: PersistentHashMap[K, V]) -> None:
        """Begin a session over ``source``, which is itself never modified."""

        self._current = source
        self._active = True
        self._version = 0

    @property
    def size(self) -> int:
        """Number of entries currently in the session."""

        self._ensure_active()
        return self._current.size

    @property
    def is_empty(self) -> bool:
        """Whether the session holds no entries."""

        return self.size == 0

    @property
    def policy(self) -> HashPolicy[K]:
        """The retained hash policy defining key equivalence."""

        self._ensure_active()
        return self._current.policy

    def contains_key(self, key: K) -> bool:
        """Whether ``key`` is present in the session's current state."""

        self._ensure_active()
        return self._current.contains_key(key)

    def get(self, key: K) -> V | None:
        """The value stored for ``key``, or ``None`` when absent."""

        self._ensure_active()
        return self._current.get(key)

    def get_entry(self, key: K) -> HamtEntry[K, V] | None:
        """The stored key representative and value, or ``None`` when absent."""

        self._ensure_active()
        return self._current.get_entry(key)

    def set(self, key: K, value: V) -> None:
        """Add or replace one entry in the session."""

        self._publish_mutation(self._current.put(key, value))

    def try_add(self, key: K, value: V) -> bool:
        """Add one entry unless its key is present, reporting whether it was added."""

        self._ensure_active()
        result = self._current.try_add(key, value)
        if result.added:
            self._publish_mutation(result.value)
        return result.added

    def add(self, key: K, value: V) -> None:
        """Add one entry, raising :class:`DuplicateKeyError` when the key is already present."""

        if not self.try_add(key, value):
            raise DuplicateKeyError("An equivalent key is already present.")

    def remove(self, key: K) -> bool:
        """Remove ``key``, reporting whether it was there to remove."""

        self._ensure_active()
        result = self._current.try_remove_entry(key)
        if result is None:
            return False
        self._publish_mutation(result.map)
        return True

    def clear(self) -> None:
        """Discard every entry, retaining the policy."""

        self._publish_mutation(self._current.clear())

    def persist(self) -> PersistentHashMap[K, V]:
        """End the session and return its current map. The session is closed afterwards: every
        further operation raises, which is what makes the published map safe to share.
        """

        self._ensure_active()
        self._active = False
        return self._current

    def keys(self) -> Iterator[K]:
        """Iterate the keys of the session's current state."""

        entries = iter(self)
        return (entry.key for entry in entries)

    def values(self) -> Iterator[V]:
        """Iterate the values of the session's current state."""

        entries = iter(self)
        return (entry.value for entry in entries)

    def __iter__(self) -> Iterator[HamtEntry[K, V]]:
        """Iterate the session's entries, raising if the session is mutated during iteration."""

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
        """Begin a session over ``source``, which is itself never modified."""

        self._current = source
        self._active = True
        self._version = 0

    @property
    def size(self) -> int:
        """Number of elements currently in the session."""

        self._ensure_active()
        return self._current.size

    @property
    def is_empty(self) -> bool:
        """Whether the session holds no elements."""

        return self.size == 0

    @property
    def policy(self) -> HashPolicy[T]:
        """The retained hash policy defining equivalence classes."""

        self._ensure_active()
        return self._current.policy

    def contains(self, value: T) -> bool:
        """Whether ``value`` is present in the session's current state."""

        self._ensure_active()
        return self._current.contains(value)

    def get(self, value: T) -> T | None:
        """The stored representative equivalent to ``value``, or ``None`` when absent."""

        self._ensure_active()
        return self._current.get(value)

    def is_subset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of the session also occurs in ``values``."""

        self._ensure_active()
        return self._current.is_subset_of(values)

    def is_proper_subset_of(self, values: Iterable[T]) -> bool:
        """Whether the session is a subset of ``values`` and ``values`` has an element it lacks."""

        self._ensure_active()
        return self._current.is_proper_subset_of(values)

    def is_superset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of ``values`` occurs in the session."""

        self._ensure_active()
        return self._current.is_superset_of(values)

    def is_proper_superset_of(self, values: Iterable[T]) -> bool:
        """Whether the session is a superset of ``values`` and holds an element they lack."""

        self._ensure_active()
        return self._current.is_proper_superset_of(values)

    def overlaps(self, values: Iterable[T]) -> bool:
        """Whether the session shares at least one element with ``values``."""

        self._ensure_active()
        return self._current.overlaps(values)

    def set_equals(self, values: Iterable[T]) -> bool:
        """Whether the session holds exactly the distinct elements of ``values``."""

        self._ensure_active()
        return self._current.set_equals(values)

    def add(self, value: T) -> bool:
        """Add ``value`` unless present, reporting whether it was added."""

        self._ensure_active()
        result = self._current.try_add(value)
        if result.added:
            self._publish_mutation(result.value)
        return result.added

    def put(self, value: T) -> None:
        """Add ``value``, keeping the existing representative when its class is present."""

        self._publish_mutation(self._current.put(value))

    def remove(self, value: T) -> bool:
        """Remove ``value``'s class, reporting whether it was there to remove."""

        self._ensure_active()
        result = self._current.try_remove(value)
        if result is None:
            return False
        self._publish_mutation(result.set)
        return True

    def clear(self) -> None:
        """Discard every element, retaining the policy."""

        self._publish_mutation(self._current.clear())

    def persist(self) -> PersistentHashSet[T]:
        """End the session and return its current set. The session is closed afterwards: every
        further operation raises.
        """

        self._ensure_active()
        self._active = False
        return self._current

    def __iter__(self) -> Iterator[T]:
        """Iterate the session's elements, raising if the session is mutated during iteration."""

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
    "HashMapBulkBuilder",
    "MapDifference",
    "MapDifferenceKind",
    "MapRemoveEntryResult",
    "MapRemoveResult",
    "MapUpdateResult",
    "PersistentHashMap",
    "PersistentHashSet",
    "SetRemoveResult",
    "TransientConsumedError",
    "TransientHashMap",
    "TransientHashSet",
]
