"""Exact-wire, persistence, proof, budget, synchronization, and merge tests."""

from __future__ import annotations

import uuid
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.hamt.merkle_encoding import (
    INT32_MERKLE_CODEC,
    INT64_MERKLE_CODEC,
    NULLABLE_BYTES_MERKLE_CODEC,
    NULLABLE_UTF8_MERKLE_CODEC,
    RFC4122_UUID_MERKLE_CODEC,
    MerkleDigest,
    MerkleSearchTreePolicy,
)
from vladimir_reshetnikov.data_structures.hamt.merkle_persistence import (
    InMemoryMerkleBlockStore,
    MerkleBlock,
    MerkleBlockPack,
    MerklePersistenceError,
    MerklePersistenceFailure,
    MerkleVerificationBudget,
    create_merkle_sync_pack,
    export_merkle_pack,
    import_merkle_pack,
    load_merkle_tree,
    plan_merkle_sync,
    save_merkle_tree,
)
from vladimir_reshetnikov.data_structures.hamt.merkle_proofs_merge import (
    MerkleMembershipQuery,
    MerkleMergeResolution,
    MerkleProof,
    MerkleProofFailure,
    MerkleProofStep,
    create_merkle_proof,
    create_merkle_range_proof,
    merge_merkle_trees,
    verify_merkle_proof,
)
from vladimir_reshetnikov.data_structures.hamt.merkle_search_tree import (
    MerkleEntry,
    MerkleSearchTree,
)


def compare_int(left: int, right: int) -> int:
    return (left > right) - (left < right)


def string_policy(
    policy_id: str = "golden-int-string-v1",
) -> MerkleSearchTreePolicy[int, str | None]:
    return MerkleSearchTreePolicy(
        policy_id,
        compare_int,
        INT32_MERKLE_CODEC,
        NULLABLE_UTF8_MERKLE_CODEC,
    )


