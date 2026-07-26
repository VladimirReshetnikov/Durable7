"""Tests for the history-independent canonical sorted set.

The defining property under test is convergence: sets built by different insertion and removal
histories must reach one identical topology. Also covers exact keyed rank vectors, rank-hash
stability and coherence with the comparer's equivalence classes, key ownership and minimum key
length, stack safety under colliding priorities, algebra no-ops and root sharing, and consistent
digest publication across concurrent readers.
"""

from __future__ import annotations

import os
import random
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.canonical_sorted_set import (
    CanonicalSortedSet,
    ZipTreeRank,
    ZipTreeRankPolicy,
    stable_rank_hash,
)


def test_canonical_keyed_and_seeded_rank_vectors_are_exact() -> None:
    keyed: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create_keyed(
        bytes(range(32)), rank_hash=lambda _: 0x0102_0304_0506_0708
    )
    assert keyed.rank(0) == ZipTreeRank(
        geometric=1,
        secondary=0x1975_2063_8AF1_F7A6,
        content=0xF31E_BE0A_B983_FF0F,
    )

    seed = 0x0123_4567_89AB_CDEF
    seeded: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(
        seed=seed, rank_hash=lambda _: 0xFEDC_BA98_7654_3210
    )
    assert seeded.rank(0) == ZipTreeRank(
        geometric=0,
        secondary=0x9EFD_AEF0_3F68_C6BD,
        content=0x4DA7_5484_837A_7798,
    )
    assert seeded.seed == seed


def test_default_python_rank_hash_is_stable_and_equivalence_coherent() -> None:
    assert stable_rank_hash("alpha") == 0x353A_4484_8E49_695F
    assert stable_rank_hash(True) == stable_rank_hash(1) == stable_rank_hash(1.0)
    assert stable_rank_hash(-0.0) == stable_rank_hash(0)
    rank_policy: ZipTreeRankPolicy[str] = ZipTreeRankPolicy.create(seed=0x0123_4567_89AB_CDEF)
    rank = rank_policy.rank("alpha")
    assert rank == ZipTreeRank(0, 0x4CD4_2C0F_CFA9_D547, 0x39BB_C03A_DEBB_0445)

    code = (
        "from durable7.finger_tree."
        "canonical_sorted_set import stable_rank_hash; print(stable_rank_hash('alpha'))"
    )
    outputs = []
    for seed in ("1", "8675309"):
        environment = os.environ.copy()
        environment["PYTHONHASHSEED"] = seed
        result = subprocess.run(
            [sys.executable, "-c", code],
            check=True,
            capture_output=True,
            text=True,
            env=environment,
        )
        outputs.append(int(result.stdout.strip()))
    assert outputs == [stable_rank_hash("alpha"), stable_rank_hash("alpha")]

    with pytest.raises(TypeError, match="provide rank_hash"):
        stable_rank_hash(object())
    with pytest.raises(ValueError, match="NaN"):
        stable_rank_hash(float("nan"))


def test_canonical_histories_converge_to_one_topology() -> None:
    policy: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(seed=0x1234_5678_9ABC_DEF0)
    values = list(range(-300, 300))
    baseline = CanonicalSortedSet.from_iterable(values, policy)
    generator = random.Random(20260718)
    for _ in range(8):
        permutation = values.copy()
        generator.shuffle(permutation)
        bulk = CanonicalSortedSet.from_iterable(permutation, policy)
        assert bulk.shape_signature() == baseline.shape_signature()
        assert bulk.content_hash == baseline.content_hash
        assert bulk.validate_structure() == baseline.validate_structure()
        for value in permutation[:50]:
            bulk = bulk.remove(value)
        for value in reversed(permutation[:50]):
            bulk = bulk.add(value)
        assert bulk.shape_signature() == baseline.shape_signature()


@dataclass(frozen=True)
class _Item:
    key: int
    identity: int


def _item_comparator(left: _Item, right: _Item) -> int:
    return left.key - right.key


def test_canonical_representatives_rank_coherence_and_policy_identity() -> None:
    policy = ZipTreeRankPolicy.create(
        comparator=_item_comparator,
        rank_hash=lambda value: value.key,
        seed=42,
    )
    first = _Item(1, 1)
    duplicate = _Item(1, 2)
    basis = CanonicalSortedSet.from_iterable((_Item(0, 0), first, _Item(2, 2)), policy)
    assert basis.add(duplicate) is basis
    lookup = basis.try_get_value(duplicate)
    assert lookup.found and lookup.value is first
    edited = basis.add(_Item(10, 10))
    assert edited.shares_storage_with(basis)
    assert edited.remove(_Item(10, 99)).set_equals(basis)

    with pytest.raises(TypeError, match="explicit comparator"):
        ZipTreeRankPolicy.create(comparator=_item_comparator, seed=1)
    incoherent = ZipTreeRankPolicy.create(
        comparator=_item_comparator,
        rank_hash=lambda value: value.identity,
        seed=1,
    )
    with pytest.raises(ValueError, match="equivalence classes"):
        CanonicalSortedSet.empty(incoherent).add(first).add(duplicate)

    same_material: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(seed=42)
    with pytest.raises(TypeError, match="same rank-policy object"):
        CanonicalSortedSet.from_iterable((1, 2), same_material).union(
            CanonicalSortedSet.from_iterable((1, 2), ZipTreeRankPolicy.create(seed=42))
        )


