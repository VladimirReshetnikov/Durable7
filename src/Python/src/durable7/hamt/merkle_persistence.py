"""Verified ``MST2`` block persistence, finite budgets, and frontier synchronization."""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
from enum import StrEnum
from threading import RLock
from typing import Generic, Protocol, TypeVar, cast

from .merkle_encoding import MerkleDigest, MerkleSearchTreePolicy
from .merkle_search_tree import MerkleSearchTree, _Node, _StoredEntry

K = TypeVar("K")
V = TypeVar("V")


class MerklePersistenceFailure(StrEnum):
    CONFLICTING_BLOCK = "conflicting-block"
    MISSING_BLOCK = "missing-block"
    DIGEST_MISMATCH = "digest-mismatch"
    INVALID_BLOCK = "invalid-block"
    POLICY_MISMATCH = "policy-mismatch"
    BUDGET_EXCEEDED = "budget-exceeded"
    CYCLE = "cycle"
    INVALID_STRUCTURE = "invalid-structure"


class MerklePersistenceError(ValueError):
    """Typed rejection of untrusted Merkle persistence input."""

    def __init__(self, kind: MerklePersistenceFailure, message: str) -> None:
        super().__init__(message)
        self.kind = kind


@dataclass(frozen=True, slots=True, init=False)
class MerkleBlock:
    digest: MerkleDigest
    content: bytes

    def __init__(self, digest: MerkleDigest, content: bytes | bytearray | memoryview) -> None:
        object.__setattr__(self, "digest", digest)
        object.__setattr__(self, "content", bytes(content))


class MerkleBlockStore(Protocol):
    def get(self, digest: MerkleDigest) -> MerkleBlock | None: ...

    def put(self, block: MerkleBlock) -> bool: ...

    def contains(self, digest: MerkleDigest) -> bool: ...

    def digests(self) -> tuple[MerkleDigest, ...]: ...


class InMemoryMerkleBlockStore:
    """Synchronized immutable-block store with atomic multi-block publication."""

    __slots__ = ("_blocks", "_lock")

    def __init__(self, blocks: Iterable[MerkleBlock] = ()) -> None:
        self._blocks: dict[str, MerkleBlock] = {}
        self._lock = RLock()
        self.put_many_atomic(blocks)

    @property
    def count(self) -> int:
        with self._lock:
            return len(self._blocks)

    def __len__(self) -> int:
        return self.count

    def get(self, digest: MerkleDigest) -> MerkleBlock | None:
        with self._lock:
            return self._blocks.get(str(digest))

    def contains(self, digest: MerkleDigest) -> bool:
        with self._lock:
            return str(digest) in self._blocks

    def put(self, block: MerkleBlock) -> bool:
        return self.put_many_atomic((block,)) == 1

    def put_many_atomic(self, blocks: Iterable[MerkleBlock]) -> int:
        incoming: dict[str, MerkleBlock] = {}
        for block in blocks:
            key = str(block.digest)
            previous = incoming.get(key)
            if previous is not None and previous != block:
                raise MerklePersistenceError(
                    MerklePersistenceFailure.CONFLICTING_BLOCK,
                    f"incoming batch repeats digest {key} with different content",
                )
            incoming[key] = block
        with self._lock:
            for key, block in incoming.items():
                previous = self._blocks.get(key)
                if previous is not None and previous != block:
                    raise MerklePersistenceError(
                        MerklePersistenceFailure.CONFLICTING_BLOCK,
                        f"different bytes already occupy digest {key}",
                    )
            written = 0
            for key, block in incoming.items():
                if key not in self._blocks:
                    self._blocks[key] = block
                    written += 1
            return written

    def digests(self) -> tuple[MerkleDigest, ...]:
        with self._lock:
            return tuple(sorted(block.digest for block in self._blocks.values()))


@dataclass(frozen=True, slots=True, init=False)
class MerkleBlockPack:
    algorithm_id: str
    domain_digest: MerkleDigest
    root_hash: MerkleDigest
    blocks: tuple[MerkleBlock, ...]

    def __init__(
        self,
        algorithm_id: str,
        domain_digest: MerkleDigest,
        root_hash: MerkleDigest,
        blocks: Iterable[MerkleBlock],
    ) -> None:
        unique: dict[str, MerkleBlock] = {}
        for block in blocks:
            key = str(block.digest)
            previous = unique.get(key)
            if previous is not None and previous != block:
                raise MerklePersistenceError(
                    MerklePersistenceFailure.CONFLICTING_BLOCK,
                    f"pack repeats digest {key} with different content",
                )
            if previous is None:
                unique[key] = block
        object.__setattr__(self, "algorithm_id", algorithm_id)
        object.__setattr__(self, "domain_digest", domain_digest)
        object.__setattr__(self, "root_hash", root_hash)
        object.__setattr__(self, "blocks", tuple(unique.values()))

    @property
    def block_count(self) -> int:
        return len(self.blocks)

    @property
    def contains_root_block(self) -> bool:
        return any(block.digest == self.root_hash for block in self.blocks)