def make_tree(
    policy: MerkleSearchTreePolicy[int, str | None], count: int = 513
) -> MerkleSearchTree[int, str | None]:
    first = -(count // 2)
    return MerkleSearchTree.from_entries(
        ((key, None if key % 29 == 0 else f"value:{key}") for key in range(first, first + count)),
        policy,
    )


def test_cursor_navigates_ranks_and_publishes_canonical_edits() -> None:
    policy = string_policy("python-cursor-v1")
    source = MerkleSearchTree.from_entries(((-10, "a"), (0, None), (10, "c")), policy)
    entries = tuple(source)
    for position in range(source.count + 1):
        cursor = source.cursor(position)
        assert cursor.position == position
        assert cursor.is_at_start is (position == 0)
        assert cursor.is_at_end is (position == source.count)
        assert cursor.snapshot() is source
        assert cursor.peek_previous() == (None if position == 0 else entries[position - 1])
        assert cursor.peek_next() == (None if position == source.count else entries[position])

    assert source.lower_bound_cursor(-5).position == 1
    assert source.upper_bound_cursor(0).position == 2
    assert source.cursor_at_key(0).found
    assert source.cursor_at_key(0).cursor.position == 1
    assert not source.cursor_at_key(5).found
    assert source.cursor_at_key(5).cursor.position == 2

    exact = source.cursor_at_key(0).cursor
    assert exact.set_next_value(None) is exact
    changed = exact.set_next_value("b").snapshot()
    assert changed.get(0) == "b"
    assert changed.root_hash != source.root_hash
    assert changed.policy is source.policy
    assert source.get_entry(0) == MerkleEntry(0, None)

    inserted = source.lower_bound_cursor(5).insert(5, "five")
    assert inserted.position == 3
    assert [entry.key for entry in inserted.snapshot()] == [-10, 0, 5, 10]
    assert inserted.delete_previous().snapshot().root_hash == source.root_hash
    assert [entry.key for entry in source.cursor_at_end().delete_previous().snapshot()] == [-10, 0]
    assert [entry.key for entry in source.cursor().delete_next().snapshot()] == [0, 10]

    with pytest.raises(IndexError):
        source.cursor(-1)
    with pytest.raises(IndexError):
        source.cursor().move_previous()
    with pytest.raises(IndexError):
        source.cursor_at_end().move_next()
    with pytest.raises(KeyError):
        exact.insert(0, "duplicate")
    with pytest.raises(ValueError):
        source.cursor().insert(5, "wrong gap")


@given(st.sets(st.integers(min_value=-2_000, max_value=2_000), max_size=300))
@settings(max_examples=50)
def test_cursor_cached_ranks_and_bounds_match_sorted_model(values: set[int]) -> None:
    keys = sorted(values)
    tree = MerkleSearchTree.from_entries(((key, str(key)) for key in keys), string_policy())
    for position in range(len(keys) + 1):
        cursor = tree.cursor(position)
        previous = cursor.peek_previous()
        next_entry = cursor.peek_next()
        assert (None if previous is None else previous.key) == (
            None if position == 0 else keys[position - 1]
        )
        assert (None if next_entry is None else next_entry.key) == (
            None if position == len(keys) else keys[position]
        )
    for probe in range(-2_100, 2_101, 97):
        rank = next((index for index, key in enumerate(keys) if key >= probe), len(keys))
        found = rank < len(keys) and keys[rank] == probe
        assert tree.lower_bound_cursor(probe).position == rank
        assert tree.upper_bound_cursor(probe).position == rank + (1 if found else 0)
        result = tree.cursor_at_key(probe)
        assert result.cursor.position == rank
        assert result.found is found


def test_canonical_codecs_digest_and_policy_vectors() -> None:
    assert INT32_MERKLE_CODEC.encoding_id == "i32-be-v1"
    assert INT32_MERKLE_CODEC.encode(0x01020304) == bytes.fromhex("01020304")
    assert INT32_MERKLE_CODEC.encode(-(1 << 31)) == bytes.fromhex("80000000")
    assert INT32_MERKLE_CODEC.decode(b"\xff" * 4) == -1
    assert INT64_MERKLE_CODEC.encoding_id == "i64-be-v1"
    assert INT64_MERKLE_CODEC.encode(0x0102030405060708) == bytes.fromhex("0102030405060708")
    assert INT64_MERKLE_CODEC.decode(b"\xff" * 8) == -1
    for malformed in (b"", b"\x00", b"\x00" * 3, b"\x00" * 5):
        with pytest.raises(ValueError):
            INT32_MERKLE_CODEC.decode(malformed)
    with pytest.raises(TypeError):
        INT32_MERKLE_CODEC.encode(True)

    text = "Aé😀"
    assert NULLABLE_UTF8_MERKLE_CODEC.encode(text) == bytes.fromhex("0141c3a9f09f9880")
    assert NULLABLE_UTF8_MERKLE_CODEC.decode(bytes.fromhex("0141c3a9f09f9880")) == text
    assert NULLABLE_UTF8_MERKLE_CODEC.encode(None) == b"\x00"
    assert NULLABLE_UTF8_MERKLE_CODEC.decode(b"\x00") is None
    for malformed in (
        b"",
        b"\x00\x00",
        b"\x02",
        b"\x01\xc0\x80",
        b"\x01\xe2\x82",
        b"\x01\xed\xa0\x80",
    ):
        with pytest.raises((UnicodeDecodeError, ValueError)):
            NULLABLE_UTF8_MERKLE_CODEC.decode(malformed)
    with pytest.raises(UnicodeEncodeError):
        NULLABLE_UTF8_MERKLE_CODEC.encode("\ud800")

    assert NULLABLE_BYTES_MERKLE_CODEC.encode(b"\x00\x01\xff") == bytes.fromhex("010001ff")
    assert NULLABLE_BYTES_MERKLE_CODEC.decode(bytes.fromhex("010001ff")) == b"\x00\x01\xff"
    assert NULLABLE_BYTES_MERKLE_CODEC.decode(b"\x00") is None
    for malformed in (b"", b"\x00\x01", b"\x02"):
        with pytest.raises(ValueError):
            NULLABLE_BYTES_MERKLE_CODEC.decode(malformed)

    value = uuid.UUID("00112233-4455-6677-8899-aabbccddeeff")
    assert RFC4122_UUID_MERKLE_CODEC.encode(value) == bytes.fromhex(
        "00112233445566778899aabbccddeeff"
    )
    assert RFC4122_UUID_MERKLE_CODEC.decode(value.bytes) == value

    digest_bytes = bytes(range(32))
    digest = MerkleDigest.from_bytes(digest_bytes)
    expected_hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
    assert str(digest) == expected_hex
    assert MerkleDigest.parse(expected_hex.upper()) == digest
    assert bytes(digest) == digest_bytes
    with pytest.raises(ValueError):
        MerkleDigest.parse("0" * 63)

    policy = string_policy()
    assert MerkleSearchTreePolicy.ALGORITHM_ID == "mst-sha256-b16-v2"
    assert (
        str(policy.domain_digest)
        == "fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
    )
    assert (
        str(policy.empty_digest)
        == "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
    )
    for invalid in ("", " ", "codec", "-v1", "codec-v", "codec-vx", " codec-v1", "codec-v1 "):

        class InvalidCodec:
            encoding_id = invalid

            @staticmethod
            def encode(value: int) -> bytes:
                return INT32_MERKLE_CODEC.encode(value)

            @staticmethod
            def decode(encoding: bytes) -> int:
                return INT32_MERKLE_CODEC.decode(encoding)

        with pytest.raises(ValueError):
            MerkleSearchTreePolicy(
                "wire-policy-v1", compare_int, InvalidCodec(), INT32_MERKLE_CODEC
            )


SINGLE_BLOCK_HEX = (
    "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
    "000000000100000001000000040000002a0000000a01666f7274792d74776f"
    "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
    "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
)

WIDE_ROOT_BLOCK_HEX = (
    "4d53543201eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917"
    "020000000e00000002000000040000003b00000004ffffffc400000004000001d0"
    "00000004fffffe2f790b862e0ef81c9e6debdf38c1099c565887fe87aed84f26dfba736de256d4d5"
    "018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608"
    "018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608"
)


def test_exact_single_entry_and_wide_mst2_vectors() -> None:
    policy = string_policy()
    tree = MerkleSearchTree.empty(policy).set_item(42, "forty-two")
    block = next(tree.blocks_preorder())
    assert str(tree.root_hash) == "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
    assert block.bytes.hex() == SINGLE_BLOCK_HEX
    assert MerkleDigest.hash(block.bytes) == tree.root_hash
    assert tree.validate_structure().count == 1

    wide_policy = MerkleSearchTreePolicy(
        "golden-wide-i32-i32-v1",
        compare_int,
        INT32_MERKLE_CODEC,
        INT32_MERKLE_CODEC,
    )
    keys = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 38, 44, 59, 464]
    wide = MerkleSearchTree.from_entries(((key, -key - 1) for key in keys), wide_policy)
    assert (
        str(wide_policy.domain_digest)
        == "eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917"
    )
    assert str(wide.root_hash) == "9afd7ba98ec91f72074c5f2c272ca1334244fb43a631e0fb440e02799eee8755"
    assert (wide.count, wide.block_count, wide.height) == (14, 4, 3)
    assert next(wide.blocks_preorder()).bytes.hex() == WIDE_ROOT_BLOCK_HEX
    assert [entry.key for entry in wide] == keys
    assert wide.validate_structure().count == 14


