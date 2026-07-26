"""Policy-canonical persistent zip-zip sorted sets."""

from __future__ import annotations

import hashlib
import hmac
import math
import secrets
import struct
import threading
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, TypeVar, cast

from .ordering import Comparator, default_comparator

T = TypeVar("T")

_MASK64 = (1 << 64) - 1
_MISSING = object()


def _leading_zero_bits_64(value: int) -> int:
    return 64 if value == 0 else 64 - value.bit_length()


def _rotate_left_64(value: int, count: int) -> int:
    normalized = value & _MASK64
    return ((normalized << count) | (normalized >> (64 - count))) & _MASK64


def _mix_64(value: int) -> int:
    mixed = value & _MASK64
    mixed ^= mixed >> 30
    mixed = (mixed * 0xBF58476D1CE4E5B9) & _MASK64
    mixed ^= mixed >> 27
    mixed = (mixed * 0x94D049BB133111EB) & _MASK64
    return (mixed ^ (mixed >> 31)) & _MASK64


def _digest_rank_source(tag: bytes, payload: bytes) -> int:
    framed = tag + len(payload).to_bytes(8, "big") + payload
    return int.from_bytes(hashlib.sha256(framed).digest()[:8], "big")


def stable_rank_hash(value: object) -> int:
    """Return a process-independent 64-bit hash for common immutable Python values.

    Python's built-in hash for strings and bytes is intentionally randomized.  Canonical ranks
    cannot use it, so the default policy supports values with an unambiguous stable encoding and
    requires an explicit equivalence-coherent ``rank_hash`` for application objects.
    """

    if value is None:
        return 0x6E6F6E65
    if isinstance(value, bool):
        # bool and int compare as one equivalence domain in Python.
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        if math.isnan(value):
            raise ValueError("NaN has no coherent default total-order rank hash.")
        if value == 0.0:
            return 0
        if math.isfinite(value) and value.is_integer():
            return int(value)
        return _digest_rank_source(b"float", struct.pack(">d", value))
    if isinstance(value, str):
        return _digest_rank_source(b"str", value.encode("utf-8"))
    if isinstance(value, bytes):
        return _digest_rank_source(b"bytes", value)
    if isinstance(value, tuple):
        payload = b"".join((stable_rank_hash(item) & _MASK64).to_bytes(8, "big") for item in value)
        return _digest_rank_source(b"tuple", payload)
    raise TypeError(
        f"{type(value).__qualname__} has no default stable canonical rank encoding; "
        "provide rank_hash explicitly."
    )


@dataclass(frozen=True, slots=True)
class ZipTreeRank:
    """One element's derived rank, deciding where it sits in the canonical shape. The geometric rank
    is the primary priority; ``secondary`` and ``content`` break ties deterministically, so equal
    geometric ranks still produce one well-defined tree.
    """

    geometric: int
    secondary: int
    content: int


