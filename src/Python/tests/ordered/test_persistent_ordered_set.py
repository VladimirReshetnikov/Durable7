from __future__ import annotations

from collections.abc import Callable, Iterable, Iterator, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from inspect import getsource
from typing import TypeVar, cast

import pytest

from vladimir_reshetnikov.data_structures import PersistentOrderedSet
from vladimir_reshetnikov.data_structures.hamt import (
    HashPolicy,
    create_hash_policy,
    default_hash_policy,
)
from vladimir_reshetnikov.data_structures.ordered import (
    OrderedSetValueResult,
)
from vladimir_reshetnikov.data_structures.ordered import (
    persistent_ordered_set as _ordered_module,
)


@dataclass(frozen=True, slots=True, eq=False)
class _Representative:
    equivalence_class: int
    name: str


def _representative_policy(hash_buckets: int = 1) -> HashPolicy[_Representative | None]:
    return create_hash_policy(
        lambda value: 0 if value is None else value.equivalence_class % hash_buckets,
        lambda left, right: (
            (None if left is None else left.equivalence_class)
            == (None if right is None else right.equivalence_class)
        ),
    )


def _reference_policy() -> HashPolicy[_Representative]:
    return create_hash_policy(id, lambda left, right: left is right)


class _SwitchablePolicy(HashPolicy[_Representative]):
    def __init__(self, hash_buckets: int = 1) -> None:
        self.hash_buckets = hash_buckets
        self.throw_hash = False
        self.throw_equality = False
        self.hash_calls = 0
        self.equality_calls = 0

    def hash(self, key: _Representative) -> int:
        self.hash_calls += 1
        if self.throw_hash:
            raise RuntimeError("hash failure")
        return key.equivalence_class % self.hash_buckets

    def equivalent(self, left: _Representative, right: _Representative) -> bool:
        self.equality_calls += 1
        if self.throw_equality:
            raise RuntimeError("equality failure")
        return left.equivalence_class == right.equivalence_class

    def reset(self) -> None:
        self.hash_calls = 0
        self.equality_calls = 0


T = TypeVar("T")


def _assert_representatives(
    expected: Sequence[T],
    actual: PersistentOrderedSet[T],
) -> None:
    assert len(actual) == len(expected)
    assert actual.is_empty == (not expected)
    observed = actual.to_list()
    assert all(left is right for left, right in zip(expected, observed, strict=True))
    for index, value in enumerate(expected):
        assert actual[index] is value
        assert actual.get_at(index) is value
        assert actual.index_of(value) == index
        assert actual.contains(value)
        lookup = actual.try_get_value(value)
        assert lookup.found and lookup.value is value
    if expected:
        assert actual.first is expected[0]
        assert actual.last is expected[-1]
    actual._validate_invariants()


def test_construction_canonical_empty_nullable_lookup_and_first_representatives() -> None:
    default_policy: HashPolicy[object] = default_hash_policy()
    assert PersistentOrderedSet.empty() is PersistentOrderedSet.empty(default_policy)
    assert PersistentOrderedSet.from_values([]) is PersistentOrderedSet.empty()

    policy = _representative_policy()
    custom = PersistentOrderedSet.empty(policy)
    assert custom is not PersistentOrderedSet.empty()
    assert custom.policy is policy

    first = _Representative(1, "first")
    duplicate = _Representative(1, "duplicate")
    second = _Representative(2, "second")
    actual = PersistentOrderedSet.from_values([None, first, duplicate, second, None], policy)
    _assert_representatives([None, first, second], actual)
    assert actual.try_get_value(duplicate) == OrderedSetValueResult(True, first)
    missing = _Representative(9, "missing")
    miss = actual.try_get_value(missing)
    assert not miss.found and miss.value is missing
    assert actual.index_of(missing) == -1
    assert actual.get(None) is None
    assert actual.try_get_value(None).found

    moved = actual.move_to_last(None)
    _assert_representatives([first, second, None], moved)
    _assert_representatives([first, second], moved.remove(None))
    _assert_representatives([None, first, second], actual)

    failure = RuntimeError("construction enumeration failure")

    def failing_values() -> Iterator[int]:
        yield 1
        yield 2
        raise failure

    with pytest.raises(RuntimeError) as captured:
        PersistentOrderedSet.from_values(failing_values())
    assert captured.value is failure