@given(st.lists(st.integers(-20_000, 20_000), unique=True, max_size=100))
@settings(max_examples=35)
def test_incremental_bulk_and_churn_histories_converge(keys: list[int]) -> None:
    policy = string_policy("python-history-i32-string-v1")
    incremental = MerkleSearchTree.empty(policy)
    snapshots = [incremental]
    for key in keys:
        incremental = incremental.set_item(key, str(key))
        snapshots.append(incremental)
    bulk = MerkleSearchTree.from_entries(((key, str(key)) for key in reversed(keys)), policy)
    assert incremental.root_hash == bulk.root_hash
    assert tuple(incremental) == tuple(bulk)
    assert snapshots[0].is_empty
    if keys:
        removed = incremental.remove(keys[0])
        assert removed.get_entry(keys[0]) is None
        assert removed.set_item(keys[0], str(keys[0])).root_hash == incremental.root_hash
    assert incremental.validate_structure().count == len(keys)


def test_persistence_round_trip_is_exact_and_destination_is_atomic() -> None:
    policy = string_policy("python-persistence-v1")
    tree = make_tree(policy)
    store = InMemoryMerkleBlockStore()
    assert save_merkle_tree(tree, store) == tree.block_count
    assert save_merkle_tree(tree, store) == 0
    loaded = load_merkle_tree(tree.root_hash, policy, store)
    assert loaded.root_hash == tree.root_hash
    assert tuple(loaded) == tuple(tree)
    assert export_merkle_pack(loaded) == export_merkle_pack(tree)

    pack = export_merkle_pack(tree)
    destination = InMemoryMerkleBlockStore()
    imported = import_merkle_pack(pack, policy, destination)
    assert imported.root_hash == tree.root_hash
    assert destination.count == tree.block_count
    assert export_merkle_pack(imported) == pack

    empty = MerkleSearchTree.empty(policy)
    assert export_merkle_pack(empty).block_count == 0
    assert load_merkle_tree(empty.root_hash, policy, InMemoryMerkleBlockStore()).is_empty

    conflicting_destination = InMemoryMerkleBlockStore()
    conflict_target = pack.blocks[-1]
    conflicting_destination.put(MerkleBlock(conflict_target.digest, b"not the canonical bytes"))
    before = conflicting_destination.digests()
    with pytest.raises(MerklePersistenceError) as failure:
        import_merkle_pack(pack, policy, conflicting_destination)
    assert failure.value.kind is MerklePersistenceFailure.CONFLICTING_BLOCK
    assert conflicting_destination.digests() == before
    assert conflicting_destination.count == 1


