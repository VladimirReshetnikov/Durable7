"""Factory updates, bulk construction, transient relations, and hash-bag tests."""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.hamt import (
    HashMapBulkBuilder,
    HashPolicy,
    PersistentHashBag,
    PersistentHashMap,
    PersistentHashSet,
    TransientConsumedError,
    create_hash_policy,
    default_hash_policy,
)

_INT_MAX = (1 << 31) - 1


@dataclass(frozen=True, slots=True)
class _Key:
    text: str
    identity: int


@dataclass(frozen=True, slots=True, eq=False)
class _Value:
    semantic: int
    identity: int

    def __eq__(self, other: object) -> bool:
        return isinstance(other, _Value) and self.semantic == other.semantic


@dataclass(frozen=True, slots=True, eq=False)
class _EqualityFailingValue:
    identity: int

    def __eq__(self, other: object) -> bool:
        raise ValueError("value equality failure")


class _CountingPolicy(HashPolicy[_Key]):
    def __init__(self) -> None:
        self.hash_calls = 0
        self.equivalent_calls = 0

    def hash(self, key: _Key) -> int:
        self.hash_calls += 1
        return sum(ord(character) for character in key.text.casefold())

    def equivalent(self, left: _Key, right: _Key) -> bool:
        self.equivalent_calls += 1
        return left.text.casefold() == right.text.casefold()

    def reset(self) -> None:
        self.hash_calls = 0
        self.equivalent_calls = 0


def test_factory_updates_select_one_branch_and_retain_representatives() -> None:
    policy = _CountingPolicy()
    stored_key = _Key("alpha", 1)
    lookup_key = _Key("ALPHA", 2)
    stored_value = _Value(10, 1)
    source = PersistentHashMap[_Key, _Value].empty(policy).put(stored_key, stored_value)

    add_calls = 0

    def unexpected_add(_key: _Key) -> _Value:
        nonlocal add_calls
        add_calls += 1
        return _Value(99, 99)

    policy.reset()
    hit = source.get_or_add(lookup_key, unexpected_add)
    assert hit.map is source
    assert hit.value is stored_value
    assert add_calls == 0
    assert (policy.hash_calls, policy.equivalent_calls) == (1, 1)

    observed: tuple[_Key, _Value] | None = None

    def equal_update(key: _Key, value: _Value) -> _Value:
        nonlocal observed
        observed = (key, value)
        return _Value(value.semantic, 2)

    policy.reset()
    equal = source.add_or_update(lookup_key, unexpected_add, equal_update)
    assert equal.map is source
    assert equal.value is stored_value
    assert observed == (lookup_key, stored_value)
    assert add_calls == 0
    assert (policy.hash_calls, policy.equivalent_calls) == (1, 1)

    changed = source.add_or_update(
        lookup_key,
        unexpected_add,
        lambda key, value: _Value(value.semantic + key.identity, 3),
    )
    changed_entry = changed.map.get_entry(lookup_key)
    assert changed_entry is not None
    assert changed_entry.key is stored_key
    assert changed_entry.value is changed.value
    assert changed.value.semantic == 12
    assert source.get_entry(lookup_key) is not None
    assert source.get_entry(lookup_key).value is stored_value  # type: ignore[union-attr]


def test_factory_updates_validate_before_hash_and_publish_nothing_on_failure() -> None:
    policy = _CountingPolicy()
    key = _Key("x", 1)
    source = PersistentHashMap[_Key, int].empty(policy).put(key, 10)
    policy.reset()

    with pytest.raises(TypeError, match="callable"):
        source.get_or_add(key, None)  # type: ignore[arg-type]
    with pytest.raises(TypeError, match="callable"):
        source.add_or_update(key, None, lambda _key, value: value)  # type: ignore[arg-type]
    with pytest.raises(TypeError, match="callable"):
        source.add_or_update(key, lambda _key: 0, None)  # type: ignore[arg-type]
    assert policy.hash_calls == 0

    def fail_update(_key: _Key, _value: int) -> int:
        raise RuntimeError("factory failed")

    with pytest.raises(RuntimeError, match="factory failed"):
        source.add_or_update(key, lambda _key: 0, fail_update)
    assert source[key] == 10

    missing = _Key("missing", 2)
    with pytest.raises(RuntimeError, match="factory failed"):
        source.get_or_add(
            missing,
            lambda _key: (_ for _ in ()).throw(RuntimeError("factory failed")),
        )
    assert not source.contains_key(missing)