class ZipTreeRankPolicy(Generic[T]):
    """Comparator and keyed deterministic rank policy for canonical zip-zip sets."""

    __slots__ = ("_rank_hash", "_rank_key", "comparator", "seed")

    def __init__(
        self,
        comparator: Comparator[T],
        rank_hash: Callable[[T], int],
        rank_key: bytes,
        seed: int | None,
    ) -> None:
        """Retain the comparator, rank hash, and HMAC key; use the factory methods instead."""

        self.comparator = comparator
        self._rank_hash = rank_hash
        self._rank_key = bytes(rank_key)
        self.seed = seed

    @classmethod
    def create(
        cls,
        *,
        comparator: Comparator[T] | None = None,
        rank_hash: Callable[[T], int] | None = None,
        seed: int | None = None,
    ) -> ZipTreeRankPolicy[T]:
        """Create a policy, generating a fresh random rank key unless ``seed`` fixes one. Supplying
        a comparator obliges the caller to supply a matching rank hash: the hash must be constant
        on the comparator's equivalence classes, and the default hash only knows about the
        default ordering. Without a seed the key is random per policy, so shapes are
        unpredictable to an adversary but not reproducible across processes.
        """

        if comparator is not None and rank_hash is None:
            raise TypeError("An explicit comparator requires an equivalence-coherent rank hash.")
        actual_comparator = default_comparator if comparator is None else comparator
        actual_rank_hash = (
            cast(Callable[[T], int], stable_rank_hash) if rank_hash is None else rank_hash
        )
        rank_key = secrets.token_bytes(32) if seed is None else _derive_seed_key(seed)
        return cls(actual_comparator, actual_rank_hash, rank_key, seed)

    @classmethod
    def create_keyed(
        cls,
        rank_key: bytes | bytearray | memoryview,
        *,
        comparator: Comparator[T] | None = None,
        rank_hash: Callable[[T], int] | None = None,
    ) -> ZipTreeRankPolicy[T]:
        """Create a policy from an explicit rank key of at least 32 bytes. This is how two processes
        agree on one canonical shape: the same key and comparator yield identical trees for
        identical contents.
        """

        owned_key = bytes(rank_key)
        if len(owned_key) < 32:
            raise ValueError("A zip-zip rank key must contain at least 32 bytes.")
        if comparator is not None and rank_hash is None:
            raise TypeError("An explicit comparator requires an equivalence-coherent rank hash.")
        actual_comparator = default_comparator if comparator is None else comparator
        actual_rank_hash = (
            cast(Callable[[T], int], stable_rank_hash) if rank_hash is None else rank_hash
        )
        return cls(actual_comparator, actual_rank_hash, owned_key, None)

    def rank(self, value: T) -> ZipTreeRank:
        """Derive ``value``'s rank by HMAC over its rank hash. Keying the derivation is what makes
        the shape resistant to adversarially chosen elements. It cannot repair a rank hash that
        is unstable or inconsistent with the comparer's equivalence classes; those remain caller
        obligations.
        """

        source = (int(self._rank_hash(value)) & _MASK64).to_bytes(8, "big")
        digest = hmac.new(self._rank_key, source, hashlib.sha256).digest()
        primary = int.from_bytes(digest[:8], "big")
        return ZipTreeRank(
            _leading_zero_bits_64(primary),
            int.from_bytes(digest[8:16], "big"),
            int.from_bytes(digest[16:24], "big"),
        )


def _derive_seed_key(seed: int) -> bytes:
    return hashlib.sha256(b"ZZT2" + (seed & _MASK64).to_bytes(8, "big")).digest()


class _Node(Generic[T]):
    __slots__ = (
        "_digest",
        "_digest_lock",
        "count",
        "height",
        "item",
        "left",
        "rank",
        "right",
    )

    def __init__(
        self,
        item: T,
        rank: ZipTreeRank,
        left: _Node[T] | None,
        right: _Node[T] | None,
    ) -> None:
        """Build a node, deriving its subtree count and height from its children. The content digest
        is left uncomputed until first requested.
        """

        self.item = item
        self.rank = rank
        self.left = left
        self.right = right
        self.count: int = (
            1 + (0 if left is None else left.count) + (0 if right is None else right.count)
        )
        self.height: int = 1 + max(
            0 if left is None else left.height, 0 if right is None else right.height
        )
        self._digest: int | None = None
        self._digest_lock = threading.Lock()

    def digest(self) -> int:
        """The subtree's content digest, computed once and cached. Uses an explicit stack rather
        than recursion, so a deep tree cannot overflow the interpreter stack, and takes a lock so
        concurrent readers publish one consistent value.
        """

        if self._digest is not None:
            return self._digest
        pending: list[tuple[_Node[T], bool]] = [(self, False)]
        while pending:
            node, expanded = pending.pop()
            if node._digest is not None:
                continue
            if not expanded:
                pending.append((node, True))
                if node.right is not None and node.right._digest is None:
                    pending.append((node.right, False))
                if node.left is not None and node.left._digest is None:
                    pending.append((node.left, False))
                continue
            left = 0x243F6A8885A308D3 if node.left is None else node.left.digest()
            right = 0x13198A2E03707344 if node.right is None else node.right.digest()
            computed = _mix_64(
                node.rank.content ^ _rotate_left_64(left, 17) ^ _rotate_left_64(right, 43)
            )
            with node._digest_lock:
                if node._digest is None:
                    node._digest = computed
        return cast(int, self._digest)


def _rank_equal(left: ZipTreeRank, right: ZipTreeRank) -> bool:
    return left == right


