"""Tests for the persistent sparse chunked bit set.

Covers construction sorting and deduplicating indices across word boundaries, enforcement of the
nonnegative signed-32-bit domain, receiver identity for point no-ops, inclusive rank, zero-based
select, all four algebra operations, and contraction of emptied words together with snapshot and
annotation consistency.
"""

from __future__ import annotations

import pytest

from durable7 import PersistentChunkedBitSet


def test_construction_sorts_deduplicates_and_crosses_word_boundaries() -> None:
    bit_set = PersistentChunkedBitSet.from_values([130, 0, 64, 63, 64, 129])
    assert list(bit_set) == [0, 63, 64, 129, 130]
    assert bit_set.count == 5
    assert bit_set.chunk_count == 3


def test_signed_32_bit_domain_is_enforced_for_updates() -> None:
    largest = (1 << 31) - 1
    bit_set = PersistentChunkedBitSet.empty().add(largest)
    assert bit_set.contains(largest)
    assert not bit_set.contains(-1)
    assert bit_set.remove(-1) is bit_set
    with pytest.raises(ValueError):
        bit_set.add(-1)
    with pytest.raises(ValueError):
        bit_set.add(largest + 1)


def test_point_no_ops_return_receiver() -> None:
    bit_set = PersistentChunkedBitSet.from_values([1, 65])
    assert bit_set.add(1) is bit_set
    assert bit_set.remove(2) is bit_set
    assert not bit_set.try_add(1).changed
    assert not bit_set.try_remove(2).changed


def test_rank_is_inclusive() -> None:
    bit_set = PersistentChunkedBitSet.from_values([0, 2, 63, 64, 130])
    assert [bit_set.rank(index) for index in (-1, 0, 1, 2, 63, 64, 129, 130)] == [
        0,
        1,
        1,
        2,
        3,
        4,
        4,
        5,
    ]


def test_select_uses_zero_based_population_rank() -> None:
    bit_set = PersistentChunkedBitSet.from_values([1, 64, 66, 200])
    assert [bit_set.select(rank) for rank in range(4)] == [1, 64, 66, 200]
    assert bit_set.try_select(4) is None
    with pytest.raises(IndexError):
        bit_set.select(-1)


def test_all_four_persistent_algebra_operations() -> None:
    left = PersistentChunkedBitSet.from_values([1, 2, 64, 130])
    right = PersistentChunkedBitSet.from_values([2, 3, 64, 200])
    assert list(left.union(right)) == [1, 2, 3, 64, 130, 200]
    assert list(left.intersect(right)) == [2, 64]
    assert list(left.except_(right)) == [1, 130]
    assert list(left.symmetric_except(right)) == [1, 3, 130, 200]
    assert left.union(PersistentChunkedBitSet.empty()) is left


def test_empty_word_contraction_snapshots_and_annotations() -> None:
    source = PersistentChunkedBitSet.from_values([63, 64])
    branch = source.remove(63)
    assert source.contains(63)
    assert branch.chunk_count == 1
    assert source.validate_structure().pop_count == 2
    assert branch.validate_structure().pop_count == 1
