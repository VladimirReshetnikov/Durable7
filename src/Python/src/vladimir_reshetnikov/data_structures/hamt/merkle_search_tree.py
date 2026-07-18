"""Canonical wide persistent Merkle search tree with exact ``MST2`` node blocks."""

from __future__ import annotations

import struct
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from functools import cmp_to_key
from typing import Generic, Literal, TypeVar, cast

from .merkle_encoding import MerkleDigest, MerkleSearchTreePolicy, merkle_level

K = TypeVar("K")
V = TypeVar("V")
W = TypeVar("W")


@dataclass(frozen=True, slots=True)
class MerkleEntry(Generic[K, V]):
    key: K
    value: V


@dataclass(frozen=True, slots=True)
class _StoredEntry(Generic[K, V]):
    key: K
    value: V
    key_bytes: bytes
    value_bytes: bytes
    level: int


@dataclass(frozen=True, slots=True)
class _Node(Generic[K, V]):
    level: int
    entries: tuple[_StoredEntry[K, V], ...]
    children: tuple[_Node[K, V] | None, ...]
    count: int
    height: int
    block_count: int
    minimum_key: K
    maximum_key: K
    block_bytes: bytes
    digest: MerkleDigest


MerkleMapDifferenceKind = Literal["left-only", "right-only", "different-value"]


@dataclass(frozen=True, slots=True)
class MerkleMapDifference(Generic[K, V]):
    kind: MerkleMapDifferenceKind
    key: K
    left_present: bool
    right_present: bool
    left: V | None = None
    right: V | None = None


@dataclass(frozen=True, slots=True)
class MerkleSearchTreeStatistics:
    count: int
    height: int
    block_count: int
    maximum_entries_per_block: int
    maximum_children_per_block: int
    encoded_bytes: int
    maximum_level: int


@dataclass(frozen=True, slots=True)
class MerkleEncodedBlock:
    digest: MerkleDigest
    bytes: bytes


def _i32(value: int) -> bytes:
    if not -(1 << 31) <= value < (1 << 31):
        raise OverflowError("MST2 signed 32-bit field overflow")
    return struct.pack(">i", value)


def _lower_bound(
    entries: tuple[_StoredEntry[K, V], ...],
    key: K,
    comparer: Callable[[K, K], int],
) -> int:
    low = 0
    high = len(entries)
    while low < high:
        middle = (low + high) // 2
        if comparer(entries[middle].key, key) < 0:
            low = middle + 1
        else:
            high = middle
    return low


def _value_equal(left: object, right: object) -> bool:
    return left is right or bool(left == right)