def _higher(
    left_item: T,
    left: ZipTreeRank,
    right_item: T,
    right: ZipTreeRank,
    comparator: Comparator[T],
) -> bool:
    if left.geometric != right.geometric:
        return left.geometric > right.geometric
    if left.secondary != right.secondary:
        return left.secondary > right.secondary
    return comparator(left_item, right_item) < 0


def _split(
    root: _Node[T] | None, item: T, comparator: Comparator[T]
) -> tuple[_Node[T] | None, _Node[T] | None]:
    path: list[tuple[_Node[T], bool]] = []
    cursor = root
    while cursor is not None:
        went_left = comparator(item, cursor.item) < 0
        path.append((cursor, went_left))
        cursor = cursor.left if went_left else cursor.right
    left: _Node[T] | None = None
    right: _Node[T] | None = None
    while path:
        node, went_left = path.pop()
        if went_left:
            right = _Node(node.item, node.rank, right, node.right)
        else:
            left = _Node(node.item, node.rank, node.left, left)
    return left, right


def _insert(root: _Node[T] | None, item: _Node[T], comparator: Comparator[T]) -> _Node[T]:
    if root is None:
        return item
    path: list[tuple[_Node[T], bool]] = []
    cursor: _Node[T] | None = root
    while cursor is not None and not _higher(
        item.item, item.rank, cursor.item, cursor.rank, comparator
    ):
        went_left = comparator(item.item, cursor.item) < 0
        path.append((cursor, went_left))
        cursor = cursor.left if went_left else cursor.right
    left, right = _split(cursor, item.item, comparator)
    result = _Node(item.item, item.rank, left, right)
    while path:
        node, went_left = path.pop()
        result = (
            _Node(node.item, node.rank, result, node.right)
            if went_left
            else _Node(node.item, node.rank, node.left, result)
        )
    return result


def _merge(
    initial_left: _Node[T] | None,
    initial_right: _Node[T] | None,
    comparator: Comparator[T],
) -> _Node[T] | None:
    if initial_left is None:
        return initial_right
    if initial_right is None:
        return initial_left
    left: _Node[T] | None = initial_left
    right: _Node[T] | None = initial_right
    path: list[tuple[_Node[T], bool]] = []
    while left is not None and right is not None:
        if _higher(left.item, left.rank, right.item, right.rank, comparator):
            path.append((left, True))
            left = left.right
        else:
            path.append((right, False))
            right = right.left
    result = left if left is not None else right
    while path:
        node, chose_left = path.pop()
        result = (
            _Node(node.item, node.rank, node.left, result)
            if chose_left
            else _Node(node.item, node.rank, result, node.right)
        )
    return result


def _remove(
    root: _Node[T] | None, item: T, comparator: Comparator[T]
) -> tuple[_Node[T] | None, bool]:
    path: list[tuple[_Node[T], bool]] = []
    cursor = root
    while cursor is not None:
        comparison = comparator(item, cursor.item)
        if comparison == 0:
            break
        went_left = comparison < 0
        path.append((cursor, went_left))
        cursor = cursor.left if went_left else cursor.right
    if cursor is None:
        return root, False
    result = _merge(cursor.left, cursor.right, comparator)
    while path:
        node, went_left = path.pop()
        result = (
            _Node(node.item, node.rank, result, node.right)
            if went_left
            else _Node(node.item, node.rank, node.left, result)
        )
    return result, True


def _iterate(root: _Node[T]) -> Iterator[T]:
    pending: list[_Node[T]] = []
    cursor: _Node[T] | None = root
    while cursor is not None or pending:
        while cursor is not None:
            pending.append(cursor)
            cursor = cursor.left
        node = pending.pop()
        yield node.item
        cursor = node.right


@dataclass(frozen=True, slots=True)
class CanonicalSetLookup(Generic[T]):
    """A membership result carrying the stored representative. On a miss it echoes the probe, so
    callers always have a value to work with.
    """

    found: bool
    value: T


@dataclass(frozen=True, slots=True)
class CanonicalSortedSetStatistics:
    """Shape measurements returned by a successful structural audit. The priority collision count
    reports how often the geometric rank tied and the secondary ranks had to decide.
    """

    count: int
    height: int
    maximum_geometric_rank: int
    priority_collision_count: int


