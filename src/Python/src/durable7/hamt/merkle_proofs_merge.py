"""Canonical ``MSP2`` proofs and present-``None``-safe typed three-way merge."""

from __future__ import annotations

import struct
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from enum import StrEnum
from functools import cmp_to_key
from typing import Generic, TypeAlias, TypeVar, cast

from .merkle_encoding import MerkleDigest, MerkleSearchTreePolicy
from .merkle_persistence import (
    MerkleBlock,
    MerklePersistenceError,
    MerklePersistenceFailure,
    MerkleVerificationBudget,
    _decode_merkle_block,
    _DecodedBlock,
    _VerificationContext,
)
from .merkle_search_tree import MerkleEntry, MerkleSearchTree, _Node, _StoredEntry

K = TypeVar("K")
V = TypeVar("V")


class MerkleProofKind(StrEnum):
    MEMBERSHIP = "membership"
    NONMEMBERSHIP = "nonmembership"
    RANGE = "range"


@dataclass(frozen=True, slots=True)
class MerkleMembershipQuery(Generic[K, V]):
    key: K
    value: V
    kind: MerkleProofKind = MerkleProofKind.MEMBERSHIP


@dataclass(frozen=True, slots=True)
class MerkleNonmembershipQuery(Generic[K]):
    key: K
    kind: MerkleProofKind = MerkleProofKind.NONMEMBERSHIP


@dataclass(frozen=True, slots=True)
class MerkleRangeQuery(Generic[K]):
    minimum: K
    maximum: K
    kind: MerkleProofKind = MerkleProofKind.RANGE


MerkleProofQuery: TypeAlias = (
    MerkleMembershipQuery[K, V] | MerkleNonmembershipQuery[K] | MerkleRangeQuery[K]
)


@dataclass(frozen=True, slots=True, init=False)
class MerkleProofStep:
    block: MerkleBlock
    expanded_child_indexes: tuple[int, ...]

    def __init__(self, block: MerkleBlock, expanded_child_indexes: Iterable[int]) -> None:
        indexes = tuple(expanded_child_indexes)
        if any(
            isinstance(value, bool)
            or not isinstance(value, int)
            or value < 0
            or (index > 0 and indexes[index - 1] >= value)
            for index, value in enumerate(indexes)
        ):
            raise ValueError("expanded child indexes must be sorted, unique, nonnegative integers")
        object.__setattr__(self, "block", block)
        object.__setattr__(self, "expanded_child_indexes", indexes)


@dataclass(frozen=True, slots=True, init=False)
class MerkleProof(Generic[K, V]):
    algorithm_id: str
    domain_digest: MerkleDigest
    root_hash: MerkleDigest
    query: MerkleProofQuery[K, V]
    query_bytes: bytes
    steps: tuple[MerkleProofStep, ...]

    def __init__(
        self,
        algorithm_id: str,
        domain_digest: MerkleDigest,
        root_hash: MerkleDigest,
        query: MerkleProofQuery[K, V],
        query_bytes: bytes | bytearray | memoryview,
        steps: Iterable[MerkleProofStep],
    ) -> None:
        object.__setattr__(self, "algorithm_id", algorithm_id)
        object.__setattr__(self, "domain_digest", domain_digest)
        object.__setattr__(self, "root_hash", root_hash)
        object.__setattr__(self, "query", query)
        object.__setattr__(self, "query_bytes", bytes(query_bytes))
        object.__setattr__(self, "steps", tuple(steps))

    @property
    def kind(self) -> MerkleProofKind:
        return self.query.kind


class MerkleProofFailure(StrEnum):
    NONE = "none"
    INVALID_ENVELOPE = "invalid-envelope"
    BUDGET_EXCEEDED = "budget-exceeded"
    INVALID_QUERY = "invalid-query"
    INVALID_BLOCK = "invalid-block"
    INVALID_EXPANSION = "invalid-expansion"
    WRONG_RESULT = "wrong-result"


@dataclass(frozen=True, slots=True)
class MerkleProofVerificationResult(Generic[K, V]):
    valid: bool
    failure: MerkleProofFailure
    computed_root_hash: MerkleDigest
    entries: tuple[MerkleEntry[K, V], ...]
    accounted_blocks: int
    accounted_bytes: int