class MerkleSearchTree(Generic[K, V]):
    """Immutable ordered map whose canonical wide nodes are SHA-256 content addressed."""

    __slots__ = ("_policy", "_root")

    def __init__(
        self,
        policy: MerkleSearchTreePolicy[K, V],
        root: _Node[K, V] | None = None,
    ) -> None:
        self._policy = policy
        self._root = root

    @classmethod
    def empty(cls, policy: MerkleSearchTreePolicy[K, V]) -> MerkleSearchTree[K, V]:
        return cls(policy)

    @classmethod
    def from_entries(
        cls,
        entries: Iterable[tuple[K, V] | MerkleEntry[K, V]],
        policy: MerkleSearchTreePolicy[K, V],
    ) -> MerkleSearchTree[K, V]:
        prepared: list[_StoredEntry[K, V]] = []
        for item in entries:
            key, value = (item.key, item.value) if isinstance(item, MerkleEntry) else item
            prepared.append(cls._create_entry(policy, key, value))
        prepared.sort(key=cmp_to_key(lambda left, right: policy.comparer(left.key, right.key)))
        unique: list[_StoredEntry[K, V]] = []
        for stored in prepared:
            if unique and policy.comparer(unique[-1].key, stored.key) == 0:
                previous = unique[-1]
                unique[-1] = _StoredEntry(
                    previous.key,
                    stored.value,
                    previous.key_bytes,
                    stored.value_bytes,
                    previous.level,
                )
            else:
                unique.append(stored)
        shell = cls(policy)
        return cls(policy, shell._build_canonical(unique, 0, len(unique)))

    @property
    def policy(self) -> MerkleSearchTreePolicy[K, V]:
        return self._policy

    @property
    def count(self) -> int:
        return 0 if self._root is None else self._root.count

    def __len__(self) -> int:
        return self.count

    @property
    def is_empty(self) -> bool:
        return self._root is None

    @property
    def height(self) -> int:
        return 0 if self._root is None else self._root.height

    @property
    def block_count(self) -> int:
        return 0 if self._root is None else self._root.block_count

    @property
    def root_hash(self) -> MerkleDigest:
        return self._policy.empty_digest if self._root is None else self._root.digest

    def cursor(self, position: int = 0) -> MerkleSearchTreeCursor[K, V]:
        """Create an immutable ordered cursor at a rank gap in ``0 .. count``."""
        return MerkleSearchTreeCursor(self, position)

    def cursor_at_end(self) -> MerkleSearchTreeCursor[K, V]:
        """Create an immutable ordered cursor after the final entry."""
        return MerkleSearchTreeCursor(self, self.count)

    def lower_bound_cursor(self, key: K) -> MerkleSearchTreeCursor[K, V]:
        """Create a cursor before the first entry whose key is not less than ``key``."""
        rank, _ = self._lower_bound_rank_for_cursor(key)
        return MerkleSearchTreeCursor(self, rank)

    def upper_bound_cursor(self, key: K) -> MerkleSearchTreeCursor[K, V]:
        """Create a cursor before the first entry whose key is greater than ``key``."""
        rank, found = self._lower_bound_rank_for_cursor(key)
        return MerkleSearchTreeCursor(self, rank + 1 if found else rank)

    def cursor_at_key(self, key: K) -> MerkleCursorSearch[K, V]:
        """Create a lower-bound cursor and report whether its next entry has ``key``."""
        rank, found = self._lower_bound_rank_for_cursor(key)
        return MerkleCursorSearch(MerkleSearchTreeCursor(self, rank), found)

    def _entry_at_for_cursor(self, rank: int) -> MerkleEntry[K, V] | None:
        if rank < 0 or rank >= self.count:
            return None
        node = self._root
        while node is not None:
            current = node
            descended = False
            for index, stored in enumerate(current.entries):
                child = current.children[index]
                child_count = 0 if child is None else child.count
                if rank < child_count:
                    node = child
                    descended = True
                    break
                rank -= child_count
                if rank == 0:
                    return MerkleEntry(stored.key, stored.value)
                rank -= 1
            if not descended:
                node = current.children[-1]
        raise AssertionError("Merkle subtree counts do not contain the requested rank")

    def _lower_bound_rank_for_cursor(self, key: K) -> tuple[int, bool]:
        rank = 0
        node = self._root
        while node is not None:
            position = _lower_bound(node.entries, key, self._policy.comparer)
            for index in range(position):
                child = node.children[index]
                rank += (0 if child is None else child.count) + 1
            found = (
                position < len(node.entries)
                and self._policy.comparer(node.entries[position].key, key) == 0
            )
            if found:
                child = node.children[position]
                return rank + (0 if child is None else child.count), True
            node = node.children[position]
        return rank, False

    def get_entry(self, key: K) -> MerkleEntry[K, V] | None:
        found = self._find_record(key)
        return None if found is None else MerkleEntry(found.key, found.value)

    def get(self, key: K, default: W | None = None) -> V | W | None:
        found = self._find_record(key)
        return default if found is None else found.value

    def contains_key(self, key: K) -> bool:
        return self._find_record(key) is not None

    def set_item(self, key: K, value: V) -> MerkleSearchTree[K, V]:
        existing = self._find_record(key)
        if existing is not None:
            value_bytes = bytes(self._policy.value_codec.encode(value))
            if existing.value_bytes == value_bytes:
                return self
            replacement = _StoredEntry(
                existing.key,
                value,
                existing.key_bytes,
                value_bytes,
                existing.level,
            )
            if self._root is None:  # pragma: no cover - guarded by existing
                raise AssertionError("found record without a root")
            return MerkleSearchTree(self._policy, self._update_value(self._root, key, replacement))
        return MerkleSearchTree(
            self._policy,
            self._insert(self._root, self._create_entry(self._policy, key, value)),
        )

    # Familiar alias used by several sibling ports.
    set = set_item

    def remove(self, key: K) -> MerkleSearchTree[K, V]:
        root, removed = self._remove_node(self._root, key)
        return MerkleSearchTree(self._policy, root) if removed else self

    def clear(self) -> MerkleSearchTree[K, V]:
        return self if self.is_empty else MerkleSearchTree.empty(self._policy)

    def shares_root_with(self, other: MerkleSearchTree[K, V]) -> bool:
        return self._root is other._root

    def shared_block_count(self, other: MerkleSearchTree[K, V]) -> int:
        identities = {id(node) for node in self._nodes_preorder()}
        return sum(id(node) in identities for node in other._nodes_preorder())

    def content_equals(self, other: MerkleSearchTree[K, V]) -> bool:
        return (
            self._policy.is_compatible_with(
                cast("MerkleSearchTreePolicy[object, object]", other.policy)
            )
            and self.root_hash == other.root_hash
        )

    def map_equals(
        self,
        other: MerkleSearchTree[K, V],
        values_equal: Callable[[V, V], bool] = _value_equal,
    ) -> bool:
        if self.count != other.count:
            return False
        for left, right in zip(self, other, strict=True):
            if self._policy.comparer(left.key, right.key) != 0:
                return False
            if left.value is not right.value and not values_equal(left.value, right.value):
                return False
        return True

    def diff(
        self,
        other: MerkleSearchTree[K, V],
        values_equal: Callable[[V, V], bool] = _value_equal,
    ) -> tuple[MerkleMapDifference[K, V], ...]:
        if not self._policy.is_compatible_with(
            cast("MerkleSearchTreePolicy[object, object]", other.policy)
        ):
            raise ValueError("Merkle policy domains differ")
        if self.root_hash == other.root_hash:
            return ()
        left = tuple(self)
        right = tuple(other)
        result: list[MerkleMapDifference[K, V]] = []
        i = j = 0
        while i < len(left) or j < len(right):
            if i == len(left):
                item = right[j]
                result.append(
                    MerkleMapDifference("right-only", item.key, False, True, right=item.value)
                )
                j += 1
                continue
            if j == len(right):
                item = left[i]
                result.append(
                    MerkleMapDifference("left-only", item.key, True, False, left=item.value)
                )
                i += 1
                continue
            left_item = left[i]
            right_item = right[j]
            comparison = self._policy.comparer(left_item.key, right_item.key)
            if comparison < 0:
                result.append(
                    MerkleMapDifference(
                        "left-only", left_item.key, True, False, left=left_item.value
                    )
                )
                i += 1
            elif comparison > 0:
                result.append(
                    MerkleMapDifference(
                        "right-only", right_item.key, False, True, right=right_item.value
                    )
                )
                j += 1
            else:
                if left_item.value is not right_item.value and not values_equal(
                    left_item.value, right_item.value
                ):
                    result.append(
                        MerkleMapDifference(
                            "different-value",
                            left_item.key,
                            True,
                            True,
                            left_item.value,
                            right_item.value,
                        )
                    )
                i += 1
                j += 1
        return tuple(result)

    def range(self, minimum: K, maximum: K) -> Iterator[MerkleEntry[K, V]]:
        if self._policy.comparer(minimum, maximum) > 0:
            raise ValueError("minimum key exceeds maximum key")
        for entry in self:
            if self._policy.comparer(entry.key, minimum) < 0:
                continue
            if self._policy.comparer(entry.key, maximum) > 0:
                return
            yield entry

    def keys(self) -> Iterator[K]:
        for entry in self:
            yield entry.key

    def values(self) -> Iterator[V]:
        for entry in self:
            yield entry.value

    @property
    def shape(self) -> str:
        def visit(node: _Node[K, V] | None) -> str:
            if node is None:
                return "."
            return f"L{node.level}[{len(node.entries)}]({','.join(map(visit, node.children))})"

        return visit(self._root)

    def blocks_preorder(self) -> Iterator[MerkleEncodedBlock]:
        for node in self._nodes_preorder():
            yield MerkleEncodedBlock(node.digest, node.block_bytes)

    def validate_structure(self) -> MerkleSearchTreeStatistics:
        maximum_entries = maximum_children = encoded_bytes = maximum_level = 0
        blocks = logical_count = height = 0
        seen: set[int] = set()
        pending: list[tuple[_Node[K, V], K | None, K | None, bool, bool]] = []
        if self._root is not None:
            pending.append((self._root, None, None, False, False))
        while pending:
            node, minimum, maximum, has_minimum, has_maximum = pending.pop()
            if id(node) in seen:
                raise ValueError("Merkle tree contains a cycle or repeated nonempty child")
            seen.add(id(node))
            blocks += 1
            logical_count += len(node.entries)
            height = max(height, node.height)
            maximum_entries = max(maximum_entries, len(node.entries))
            maximum_children = max(maximum_children, len(node.children))
            encoded_bytes += len(node.block_bytes)
            maximum_level = max(maximum_level, node.level)
            if not node.entries or len(node.children) != len(node.entries) + 1:
                raise ValueError("invalid Merkle block arity")
            for index, entry in enumerate(node.entries):
                if entry.level != node.level:
                    raise ValueError("separator level differs from block level")
                if index and self._policy.comparer(node.entries[index - 1].key, entry.key) >= 0:
                    raise ValueError("Merkle separators are not strictly ordered")
                if has_minimum and self._policy.comparer(entry.key, cast(K, minimum)) <= 0:
                    raise ValueError("entry violates lower interval bound")
                if has_maximum and self._policy.comparer(entry.key, cast(K, maximum)) >= 0:
                    raise ValueError("entry violates upper interval bound")
                if bytes(self._policy.key_codec.encode(entry.key)) != entry.key_bytes:
                    raise ValueError("stored key no longer has its canonical encoding")
                if bytes(self._policy.value_codec.encode(entry.value)) != entry.value_bytes:
                    raise ValueError("stored value no longer has its canonical encoding")
                if merkle_level(self._policy.hash_key(entry.key_bytes)) != entry.level:
                    raise ValueError("key level cache is invalid")
            for index, child in enumerate(node.children):
                if child is None:
                    continue
                if child.level >= node.level:
                    raise ValueError("child levels must strictly decrease")
                child_min = minimum if index == 0 else node.entries[index - 1].key
                child_max = maximum if index == len(node.entries) else node.entries[index].key
                pending.append(
                    (
                        child,
                        child_min,
                        child_max,
                        has_minimum or index > 0,
                        has_maximum or index < len(node.entries),
                    )
                )
            rebuilt = self._new_node(node.level, node.entries, node.children)
            if (
                rebuilt.count != node.count
                or rebuilt.height != node.height
                or rebuilt.block_count != node.block_count
                or rebuilt.digest != node.digest
                or rebuilt.block_bytes != node.block_bytes
                or rebuilt.minimum_key is not node.minimum_key
                or rebuilt.maximum_key is not node.maximum_key
            ):
                raise ValueError("Merkle node caches are invalid")
        if logical_count != self.count or blocks != self.block_count:
            raise ValueError("Merkle aggregate caches are invalid")
        return MerkleSearchTreeStatistics(
            logical_count,
            height,
            blocks,
            maximum_entries,
            maximum_children,
            encoded_bytes,
            maximum_level,
        )

    def __iter__(self) -> Iterator[MerkleEntry[K, V]]:
        stack: list[tuple[_Node[K, V], int]] = []
        node = self._root
        while node is not None or stack:
            while node is not None:
                stack.append((node, 0))
                node = node.children[0]
            current, index = stack.pop()
            if index >= len(current.entries):
                continue
            entry = current.entries[index]
            stack.append((current, index + 1))
            node = current.children[index + 1]
            yield MerkleEntry(entry.key, entry.value)

    @classmethod
    def _from_verified_root(
        cls,
        policy: MerkleSearchTreePolicy[K, V],
        root: _Node[K, V] | None,
    ) -> MerkleSearchTree[K, V]:
        return cls(policy, root)

    def _create_verified_node(
        self,
        level: int,
        entries: tuple[_StoredEntry[K, V], ...],
        children: tuple[_Node[K, V] | None, ...],
    ) -> _Node[K, V]:
        return self._new_node(level, entries, children)

    def _create_verified_entry(
        self,
        key: K,
        value: V,
        key_bytes: bytes,
        value_bytes: bytes,
    ) -> _StoredEntry[K, V]:
        canonical_key = bytes(key_bytes)
        return _StoredEntry(
            key,
            value,
            canonical_key,
            bytes(value_bytes),
            merkle_level(self._policy.hash_key(canonical_key)),
        )

    @staticmethod
    def _create_entry(policy: MerkleSearchTreePolicy[K, V], key: K, value: V) -> _StoredEntry[K, V]:
        key_bytes = bytes(policy.key_codec.encode(key))
        value_bytes = bytes(policy.value_codec.encode(value))
        return _StoredEntry(
            key, value, key_bytes, value_bytes, merkle_level(policy.hash_key(key_bytes))
        )

    def _find_record(self, key: K) -> _StoredEntry[K, V] | None:
        node = self._root
        while node is not None:
            position = _lower_bound(node.entries, key, self._policy.comparer)
            if (
                position < len(node.entries)
                and self._policy.comparer(node.entries[position].key, key) == 0
            ):
                return node.entries[position]
            node = node.children[position]
        return None

    def _build_canonical(
        self, items: list[_StoredEntry[K, V]], start: int, length: int
    ) -> _Node[K, V] | None:
        if length == 0:
            return None
        maximum = max(item.level for item in items[start : start + length])
        entries: list[_StoredEntry[K, V]] = []
        children: list[_Node[K, V] | None] = []
        segment = start
        end = start + length
        for index in range(start, end):
            if items[index].level == maximum:
                children.append(self._build_canonical(items, segment, index - segment))
                entries.append(items[index])
                segment = index + 1
        children.append(self._build_canonical(items, segment, end - segment))
        return self._new_node(maximum, tuple(entries), tuple(children))

    def _new_node(
        self,
        level: int,
        entries: tuple[_StoredEntry[K, V], ...],
        children: tuple[_Node[K, V] | None, ...],
    ) -> _Node[K, V]:
        if not 0 <= level <= 64 or not entries or len(children) != len(entries) + 1:
            raise ValueError("invalid Merkle node")
        count = len(entries)
        height = block_count = 1
        for child in children:
            if child is not None:
                count += child.count
                height = max(height, child.height + 1)
                block_count += child.block_count
        if count >= 1 << 31:
            raise OverflowError("Merkle tree count exceeds the MST2 int32 field")
        block_bytes = self._encode_block(level, count, entries, children)
        return _Node(
            level,
            entries,
            children,
            count,
            height,
            block_count,
            children[0].minimum_key if children[0] is not None else entries[0].key,
            children[-1].maximum_key if children[-1] is not None else entries[-1].key,
            block_bytes,
            MerkleDigest.hash(block_bytes),
        )

    def _encode_block(
        self,
        level: int,
        count: int,
        entries: tuple[_StoredEntry[K, V], ...],
        children: tuple[_Node[K, V] | None, ...],
    ) -> bytes:
        parts = [
            b"MST2\x01",
            bytes(self._policy.domain_digest),
            bytes((level,)),
            _i32(count),
            _i32(len(entries)),
        ]
        for entry in entries:
            parts.extend(
                (
                    _i32(len(entry.key_bytes)),
                    entry.key_bytes,
                    _i32(len(entry.value_bytes)),
                    entry.value_bytes,
                )
            )
        parts.extend(
            bytes(self._policy.empty_digest if child is None else child.digest)
            for child in children
        )
        return b"".join(parts)

    def _update_value(
        self,
        node: _Node[K, V],
        key: K,
        replacement: _StoredEntry[K, V],
    ) -> _Node[K, V]:
        position = _lower_bound(node.entries, key, self._policy.comparer)
        if (
            position < len(node.entries)
            and self._policy.comparer(node.entries[position].key, key) == 0
        ):
            entries = list(node.entries)
            entries[position] = replacement
            return self._new_node(node.level, tuple(entries), node.children)
        children = list(node.children)
        child = children[position]
        if child is None:  # pragma: no cover - existing key guarantees a route
            raise AssertionError("missing route to existing Merkle entry")
        children[position] = self._update_value(child, key, replacement)
        return self._new_node(node.level, node.entries, tuple(children))

    def _insert(self, node: _Node[K, V] | None, item: _StoredEntry[K, V]) -> _Node[K, V]:
        if node is None:
            return self._new_node(item.level, (item,), (None, None))
        if item.level > node.level:
            left, right = self._split(node, item.key)
            return self._new_node(item.level, (item,), (left, right))
        position = _lower_bound(node.entries, item.key, self._policy.comparer)
        if item.level < node.level:
            children = list(node.children)
            children[position] = self._insert(children[position], item)
            return self._new_node(node.level, node.entries, tuple(children))
        left, right = self._split(node.children[position], item.key)
        return self._new_node(
            node.level,
            (*node.entries[:position], item, *node.entries[position:]),
            (*node.children[:position], left, right, *node.children[position + 1 :]),
        )

    def _split(
        self, node: _Node[K, V] | None, key: K
    ) -> tuple[_Node[K, V] | None, _Node[K, V] | None]:
        if node is None:
            return None, None
        position = _lower_bound(node.entries, key, self._policy.comparer)
        child_left, child_right = self._split(node.children[position], key)
        return (
            self._create_or_collapse(
                node.level, node.entries[:position], (*node.children[:position], child_left)
            ),
            self._create_or_collapse(
                node.level,
                node.entries[position:],
                (child_right, *node.children[position + 1 :]),
            ),
        )

    def _remove_node(self, node: _Node[K, V] | None, key: K) -> tuple[_Node[K, V] | None, bool]:
        if node is None:
            return None, False
        position = _lower_bound(node.entries, key, self._policy.comparer)
        found = (
            position < len(node.entries)
            and self._policy.comparer(node.entries[position].key, key) == 0
        )
        if not found:
            child, removed = self._remove_node(node.children[position], key)
            if not removed:
                return node, False
            children = list(node.children)
            children[position] = child
            return self._new_node(node.level, node.entries, tuple(children)), True
        entries = (*node.entries[:position], *node.entries[position + 1 :])
        remaining_children = (
            *node.children[:position],
            self._join(node.children[position], node.children[position + 1]),
            *node.children[position + 2 :],
        )
        return self._create_or_collapse(node.level, entries, remaining_children), True

    def _join(self, left: _Node[K, V] | None, right: _Node[K, V] | None) -> _Node[K, V] | None:
        if left is None:
            return right
        if right is None:
            return left
        if left.level > right.level:
            children = list(left.children)
            children[-1] = self._join(children[-1], right)
            return self._new_node(left.level, left.entries, tuple(children))
        if left.level < right.level:
            children = list(right.children)
            children[0] = self._join(left, children[0])
            return self._new_node(right.level, right.entries, tuple(children))
        return self._new_node(
            left.level,
            (*left.entries, *right.entries),
            (
                *left.children[:-1],
                self._join(left.children[-1], right.children[0]),
                *right.children[1:],
            ),
        )

    def _create_or_collapse(
        self,
        level: int,
        entries: tuple[_StoredEntry[K, V], ...],
        children: tuple[_Node[K, V] | None, ...],
    ) -> _Node[K, V] | None:
        return children[0] if not entries else self._new_node(level, entries, children)

    def _nodes_preorder(self) -> Iterator[_Node[K, V]]:
        if self._root is None:
            return
        pending = [self._root]
        while pending:
            node = pending.pop()
            yield node
            pending.extend(child for child in reversed(node.children) if child is not None)