@dataclass(frozen=True, slots=True)
class MerkleVerificationBudget:
    maximum_blocks: int = 1_000_000
    maximum_total_bytes: int = 1_073_741_824
    maximum_block_bytes: int = 16_777_216
    maximum_depth: int = 256
    maximum_entries: int = 100_000_000
    maximum_children_per_block: int = 65_536
    maximum_query_bytes: int | None = None

    def __post_init__(self) -> None:
        if self.maximum_query_bytes is None:
            object.__setattr__(self, "maximum_query_bytes", self.maximum_block_bytes)
        values = (
            self.maximum_blocks,
            self.maximum_total_bytes,
            self.maximum_block_bytes,
            self.maximum_depth,
            self.maximum_entries,
            self.maximum_children_per_block,
            cast(int, self.maximum_query_bytes),
        )
        if any(
            isinstance(value, bool) or not isinstance(value, int) or value <= 0 for value in values
        ):
            raise ValueError("Merkle verification limits must be positive integers")
        if self.maximum_block_bytes > self.maximum_total_bytes:
            raise ValueError("maximum block bytes cannot exceed maximum total bytes")
        if cast(int, self.maximum_query_bytes) > self.maximum_total_bytes:
            raise ValueError("maximum query bytes cannot exceed maximum total bytes")


class _VerificationContext:
    __slots__ = ("blocks", "budget", "bytes", "entries", "seen")

    def __init__(self, budget: MerkleVerificationBudget) -> None:
        self.budget = budget
        self.seen: set[str] = set()
        self.blocks = 0
        self.bytes = 0
        self.entries = 0

    def check_depth(self, depth: int) -> None:
        if depth > self.budget.maximum_depth:
            self._exceeded("maximum Merkle depth exceeded")

    def account_block(self, block: MerkleBlock, depth: int) -> bool:
        self.check_depth(depth)
        if len(block.content) > self.budget.maximum_block_bytes:
            self._exceeded("maximum Merkle block size exceeded")
        key = str(block.digest)
        if key in self.seen:
            return False
        self.seen.add(key)
        self.blocks += 1
        self.bytes += len(block.content)
        if self.blocks > self.budget.maximum_blocks:
            self._exceeded("maximum Merkle block count exceeded")
        if self.bytes > self.budget.maximum_total_bytes:
            self._exceeded("maximum Merkle byte count exceeded")
        return True

    def account_entries(self, count: int) -> None:
        self.entries += count
        if self.entries > self.budget.maximum_entries:
            self._exceeded("maximum decoded-entry count exceeded")

    def account_query(self, count: int) -> None:
        if count > cast(int, self.budget.maximum_query_bytes):
            self._exceeded("maximum proof-query size exceeded")
        self.bytes += count
        if self.bytes > self.budget.maximum_total_bytes:
            self._exceeded("maximum Merkle byte count exceeded")

    @staticmethod
    def _exceeded(message: str) -> None:
        raise MerklePersistenceError(MerklePersistenceFailure.BUDGET_EXCEEDED, message)


@dataclass(frozen=True, slots=True)
class _DecodedBlock(Generic[K, V]):
    block: MerkleBlock
    level: int
    count: int
    entries: tuple[_StoredEntry[K, V], ...]
    children: tuple[MerkleDigest, ...]


class _Reader:
    __slots__ = ("data", "offset")

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def take(self, length: int) -> bytes:
        if length < 0 or self.offset + length > len(self.data):
            raise MerklePersistenceError(
                MerklePersistenceFailure.INVALID_BLOCK,
                "truncated or invalid-length MST2 block",
            )
        result = self.data[self.offset : self.offset + length]
        self.offset += length
        return result

    def byte(self) -> int:
        return self.take(1)[0]

    def int32(self) -> int:
        return int.from_bytes(self.take(4), "big", signed=True)