def _i32(value: int) -> bytes:
    if not -(1 << 31) <= value < (1 << 31):
        raise OverflowError("MSP2 signed 32-bit field overflow")
    return struct.pack(">i", value)


def _encode_query(query: MerkleProofQuery[K, V], policy: MerkleSearchTreePolicy[K, V]) -> bytes:
    if isinstance(query, MerkleMembershipQuery):
        key = bytes(policy.key_codec.encode(query.key))
        value = bytes(policy.value_codec.encode(query.value))
        return b"MSP2\x00" + _i32(len(key)) + key + _i32(len(value)) + value
    if isinstance(query, MerkleNonmembershipQuery):
        key = bytes(policy.key_codec.encode(query.key))
        return b"MSP2\x01" + _i32(len(key)) + key
    minimum = bytes(policy.key_codec.encode(query.minimum))
    maximum = bytes(policy.key_codec.encode(query.maximum))
    return b"MSP2\x02" + _i32(len(minimum)) + minimum + _i32(len(maximum)) + maximum


def _position(
    entries: tuple[_StoredEntry[K, V], ...],
    key: K,
    comparer: Callable[[K, K], int],
) -> tuple[int, bool]:
    low = 0
    high = len(entries)
    while low < high:
        middle = (low + high) // 2
        if comparer(entries[middle].key, key) < 0:
            low = middle + 1
        else:
            high = middle
    return low, low < len(entries) and comparer(entries[low].key, key) == 0


def _selected_children(
    query: MerkleProofQuery[K, V],
    entries: tuple[_StoredEntry[K, V], ...],
    children: tuple[MerkleDigest, ...],
    policy: MerkleSearchTreePolicy[K, V],
) -> tuple[int, ...]:
    if isinstance(query, (MerkleMembershipQuery, MerkleNonmembershipQuery)):
        position, found = _position(entries, query.key, policy.comparer)
        if not found and children[position] != policy.empty_digest:
            return (position,)
        return ()
    result: list[int] = []
    for index, child in enumerate(children):
        if child == policy.empty_digest:
            continue
        above_lower = index == 0 or policy.comparer(query.maximum, entries[index - 1].key) > 0
        below_upper = (
            index == len(entries) or policy.comparer(query.minimum, entries[index].key) < 0
        )
        if above_lower and below_upper:
            result.append(index)
    return tuple(result)


def _create_proof(tree: MerkleSearchTree[K, V], query: MerkleProofQuery[K, V]) -> MerkleProof[K, V]:
    steps: list[MerkleProofStep] = []

    def walk(node: _Node[K, V] | None) -> None:
        if node is None:
            return
        digests = tuple(
            tree.policy.empty_digest if child is None else child.digest for child in node.children
        )
        expanded = _selected_children(query, node.entries, digests, tree.policy)
        steps.append(MerkleProofStep(MerkleBlock(node.digest, node.block_bytes), expanded))
        for index in expanded:
            walk(node.children[index])

    walk(tree._root)
    return MerkleProof(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        tree.policy.domain_digest,
        tree.root_hash,
        query,
        _encode_query(query, tree.policy),
        steps,
    )


def create_merkle_proof(tree: MerkleSearchTree[K, V], key: K) -> MerkleProof[K, V]:
    entry = tree.get_entry(key)
    query: MerkleProofQuery[K, V]
    if entry is None:
        query = MerkleNonmembershipQuery(key)
    else:
        query = MerkleMembershipQuery(entry.key, entry.value)
    return _create_proof(tree, query)


def create_merkle_range_proof(
    tree: MerkleSearchTree[K, V], minimum: K, maximum: K
) -> MerkleProof[K, V]:
    if tree.policy.comparer(minimum, maximum) > 0:
        raise ValueError("minimum key exceeds maximum key")
    return _create_proof(tree, MerkleRangeQuery(minimum, maximum))


class _ExpansionError(ValueError):
    pass