def test_add_remove_and_explicit_movement_preserve_representatives_and_versions() -> None:
    policy = cast("HashPolicy[_Representative]", _representative_policy())
    first = _Representative(1, "first")
    middle = _Representative(2, "middle")
    last = _Representative(3, "last")
    source = PersistentOrderedSet.from_values([first, middle, last], policy)
    equal_middle = _Representative(2, "equal-middle")

    assert source.add(equal_middle) is source
    assert source.add_first(equal_middle) is source
    assert source.insert(0, equal_middle) is source
    assert source.insert(len(source), equal_middle) is source

    appended = _Representative(4, "appended")
    prepended = _Representative(5, "prepended")
    inserted = _Representative(6, "inserted")
    _assert_representatives([first, middle, last, appended], source.add(appended))
    _assert_representatives([prepended, first, middle, last], source.add_first(prepended))
    _assert_representatives([first, inserted, middle, last], source.insert(1, inserted))

    assert source.move_to_first(first) is source
    assert source.move_to_last(last) is source
    _assert_representatives([middle, first, last], source.move_to_first(equal_middle))
    _assert_representatives([first, last, middle], source.move_to_last(equal_middle))
    _assert_representatives([middle, last, first], source.move_to(2, first))

    missing = _Representative(99, "missing")
    assert source.remove(missing) is source
    removal = source.try_remove(missing)
    assert not removal.removed and removal.set is source
    successful = source.try_remove(equal_middle)
    assert successful.removed
    _assert_representatives([first, last], successful.set)
    _assert_representatives([first, last], source.remove(middle))
    _assert_representatives([first, last], source.remove_at(1))
    _assert_representatives([middle, last], source.remove_first())
    _assert_representatives([first, middle], source.remove_last())
    _assert_representatives([first, middle, last], source)

    with pytest.raises(KeyError):
        source.move_to_first(missing)
    with pytest.raises(IndexError):
        source.move_to(-1, middle)
    with pytest.raises(IndexError):
        source.insert(4, inserted)


def test_all_small_movement_pairs_use_final_result_indexes() -> None:
    for size in range(1, 9):
        source = PersistentOrderedSet.from_values(range(size))
        for old_index in range(size):
            for final_index in range(size):
                expected = list(range(size))
                value = expected.pop(old_index)
                expected.insert(final_index, value)
                actual = source.move_to(final_index, value)
                assert actual.to_list() == expected
                assert (actual is source) == (old_index == final_index)
                actual._validate_invariants()
        assert source.to_list() == list(range(size))


def test_repeated_midpoint_insertions_cross_relabels_and_preserve_branches() -> None:
    source = PersistentOrderedSet.from_values([-2, -1])
    inserted: list[int] = []
    for value in range(80):
        source = source.insert(1, value)
        inserted.append(value)
    source_snapshot = source.to_list()

    left = source
    right = source
    for value in range(1000, 1040):
        left = left.insert(1, value)
    for value in range(2000, 2040):
        right = right.insert(1, value)

    assert source.to_list() == source_snapshot == [-2, *reversed(inserted), -1]
    assert left[1] == 1039 and right[1] == 2039
    assert not left.contains(2039) and not right.contains(1039)
    source._validate_invariants()
    left._validate_invariants()
    right._validate_invariants()


def test_ranges_clear_reverse_and_boundary_identities() -> None:
    policy = cast("HashPolicy[_Representative]", _representative_policy(4))
    values = [_Representative(index, f"value-{index}") for index in range(10)]
    source = PersistentOrderedSet.from_values(values, policy)
    for index in range(len(source) + 1):
        for count in range(len(source) - index + 1):
            actual = source.get_range(index, count)
            _assert_representatives(values[index : index + count], actual)
            assert actual.policy is policy
            if index == 0 and count == len(source):
                assert actual is source

    assert source.take(len(source)) is source
    assert source.drop(0) is source
    _assert_representatives(values[:3], source.take(3))
    _assert_representatives(values[2:], source.drop(2))
    assert source.take(0).is_empty and source.take(0).policy is policy
    assert source.drop(len(source)).is_empty and source.drop(len(source)).policy is policy
    _assert_representatives(list(reversed(values)), source.reverse())
    assert source.clear().is_empty and source.clear().policy is policy
    assert PersistentOrderedSet.from_values([1]).clear() is PersistentOrderedSet.empty()
    assert PersistentOrderedSet.empty().reverse() is PersistentOrderedSet.empty()

    operations: tuple[Callable[[], object], ...] = (
        lambda: source.get_range(-1, 0),
        lambda: source.get_range(1, len(source)),
        lambda: source.take(-1),
        lambda: source.drop(len(source) + 1),
    )
    for operation in operations:
        with pytest.raises((IndexError, ValueError)):
            operation()