def _decode_merkle_block(
    block: MerkleBlock,
    tree: MerkleSearchTree[K, V],
    context: _VerificationContext,
    depth: int,
) -> _DecodedBlock[K, V]:
    fresh = context.account_block(block, depth)
    if MerkleDigest.hash(block.content) != block.digest:
        raise MerklePersistenceError(
            MerklePersistenceFailure.DIGEST_MISMATCH,
            f"block {block.digest} does not hash to its claimed address",
        )
    reader = _Reader(block.content)
    if reader.take(4) != b"MST2" or reader.byte() != 1:
        raise MerklePersistenceError(
            MerklePersistenceFailure.INVALID_BLOCK, "invalid MST2 node magic or tag"
        )
    if MerkleDigest.from_bytes(reader.take(32)) != tree.policy.domain_digest:
        raise MerklePersistenceError(
            MerklePersistenceFailure.POLICY_MISMATCH,
            "MST2 block belongs to another policy domain",
        )
    level = reader.byte()
    count = reader.int32()
    entry_count = reader.int32()
    if level > 64 or count <= 0 or entry_count <= 0 or entry_count > count:
        raise MerklePersistenceError(
            MerklePersistenceFailure.INVALID_BLOCK, "invalid MST2 level or counts"
        )
    child_count = entry_count + 1
    if child_count > context.budget.maximum_children_per_block:
        raise MerklePersistenceError(
            MerklePersistenceFailure.BUDGET_EXCEEDED,
            "maximum child references per block exceeded",
        )
    if fresh:
        context.account_entries(entry_count)
    entries: list[_StoredEntry[K, V]] = []
    for index in range(entry_count):
        key_length = reader.int32()
        if key_length < 0 or key_length > context.budget.maximum_block_bytes:
            raise MerklePersistenceError(
                MerklePersistenceFailure.INVALID_BLOCK, "invalid MST2 key length"
            )
        key_bytes = reader.take(key_length)
        value_length = reader.int32()
        if value_length < 0 or value_length > context.budget.maximum_block_bytes:
            raise MerklePersistenceError(
                MerklePersistenceFailure.INVALID_BLOCK, "invalid MST2 value length"
            )
        value_bytes = reader.take(value_length)
        try:
            key = tree.policy.key_codec.decode(key_bytes)
            value = tree.policy.value_codec.decode(value_bytes)
            if bytes(tree.policy.key_codec.encode(key)) != key_bytes:
                raise ValueError("key encoding is not canonical")
            if bytes(tree.policy.value_codec.encode(value)) != value_bytes:
                raise ValueError("value encoding is not canonical")
            entry = tree._create_verified_entry(key, value, key_bytes, value_bytes)
            if entry.level != level:
                raise ValueError("separator does not hash to the declared level")
            if index and tree.policy.comparer(entries[-1].key, entry.key) >= 0:
                raise ValueError("separators are not strictly ordered")
        except Exception as error:
            if isinstance(error, MerklePersistenceError):
                raise
            raise MerklePersistenceError(
                MerklePersistenceFailure.INVALID_BLOCK,
                f"canonical MST2 entry validation failed: {error}",
            ) from error
        entries.append(entry)
    children = tuple(MerkleDigest.from_bytes(reader.take(32)) for _ in range(child_count))
    if reader.offset != len(block.content):
        raise MerklePersistenceError(
            MerklePersistenceFailure.INVALID_BLOCK, "trailing bytes follow the MST2 block"
        )
    return _DecodedBlock(block, level, count, tuple(entries), children)


class _OverlayStore:
    __slots__ = ("destination", "staged")

    def __init__(self, staged: dict[str, MerkleBlock], destination: MerkleBlockStore) -> None:
        self.staged = staged
        self.destination = destination

    def get(self, digest: MerkleDigest) -> MerkleBlock | None:
        return self.staged.get(str(digest)) or self.destination.get(digest)

    def put(self, block: MerkleBlock) -> bool:  # pragma: no cover - verification never writes
        raise RuntimeError("read-only verification overlay")

    def contains(self, digest: MerkleDigest) -> bool:
        return str(digest) in self.staged or self.destination.contains(digest)

    def digests(self) -> tuple[MerkleDigest, ...]:
        return ()