def test_factory_value_equality_failures_are_atomic_for_every_node_shape() -> None:
    collision_policy: HashPolicy[int] = create_hash_policy(
        lambda _key: 0, lambda left, right: left == right
    )
    cases: tuple[tuple[HashPolicy[int], list[int], int], ...] = (
        (default_hash_policy(), [7], 7),
        (collision_policy, [7, 8, 9], 8),
        (default_hash_policy(), [0, 32, 1], 32),
    )

    for policy, keys, target in cases:
        stored_values = {key: _EqualityFailingValue(key) for key in keys}
        source = PersistentHashMap.from_items(
            ((key, stored_values[key]) for key in keys),
            policy,
        )

        with pytest.raises(ValueError, match="value equality failure"):
            source.add_or_update(
                target,
                lambda _key: _EqualityFailingValue(-1),
                lambda key, _stored: _EqualityFailingValue(1000 + key),
            )

        assert source.size == len(keys)
        retained = source.get_entry(target)
        assert retained is not None
        assert retained.value is stored_values[target]
        assert (
            source.add_or_update(
                target,
                lambda _key: _EqualityFailingValue(-1),
                lambda _key, stored: stored,
            ).map
            is source
        )


def test_bulk_builder_value_equality_failures_are_atomic_for_every_node_shape() -> None:
    collision_policy: HashPolicy[int] = create_hash_policy(
        lambda _key: 0, lambda left, right: left == right
    )
    cases: tuple[tuple[HashPolicy[int], list[int], int], ...] = (
        (default_hash_policy(), [7], 7),
        (collision_policy, [7, 8, 9], 8),
        (default_hash_policy(), [0, 32, 1], 32),
    )

    for policy, keys, target in cases:
        stored_values = {key: _EqualityFailingValue(key) for key in keys}
        builder = HashMapBulkBuilder[int, _EqualityFailingValue](policy)
        builder.set_items((key, stored_values[key]) for key in keys)
        before = builder.to_immutable()

        with pytest.raises(ValueError, match="value equality failure"):
            builder.set_item(target, _EqualityFailingValue(1000 + target))

        after = builder.to_immutable()
        assert builder.size == len(keys)
        before_entry = before.get_entry(target)
        after_entry = after.get_entry(target)
        assert before_entry is not None
        assert after_entry is not None
        assert before_entry.value is stored_values[target]
        assert after_entry.value is stored_values[target]


def test_factory_updates_cover_collisions_branches_and_nullable_values() -> None:
    collision_policy: HashPolicy[int] = create_hash_policy(
        lambda _key: 0, lambda left, right: left == right
    )
    source = PersistentHashMap[int, int | None].empty(collision_policy)
    for key in range(12):
        source = source.put(key, None if key == 7 else key)

    nullable_hit = source.get_or_add(7, lambda _key: 700)
    assert nullable_hit.map is source
    assert nullable_hit.value is None

    changed = source.add_or_update(8, lambda _key: -1, lambda _key, value: value + 100)  # type: ignore[operator]
    assert changed.value == 108
    assert changed.map[8] == 108

    spread = PersistentHashMap[int, int].from_items([(0, 0), (32, 32), (1, 1), (1 << 30, 9)])
    added = spread.get_or_add(64, lambda key: key * 2)
    assert added.value == 128
    assert added.map[64] == 128
    assert not spread.contains_key(64)