def verify_merkle_proof(
    proof: MerkleProof[K, V],
    policy: MerkleSearchTreePolicy[K, V],
    budget: MerkleVerificationBudget | None = None,
) -> MerkleProofVerificationResult[K, V]:
    context = _VerificationContext(budget or MerkleVerificationBudget())

    def fail(failure: MerkleProofFailure) -> MerkleProofVerificationResult[K, V]:
        return MerkleProofVerificationResult(
            False,
            failure,
            proof.root_hash,
            (),
            context.blocks,
            context.bytes,
        )

    try:
        # This limit deliberately precedes all policy codecs and block work.
        context.account_query(len(proof.query_bytes))
        if len(proof.steps) > context.budget.maximum_blocks:
            context._exceeded("maximum Merkle proof step count exceeded")
        if any(
            len(step.expanded_child_indexes) > context.budget.maximum_children_per_block
            for step in proof.steps
        ):
            context._exceeded("maximum expanded-child count per proof step exceeded")
    except MerklePersistenceError:
        return fail(MerkleProofFailure.BUDGET_EXCEEDED)
    if (
        proof.algorithm_id != MerkleSearchTreePolicy.ALGORITHM_ID
        or proof.domain_digest != policy.domain_digest
    ):
        return fail(MerkleProofFailure.INVALID_ENVELOPE)
    try:
        canonical_query = _encode_query(proof.query, policy)
        if canonical_query != proof.query_bytes:
            return fail(MerkleProofFailure.INVALID_QUERY)
        if (
            isinstance(proof.query, MerkleRangeQuery)
            and policy.comparer(proof.query.minimum, proof.query.maximum) > 0
        ):
            return fail(MerkleProofFailure.INVALID_QUERY)
    except Exception:
        return fail(MerkleProofFailure.INVALID_QUERY)
    steps: dict[str, MerkleProofStep] = {}
    shell = MerkleSearchTree.empty(policy)
    decoded_steps: dict[str, _DecodedBlock[K, V]] = {}
    try:
        for step in proof.steps:
            key = str(step.block.digest)
            if key in steps:
                return fail(MerkleProofFailure.INVALID_EXPANSION)
            decoded = _decode_merkle_block(step.block, shell, context, 1)
            for index in step.expanded_child_indexes:
                if index >= len(decoded.children) or decoded.children[index] == policy.empty_digest:
                    raise _ExpansionError("proof expands an invalid or empty child interval")
            steps[key] = step
            decoded_steps[key] = decoded
    except MerklePersistenceError as error:
        if error.kind is MerklePersistenceFailure.BUDGET_EXCEEDED:
            return fail(MerkleProofFailure.BUDGET_EXCEEDED)
        return fail(MerkleProofFailure.INVALID_BLOCK)
    except _ExpansionError:
        return fail(MerkleProofFailure.INVALID_EXPANSION)
    if proof.root_hash == policy.empty_digest:
        if proof.steps or isinstance(proof.query, MerkleMembershipQuery):
            return fail(MerkleProofFailure.WRONG_RESULT)
        return MerkleProofVerificationResult(
            True,
            MerkleProofFailure.NONE,
            proof.root_hash,
            (),
            0,
            context.bytes,
        )
    visited: set[str] = set()
    result: list[MerkleEntry[K, V]] = []
    found: _StoredEntry[K, V] | None = None

    def walk(
        digest: MerkleDigest,
        depth: int,
        minimum: K | None,
        maximum: K | None,
        has_minimum: bool,
        has_maximum: bool,
        parent_level: int,
    ) -> None:
        nonlocal found
        key = str(digest)
        step = steps.get(key)
        if step is None or key in visited:
            raise _ExpansionError("missing or repeated expanded block")
        visited.add(key)
        context.check_depth(depth)
        decoded = decoded_steps[key]
        if decoded.block.digest != digest or decoded.level >= parent_level:
            raise _ExpansionError("expanded block address or level is invalid")
        for entry in decoded.entries:
            if has_minimum and policy.comparer(entry.key, cast(K, minimum)) <= 0:
                raise _ExpansionError("expanded entry violates its lower interval")
            if has_maximum and policy.comparer(entry.key, cast(K, maximum)) >= 0:
                raise _ExpansionError("expanded entry violates its upper interval")
        expected = _selected_children(proof.query, decoded.entries, decoded.children, policy)
        if expected != step.expanded_child_indexes:
            raise _ExpansionError("expanded child indexes are not canonical for the query")
        expanded = set(expected)
        for index, entry in enumerate(decoded.entries):
            if index in expanded:
                walk(
                    decoded.children[index],
                    depth + 1,
                    minimum if index == 0 else decoded.entries[index - 1].key,
                    entry.key,
                    has_minimum or index > 0,
                    True,
                    decoded.level,
                )
            if isinstance(proof.query, MerkleRangeQuery):
                if (
                    policy.comparer(entry.key, proof.query.minimum) >= 0
                    and policy.comparer(entry.key, proof.query.maximum) <= 0
                ):
                    result.append(MerkleEntry(entry.key, entry.value))
            else:
                comparison = policy.comparer(entry.key, proof.query.key)
                if comparison == 0:
                    found = entry
        final_index = len(decoded.entries)
        if final_index in expanded:
            walk(
                decoded.children[final_index],
                depth + 1,
                decoded.entries[-1].key,
                maximum,
                True,
                has_maximum,
                decoded.level,
            )

    try:
        walk(proof.root_hash, 1, None, None, False, False, 65)
    except MerklePersistenceError as error:
        if error.kind is MerklePersistenceFailure.BUDGET_EXCEEDED:
            return fail(MerkleProofFailure.BUDGET_EXCEEDED)
        return fail(MerkleProofFailure.INVALID_BLOCK)
    except _ExpansionError:
        return fail(MerkleProofFailure.INVALID_EXPANSION)
    except Exception:
        return fail(MerkleProofFailure.INVALID_EXPANSION)
    if len(visited) != len(steps):
        return fail(MerkleProofFailure.INVALID_EXPANSION)
    if isinstance(proof.query, MerkleMembershipQuery):
        try:
            expected_value = bytes(policy.value_codec.encode(proof.query.value))
        except Exception:
            return fail(MerkleProofFailure.INVALID_QUERY)
        if (
            found is None
            or policy.comparer(found.key, proof.query.key) != 0
            or found.value_bytes != expected_value
        ):
            return fail(MerkleProofFailure.WRONG_RESULT)
        result.append(MerkleEntry(found.key, found.value))
    elif isinstance(proof.query, MerkleNonmembershipQuery) and found is not None:
        return fail(MerkleProofFailure.WRONG_RESULT)
    return MerkleProofVerificationResult(
        True,
        MerkleProofFailure.NONE,
        proof.root_hash,
        tuple(result),
        context.blocks,
        context.bytes,
    )