def test_canonical_key_is_owned_and_short_keys_are_rejected() -> None:
    key = bytearray((index * 7 + 3) & 0xFF for index in range(32))
    retained = bytes(key)
    first: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create_keyed(key)
    key[:] = b"\xff" * 32
    second: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create_keyed(retained)
    values = [value for value in range(-1_000, 1_001) if value % 3]
    left = CanonicalSortedSet.from_iterable(values, first)
    right = CanonicalSortedSet.from_iterable(reversed(values), second)
    assert left.shape_signature() == right.shape_signature()
    assert left.content_hash == right.content_hash
    assert left.set_equals(right)
    with pytest.raises(ValueError, match="at least 32"):
        ZipTreeRankPolicy.create_keyed(bytes(31))


def test_canonical_colliding_priorities_are_stack_safe() -> None:
    count = 1_024
    policy: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(seed=1, rank_hash=lambda _: 0)
    forward = CanonicalSortedSet.from_iterable(range(count), policy)
    reverse = CanonicalSortedSet.from_iterable(reversed(range(count)), policy)
    assert forward.height == count
    assert forward.shape_signature() == reverse.shape_signature()
    assert list(forward) == list(range(count))
    statistics = forward.validate_structure()
    assert statistics.priority_collision_count == count - 1
    removed = forward.remove(count - 1)
    assert removed.height == count - 1
    restored = removed.add(count - 1)
    assert restored.shape_signature() == forward.shape_signature()
    assert restored.content_hash == forward.content_hash


def test_canonical_algebra_noops_sharing_and_receiver_comparator() -> None:
    policy: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(seed=99)
    left = CanonicalSortedSet.from_iterable((1, 2, 4, 8), policy)
    right = CanonicalSortedSet.from_iterable((2, 3, 4, 5), policy)
    empty = CanonicalSortedSet.empty(policy)
    assert list(left.union(right)) == [1, 2, 3, 4, 5, 8]
    assert list(left.intersect(right)) == [2, 4]
    assert list(left.except_(right)) == [1, 8]
    assert left.add(2) is left
    assert left.remove(-1) is left
    assert left.union(left) is left
    assert left.intersect(left) is left
    assert left.except_(empty) is left
    assert empty.clear() is empty
    assert left.is_subset_of((8, 4, 2, 1, 1))
    assert left.is_proper_subset_of((8, 4, 2, 1, 16, 16))
    assert left.is_superset_of((1, 2, 2))
    assert left.is_proper_superset_of((1, 2, 2))
    assert left.overlaps((-1, 4)) and not left.overlaps((-2, -1))

    def insensitive_comparator(a: str, b: str) -> int:
        return (a.casefold() > b.casefold()) - (a.casefold() < b.casefold())

    insensitive_policy = ZipTreeRankPolicy.create(
        comparator=insensitive_comparator,
        rank_hash=lambda value: stable_rank_hash(value.casefold()),
        seed=0x1A51,
    )
    insensitive = CanonicalSortedSet.from_iterable(("alpha",), insensitive_policy)
    sensitive = CanonicalSortedSet.from_iterable(
        ("alpha", "ALPHA"), ZipTreeRankPolicy.create(seed=0x5E51)
    )
    assert insensitive.set_equals(sensitive)
    assert not sensitive.set_equals(insensitive)


def test_canonical_digest_publication_is_consistent_across_readers() -> None:
    value = CanonicalSortedSet.from_iterable(range(4_096), ZipTreeRankPolicy.create(seed=31_337))
    with ThreadPoolExecutor(max_workers=8) as executor:
        hashes = list(executor.map(lambda _: value.content_hash, range(512)))
    assert len(set(hashes)) == 1
    assert value.validate_structure().count == 4_096


@settings(max_examples=80, deadline=None)
@given(
    st.lists(
        st.tuples(st.integers(min_value=-100, max_value=100), st.booleans()),
        max_size=300,
    )
)
def test_canonical_random_histories_match_set_model(
    commands: list[tuple[int, bool]],
) -> None:
    policy: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(seed=99)
    actual = CanonicalSortedSet.empty(policy)
    expected: set[int] = set()
    snapshots: list[tuple[CanonicalSortedSet[int], set[int]]] = []
    for index, (value, remove) in enumerate(commands):
        if remove:
            actual = actual.remove(value)
            expected.discard(value)
        else:
            actual = actual.add(value)
            expected.add(value)
        if index % 47 == 0:
            snapshots.append((actual, expected.copy()))
    assert list(actual) == sorted(expected)
    assert actual.validate_structure().count == len(expected)
    for snapshot, model in snapshots:
        assert list(snapshot) == sorted(model)
        snapshot.validate_structure()