def test_bulk_builder_freezes_detached_reusable_snapshots() -> None:
    policy = _CountingPolicy()
    builder = HashMapBulkBuilder[_Key, _Value](policy)
    first_key = _Key("x", 1)
    equal_key = _Key("X", 2)
    first_value = _Value(10, 1)
    equal_value = _Value(10, 2)

    builder.set_item(first_key, first_value)
    builder.set_item(equal_key, equal_value)
    assert builder.size == 1
    assert not builder.is_empty
    first = builder.to_immutable()
    first_entry = first.get_entry(equal_key)
    assert first_entry is not None
    assert first_entry.key is first_key
    assert first_entry.value is first_value

    builder.set_items(
        [
            (_Key("x", 3), _Value(20, 3)),
            (_Key("y", 4), _Value(30, 4)),
        ]
    )
    second = builder.to_immutable()
    assert builder.size == 2
    assert first.size == 1 and first_entry.value.semantic == 10
    assert second.size == 2 and second[equal_key].semantic == 20
    assert not first.shares_root_with(second)
    assert second.policy is policy

    routed = PersistentHashMap.from_items(
        [
            (first_key, first_value),
            (equal_key, equal_value),
            (_Key("x", 5), _Value(40, 5)),
        ],
        policy,
    )
    routed_entry = routed.get_entry(equal_key)
    assert routed_entry is not None
    assert routed_entry.key is first_key
    assert routed_entry.value.semantic == 40


def test_bulk_builder_splits_keys_at_the_final_hash_level() -> None:
    policy: HashPolicy[int] = create_hash_policy(lambda key: key, lambda left, right: left == right)
    items = [(0, "zero"), (1 << 30, "one"), (2 << 30, "two"), (3 << 30, "three")]
    builder = PersistentHashMap[int, str].create_bulk_builder(policy)
    builder.set_items(items)
    frozen = builder.to_immutable()

    assert frozen.size == len(items)
    assert [(key, frozen[key]) for key, _value in items] == items


def test_bulk_builder_routes_set_construction_and_intersection() -> None:
    policy: HashPolicy[str] = create_hash_policy(
        lambda value: sum(ord(character) for character in value.casefold()),
        lambda left, right: left.casefold() == right.casefold(),
    )
    first = PersistentHashSet.from_values(["A", "a", "b", "c"], policy)
    assert first.size == 3
    assert first.get("a") == "A"

    intersection = first.intersect(["C", "c", "A"])
    assert intersection.set_equals(["a", "c"])
    assert intersection.get("a") == "A"
    assert intersection.get("c") == "c"


@settings(max_examples=100)
@given(
    st.lists(
        st.tuples(st.integers(-50, 50), st.integers()),
        max_size=300,
    )
)
def test_bulk_builder_matches_incremental_collision_heavy_maps(
    items: list[tuple[int, int]],
) -> None:
    policy: HashPolicy[int] = create_hash_policy(lambda _key: 1, lambda left, right: left == right)
    builder = PersistentHashMap[int, int].create_bulk_builder(policy)
    incremental = PersistentHashMap[int, int].empty(policy)
    for key, value in items:
        builder.set_item(key, value)
        incremental = incremental.put(key, value)
    frozen = builder.to_immutable()
    assert frozen.map_equals(incremental)
    assert frozen.policy is policy


def test_transient_set_relations_are_read_only_and_lifecycle_checked() -> None:
    policy: HashPolicy[str] = create_hash_policy(
        lambda value: sum(ord(character) for character in value.casefold()),
        lambda left, right: left.casefold() == right.casefold(),
    )
    transient = PersistentHashSet.from_values(["A", "b", "c"], policy).to_transient()
    iterator = iter(transient)
    assert transient.is_subset_of(["a", "B", "C", "d"])
    assert transient.is_proper_subset_of(["a", "b", "c", "d"])
    assert transient.is_superset_of(["a", "A", "b"])
    assert transient.is_proper_superset_of(["a", "b"])
    assert transient.overlaps(["z", "C"])
    assert transient.set_equals(["c", "B", "a", "A"])
    assert list(iterator)

    transient.persist()
    with pytest.raises(TransientConsumedError):
        transient.set_equals([])