@dataclass(frozen=True, slots=True)
class MerkleMergeValue(Generic[V]):
    present: bool
    value: V | None = None

    @classmethod
    def absent(cls) -> MerkleMergeValue[V]:
        return cls(False)

    @classmethod
    def of(cls, value: V) -> MerkleMergeValue[V]:
        return cls(True, value)


@dataclass(frozen=True, slots=True)
class MerkleThreeWayMergeConflict(Generic[K, V]):
    key: K
    base: MerkleMergeValue[V]
    left: MerkleMergeValue[V]
    right: MerkleMergeValue[V]


class MerkleMergeResolutionKind(StrEnum):
    UNRESOLVED = "unresolved"
    BASE = "base"
    LEFT = "left"
    RIGHT = "right"
    DELETE = "delete"
    VALUE = "value"


@dataclass(frozen=True, slots=True)
class MerkleMergeResolution(Generic[V]):
    kind: MerkleMergeResolutionKind
    value: V | None = None

    @classmethod
    def unresolved(cls) -> MerkleMergeResolution[V]:
        return cls(MerkleMergeResolutionKind.UNRESOLVED)

    @classmethod
    def base(cls) -> MerkleMergeResolution[V]:
        return cls(MerkleMergeResolutionKind.BASE)

    @classmethod
    def left(cls) -> MerkleMergeResolution[V]:
        return cls(MerkleMergeResolutionKind.LEFT)

    @classmethod
    def right(cls) -> MerkleMergeResolution[V]:
        return cls(MerkleMergeResolutionKind.RIGHT)

    @classmethod
    def delete(cls) -> MerkleMergeResolution[V]:
        return cls(MerkleMergeResolutionKind.DELETE)

    @classmethod
    def use_value(cls, value: V) -> MerkleMergeResolution[V]:
        return cls(MerkleMergeResolutionKind.VALUE, value)


@dataclass(frozen=True, slots=True)
class MerkleThreeWayMergeResult(Generic[K, V]):
    succeeded: bool
    tree: MerkleSearchTree[K, V] | None
    conflicts: tuple[MerkleThreeWayMergeConflict[K, V], ...]


def _merge_value(entry: MerkleEntry[K, V] | None) -> MerkleMergeValue[V]:
    return MerkleMergeValue.absent() if entry is None else MerkleMergeValue.of(entry.value)