def test_sort_is_stable_failure_atomic_and_one_shot() -> None:
    policy = cast("HashPolicy[_Representative]", _representative_policy())
    values = [
        _Representative(4, "four"),
        _Representative(1, "one"),
        _Representative(7, "seven"),
        _Representative(2, "two"),
        _Representative(5, "five"),
    ]
    source = PersistentOrderedSet.from_values(values, policy)

    def comparator(left: _Representative, right: _Representative) -> int:
        return (left.equivalence_class % 3) - (right.equivalence_class % 3)

    sorted_value = source.sort(comparator)
    expected = sorted(values, key=lambda value: value.equivalence_class % 3)
    _assert_representatives(expected, sorted_value)
    assert sorted_value.sort(comparator) is sorted_value

    appended = _Representative(10, "later")
    _assert_representatives([*expected, appended], sorted_value.add(appended))
    assert source.sort(lambda _left, _right: 0) is source
    assert PersistentOrderedSet.empty(policy).sort(lambda _left, _right: 1).is_empty

    def fail_order(_left: _Representative, _right: _Representative) -> int:
        raise RuntimeError("order failure")

    with pytest.raises(RuntimeError, match="order failure"):
        source.sort(fail_order)
    _assert_representatives(values, source)


def test_algebra_normalizes_under_receiver_policy_and_locks_result_order() -> None:
    receiver_policy = cast("HashPolicy[_Representative]", _representative_policy())
    receiver_a = _Representative(1, "receiver-a")
    receiver_b = _Representative(2, "receiver-b")
    receiver_c = _Representative(3, "receiver-c")
    argument_a = _Representative(1, "argument-a")
    argument_d = _Representative(4, "argument-d")
    receiver = PersistentOrderedSet.from_values(
        [receiver_a, receiver_b, receiver_c], receiver_policy
    )
    argument = PersistentOrderedSet.from_values(
        [argument_a, _Representative(1, "later-a"), argument_d], _reference_policy()
    )

    _assert_representatives(
        [receiver_a, receiver_b, receiver_c, argument_d], receiver.union(argument)
    )
    _assert_representatives([receiver_a], receiver.intersect(argument))
    _assert_representatives([receiver_b, receiver_c], receiver.except_(argument))
    _assert_representatives(
        [receiver_b, receiver_c, argument_d], receiver.symmetric_except(argument)
    )

    empty = PersistentOrderedSet.empty(receiver_policy)
    subset = PersistentOrderedSet.from_values([argument_a], receiver_policy)
    superset = PersistentOrderedSet.from_values(
        [
            argument_a,
            _Representative(2, "other-b"),
            _Representative(3, "other-c"),
            argument_d,
        ],
        receiver_policy,
    )
    disjoint = PersistentOrderedSet.from_values([_Representative(9, "other")], receiver_policy)
    assert receiver.union(empty) is receiver
    assert receiver.symmetric_except(empty) is receiver
    assert receiver.union(subset) is receiver
    assert receiver.intersect(superset) is receiver
    assert receiver.except_(disjoint) is receiver
    self_difference = receiver.except_(receiver)
    self_symmetric = receiver.symmetric_except(receiver)
    assert self_difference.is_empty and self_difference.policy is receiver_policy
    assert self_symmetric.is_empty and self_symmetric.policy is receiver_policy
    assert self_difference is not PersistentOrderedSet.empty()
    assert self_symmetric is not PersistentOrderedSet.empty()

    foreign_policy = _SwitchablePolicy(hash_buckets=5)
    foreign = PersistentOrderedSet.from_values([argument_a, argument_d], foreign_policy)
    foreign_policy.throw_hash = True
    foreign_policy.throw_equality = True
    _assert_representatives(
        [receiver_a, receiver_b, receiver_c, argument_d], receiver.union(foreign)
    )
    assert receiver.overlaps(foreign)