def test_hash_bag_construction_queries_iteration_and_canonical_empty() -> None:
    policy = _CountingPolicy()
    first = _Key("alpha", 1)
    equal = _Key("ALPHA", 2)
    second = _Key("beta", 3)
    bag = PersistentHashBag.from_values([first, equal, second, first], policy)

    assert bag.distinct_count == 2
    assert bag.total_count == 4
    assert bag.count_of(equal) == 3
    recovered = bag.get_entry(equal)
    assert recovered is not None and recovered.value is first and recovered.count == 3
    assert bag.policy is policy

    entries = list(bag.entries())
    assert list(bag.distinct_items()) == [entry.value for entry in entries]
    expanded = list(bag)
    expected: list[_Key] = []
    for entry in entries:
        expected.extend([entry.value] * entry.count)
    assert expanded == expected == bag.to_list()
    for forbidden in (
        "size",
        "to_transient",
        "to_builder",
        "symmetric_except",
        "set_equals",
        "validate_structure",
        "shares_root_with",
    ):
        assert not hasattr(bag, forbidden)
    with pytest.raises(TypeError):
        len(bag)  # type: ignore[arg-type]

    assert PersistentHashBag.empty() is PersistentHashBag.empty(default_hash_policy())
    assert PersistentHashBag.from_values([]) is PersistentHashBag.empty()
    default_nonempty = PersistentHashBag.from_values([1])
    assert default_nonempty.clear() is PersistentHashBag.empty()


def test_hash_bag_point_updates_validate_counts_and_preserve_failure_atomicity() -> None:
    policy = _CountingPolicy()
    value = _Key("x", 1)
    bag = PersistentHashBag[_Key].empty(policy)
    policy.reset()
    assert bag.add_copies(value, 0) is bag
    assert bag.remove_copies(value, 0) is bag
    with pytest.raises(ValueError):
        bag.add_copies(value, -1)
    with pytest.raises(OverflowError):
        bag.add_copies(value, _INT_MAX + 1)
    assert policy.hash_calls == 0

    maximum = bag.add_copies(value, _INT_MAX)
    with pytest.raises(OverflowError):
        maximum.add(value)
    assert maximum.count_of(value) == _INT_MAX
    assert maximum.remove_copies(_Key("missing", 2), 1) is maximum
    reduced = maximum.remove_copies(value, _INT_MAX - 2)
    assert reduced.count_of(value) == 2
    assert reduced.remove_copies(value, 20).is_empty
    assert reduced.remove_all(_Key("missing", 3)) is reduced


def test_hash_bag_algebra_normalizes_policy_and_preserves_representatives() -> None:
    policy = _CountingPolicy()
    receiver_a = _Key("a", 1)
    receiver_b = _Key("b", 2)
    argument_a = _Key("A", 3)
    argument_c = _Key("c", 4)
    receiver = PersistentHashBag[_Key].empty(policy).add_copies(receiver_a, 2).add(receiver_b)
    argument = (
        PersistentHashBag[_Key].empty(policy).add_copies(argument_a, 3).add_copies(argument_c, 4)
    )

    union = receiver.union(argument)
    assert (union.count_of(receiver_a), union.count_of(receiver_b), union.count_of(argument_c)) == (
        3,
        1,
        4,
    )
    assert union.get_entry(argument_a).value is receiver_a  # type: ignore[union-attr]
    assert union.get_entry(argument_c).value is argument_c  # type: ignore[union-attr]

    intersection = receiver.intersect(argument)
    assert intersection.count_of(receiver_a) == 2
    assert intersection.get_entry(argument_a).value is receiver_a  # type: ignore[union-attr]
    assert intersection.count_of(receiver_b) == 0
    difference = receiver.except_(argument)
    assert difference.count_of(receiver_a) == 0 and difference.count_of(receiver_b) == 1
    summed = receiver.sum(argument)
    assert summed.count_of(receiver_a) == 5
    assert summed.get_entry(argument_a).value is receiver_a  # type: ignore[union-attr]

    assert receiver.union(receiver) is receiver
    assert receiver.intersect(receiver) is receiver
    assert receiver.except_(receiver).is_empty
    lower = PersistentHashBag[_Key].empty(policy).add(receiver_a)
    assert receiver.union(lower) is receiver