def test_persistence_rejects_missing_tampered_malformed_and_foreign_blocks() -> None:
    policy = string_policy("python-rejection-v1")
    tree = make_tree(policy)
    pack = export_merkle_pack(tree)

    partial = MerkleBlockPack(
        pack.algorithm_id, pack.domain_digest, pack.root_hash, pack.blocks[:1]
    )
    with pytest.raises(MerklePersistenceError) as missing:
        import_merkle_pack(partial, policy)
    assert missing.value.kind is MerklePersistenceFailure.MISSING_BLOCK

    root = pack.blocks[0]
    changed = bytearray(root.content)
    changed[-1] ^= 1
    tampered_store = InMemoryMerkleBlockStore((MerkleBlock(root.digest, changed),))
    with pytest.raises(MerklePersistenceError) as tampered:
        load_merkle_tree(tree.root_hash, policy, tampered_store)
    assert tampered.value.kind is MerklePersistenceFailure.DIGEST_MISMATCH

    malformed_bytes = b"BAD!" + root.content[4:]
    malformed = MerkleBlock(MerkleDigest.hash(malformed_bytes), malformed_bytes)
    malformed_pack = MerkleBlockPack(
        pack.algorithm_id, pack.domain_digest, malformed.digest, (malformed,)
    )
    with pytest.raises(MerklePersistenceError) as malformed_failure:
        import_merkle_pack(malformed_pack, policy)
    assert malformed_failure.value.kind is MerklePersistenceFailure.INVALID_BLOCK

    trailing_bytes = root.content + b"\x00"
    trailing = MerkleBlock(MerkleDigest.hash(trailing_bytes), trailing_bytes)
    with pytest.raises(MerklePersistenceError) as trailing_failure:
        import_merkle_pack(
            MerkleBlockPack(pack.algorithm_id, pack.domain_digest, trailing.digest, (trailing,)),
            policy,
        )
    assert trailing_failure.value.kind is MerklePersistenceFailure.INVALID_BLOCK

    foreign_policy = string_policy("foreign-python-domain-v1")
    with pytest.raises(MerklePersistenceError) as foreign:
        import_merkle_pack(pack, foreign_policy)
    assert foreign.value.kind is MerklePersistenceFailure.POLICY_MISMATCH