def _load_with_store(
    root_hash: MerkleDigest,
    policy: MerkleSearchTreePolicy[K, V],
    store: MerkleBlockStore,
    budget: MerkleVerificationBudget,
    context: _VerificationContext | None = None,
    decoded_cache: dict[str, _DecodedBlock[K, V]] | None = None,
) -> MerkleSearchTree[K, V]:
    shell = MerkleSearchTree.empty(policy)
    if root_hash == policy.empty_digest:
        return shell
    verification = context or _VerificationContext(budget)
    decoded_by_digest = decoded_cache or {}
    memo: dict[str, _Node[K, V]] = {}
    active: set[str] = set()

    def load_node(
        digest: MerkleDigest,
        depth: int,
        minimum: K | None,
        maximum: K | None,
        has_minimum: bool,
        has_maximum: bool,
        parent_level: int,
    ) -> _Node[K, V] | None:
        if digest == policy.empty_digest:
            return None
        verification.check_depth(depth)
        key = str(digest)
        if key in memo:
            return memo[key]
        if key in active:
            raise MerklePersistenceError(
                MerklePersistenceFailure.CYCLE, f"cycle detected through block {key}"
            )
        block = store.get(digest)
        if block is None:
            raise MerklePersistenceError(
                MerklePersistenceFailure.MISSING_BLOCK, f"missing Merkle block {key}"
            )
        active.add(key)
        try:
            decoded = decoded_by_digest.get(key)
            if decoded is None:
                decoded = _decode_merkle_block(block, shell, verification, depth)
                decoded_by_digest[key] = decoded
            elif decoded.block != block:
                raise MerklePersistenceError(
                    MerklePersistenceFailure.CONFLICTING_BLOCK,
                    f"verification cache conflicts at {key}",
                )
            if decoded.level >= parent_level:
                raise MerklePersistenceError(
                    MerklePersistenceFailure.INVALID_STRUCTURE,
                    "Merkle child levels must strictly decrease",
                )
            for entry in decoded.entries:
                if has_minimum and policy.comparer(entry.key, cast(K, minimum)) <= 0:
                    raise MerklePersistenceError(
                        MerklePersistenceFailure.INVALID_STRUCTURE,
                        "Merkle entry violates its parent lower interval",
                    )
                if has_maximum and policy.comparer(entry.key, cast(K, maximum)) >= 0:
                    raise MerklePersistenceError(
                        MerklePersistenceFailure.INVALID_STRUCTURE,
                        "Merkle entry violates its parent upper interval",
                    )
            children: list[_Node[K, V] | None] = []
            for index, child_digest in enumerate(decoded.children):
                child_minimum = minimum if index == 0 else decoded.entries[index - 1].key
                child_maximum = (
                    maximum if index == len(decoded.entries) else decoded.entries[index].key
                )
                children.append(
                    load_node(
                        child_digest,
                        depth + 1,
                        child_minimum,
                        child_maximum,
                        has_minimum or index > 0,
                        has_maximum or index < len(decoded.entries),
                        decoded.level,
                    )
                )
            node = shell._create_verified_node(decoded.level, decoded.entries, tuple(children))
            if (
                node.count != decoded.count
                or node.digest != digest
                or node.block_bytes != block.content
            ):
                raise MerklePersistenceError(
                    MerklePersistenceFailure.INVALID_STRUCTURE,
                    "MST2 subtree count, address, or exact reserialization differs",
                )
            memo[key] = node
            return node
        finally:
            active.remove(key)

    root = load_node(root_hash, 1, None, None, False, False, 65)
    tree = MerkleSearchTree._from_verified_root(policy, root)
    try:
        tree.validate_structure()
    except MerklePersistenceError:
        raise
    except Exception as error:
        raise MerklePersistenceError(
            MerklePersistenceFailure.INVALID_STRUCTURE, f"invalid Merkle closure: {error}"
        ) from error
    return tree


def export_merkle_pack(
    tree: MerkleSearchTree[K, V], digests: Iterable[MerkleDigest] | None = None
) -> MerkleBlockPack:
    blocks = tuple(MerkleBlock(block.digest, block.bytes) for block in tree.blocks_preorder())
    if digests is None:
        selected = blocks
    else:
        by_digest = {str(block.digest): block for block in blocks}
        requested: list[MerkleBlock] = []
        for digest in digests:
            block = by_digest.get(str(digest))
            if block is None:
                raise MerklePersistenceError(
                    MerklePersistenceFailure.MISSING_BLOCK,
                    f"tree does not contain block {digest}",
                )
            requested.append(block)
        selected = tuple(requested)
    return MerkleBlockPack(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        tree.policy.domain_digest,
        tree.root_hash,
        selected,
    )


def _preflight_destination(blocks: Iterable[MerkleBlock], store: MerkleBlockStore) -> None:
    for block in blocks:
        previous = store.get(block.digest)
        if previous is not None and previous != block:
            raise MerklePersistenceError(
                MerklePersistenceFailure.CONFLICTING_BLOCK,
                f"destination conflicts at digest {block.digest}",
            )


def _publish_blocks(blocks: tuple[MerkleBlock, ...], store: MerkleBlockStore) -> int:
    _preflight_destination(blocks, store)
    batch_writer = getattr(store, "put_many_atomic", None)
    if callable(batch_writer):
        return cast(int, batch_writer(blocks))
    written = 0
    for block in blocks:
        if store.put(block):
            written += 1
    return written