@dataclass(frozen=True, slots=True)
class CanonicalCursorPeek(Generic[T]):
    """An element found next to a cursor gap, wrapped so a stored ``None`` stays distinct from
    "nothing there".
    """

    value: T


@dataclass(frozen=True, slots=True)
class CanonicalCursorSearch(Generic[T]):
    """The outcome of seeking a cursor. On a miss the cursor sits where the element would be
    inserted.
    """

    found: bool
    cursor: CanonicalSortedSetCursor[T]


class CanonicalSortedSet(Generic[T]):
    """Immutable policy-canonical Cartesian search tree."""

    __slots__ = ("_root", "policy")

    def __init__(self, root: _Node[T] | None, policy: ZipTreeRankPolicy[T]) -> None:
        """Wrap an already-built root; use :meth:`empty` or :meth:`from_iterable` instead."""

        self._root = root
        self.policy = policy

    @classmethod
    def empty(cls, policy: ZipTreeRankPolicy[T]) -> CanonicalSortedSet[T]:
        """Return the empty set under ``policy``."""

        return cls(None, policy)

    @classmethod
    def from_iterable(
        cls, values: Iterable[T], policy: ZipTreeRankPolicy[T]
    ) -> CanonicalSortedSet[T]:
        """Build a set from ``values``. The result depends only on which elements are present, not
        on the order they arrive in: that is what history independence means here.
        """

        result = cls.empty(policy)
        for value in values:
            result = result.add(value)
        return result

    def __len__(self) -> int:
        """Number of distinct elements."""

        return 0 if self._root is None else self._root.count

    @property
    def size(self) -> int:
        """Number of distinct elements."""

        return len(self)

    @property
    def count(self) -> int:
        """Number of distinct elements."""

        return len(self)

    @property
    def is_empty(self) -> bool:
        """Whether the set holds no elements."""

        return self._root is None

    @property
    def height(self) -> int:
        """Height of the tree, exposed for structural diagnostics."""

        return 0 if self._root is None else self._root.height

    @property
    def content_hash(self) -> int:
        """A digest identifying this set's contents. Because the shape is canonical, two sets under
        the same policy hold the same elements exactly when their digests agree, so this compares
        whole sets in constant time.
        """

        return 0 if self._root is None else self._root.digest()

    def _find(self, value: T) -> _Node[T] | None:
        cursor = self._root
        while cursor is not None:
            comparison = self.policy.comparator(value, cursor.item)
            if comparison == 0:
                return cursor
            cursor = cursor.left if comparison < 0 else cursor.right
        return None

    def contains(self, value: T) -> bool:
        """Whether ``value``'s equivalence class is present."""

        return self._find(value) is not None

    def __contains__(self, value: object) -> bool:
        """Whether ``value`` is present, for the ``in`` operator."""

        return self.contains(cast(T, value))

    def try_get_value(self, value: T) -> CanonicalSetLookup[T]:
        """The stored representative equivalent to ``value``, reported presence-safely."""

        found = self._find(value)
        return (
            CanonicalSetLookup(False, value)
            if found is None
            else CanonicalSetLookup(True, found.item)
        )

    def add(self, value: T) -> CanonicalSortedSet[T]:
        """Return a set containing ``value``; a no-op when its class is already present. Raises
        :class:`ValueError` when the rank hash gives an existing equivalent element a different
        rank, since that would mean the hash is not constant on the comparator's equivalence
        classes and the canonical shape would be ill defined.
        """

        rank = self.policy.rank(value)
        existing = self._find(value)
        if existing is not None:
            if not _rank_equal(existing.rank, rank):
                raise ValueError("The rank hash is not constant on comparator equivalence classes.")
            return self
        return CanonicalSortedSet(
            _insert(self._root, _Node(value, rank, None, None), self.policy.comparator), self.policy
        )

    def remove(self, value: T) -> CanonicalSortedSet[T]:
        """Return a set without ``value``'s class; a no-op when absent. The result is the same
        canonical tree that would arise from building the remaining elements from scratch.
        """

        root, removed = _remove(self._root, value, self.policy.comparator)
        return CanonicalSortedSet(root, self.policy) if removed else self

    def clear(self) -> CanonicalSortedSet[T]:
        """Return the empty set under the same policy; a no-op when already empty."""

        return self if self.is_empty else CanonicalSortedSet.empty(self.policy)

    def _require_compatible(self, other: CanonicalSortedSet[T]) -> None:
        if self.policy is not other.policy:
            raise TypeError("Canonical set algebra requires the same rank-policy object.")

    def union(self, other: CanonicalSortedSet[T]) -> CanonicalSortedSet[T]:
        """Return the elements of both sets. Both must retain the same rank-policy object, since
        elements ranked under different keys could not share one canonical shape.
        """

        self._require_compatible(other)
        if self._root is other._root:
            return self
        result = self
        for value in other:
            result = result.add(value)
        return result

    def intersect(self, other: CanonicalSortedSet[T]) -> CanonicalSortedSet[T]:
        """Return the elements present in both sets."""

        self._require_compatible(other)
        if self._root is other._root:
            return self
        result = CanonicalSortedSet.empty(self.policy)
        for value in self:
            if other.contains(value):
                result = result.add(value)
        return self if result.set_equals(self) else result

    def except_(self, other: CanonicalSortedSet[T]) -> CanonicalSortedSet[T]:
        """Return this set's elements that are absent from ``other``. Named with a trailing
        underscore because ``except`` is a Python keyword.
        """

        self._require_compatible(other)
        if self._root is other._root:
            return self.clear()
        result = self
        for value in other:
            result = result.remove(value)
        return result

    def set_equals(self, values: Iterable[T]) -> bool:
        """Whether both sets hold the same elements, compared by content digest."""

        if isinstance(values, CanonicalSortedSet) and values.policy is self.policy:
            if self is values:
                return True
            if len(self) != len(values) or self.content_hash != values.content_hash:
                return False
        other = CanonicalSortedSet.from_iterable(values, self.policy)
        if len(self) != len(other):
            return False
        return all(
            self.policy.comparator(left, right) == 0
            for left, right in zip(self, other, strict=True)
        )

    def is_subset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of this set also occurs in ``other``."""

        other = CanonicalSortedSet.from_iterable(values, self.policy)
        return len(self) <= len(other) and all(other.contains(value) for value in self)

    def is_proper_subset_of(self, values: Iterable[T]) -> bool:
        """Whether this set is a subset of ``other`` and ``other`` has an element it lacks."""

        other = CanonicalSortedSet.from_iterable(values, self.policy)
        return len(self) < len(other) and self.is_subset_of(other)

    def is_superset_of(self, values: Iterable[T]) -> bool:
        """Whether every element of ``other`` occurs in this set."""

        return all(self.contains(value) for value in values)

    def is_proper_superset_of(self, values: Iterable[T]) -> bool:
        """Whether this set is a superset of ``other`` and holds an element ``other`` lacks."""

        other = CanonicalSortedSet.from_iterable(values, self.policy)
        return len(self) > len(other) and self.is_superset_of(other)

    def overlaps(self, values: Iterable[T]) -> bool:
        """Whether the two sets share at least one element."""

        return any(self.contains(value) for value in values)

    def shares_storage_with(self, other: CanonicalSortedSet[T]) -> bool:
        """Whether both sets share any node by object identity. Used to confirm that a no-op avoided
        copying, not an equality test.
        """

        if self._root is None or other._root is None:
            return False
        if self._root is other._root:
            return True
        nodes: set[int] = set()
        first = [self._root]
        while first:
            node = first.pop()
            nodes.add(id(node))
            if node.left is not None:
                first.append(node.left)
            if node.right is not None:
                first.append(node.right)
        second = [other._root]
        while second:
            node = second.pop()
            if id(node) in nodes:
                return True
            if node.left is not None:
                second.append(node.left)
            if node.right is not None:
                second.append(node.right)
        return False

    def shape_signature(self) -> tuple[tuple[T, ZipTreeRank, int, int], ...]:
        """Return a deterministic preorder topology signature for validation and diagnostics."""

        if self._root is None:
            return ()
        result: list[tuple[T, ZipTreeRank, int, int]] = []
        pending = [self._root]
        while pending:
            node = pending.pop()
            result.append(
                (
                    node.item,
                    node.rank,
                    0 if node.left is None else node.left.count,
                    0 if node.right is None else node.right.count,
                )
            )
            if node.right is not None:
                pending.append(node.right)
            if node.left is not None:
                pending.append(node.left)
        return tuple(result)

    def validate_structure(self) -> CanonicalSortedSetStatistics:
        """Walk the whole tree and return its shape measurements. Checks ascending element order,
        that every node's rank dominates its children's, and that cached counts and heights
        agree. Raises on the first violation. A defensive audit, not part of normal use.
        """

        if self._root is None:
            return CanonicalSortedSetStatistics(0, 0, 0, 0)
        pending: list[tuple[_Node[T], object, object, int]] = [(self._root, _MISSING, _MISSING, 1)]
        visited: set[int] = set()
        priorities: set[tuple[int, int]] = set()
        count = height = max_rank = collisions = 0
        while pending:
            node, lower, upper, depth = pending.pop()
            identity = id(node)
            if identity in visited:
                raise ValueError("Canonical set contains a cycle or shared child.")
            visited.add(identity)
            if lower is not _MISSING and self.policy.comparator(node.item, cast(T, lower)) <= 0:
                raise ValueError("Canonical set lower-order invariant failed.")
            if upper is not _MISSING and self.policy.comparator(node.item, cast(T, upper)) >= 0:
                raise ValueError("Canonical set upper-order invariant failed.")
            if not _rank_equal(node.rank, self.policy.rank(node.item)):
                raise ValueError("Canonical set rank is not reproducible.")
            if node.left is not None and not _higher(
                node.item, node.rank, node.left.item, node.left.rank, self.policy.comparator
            ):
                raise ValueError("Canonical set left heap invariant failed.")
            if node.right is not None and not _higher(
                node.item, node.rank, node.right.item, node.right.rank, self.policy.comparator
            ):
                raise ValueError("Canonical set right heap invariant failed.")
            expected_count = (
                1
                + (0 if node.left is None else node.left.count)
                + (0 if node.right is None else node.right.count)
            )
            expected_height = 1 + max(
                0 if node.left is None else node.left.height,
                0 if node.right is None else node.right.height,
            )
            if node.count != expected_count or node.height != expected_height:
                raise ValueError("Canonical set metadata invariant failed.")
            count += 1
            height = max(height, depth)
            max_rank = max(max_rank, node.rank.geometric)
            priority = node.rank.geometric, node.rank.secondary
            if priority in priorities:
                collisions += 1
            else:
                priorities.add(priority)
            if node.right is not None:
                pending.append((node.right, node.item, upper, depth + 1))
            if node.left is not None:
                pending.append((node.left, lower, node.item, depth + 1))
        if count != len(self) or height != self.height:
            raise ValueError("Canonical set root metadata invariant failed.")
        return CanonicalSortedSetStatistics(count, height, max_rank, collisions)

    def _bound_rank(self, value: T, upper: bool) -> int:
        rank = 0
        node = self._root
        while node is not None:
            comparison = self.policy.comparator(node.item, value)
            if comparison < 0 or (upper and comparison == 0):
                rank += (0 if node.left is None else node.left.count) + 1
                node = node.right
            else:
                node = node.left
        return rank

    def _item_at(self, rank: int) -> T:
        node = self._root
        while node is not None:
            left_count = 0 if node.left is None else node.left.count
            if rank < left_count:
                node = node.left
            elif rank == left_count:
                return node.item
            else:
                rank -= left_count + 1
                node = node.right
        raise IndexError("Rank is outside the canonical sorted set.")

    def cursor_at(self, position: int = 0) -> CanonicalSortedSetCursor[T]:
        """A cursor at gap ``position`` of the ascending element sequence."""

        return CanonicalSortedSetCursor(self, position)

    def cursor_at_lower_bound(self, value: T) -> CanonicalSortedSetCursor[T]:
        """A cursor before the first element not below ``value``, where it would be inserted."""

        return self.cursor_at(self._bound_rank(value, False))

    def cursor_at_upper_bound(self, value: T) -> CanonicalSortedSetCursor[T]:
        """A cursor after any element equivalent to ``value``."""

        return self.cursor_at(self._bound_rank(value, True))

    def find_cursor(self, value: T) -> CanonicalCursorSearch[T]:
        """Seek to ``value`` and report whether it is present. On a miss the cursor sits at the
        lower bound and remains usable.
        """

        cursor = self.cursor_at_lower_bound(value)
        candidate = cursor.peek_next()
        return CanonicalCursorSearch(
            candidate is not None and self.policy.comparator(candidate.value, value) == 0,
            cursor,
        )

    def __iter__(self) -> Iterator[T]:
        """Iterate the elements in ascending order."""

        return iter(()) if self._root is None else _iterate(self._root)


@dataclass(frozen=True, slots=True)
class CanonicalSortedSetCursor(Generic[T]):
    """Immutable policy-preserving root-plus-rank cursor."""

    set: CanonicalSortedSet[T]
    position: int = 0

    def __post_init__(self) -> None:
        """Reject a position outside ``0..count``, so every cursor names a real gap."""

        if self.position < 0 or self.position > len(self.set):
            raise IndexError("Cursor position is outside the canonical sorted set.")

    @property
    def count(self) -> int:
        """Element count of the set version this cursor is positioned in."""

        return len(self.set)

    @property
    def is_at_start(self) -> bool:
        """Whether the gap precedes the first element."""

        return self.position == 0

    @property
    def is_at_end(self) -> bool:
        """Whether the gap follows the last element."""

        return self.position == self.count

    def peek_previous(self) -> CanonicalCursorPeek[T] | None:
        """The element immediately before the gap, or ``None`` at the start."""

        if self.is_at_start:
            return None
        return CanonicalCursorPeek(self.set._item_at(self.position - 1))

    def peek_next(self) -> CanonicalCursorPeek[T] | None:
        """The element immediately after the gap, or ``None`` at the end."""

        if self.is_at_end:
            return None
        return CanonicalCursorPeek(self.set._item_at(self.position))

    def move_previous(self) -> CanonicalSortedSetCursor[T]:
        """A cursor one position earlier, raising :class:`IndexError` at the start. The receiver is
        unchanged; movement produces a new cursor over the same set version.
        """

        if self.is_at_start:
            raise IndexError("Cursor is already at the start.")
        return CanonicalSortedSetCursor(self.set, self.position - 1)

    def move_next(self) -> CanonicalSortedSetCursor[T]:
        """A cursor one position later, raising :class:`IndexError` at the end."""

        if self.is_at_end:
            raise IndexError("Cursor is already at the end.")
        return CanonicalSortedSetCursor(self.set, self.position + 1)

    def seek_rank(self, position: int) -> CanonicalSortedSetCursor[T]:
        """A cursor at ``position`` within the same set version, raising when out of range."""

        return self if position == self.position else CanonicalSortedSetCursor(self.set, position)

    def add(self, value: T) -> CanonicalSortedSetCursor[T]:
        """Insert ``value`` and return a cursor just after it. The gap moves to the element's sorted
        position, since placement is decided by the ordering and the derived rank rather than by
        the cursor.
        """

        position = self.set._bound_rank(value, False)
        return CanonicalSortedSetCursor(self.set.add(value), position + 1)

    def delete_previous(self) -> CanonicalSortedSetCursor[T]:
        """Remove the element before the gap and return a cursor in its place, raising at the start.
        """

        item = self.peek_previous()
        if item is None:
            raise IndexError("No item precedes the cursor.")
        return CanonicalSortedSetCursor(self.set.remove(item.value), self.position - 1)

    def delete_next(self) -> CanonicalSortedSetCursor[T]:
        """Remove the element after the gap and return a cursor in its place, raising at the end."""

        item = self.peek_next()
        if item is None:
            raise IndexError("No item follows the cursor.")
        return CanonicalSortedSetCursor(self.set.remove(item.value), self.position)

    def snapshot(self) -> CanonicalSortedSet[T]:
        """The set version this cursor is positioned in."""

        return self.set


__all__ = [
    "CanonicalCursorPeek",
    "CanonicalCursorSearch",
    "CanonicalSetLookup",
    "CanonicalSortedSet",
    "CanonicalSortedSetCursor",
    "CanonicalSortedSetStatistics",
    "ZipTreeRank",
    "ZipTreeRankPolicy",
    "stable_rank_hash",
]