def test_all_seven_verification_budgets_are_exact_and_typed() -> None:
    policy = string_policy("python-budgets-v1")
    tree = make_tree(policy, 2_049)
    store = InMemoryMerkleBlockStore()
    save_merkle_tree(tree, store)
    statistics = tree.validate_structure()
    exact = MerkleVerificationBudget(
        maximum_blocks=tree.block_count,
        maximum_total_bytes=statistics.encoded_bytes,
        maximum_block_bytes=max(len(block.bytes) for block in tree.blocks_preorder()),
        maximum_depth=tree.height,
        maximum_entries=tree.count,
        maximum_children_per_block=statistics.maximum_children_per_block,
        maximum_query_bytes=1,
    )
    assert load_merkle_tree(tree.root_hash, policy, store, exact).root_hash == tree.root_hash
    constrained = (
        replace(exact, maximum_blocks=exact.maximum_blocks - 1),
        replace(exact, maximum_total_bytes=exact.maximum_total_bytes - 1),
        replace(exact, maximum_block_bytes=exact.maximum_block_bytes - 1),
        replace(exact, maximum_depth=exact.maximum_depth - 1),
        replace(exact, maximum_entries=exact.maximum_entries - 1),
        replace(
            exact,
            maximum_children_per_block=exact.maximum_children_per_block - 1,
        ),
    )
    for budget in constrained:
        with pytest.raises(MerklePersistenceError) as failure:
            load_merkle_tree(tree.root_hash, policy, store, budget)
        assert failure.value.kind is MerklePersistenceFailure.BUDGET_EXCEEDED

    proof = create_merkle_proof(tree, 0)
    proof_budget = replace(
        exact,
        maximum_total_bytes=statistics.encoded_bytes + len(proof.query_bytes),
        maximum_query_bytes=len(proof.query_bytes) - 1,
    )
    result = verify_merkle_proof(proof, policy, proof_budget)
    assert not result.valid
    assert result.failure is MerkleProofFailure.BUDGET_EXCEEDED
    assert result.accounted_blocks == 0

    step_budget = replace(
        proof_budget,
        maximum_blocks=len(proof.steps) - 1,
        maximum_query_bytes=len(proof.query_bytes),
    )
    wrong_envelope = MerkleProof[int, str | None](
        "wrong-algorithm",
        proof.domain_digest,
        proof.root_hash,
        proof.query,
        proof.query_bytes,
        proof.steps,
    )
    step_result = verify_merkle_proof(wrong_envelope, policy, step_budget)
    assert step_result.failure is MerkleProofFailure.BUDGET_EXCEEDED
    assert step_result.accounted_blocks == 0

    expanded_step = MerkleProofStep(proof.steps[0].block, (0, 1))
    expansion_proof = MerkleProof[int, str | None](
        "wrong-algorithm",
        proof.domain_digest,
        proof.root_hash,
        proof.query,
        proof.query_bytes,
        (expanded_step, *proof.steps[1:]),
    )
    expansion_budget = replace(
        proof_budget,
        maximum_children_per_block=1,
        maximum_query_bytes=len(proof.query_bytes),
    )
    expansion_result = verify_merkle_proof(expansion_proof, policy, expansion_budget)
    assert expansion_result.failure is MerkleProofFailure.BUDGET_EXCEEDED
    assert expansion_result.accounted_blocks == 0


def test_iterative_frontier_sync_converges_and_prunes_complete_closures() -> None:
    policy = string_policy("python-sync-v1")
    tree = make_tree(policy, 2_049)
    receiver = InMemoryMerkleBlockStore()
    local = MerkleSearchTree.empty(policy)
    rounds = 0
    while True:
        plan = plan_merkle_sync(tree, local, receiver)
        if not plan.requires_blocks:
            break
        round_pack = export_merkle_pack(tree, plan.requested_digests)
        receiver.put_many_atomic(round_pack.blocks)
        rounds += 1
        assert rounds <= tree.height + 1
    assert load_merkle_tree(tree.root_hash, policy, receiver).root_hash == tree.root_hash
    assert create_merkle_sync_pack(tree, receiver).block_count == 0
    assert plan_merkle_sync(tree, tree, receiver).roots_match


