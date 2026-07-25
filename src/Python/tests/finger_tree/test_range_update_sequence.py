from __future__ import annotations

from collections.abc import Callable, Iterable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from itertools import product
from typing import cast

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7 import (
    RangeUpdateAlgebra,
    RangeUpdateSequence,
    RangeUpdateSplit,
    create_range_update_algebra,
)
from durable7.finger_tree import (
    range_update_sequence as _range_update_module,
)
from durable7.finger_tree._range_update_diagnostics import (
    _observe_range_update_operations,
)


@dataclass(frozen=True, slots=True)
class _Tag:
    multiplier: int
    offset: int
    identity_flavor: int = 0

    @classmethod
    def identity(cls, flavor: int = 0) -> _Tag:
        return cls(1, 0, flavor)

    @classmethod
    def add(cls, value: int) -> _Tag:
        return cls(1, value)

    @classmethod
    def assign(cls, value: int) -> _Tag:
        return cls(0, value)


@dataclass(frozen=True, slots=True)
class _Measure:
    count: int
    total: int
    position_weighted_total: int


class _AffineAlgebra:
    identity_tag = _Tag.identity()

    @property
    def identity(self) -> _Measure:
        return _Measure(0, 0, 0)

    def combine(self, left: _Measure, right: _Measure) -> _Measure:
        return _Measure(
            left.count + right.count,
            left.total + right.total,
            left.position_weighted_total + right.position_weighted_total + left.count * right.total,
        )

    def measure(self, element: int) -> _Measure:
        return _Measure(1, element, 0)

    def is_identity(self, tag: _Tag) -> bool:
        return tag.multiplier == 1 and tag.offset == 0

    def compose(self, newer: _Tag, older: _Tag) -> _Tag:
        if newer.multiplier == 1 and newer.offset == 0:
            return older
        if older.multiplier == 1 and older.offset == 0:
            return newer
        return _Tag(
            newer.multiplier * older.multiplier,
            newer.multiplier * older.offset + newer.offset,
        )

    def apply_element(self, tag: _Tag, element: int) -> int:
        return tag.multiplier * element + tag.offset

    def apply_measure(self, tag: _Tag, measure: _Measure, count: int) -> _Measure:
        if count == 0:
            return self.identity
        return _Measure(
            measure.count,
            tag.multiplier * measure.total + tag.offset * count,
            tag.multiplier * measure.position_weighted_total
            + tag.offset * count * (count - 1) // 2,
        )


_AFFINE = _AffineAlgebra()
_Sequence = RangeUpdateSequence[int, _Measure, _Tag]


def _sequence(values: Iterable[int] = ()) -> _Sequence:
    return RangeUpdateSequence.from_iterable(values, _AFFINE)


def _fold(values: Iterable[int]) -> _Measure:
    result = _AFFINE.identity
    for value in values:
        result = _AFFINE.combine(result, _AFFINE.measure(value))
    return result


def _assert_matches(sequence: _Sequence, expected: list[int]) -> None:
    assert len(sequence) == len(expected)
    assert sequence.is_empty is (not expected)
    assert sequence.to_list() == expected
    assert sequence.measure == _fold(expected)
    assert [sequence[index] for index in range(len(sequence))] == expected
    report = sequence._validate_invariants()
    assert report.count == len(expected)
    assert report.node_count == len(expected)
    assert report.maximum_absolute_balance_factor <= 1


def _apply_model(values: list[int], index: int, count: int, tag: _Tag) -> None:
    for offset in range(count):
        values[index + offset] = _AFFINE.apply_element(tag, values[index + offset])


def test_public_surface_and_functional_algebra_adapter() -> None:
    algebra = create_range_update_algebra(
        _AFFINE.identity,
        _AFFINE.identity_tag,
        _AFFINE.combine,
        _AFFINE.measure,
        _AFFINE.is_identity,
        _AFFINE.compose,
        _AFFINE.apply_element,
        _AFFINE.apply_measure,
    )
    declared: RangeUpdateAlgebra[int, _Measure, _Tag] = algebra
    sequence = RangeUpdateSequence.from_iterable((1, 2, 3), declared)
    split: RangeUpdateSplit[int, _Measure, _Tag] = sequence.split_at(1)
    assert split.left.to_list() == [1]
    assert split.right.apply_range(0, 2, _Tag.add(5)).to_list() == [7, 8]
    assert not hasattr(sequence, "add_range")
    assert not hasattr(sequence, "assign_range")