@dataclass(frozen=True, slots=True)
class MerkleSearchTreeCursor(Generic[K, V]):
    """Immutable tree-snapshot-plus-rank gap cursor in policy-comparer order."""

    tree: MerkleSearchTree[K, V]
    position: int = 0

    def __post_init__(self) -> None:
        if self.position < 0 or self.position > self.tree.count:
            raise IndexError(f"Cursor position {self.position} is outside 0 .. {self.tree.count}.")

    @property
    def count(self) -> int:
        return self.tree.count

    @property
    def is_at_start(self) -> bool:
        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        return self.position == self.count

    def peek_previous(self) -> MerkleEntry[K, V] | None:
        return None if self.is_at_start else self.tree._entry_at_for_cursor(self.position - 1)

    def peek_next(self) -> MerkleEntry[K, V] | None:
        return self.tree._entry_at_for_cursor(self.position)

    def move_previous(self) -> MerkleSearchTreeCursor[K, V]:
        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return MerkleSearchTreeCursor(self.tree, self.position - 1)

    def move_next(self) -> MerkleSearchTreeCursor[K, V]:
        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return MerkleSearchTreeCursor(self.tree, self.position + 1)

    def seek(self, position: int) -> MerkleSearchTreeCursor[K, V]:
        return self if position == self.position else MerkleSearchTreeCursor(self.tree, position)

    def insert(self, key: K, value: V) -> MerkleSearchTreeCursor[K, V]:
        """Strictly insert at a missing lower-bound gap and return the gap after it."""
        rank, found = self.tree._lower_bound_rank_for_cursor(key)
        if found:
            raise KeyError("The key is already present.")
        self._ensure_current_gap(rank)
        return MerkleSearchTreeCursor(self.tree.set_item(key, value), self.position + 1)

    def set_item(self, key: K, value: V) -> MerkleSearchTreeCursor[K, V]:
        """Update an exact next entry or insert at a missing lower-bound gap."""
        rank, found = self.tree._lower_bound_rank_for_cursor(key)
        self._ensure_current_gap(rank)
        edited = self.tree.set_item(key, value)
        if edited is self.tree:
            return self
        return MerkleSearchTreeCursor(edited, self.position if found else self.position + 1)

    set = set_item

    def set_next_value(self, value: V) -> MerkleSearchTreeCursor[K, V]:
        next_entry = self.peek_next()
        if next_entry is None:
            raise IndexError("No entry follows the cursor gap.")
        edited = self.tree.set_item(next_entry.key, value)
        return self if edited is self.tree else MerkleSearchTreeCursor(edited, self.position)

    def delete_previous(self) -> MerkleSearchTreeCursor[K, V]:
        previous = self.peek_previous()
        if previous is None:
            raise IndexError("No entry precedes the cursor gap.")
        return MerkleSearchTreeCursor(self.tree.remove(previous.key), self.position - 1)

    def delete_next(self) -> MerkleSearchTreeCursor[K, V]:
        next_entry = self.peek_next()
        if next_entry is None:
            raise IndexError("No entry follows the cursor gap.")
        return MerkleSearchTreeCursor(self.tree.remove(next_entry.key), self.position)

    def snapshot(self) -> MerkleSearchTree[K, V]:
        return self.tree

    def _ensure_current_gap(self, rank: int) -> None:
        if rank != self.position:
            raise ValueError(
                f"The key belongs at gap {rank}, not at the current gap {self.position}."
            )


@dataclass(frozen=True, slots=True)
class MerkleCursorSearch(Generic[K, V]):
    """Presence-safe exact-key cursor search result."""

    cursor: MerkleSearchTreeCursor[K, V]
    found: bool


__all__ = [
    "MerkleCursorSearch",
    "MerkleEncodedBlock",
    "MerkleEntry",
    "MerkleMapDifference",
    "MerkleMapDifferenceKind",
    "MerkleSearchTree",
    "MerkleSearchTreeCursor",
    "MerkleSearchTreeStatistics",
]