def _merge_equal(
    left: MerkleMergeValue[V],
    right: MerkleMergeValue[V],
    values_equal: Callable[[V, V], bool],
) -> bool:
    if left.present != right.present:
        return False
    if not left.present:
        return True
    left_value = cast(V, left.value)
    right_value = cast(V, right.value)
    return left_value is right_value or values_equal(left_value, right_value)


def merge_merkle_trees(
    base: MerkleSearchTree[K, V],
    left: MerkleSearchTree[K, V],
    right: MerkleSearchTree[K, V],
    resolver: Callable[[MerkleThreeWayMergeConflict[K, V]], MerkleMergeResolution[V]] | None = None,
    values_equal: Callable[[V, V], bool] = lambda left, right: bool(left == right),
) -> MerkleThreeWayMergeResult[K, V]:
    if not base.policy.is_compatible_with(
        cast("MerkleSearchTreePolicy[object, object]", left.policy)
    ) or not base.policy.is_compatible_with(
        cast("MerkleSearchTreePolicy[object, object]", right.policy)
    ):
        raise ValueError("Merkle merge policy domains differ")
    if left.root_hash == right.root_hash:
        return MerkleThreeWayMergeResult(True, left, ())
    if base.root_hash == left.root_hash:
        return MerkleThreeWayMergeResult(True, right, ())
    if base.root_hash == right.root_hash:
        return MerkleThreeWayMergeResult(True, left, ())
    resolve = resolver or (lambda _conflict: MerkleMergeResolution.unresolved())
    all_keys = [entry.key for tree in (base, left, right) for entry in tree]
    all_keys.sort(key=cmp_to_key(base.policy.comparer))
    keys: list[K] = []
    for key in all_keys:
        if not keys or base.policy.comparer(keys[-1], key) != 0:
            keys.append(key)
    merged: list[tuple[K, V]] = []
    conflicts: list[MerkleThreeWayMergeConflict[K, V]] = []
    for key in keys:
        base_entry = base.get_entry(key)
        left_entry = left.get_entry(key)
        right_entry = right.get_entry(key)
        base_value = _merge_value(base_entry)
        left_value = _merge_value(left_entry)
        right_value = _merge_value(right_entry)
        representative = (
            left_entry.key
            if left_entry is not None
            else right_entry.key
            if right_entry is not None
            else cast(MerkleEntry[K, V], base_entry).key
        )
        selected: MerkleMergeValue[V]
        if _merge_equal(left_value, right_value, values_equal):
            selected = left_value
        elif _merge_equal(left_value, base_value, values_equal):
            selected = right_value
        elif _merge_equal(right_value, base_value, values_equal):
            selected = left_value
        else:
            conflict = MerkleThreeWayMergeConflict(
                representative, base_value, left_value, right_value
            )
            resolution = resolve(conflict)
            if resolution.kind is MerkleMergeResolutionKind.UNRESOLVED:
                conflicts.append(conflict)
                continue
            if resolution.kind is MerkleMergeResolutionKind.BASE:
                selected = base_value
            elif resolution.kind is MerkleMergeResolutionKind.LEFT:
                selected = left_value
            elif resolution.kind is MerkleMergeResolutionKind.RIGHT:
                selected = right_value
            elif resolution.kind is MerkleMergeResolutionKind.DELETE:
                selected = MerkleMergeValue.absent()
            else:
                selected = MerkleMergeValue.of(cast(V, resolution.value))
        if selected.present:
            merged.append((representative, cast(V, selected.value)))
    if conflicts:
        return MerkleThreeWayMergeResult(False, None, tuple(conflicts))
    return MerkleThreeWayMergeResult(
        True,
        MerkleSearchTree.from_entries(merged, base.policy),
        (),
    )


__all__ = [
    "MerkleMembershipQuery",
    "MerkleMergeResolution",
    "MerkleMergeResolutionKind",
    "MerkleMergeValue",
    "MerkleNonmembershipQuery",
    "MerkleProof",
    "MerkleProofFailure",
    "MerkleProofKind",
    "MerkleProofStep",
    "MerkleProofVerificationResult",
    "MerkleRangeQuery",
    "MerkleThreeWayMergeConflict",
    "MerkleThreeWayMergeResult",
    "create_merkle_proof",
    "create_merkle_range_proof",
    "merge_merkle_trees",
    "verify_merkle_proof",
]