def test_hash_bag_eager_foreign_policy_normalization_checks_collapsed_counts() -> None:
    receiver_policy: HashPolicy[str] = create_hash_policy(
        lambda value: sum(ord(character) for character in value.casefold()),
        lambda left, right: left.casefold() == right.casefold(),
    )
    argument_policy: HashPolicy[str] = create_hash_policy(
        lambda value: sum(ord(character) for character in value),
        lambda left, right: left == right,
    )
    argument = PersistentHashBag[str].empty(argument_policy).add_copies("A", _INT_MAX).add("a")
    receiver = PersistentHashBag[str].empty(receiver_policy)
    with pytest.raises(OverflowError):
        receiver.intersect(argument)
    assert receiver.is_empty
    assert argument.count_of("A") == _INT_MAX and argument.count_of("a") == 1


def test_hash_bag_successful_foreign_policy_normalization_preserves_precedence() -> None:
    receiver_policy = _CountingPolicy()
    argument_policy: HashPolicy[_Key] = create_hash_policy(
        lambda value: value.identity,
        lambda left, right: left is right,
    )
    receiver_value = _Key("alpha", 1)
    argument_first = _Key("ALPHA", 2)
    argument_second = _Key("alpha", 3)
    argument = (
        PersistentHashBag[_Key]
        .empty(argument_policy)
        .add_copies(argument_first, 2)
        .add_copies(argument_second, 3)
    )
    first_in_argument_order = next(argument.entries()).value

    normalized = PersistentHashBag[_Key].empty(receiver_policy).union(argument)
    normalized_entry = normalized.get_entry(receiver_value)
    assert normalized_entry is not None
    assert normalized_entry.count == 5
    assert normalized_entry.value is first_in_argument_order

    receiver = PersistentHashBag[_Key].empty(receiver_policy).add(receiver_value)
    union = receiver.union(argument)
    union_entry = union.get_entry(argument_first)
    assert union_entry is not None
    assert union_entry.count == 5
    assert union_entry.value is receiver_value

    summed = receiver.sum(argument)
    summed_entry = summed.get_entry(argument_second)
    assert summed_entry is not None
    assert summed_entry.count == 6
    assert summed_entry.value is receiver_value


@settings(max_examples=100)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=-10, max_value=10),
            st.integers(min_value=0, max_value=5),
            st.booleans(),
        ),
        max_size=200,
    )
)
def test_hash_bag_generated_histories_match_counter(
    operations: list[tuple[int, int, bool]],
) -> None:
    actual = PersistentHashBag[int].empty()
    expected: Counter[int] = Counter()
    retained: list[tuple[PersistentHashBag[int], Counter[int]]] = []
    for value, count, remove in operations:
        retained.append((actual, expected.copy()))
        if remove:
            actual = actual.remove_copies(value, count)
            expected[value] = max(0, expected[value] - count)
            if expected[value] == 0:
                del expected[value]
        else:
            actual = actual.add_copies(value, count)
            if count != 0:
                expected[value] += count

    assert Counter(actual) == expected
    assert actual.distinct_count == len(expected)
    assert actual.total_count == sum(expected.values())
    assert all(Counter(snapshot) == model for snapshot, model in retained)