def save_merkle_tree(tree: MerkleSearchTree[K, V], store: MerkleBlockStore) -> int:
    return _publish_blocks(export_merkle_pack(tree).blocks, store)


def load_merkle_tree(
    root_hash: MerkleDigest,
    policy: MerkleSearchTreePolicy[K, V],
    store: MerkleBlockStore,
    budget: MerkleVerificationBudget | None = None,
) -> MerkleSearchTree[K, V]:
    actual_budget = budget or MerkleVerificationBudget()
    return _load_with_store(root_hash, policy, store, actual_budget)


def import_merkle_pack(
    pack: MerkleBlockPack,
    policy: MerkleSearchTreePolicy[K, V],
    destination: MerkleBlockStore | None = None,
    budget: MerkleVerificationBudget | None = None,
) -> MerkleSearchTree[K, V]:
    if (
        pack.algorithm_id != MerkleSearchTreePolicy.ALGORITHM_ID
        or pack.domain_digest != policy.domain_digest
    ):
        raise MerklePersistenceError(
            MerklePersistenceFailure.POLICY_MISMATCH,
            "Merkle pack algorithm or policy domain differs",
        )
    target = destination if destination is not None else InMemoryMerkleBlockStore()
    actual_budget = budget or MerkleVerificationBudget()
    context = _VerificationContext(actual_budget)
    shell = MerkleSearchTree.empty(policy)
    staged = {str(block.digest): block for block in pack.blocks}
    decoded: dict[str, _DecodedBlock[K, V]] = {}
    # Authenticate and budget every supplied block exactly once before considering publication.
    for block in pack.blocks:
        decoded[str(block.digest)] = _decode_merkle_block(block, shell, context, 1)
    overlay = _OverlayStore(staged, target)
    tree = _load_with_store(
        pack.root_hash,
        policy,
        overlay,
        actual_budget,
        context,
        decoded,
    )
    _publish_blocks(pack.blocks, target)
    return tree


@dataclass(frozen=True, slots=True)
class MerkleSyncPlan:
    target_root: MerkleDigest
    roots_match: bool
    requested_digests: tuple[MerkleDigest, ...]
    examined_blocks: int
    examined_bytes: int

    @property
    def requires_blocks(self) -> bool:
        return bool(self.requested_digests)


def create_merkle_sync_pack(
    tree: MerkleSearchTree[K, V], receiver: MerkleBlockStore
) -> MerkleBlockPack:
    missing: list[MerkleBlock] = []
    root = tree._root
    pending = [] if root is None else [root]
    while pending:
        node = pending.pop()
        if receiver.contains(node.digest):
            continue
        missing.append(MerkleBlock(node.digest, node.block_bytes))
        pending.extend(child for child in reversed(node.children) if child is not None)
    return MerkleBlockPack(
        MerkleSearchTreePolicy.ALGORITHM_ID,
        tree.policy.domain_digest,
        tree.root_hash,
        missing,
    )


def plan_merkle_sync(
    target: MerkleSearchTree[K, V],
    published_local: MerkleSearchTree[K, V],
    receiver: MerkleBlockStore,
) -> MerkleSyncPlan:
    if not target.policy.is_compatible_with(
        cast("MerkleSearchTreePolicy[object, object]", published_local.policy)
    ):
        raise MerklePersistenceError(
            MerklePersistenceFailure.POLICY_MISMATCH,
            "cannot synchronize different Merkle policy domains",
        )
    requested: list[MerkleDigest] = []
    examined_blocks = examined_bytes = 0
    pending = [] if target._root is None else [target._root]
    while pending:
        node = pending.pop()
        examined_blocks += 1
        examined_bytes += len(node.block_bytes)
        if not receiver.contains(node.digest):
            requested.append(node.digest)
            continue
        pending.extend(child for child in reversed(node.children) if child is not None)
    return MerkleSyncPlan(
        target.root_hash,
        target.root_hash == published_local.root_hash,
        tuple(requested),
        examined_blocks,
        examined_bytes,
    )


__all__ = [
    "InMemoryMerkleBlockStore",
    "MerkleBlock",
    "MerkleBlockPack",
    "MerkleBlockStore",
    "MerklePersistenceError",
    "MerklePersistenceFailure",
    "MerkleSyncPlan",
    "MerkleVerificationBudget",
    "create_merkle_sync_pack",
    "export_merkle_pack",
    "import_merkle_pack",
    "load_merkle_tree",
    "plan_merkle_sync",
    "save_merkle_tree",
]