def test_tag_and_measure_algebra_laws_are_order_sensitive() -> None:
    tags = [
        _Tag.identity(),
        _Tag.identity(41),
        _Tag.add(-3),
        _Tag.add(7),
        _Tag.assign(5),
        _Tag(-2, 4),
    ]
    values = [-5, 0, 8]
    measures = [_fold(()), _fold((1,)), _fold((2, 3)), _fold((-4, 5, 6))]
    for left, middle, right in product(measures, repeat=3):
        assert _AFFINE.combine(_AFFINE.combine(left, middle), right) == _AFFINE.combine(
            left, _AFFINE.combine(middle, right)
        )
        assert _AFFINE.combine(_AFFINE.identity, left) == left
        assert _AFFINE.combine(left, _AFFINE.identity) == left
    for tag_newer, tag_middle, tag_older in product(tags, repeat=3):
        tag_left = _AFFINE.compose(tag_newer, _AFFINE.compose(tag_middle, tag_older))
        tag_right = _AFFINE.compose(_AFFINE.compose(tag_newer, tag_middle), tag_older)
        for value in values:
            assert _AFFINE.apply_element(tag_left, value) == _AFFINE.apply_element(tag_right, value)
            assert _AFFINE.apply_element(
                _AFFINE.compose(tag_newer, tag_older), value
            ) == _AFFINE.apply_element(tag_newer, _AFFINE.apply_element(tag_older, value))
        for measure in measures:
            composed = _AFFINE.compose(tag_newer, tag_older)
            assert _AFFINE.apply_measure(composed, measure, measure.count) == (
                _AFFINE.apply_measure(
                    tag_newer,
                    _AFFINE.apply_measure(tag_older, measure, measure.count),
                    measure.count,
                )
            )

    left_measure = _fold([1, 2])
    right_measure = _fold([8, 16, 32])
    assert _AFFINE.combine(left_measure, right_measure) != _AFFINE.combine(
        right_measure, left_measure
    )
    for tag in tags:
        combined = _AFFINE.combine(left_measure, right_measure)
        assert _AFFINE.apply_measure(tag, combined, combined.count) == _AFFINE.combine(
            _AFFINE.apply_measure(tag, left_measure, left_measure.count),
            _AFFINE.apply_measure(tag, right_measure, right_measure.count),
        )


def test_factories_edits_split_concat_ranges_and_identity_retention() -> None:
    empty = _sequence()
    assert empty.is_empty
    assert empty.measure == _Measure(0, 0, 0)
    assert empty.to_list() == []

    basis = _sequence([1, 2, 3, 4])
    assert RangeUpdateSequence.from_iterable(basis, _AFFINE) is basis
    changed = basis.prepend(0).append(5).insert(3, 99).set_item(3, 3).remove_at(4)
    _assert_matches(changed, [0, 1, 2, 3, 4, 5])
    _assert_matches(basis, [1, 2, 3, 4])

    same_replacement = basis.set_item(0, 1)
    assert same_replacement is not basis
    assert same_replacement._root_identity is not basis._root_identity

    for index in range(len(changed) + 1):
        split = changed.split_at(index)
        assert split.left.to_list() == changed.to_list()[:index]
        assert split.right.to_list() == changed.to_list()[index:]
        assert split.left.concat(split.right).to_list() == changed.to_list()

    assert changed.split_at(0).right is changed
    assert changed.split_at(len(changed)).left is changed
    assert changed.concat(empty) is changed
    assert empty.concat(changed) is changed
    assert changed.get_range(0, len(changed)) is changed
    canonical_empty = changed.get_range(2, 0)
    assert canonical_empty is changed.split_at(0).left
    assert canonical_empty is changed.remove_at(0).get_range(0, 0)

    for index in range(len(changed) + 1):
        for count in range(len(changed) - index + 1):
            expected = changed.to_list()[index : index + count]
            assert changed.get_range(index, count).to_list() == expected
            assert changed.measure_range(index, count) == _fold(expected)