def test_exact_msp2_queries_and_point_range_proofs() -> None:
    policy = string_policy()
    one = MerkleSearchTree.empty(policy).set_item(42, "forty-two")
    membership = create_merkle_proof(one, 42)
    absence = create_merkle_proof(one, 43)
    range_proof = create_merkle_range_proof(one, 40, 44)
    assert membership.query_bytes.hex() == "4d53503200000000040000002a0000000a01666f7274792d74776f"
    assert absence.query_bytes.hex() == "4d53503201000000040000002b"
    assert range_proof.query_bytes.hex() == "4d535032020000000400000028000000040000002c"
    assert verify_merkle_proof(membership, policy).entries == (MerkleEntry(42, "forty-two"),)
    assert verify_merkle_proof(absence, policy).entries == ()
    assert verify_merkle_proof(range_proof, policy).entries == (MerkleEntry(42, "forty-two"),)

    tree = MerkleSearchTree.from_entries(
        ((key * 2, None if key % 11 == 0 else f"v{key}") for key in range(1_000)),
        policy,
    )
    member_result = verify_merkle_proof(create_merkle_proof(tree, 400), policy)
    assert member_result.valid
    assert member_result.entries == (MerkleEntry(400, "v200"),)
    absent_result = verify_merkle_proof(create_merkle_proof(tree, 401), policy)
    assert absent_result.valid and absent_result.entries == ()
    result = verify_merkle_proof(create_merkle_range_proof(tree, 391, 413), policy)
    assert result.valid
    assert [entry.key for entry in result.entries] == list(range(392, 414, 2))
    assert result.accounted_blocks < tree.block_count


def test_proofs_reject_query_block_step_and_expansion_tampering() -> None:
    policy = string_policy("python-proof-tamper-v1")
    tree = make_tree(policy)
    proof = create_merkle_proof(tree, 0)

    changed_query = bytearray(proof.query_bytes)
    changed_query[-1] ^= 1
    assert (
        verify_merkle_proof(
            MerkleProof[int, str | None](
                proof.algorithm_id,
                proof.domain_digest,
                proof.root_hash,
                proof.query,
                changed_query,
                proof.steps,
            ),
            policy,
        ).failure
        is MerkleProofFailure.INVALID_QUERY
    )

    assert isinstance(proof.query, MerkleMembershipQuery)
    wrong_query: MerkleMembershipQuery[int, str | None] = MerkleMembershipQuery(
        proof.query.key, "wrong"
    )
    key_bytes = INT32_MERKLE_CODEC.encode(proof.query.key)
    wrong_bytes = (
        b"MSP2\x00"
        + len(key_bytes).to_bytes(4, "big", signed=True)
        + key_bytes
        + len(NULLABLE_UTF8_MERKLE_CODEC.encode("wrong")).to_bytes(4, "big", signed=True)
        + NULLABLE_UTF8_MERKLE_CODEC.encode("wrong")
    )
    wrong_result = verify_merkle_proof(
        MerkleProof[int, str | None](
            proof.algorithm_id,
            proof.domain_digest,
            proof.root_hash,
            wrong_query,
            wrong_bytes,
            proof.steps,
        ),
        policy,
    )
    assert wrong_result.failure is MerkleProofFailure.WRONG_RESULT

    first = proof.steps[0]
    changed_block = bytearray(first.block.content)
    changed_block[-1] ^= 1
    bad_steps = (
        MerkleProofStep(
            MerkleBlock(first.block.digest, changed_block), first.expanded_child_indexes
        ),
        *proof.steps[1:],
    )
    bad_block = MerkleProof(
        proof.algorithm_id,
        proof.domain_digest,
        proof.root_hash,
        proof.query,
        proof.query_bytes,
        bad_steps,
    )
    assert verify_merkle_proof(bad_block, policy).failure is MerkleProofFailure.INVALID_BLOCK

    missing = MerkleProof(
        proof.algorithm_id,
        proof.domain_digest,
        proof.root_hash,
        proof.query,
        proof.query_bytes,
        proof.steps[:-1],
    )
    assert verify_merkle_proof(missing, policy).failure is MerkleProofFailure.INVALID_EXPANSION

    duplicate = MerkleProof(
        proof.algorithm_id,
        proof.domain_digest,
        proof.root_hash,
        proof.query,
        proof.query_bytes,
        (*proof.steps, proof.steps[0]),
    )
    assert verify_merkle_proof(duplicate, policy).failure is MerkleProofFailure.INVALID_EXPANSION

    alternate_indexes = () if first.expanded_child_indexes else (0,)
    wrong_expansion = MerkleProof(
        proof.algorithm_id,
        proof.domain_digest,
        proof.root_hash,
        proof.query,
        proof.query_bytes,
        (MerkleProofStep(first.block, alternate_indexes), *proof.steps[1:]),
    )
    assert (
        verify_merkle_proof(wrong_expansion, policy).failure is MerkleProofFailure.INVALID_EXPANSION
    )