def test_relations_use_eager_receiver_policy_normalization() -> None:
    for receiver_mask in range(1 << 4):
        receiver_values = [value for value in range(4) if receiver_mask & (1 << value)]
        receiver = PersistentOrderedSet.from_values(receiver_values)
        for argument_mask in range(1 << 4):
            distinct = [value for value in range(4) if argument_mask & (1 << value)]
            argument = [value for value in distinct for _ in range(2)]
            receiver_only = receiver_mask & ~argument_mask
            argument_only = argument_mask & ~receiver_mask
            assert receiver.is_subset_of(argument) == (receiver_only == 0)
            assert receiver.is_proper_subset_of(argument) == (
                receiver_only == 0 and argument_only != 0
            )
            assert receiver.is_superset_of(argument) == (argument_only == 0)
            assert receiver.is_proper_superset_of(argument) == (
                argument_only == 0 and receiver_only != 0
            )
            assert receiver.overlaps(argument) == ((receiver_mask & argument_mask) != 0)
            assert receiver.set_equals(argument) == (receiver_mask == argument_mask)

    failure = RuntimeError("late enumeration failure")

    def failing_argument() -> Iterator[int]:
        yield 1
        raise failure

    source = PersistentOrderedSet.from_values([1, 2])
    set_operations: tuple[Callable[[Iterable[int]], object], ...] = (
        source.union,
        source.intersect,
        source.except_,
        source.symmetric_except,
        source.is_subset_of,
        source.is_proper_subset_of,
        source.is_superset_of,
        source.is_proper_superset_of,
        source.overlaps,
        source.set_equals,
    )
    for operation in set_operations:
        with pytest.raises(RuntimeError) as captured:
            operation(failing_argument())
        assert captured.value is failure

    invalid = cast("Iterable[int]", None)
    for operation in set_operations:
        with pytest.raises(TypeError):
            operation(invalid)


def test_validation_precedes_callbacks_and_failures_leave_sources_unchanged(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    policy = _SwitchablePolicy()
    values = [_Representative(1, "first"), _Representative(2, "second")]
    source = PersistentOrderedSet.from_values(values, policy)
    policy.reset()
    policy.throw_hash = True
    policy.throw_equality = True

    operations: tuple[Callable[[], object], ...] = (
        lambda: source.insert(-1, values[0]),
        lambda: source.insert(3, values[0]),
        lambda: source.move_to(-1, values[0]),
        lambda: source.move_to(2, values[0]),
        lambda: source.get_range(-1, 0),
        lambda: source.get_range(0, 3),
    )
    for operation in operations:
        with pytest.raises((IndexError, ValueError)):
            operation()
    assert policy.hash_calls == 0 and policy.equality_calls == 0

    with pytest.raises(RuntimeError, match="hash failure"):
        source.add(_Representative(3, "new"))
    policy.throw_hash = False
    policy.throw_equality = False
    _assert_representatives(values, source)

    policy.throw_hash = True
    with pytest.raises(RuntimeError, match="hash failure"):
        source.reverse()
    policy.throw_hash = False
    _assert_representatives(values, source)

    larger_values = [_Representative(index, f"value-{index}") for index in range(12)]
    larger = PersistentOrderedSet.from_values(larger_values, policy)
    policy.throw_hash = True
    for operation in (
        lambda: larger.get_range(6, 1),
        lambda: larger.get_range(1, 10),
    ):
        with pytest.raises(RuntimeError, match="hash failure"):
            operation()
    policy.throw_hash = False
    _assert_representatives(larger_values, larger)

    single = PersistentOrderedSet.empty(policy).add(values[0])
    policy.throw_hash = True
    policy.throw_equality = True
    assert single.reverse() is single
    assert single.sort(lambda _left, _right: (_ for _ in ()).throw(RuntimeError())) is single
    assert single.clear().is_empty

    bounded = PersistentOrderedSet.from_values([1, 2])
    monkeypatch.setattr(_ordered_module, "_INT32_MAX", 2)
    assert bounded.add(2) is bounded
    for operation in (
        lambda: bounded.add(3),
        lambda: bounded.add_first(3),
        lambda: bounded.insert(1, 3),
        lambda: bounded.union([3]),
        lambda: bounded.overlaps([3, 4, 5]),
        lambda: PersistentOrderedSet.from_values([1, 2, 3]),
    ):
        with pytest.raises(OverflowError, match="signed 32-bit"):
            operation()
    assert bounded.to_list() == [1, 2]


def test_iteration_is_version_bound_concurrently_readable_and_neutral() -> None:
    source = PersistentOrderedSet.from_values(range(200))
    iterator = iter(source)
    assert next(iterator) == 0
    successor = source.move_to_first(199).add(200)
    assert list(iterator) == list(range(1, 200))
    assert source.to_list() == list(range(200))
    assert successor.to_list() == [199, *range(199), 200]

    def checksum(value: PersistentOrderedSet[int]) -> tuple[int, int, int]:
        return sum(value), value.index_of(100), value[100]

    with ThreadPoolExecutor(max_workers=8) as executor:
        observed = list(executor.map(lambda _index: checksum(source), range(32)))
    assert observed == [(sum(range(200)), 100, 100)] * 32

    module = __import__(
        "vladimir_reshetnikov.data_structures.ordered.persistent_ordered_set",
        fromlist=["PersistentOrderedSet"],
    )
    assert "tungsten" not in getsource(module).casefold()