class _CountingIdentityAlgebra(_AffineAlgebra):
    def __init__(self) -> None:
        self.identity_reads = 0

    @property
    def identity(self) -> _Measure:
        self.identity_reads += 1
        return _Measure(0, 0, 0)


def test_empty_measure_is_captured_once_after_complete_source_materialization() -> None:
    algebra = _CountingIdentityAlgebra()
    sequence = RangeUpdateSequence.from_iterable((1, 2, 3), algebra)
    assert algebra.identity_reads == 1
    empty = sequence.get_range(1, 0)
    assert empty.measure == _Measure(0, 0, 0)
    assert sequence.measure_range(2, 0) == _Measure(0, 0, 0)
    assert sequence.split_at(0).left is empty
    assert algebra.identity_reads == 1

    def failing_source() -> Iterable[int]:
        yield 1
        raise RuntimeError("materialization failed")

    fresh = _CountingIdentityAlgebra()
    with pytest.raises(RuntimeError, match="materialization failed"):
        RangeUpdateSequence.from_iterable(failing_source(), fresh)
    assert fresh.identity_reads == 0


def test_signed_32_bit_count_limit_precedes_policy_callbacks(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    algebra = _ThrowingAlgebra()
    monkeypatch.setattr(_range_update_module, "_INT32_MAX", 3)
    sequence = RangeUpdateSequence.from_iterable((1, 2, 3), algebra)
    algebra.arm("measure", 1)
    with pytest.raises(OverflowError):
        sequence.append(4)
    assert algebra.matching_invocations == 0
    algebra.disable()

    with pytest.raises(OverflowError):
        RangeUpdateSequence.from_iterable((1, 2, 3, 4), algebra)
    assert algebra.matching_invocations == 0

    left = RangeUpdateSequence.from_iterable((1, 2), algebra)
    right = RangeUpdateSequence.from_iterable((3, 4), algebra)
    algebra.arm("measure", 1)
    with pytest.raises(OverflowError):
        left.concat(right)
    assert algebra.matching_invocations == 0
    algebra.disable()


def test_range_validation_precedes_callbacks_and_uses_overflow_safe_order() -> None:
    sequence = _sequence([1, 2, 3])
    with pytest.raises(IndexError):
        _ = sequence[-1]
    with pytest.raises(IndexError):
        sequence.get_at(3)
    with pytest.raises(IndexError):
        sequence.insert(4, 0)
    with pytest.raises(IndexError):
        sequence.apply_range(-1, 0, _Tag.add(1))
    with pytest.raises(ValueError):
        sequence.apply_range(0, -1, _Tag.add(1))
    with pytest.raises(IndexError):
        sequence.apply_range(4, 0, _Tag.add(1))
    with pytest.raises(ValueError):
        sequence.apply_range(2, 2, _Tag.add(1))
    with pytest.raises(TypeError):
        _ = sequence[cast(int, slice(0, 1))]

    other_algebra = _AffineAlgebra()
    other = RangeUpdateSequence.from_iterable((4, 5), other_algebra)
    with pytest.raises(TypeError, match="same range-update algebra"):
        sequence.concat(other)


def test_nested_add_assign_and_affine_tags_preserve_composition_direction() -> None:
    basis = _sequence(range(10))
    older = basis.apply_range(1, 8, _Tag.assign(4))
    newer = older.apply_range(3, 5, _Tag.add(7))
    newest = newer.apply_range(4, 3, _Tag(-2, 1))
    expected = list(range(10))
    _apply_model(expected, 1, 8, _Tag.assign(4))
    _apply_model(expected, 3, 5, _Tag.add(7))
    _apply_model(expected, 4, 3, _Tag(-2, 1))
    _assert_matches(newest, expected)
    _assert_matches(older, [0, 4, 4, 4, 4, 4, 4, 4, 4, 9])
    for index in range(len(expected) + 1):
        for count in range(len(expected) - index + 1):
            assert newest.measure_range(index, count) == _fold(expected[index : index + count])

    cancelled = basis.apply_range(0, len(basis), _Tag.add(5)).apply_range(
        0, len(basis), _Tag.add(-5)
    )
    assert cancelled.to_list() == basis.to_list()
    assert cancelled._structure_statistics().pending_tag_node_count == 0
    assert cancelled is not basis


def test_structural_edits_push_old_tags_without_transforming_new_values() -> None:
    tagged = _sequence(range(16)).apply_range(0, 16, _Tag.add(100))
    inserted = tagged.insert(8, -7)
    replaced = inserted.set_item(3, -11)
    removed = replaced.remove_at(12)
    expected = [value + 100 for value in range(16)]
    expected.insert(8, -7)
    expected[3] = -11
    expected.pop(12)
    _assert_matches(removed, expected)
    _assert_matches(tagged, [value + 100 for value in range(16)])


_NULL_IDENTITY = object()


@dataclass(frozen=True, slots=True)
class _NullableMeasure:
    count: int
    non_null_count: int
    projection: str


class _NullableAlgebra:
    identity = _NullableMeasure(0, 0, "")
    identity_tag = _NULL_IDENTITY

    @staticmethod
    def _token(value: str | None) -> str:
        return "<null>;" if value is None else f"<{len(value)}:{value}>;"

    def combine(self, left: _NullableMeasure, right: _NullableMeasure) -> _NullableMeasure:
        return _NullableMeasure(
            left.count + right.count,
            left.non_null_count + right.non_null_count,
            left.projection + right.projection,
        )

    def measure(self, element: str | None) -> _NullableMeasure:
        return _NullableMeasure(1, int(element is not None), self._token(element))

    def is_identity(self, tag: object) -> bool:
        return tag is _NULL_IDENTITY

    def compose(self, newer: object, older: object) -> object:
        return older if self.is_identity(newer) else newer

    def apply_element(self, tag: object, element: str | None) -> str | None:
        return element if self.is_identity(tag) else cast(str | None, tag)

    def apply_measure(self, tag: object, measure: _NullableMeasure, count: int) -> _NullableMeasure:
        if self.is_identity(tag):
            return measure
        replacement = cast(str | None, tag)
        return _NullableMeasure(
            count,
            count if replacement is not None else 0,
            self._token(replacement) * count,
        )


def test_none_is_an_ordinary_element_and_an_active_pending_tag() -> None:
    algebra = _NullableAlgebra()
    sequence = RangeUpdateSequence.from_iterable(["a", None, "bbb", "c"], algebra)
    assigned_null = sequence.apply_range(0, 4, None)
    assert assigned_null.to_list() == [None, None, None, None]
    assert assigned_null._structure_statistics().pending_tag_node_count == 1
    assigned_value = assigned_null.apply_range(1, 2, "xy")
    assert assigned_value.to_list() == [None, "xy", "xy", None]
    assert assigned_value.measure == _NullableMeasure(4, 2, "<null>;<2:xy>;<2:xy>;<null>;")
    assigned_value._validate_invariants()


def test_native_iterators_are_independent_snapshot_bound_and_lazy() -> None:
    tagged = _sequence(range(32)).apply_range(0, 32, _Tag.add(10))
    first = iter(tagged)
    second = iter(tagged)
    assert next(first) == 10
    assert next(first) == 11
    assert next(second) == 10
    successor = tagged.apply_range(0, 32, _Tag.add(100))
    assert list(first) == list(range(12, 42))
    assert list(second) == list(range(11, 42))
    assert successor.to_list() == list(range(110, 142))
    assert tagged.to_list() == list(range(10, 42))
    assert list(iter(_sequence())) == []


def test_whole_range_and_no_op_diagnostics_are_exact() -> None:
    sequence = _sequence(range(64))
    with _observe_range_update_operations() as observation:
        changed = sequence.apply_range(0, len(sequence), _Tag.add(5))
    statistics = observation.snapshot
    assert statistics.node_visits == 1
    assert statistics.node_allocations == 1
    assert statistics.facade_allocations == 1
    assert statistics.subtree_applications == 1
    assert statistics.element_apply_callbacks == 1
    assert statistics.measure_apply_callbacks == 1
    assert statistics.identity_test_callbacks == 1
    assert statistics.tag_compose_callbacks == 0
    assert statistics.pushes == 0
    assert statistics.rotations == 0
    assert changed.to_list() == [value + 5 for value in range(64)]

    with _observe_range_update_operations() as empty_observation:
        assert sequence.apply_range(10, 0, _Tag.add(5)) is sequence
        assert sequence.measure_range(10, 0) == _AFFINE.identity
    assert empty_observation.snapshot.policy_callbacks == 0
    assert empty_observation.snapshot.node_allocations == 0
    assert empty_observation.snapshot.facade_allocations == 0

    with _observe_range_update_operations() as identity_observation:
        assert sequence.apply_range(0, len(sequence), _Tag.identity(99)) is sequence
    assert identity_observation.snapshot.identity_test_callbacks == 1
    assert identity_observation.snapshot.node_allocations == 0
    assert identity_observation.snapshot.facade_allocations == 0

    with _observe_range_update_operations() as endpoint_observation:
        assert sequence.split_at(0).right is sequence
        assert sequence.split_at(len(sequence)).left is sequence
        assert sequence.get_range(0, len(sequence)) is sequence
        assert sequence.measure_range(0, len(sequence)) == sequence.measure
        assert RangeUpdateSequence.from_iterable(sequence, _AFFINE) is sequence
    assert endpoint_observation.snapshot.policy_callbacks == 0
    assert endpoint_observation.snapshot.node_allocations == 0
    assert endpoint_observation.snapshot.facade_allocations == 0

    tagged = sequence.apply_range(0, len(sequence), _Tag.add(5))
    with _observe_range_update_operations() as read_observation:
        assert tagged[3] == 8
        assert tagged.measure_range(2, 19) == _fold(range(7, 26))
        assert tagged.to_list() == [value + 5 for value in range(64)]
    assert read_observation.snapshot.node_allocations == 0
    assert read_observation.snapshot.facade_allocations == 0


def test_invariants_pending_depth_and_path_copy_sharing() -> None:
    basis = _sequence(range(1_024))
    whole = basis.apply_range(0, len(basis), _Tag.add(1))
    assert basis._count_shared_nodes(whole) == len(basis) - 1
    changed = whole.apply_range(256, 512, _Tag(-1, 3))
    report = changed._validate_invariants()
    assert report.height <= 11
    assert report.pending_tag_node_count > 1
    assert report.maximum_pending_tag_depth >= 1
    nested = changed.apply_range(0, len(changed), _Tag.add(2))
    assert nested._validate_invariants().maximum_pending_tag_depth >= 2
    assert whole._count_shared_nodes(changed) > 900

    point = changed.set_item(500, 12345)
    assert changed._count_shared_nodes(point) > 900
    assert point[500] == 12345
    changed._validate_invariants()
    point._validate_invariants()


def test_all_small_splits_rejoin_and_retain_balanced_shapes() -> None:
    for size in range(65):
        sequence = _sequence(range(size)).apply_range(0, size, _Tag.add(3))
        for index in range(size + 1):
            split = sequence.split_at(index)
            restored = split.left.concat(split.right)
            assert restored.to_list() == [value + 3 for value in range(size)]
            restored._validate_invariants()


class _CallbackFailure(RuntimeError):
    def __init__(self, callback: str, ordinal: int) -> None:
        super().__init__(f"range-update callback {callback} failed at invocation {ordinal}")
        self.callback = callback
        self.ordinal = ordinal


class _ThrowingAlgebra(_AffineAlgebra):
    def __init__(self) -> None:
        self.armed_callback: str | None = None
        self.armed_ordinal = 0
        self.matching_invocations = 0

    def disable(self) -> None:
        self.armed_callback = None
        self.armed_ordinal = 0
        self.matching_invocations = 0

    def arm(self, callback: str, ordinal: int) -> None:
        if ordinal < 1:
            raise ValueError("ordinal must be positive")
        self.armed_callback = callback
        self.armed_ordinal = ordinal
        self.matching_invocations = 0

    def _hit(self, callback: str) -> None:
        if self.armed_callback != callback:
            return
        self.matching_invocations += 1
        if self.matching_invocations == self.armed_ordinal:
            raise _CallbackFailure(callback, self.matching_invocations)

    def combine(self, left: _Measure, right: _Measure) -> _Measure:
        self._hit("combine")
        return super().combine(left, right)

    def measure(self, element: int) -> _Measure:
        self._hit("measure")
        return super().measure(element)

    def is_identity(self, tag: _Tag) -> bool:
        self._hit("is_identity")
        return super().is_identity(tag)

    def compose(self, newer: _Tag, older: _Tag) -> _Tag:
        self._hit("compose")
        return super().compose(newer, older)

    def apply_element(self, tag: _Tag, element: int) -> int:
        self._hit("apply_element")
        return super().apply_element(tag, element)

    def apply_measure(self, tag: _Tag, measure: _Measure, count: int) -> _Measure:
        self._hit("apply_measure")
        return super().apply_measure(tag, measure, count)


def _assert_every_failpoint_is_atomic(
    sequence: RangeUpdateSequence[int, _Measure, _Tag],
    algebra: _ThrowingAlgebra,
    callback: str,
    operation: Callable[[], object],
) -> None:
    before_values = sequence.to_list()
    before_measure = sequence.measure
    before_root = sequence._root_identity
    algebra.arm(callback, 1_000_000)
    operation()
    invocation_count = algebra.matching_invocations
    assert invocation_count > 0
    for ordinal in range(1, invocation_count + 1):
        algebra.arm(callback, ordinal)
        with pytest.raises(_CallbackFailure) as failure:
            operation()
        assert failure.value.callback == callback
        assert failure.value.ordinal == ordinal
        algebra.disable()
        assert sequence._root_identity is before_root
        assert sequence.to_list() == before_values
        assert sequence.measure == before_measure
        sequence._validate_invariants()
    algebra.disable()


def test_every_policy_callback_failpoint_is_failure_atomic() -> None:
    algebra = _ThrowingAlgebra()
    plain = RangeUpdateSequence.from_iterable(range(31), algebra)
    pending = plain.apply_range(0, len(plain), _Tag.add(10))

    _assert_every_failpoint_is_atomic(pending, algebra, "measure", lambda: pending.append(99))
    _assert_every_failpoint_is_atomic(pending, algebra, "combine", lambda: pending.append(99))
    for callback in ("is_identity", "compose", "apply_element", "apply_measure"):
        _assert_every_failpoint_is_atomic(
            pending,
            algebra,
            callback,
            lambda: pending.apply_range(0, len(pending), _Tag(-1, 7)),
        )


def test_throwing_enumeration_queries_and_sources_leave_versions_usable() -> None:
    algebra = _ThrowingAlgebra()
    sequence = RangeUpdateSequence.from_iterable(range(31), algebra).apply_range(0, 31, _Tag.add(5))
    algebra.arm("apply_element", 1)
    with pytest.raises(_CallbackFailure):
        list(sequence)
    algebra.disable()
    assert sequence.to_list() == [value + 5 for value in range(31)]

    algebra.arm("apply_measure", 1)
    with pytest.raises(_CallbackFailure):
        sequence.measure_range(0, 10)
    algebra.disable()
    sequence._validate_invariants()

    def failing_source() -> Iterable[int]:
        yield 1
        raise RuntimeError("source failed")

    algebra.arm("measure", 1)
    with pytest.raises(RuntimeError, match="source failed"):
        RangeUpdateSequence.from_iterable(failing_source(), algebra)
    assert algebra.matching_invocations == 0
    algebra.disable()


def test_invalid_and_empty_ranges_invoke_no_armed_policy_callback() -> None:
    algebra = _ThrowingAlgebra()
    sequence = RangeUpdateSequence.from_iterable(range(8), algebra)
    for callback in (
        "measure",
        "combine",
        "is_identity",
        "compose",
        "apply_element",
        "apply_measure",
    ):
        algebra.arm(callback, 1)
        with pytest.raises(IndexError):
            sequence.apply_range(-1, 0, _Tag.add(1))
        with pytest.raises(ValueError):
            sequence.measure_range(0, -1)
        assert algebra.matching_invocations == 0
        assert sequence.apply_range(4, 0, _Tag.add(1)) is sequence
        assert sequence.measure_range(4, 0) == algebra.identity
        assert algebra.matching_invocations == 0
        algebra.disable()


def test_concurrent_reads_of_retained_tagged_snapshots_are_deterministic() -> None:
    basis = _sequence(range(2_000))
    tagged = basis.apply_range(100, 1_700, _Tag(-1, 11))
    expected = tagged.to_list()

    def read_snapshot(seed: int) -> tuple[int, _Measure, int]:
        index = seed % len(tagged)
        count = min(97, len(tagged) - index)
        return tagged[index], tagged.measure_range(index, count), sum(tagged)

    with ThreadPoolExecutor(max_workers=4) as executor:
        results = list(executor.map(read_snapshot, range(64)))
    for seed, result in enumerate(results):
        index = seed % len(tagged)
        count = min(97, len(tagged) - index)
        assert result == (expected[index], _fold(expected[index : index + count]), sum(expected))
    assert basis.to_list() == list(range(2_000))


@settings(max_examples=80, deadline=None)
@given(
    st.lists(
        st.tuples(
            st.integers(min_value=0, max_value=9),
            st.integers(min_value=0, max_value=10_000),
            st.integers(min_value=-100, max_value=100),
            st.integers(min_value=-2, max_value=2),
            st.integers(min_value=-20, max_value=20),
            st.booleans(),
        ),
        max_size=160,
    )
)
def test_branching_random_histories_match_list_and_measure_models(
    operations: list[tuple[int, int, int, int, int, bool]],
) -> None:
    sequence = _sequence()
    model: list[int] = []
    retained: list[tuple[_Sequence, list[int]]] = [(sequence, [])]

    for kind, raw_index, value, multiplier, offset, branch in operations:
        if branch and retained:
            sequence, saved = retained[raw_index % len(retained)]
            model = saved.copy()

        if kind == 0:
            sequence = sequence.append(value)
            model.append(value)
        elif kind == 1:
            index = raw_index % (len(model) + 1)
            sequence = sequence.insert(index, value)
            model.insert(index, value)
        elif kind == 2 and model:
            index = raw_index % len(model)
            sequence = sequence.set_item(index, value)
            model[index] = value
        elif kind == 3 and model:
            index = raw_index % len(model)
            sequence = sequence.remove_at(index)
            model.pop(index)
        elif kind == 4 and model:
            index = raw_index % (len(model) + 1)
            count = (abs(value) % (len(model) - index + 1)) if index < len(model) else 0
            tag = _Tag(multiplier, offset)
            sequence = sequence.apply_range(index, count, tag)
            _apply_model(model, index, count, tag)
        elif kind == 5:
            index = raw_index % (len(model) + 1)
            split = sequence.split_at(index)
            sequence = split.left.concat(split.right)
        elif kind == 6:
            index = raw_index % (len(model) + 1)
            count = (abs(value) % (len(model) - index + 1)) if index < len(model) else 0
            sequence = sequence.get_range(index, count)
            model = model[index : index + count]
        elif kind == 7:
            suffix_values = [value, offset]
            sequence = sequence.concat(_sequence(suffix_values))
            model.extend(suffix_values)
        elif kind == 8 and model:
            index = raw_index % len(model)
            count = abs(value) % (len(model) - index + 1)
            assert sequence.measure_range(index, count) == _fold(model[index : index + count])
        else:
            assert sequence.apply_range(0, 0, _Tag(multiplier, offset)) is sequence

        _assert_matches(sequence, model)
        if raw_index % 11 == 0:
            retained.append((sequence, model.copy()))
            if len(retained) > 32:
                retained.pop(0)

    for snapshot, expected in retained:
        _assert_matches(snapshot, expected)