def test_empty_proofs_and_defensive_byte_ownership() -> None:
    policy = string_policy("python-empty-proof-v1")
    empty = MerkleSearchTree.empty(policy)
    absent = create_merkle_proof(empty, 1)
    ranged = create_merkle_range_proof(empty, -1, 1)
    assert verify_merkle_proof(absent, policy).valid
    assert verify_merkle_proof(ranged, policy).valid

    content = bytearray(b"content")
    block = MerkleBlock(MerkleDigest.hash(content), content)
    content[0] ^= 1
    assert block.content == b"content"
    query = bytearray(absent.query_bytes)
    copied = MerkleProof(
        absent.algorithm_id,
        absent.domain_digest,
        absent.root_hash,
        absent.query,
        query,
        absent.steps,
    )
    query[-1] ^= 1
    assert copied.query_bytes == absent.query_bytes


def test_three_way_merge_distinguishes_deletion_from_present_none() -> None:
    policy = string_policy("python-merge-v1")
    base = MerkleSearchTree.from_entries(((1, "one"), (2, None), (3, "three")), policy)
    left = base.set_item(1, "left").remove(2).set_item(4, "left-only")
    right = base.set_item(1, "right").set_item(2, "present").set_item(5, "right-only")
    unresolved = merge_merkle_trees(base, left, right)
    assert not unresolved.succeeded
    assert unresolved.tree is None
    assert [conflict.key for conflict in unresolved.conflicts] == [1, 2]
    assert not unresolved.conflicts[1].left.present
    assert unresolved.conflicts[1].base.present
    assert unresolved.conflicts[1].base.value is None

    resolved = merge_merkle_trees(
        base,
        left,
        right,
        lambda conflict: (
            MerkleMergeResolution.use_value("combined")
            if conflict.key == 1
            else MerkleMergeResolution.right()
        ),
    )
    assert resolved.succeeded
    assert resolved.conflicts == ()
    assert tuple(resolved.tree or ()) == (
        MerkleEntry(1, "combined"),
        MerkleEntry(2, "present"),
        MerkleEntry(3, "three"),
        MerkleEntry(4, "left-only"),
        MerkleEntry(5, "right-only"),
    )
    assert merge_merkle_trees(base, base, right).tree is right
    assert merge_merkle_trees(base, left, left).tree is left


def test_store_and_immutable_snapshots_support_concurrent_readers() -> None:
    policy = string_policy("python-threaded-store-v1")
    tree = make_tree(policy, 1_025)
    blocks = export_merkle_pack(tree).blocks
    store = InMemoryMerkleBlockStore()

    def publish_and_read(_: int) -> str:
        for block in blocks:
            store.put(block)
        return str(load_merkle_tree(tree.root_hash, policy, store).root_hash)

    with ThreadPoolExecutor(max_workers=8) as executor:
        roots = tuple(executor.map(publish_and_read, range(32)))
    assert set(roots) == {str(tree.root_hash)}
    assert store.count == tree.block_count
    assert store.digests() == tuple(sorted(store.digests()))
