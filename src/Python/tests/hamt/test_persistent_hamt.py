"""CHAMP, transient, and lock-coordinated snapshot tests."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from threading import Lock

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.hamt import (
    ConcurrentHashTrie,
    DuplicateKeyError,
    HashPolicy,
    PersistentHashMap,
    PersistentHashSet,
    TransientConsumedError,
    create_hash_policy,
    default_hash_policy,
)


def test_updates_preserve_versions_and_no_op_roots() -> None:
    empty = PersistentHashMap[str, int].empty()
    one = empty.put("a", 1)
    two = one.put("b", 2)
    replaced = two.put("a", 3)

    assert empty.get("a") is None
    assert one.get("a") == 1
    assert two.get("a") == 1
    assert replaced.get("a") == 3
    assert two.put("a", 1) is two
    assert two.remove("missing") is two
    assert one.shares_root_with(one)
    assert not one.shares_root_with(two)


def test_duplicate_insertion_rejects_without_a_successor() -> None:
    map_value = PersistentHashMap[str, int].empty().put("a", 1)
    result = map_value.try_add("a", 2)
    assert not result.added
    assert result.value is map_value
    with pytest.raises(DuplicateKeyError):
        map_value.add("a", 2)


def test_collision_buckets_retain_entries_and_exact_removal_representatives() -> None:
    policy: HashPolicy[int] = create_hash_policy(
        lambda _value: 0, lambda left, right: left == right
    )
    map_value = PersistentHashMap[int, int].empty(policy).put(1, 10).put(2, 20).put(3, 30)
    assert map_value.get(1) == 10
    assert map_value.get(2) == 20
    removed = map_value.try_remove_entry(2)
    assert removed is not None
    assert (removed.entry.key, removed.entry.value) == (2, 20)
    assert [(entry.key, entry.value) for entry in removed.map] == [(1, 10), (3, 30)]


@dataclass(frozen=True)
class _Key:
    text: str
    identity: int


def test_equivalent_replacement_retains_the_original_key() -> None:
    policy: HashPolicy[_Key] = create_hash_policy(
        lambda key: ord(key.text[0]), lambda left, right: left.text == right.text
    )
    original = _Key("x", 1)
    replacement = _Key("x", 2)
    map_value = PersistentHashMap.from_items([(original, 10), (replacement, 20)], policy)
    assert map_value.size == 1
    entry = map_value.get_entry(replacement)
    assert entry is not None
    assert entry.key is original
    assert entry.value == 20


def test_typed_diff_distinguishes_absence_from_stored_none() -> None:
    left = PersistentHashMap[str, int | None].from_items([("a", None), ("b", 2)])
    right = left.remove("a").put("b", 3).put("c", None)
    differences = sorted(left.diff(right), key=lambda item: item.key)
    assert [(item.kind, item.key, item.before, item.after) for item in differences] == [
        ("removed", "a", None, None),
        ("changed", "b", 2, 3),
        ("added", "c", None, None),
    ]
    assert left.contains_key("a")
    assert not right.contains_key("a")


@settings(max_examples=200)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=-100, max_value=100),
            st.integers(),
            st.booleans(),
        ),
        max_size=500,
    )
)
def test_random_histories_agree_with_a_mutable_model(
    operations: list[tuple[int, int, bool]],
) -> None:
    actual = PersistentHashMap[int, int].empty()
    expected: dict[int, int] = {}
    for key, value, remove in operations:
        if remove:
            actual = actual.remove(key)
            expected.pop(key, None)
        else:
            actual = actual.put(key, value)
            expected[key] = value
    assert actual.size == len(expected)
    assert {entry.key: entry.value for entry in actual} == expected


def test_map_algebra_equality_and_policy_identity() -> None:
    policy: HashPolicy[int] = default_hash_policy()
    left = PersistentHashMap.from_items([(1, "one"), (2, "two")], policy)
    right = PersistentHashMap.from_items([(2, "TWO"), (3, "three")], policy)
    assert {entry.key: entry.value for entry in left.union(right)} == {
        1: "one",
        2: "TWO",
        3: "three",
    }
    assert {entry.key for entry in left.intersect(right)} == {2}
    assert {entry.key for entry in left.except_(right)} == {1}
    assert {entry.key for entry in left.symmetric_except(right)} == {1, 3}
    assert left.map_equals(PersistentHashMap.from_items([(2, "two"), (1, "one")], policy))

    incompatible = create_hash_policy(hash, lambda first, second: first == second)
    with pytest.raises(TypeError):
        left.union(PersistentHashMap.from_items([(4, "four")], incompatible))


def test_set_algebra_relations_and_stored_none() -> None:
    left = PersistentHashSet.from_values([1, 2, 3])
    assert set(left.union([3, 4, 5])) == {1, 2, 3, 4, 5}
    assert set(left.intersect([2, 3, 9])) == {2, 3}
    assert set(left.except_([1, 3])) == {2}
    assert set(left.symmetric_except([3, 4])) == {1, 2, 4}
    assert left.is_proper_superset_of([1, 3])
    assert left.is_proper_subset_of([1, 2, 3, 4])
    assert left.is_superset_of([1, 2])
    assert left.is_subset_of([1, 2, 3])
    assert left.overlaps([9, 3])
    assert left.set_equals([3, 2, 1, 1])

    nullable = PersistentHashSet.from_values([None, "a"])
    assert nullable.contains(None)
    removed = nullable.try_remove(None)
    assert removed is not None and removed.value is None
    assert not removed.set.contains(None)


def test_default_policy_gives_unhashable_objects_identity_semantics() -> None:
    first = [1, 2]
    equal_but_distinct = [1, 2]
    map_value = PersistentHashMap[list[int], str].empty().put(first, "first")
    assert map_value.get(first) == "first"
    assert map_value.get(equal_but_distinct) is None
    assert map_value.put(first, "first") is map_value


def test_clean_and_no_op_transients_republish_the_exact_source() -> None:
    source = PersistentHashMap.from_items([("a", 1), ("b", 2)])
    assert source.to_transient().persist() is source

    transient = source.to_transient()
    iterator = iter(transient)
    transient.set("a", 1)
    assert not transient.remove("missing")
    assert not transient.try_add("a", 9)
    assert list(iterator)
    assert transient.persist() is source


def test_transient_publication_consumes_and_real_edits_invalidate_iterators() -> None:
    transient = PersistentHashMap[int, int].create_transient()
    for key in range(100):
        transient.set(key, key * 2)
    iterator = iter(transient)
    next(iterator)
    transient.set(100, 200)
    with pytest.raises(RuntimeError, match="modified"):
        next(iterator)
    persistent = transient.persist()
    assert persistent.size == 101
    with pytest.raises(TransientConsumedError):
        transient.get(1)
    with pytest.raises(TransientConsumedError):
        transient.persist()


def test_set_transients_preserve_snapshots_and_representatives() -> None:
    source = PersistentHashSet.from_values([1, 2, 3])
    transient = source.to_transient()
    assert not transient.add(3)
    assert transient.add(4)
    assert transient.remove(1)
    published = transient.persist()
    assert set(source) == {1, 2, 3}
    assert set(published) == {2, 3, 4}


def test_transient_callback_failure_does_not_install_a_partial_successor() -> None:
    fail = False

    def key_hash(key: int) -> int:
        if fail:
            raise RuntimeError("boom")
        return key

    transient = (
        PersistentHashMap[int, int]
        .empty(create_hash_policy(key_hash, lambda left, right: left == right))
        .put(1, 1)
        .to_transient()
    )
    fail = True
    with pytest.raises(RuntimeError, match="boom"):
        transient.set(2, 2)
    fail = False
    assert [(entry.key, entry.value) for entry in transient.persist()] == [(1, 1)]


def test_concurrent_snapshot_is_stable_and_generation_counts_publications() -> None:
    trie = ConcurrentHashTrie[str, int]()
    assert trie.try_add("alpha", 1)
    assert not trie.try_add("alpha", 2)
    snapshot = trie.snapshot()
    assert trie.compute("alpha", lambda _key: 0, lambda _key, value: value + 1) == 2
    trie.set("beta", 2)
    trie.set("beta", 2)
    assert snapshot.get("alpha") == 1
    assert snapshot.get("beta") is None
    assert snapshot.to_persistent_hash_map().size == 1
    assert trie.generation == 3


def test_concurrent_facade_serializes_writers_and_factory_publication() -> None:
    trie = ConcurrentHashTrie[int, int]()
    with ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(lambda key: trie.set(key, key * 2), range(1_000)))
    assert trie.size == 1_000
    assert trie.generation == 1_000
    assert all(trie.get(key) == key * 2 for key in range(1_000))

    calls = 0
    calls_lock = Lock()

    def factory(key: int) -> int:
        nonlocal calls
        with calls_lock:
            calls += 1
        return key * 3

    second = ConcurrentHashTrie[int, int]()
    with ThreadPoolExecutor(max_workers=8) as executor:
        results = list(executor.map(lambda _index: second.get_or_put(7, factory), range(64)))
    assert results == [21] * 64
    assert calls == 1
    assert second.generation == 1
