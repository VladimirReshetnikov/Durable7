from __future__ import annotations

from collections import deque

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.finger_tree.daba_lite import DabaLite


class _StringMonoid:
    identity = ""

    @staticmethod
    def combine(left: str, right: str) -> str:
        return left + right


def test_daba_tracks_noncommutative_fifo_model_through_chunk_churn() -> None:
    daba = DabaLite(_StringMonoid())
    model: deque[str] = deque()
    for index in range(10_000):
        if index % 3 == 0 and model:
            daba.evict()
            model.popleft()
        else:
            value = chr(65 + index % 26)
            daba.insert(value)
            model.append(value)
        assert daba.aggregate == "".join(model)
        if index % 101 == 0:
            statistics = daba.validate_structure()
            assert statistics.count == len(model)
            assert statistics.left_length == statistics.right_length
            assert 1 <= statistics.slack_slot_count < 128
    statistics = daba.validate_structure()
    assert statistics.block_count > 1


def test_daba_callback_ceilings_are_worst_case_bounded() -> None:
    class CountingMonoid:
        identity = ""

        def __init__(self) -> None:
            self.calls = 0

        def combine(self, left: str, right: str) -> str:
            self.calls += 1
            return left + right

    monoid = CountingMonoid()
    daba = DabaLite(monoid)
    for index in range(2_000):
        monoid.calls = 0
        daba.insert(chr(65 + index % 26))
        assert monoid.calls <= 3
        monoid.calls = 0
        _ = daba.aggregate
        assert monoid.calls <= 1
        if index % 2 == 0:
            monoid.calls = 0
            assert daba.try_evict()
            assert monoid.calls <= 2
        if index % 97 == 0:
            daba.validate_structure()


def test_daba_mutation_callbacks_are_failure_atomic() -> None:
    class FailureMonoid:
        def __init__(self) -> None:
            self.fail_combine = False
            self.fail_identity = False

        @property
        def identity(self) -> int:
            if self.fail_identity:
                raise RuntimeError("identity failed")
            return 0

        def combine(self, left: int, right: int) -> int:
            if self.fail_combine:
                raise RuntimeError("combine failed")
            return left + right

    monoid = FailureMonoid()
    daba = DabaLite(monoid)
    for value in range(1, 11):
        daba.insert(value)
    assert daba.aggregate == 55
    before = daba.validate_structure()

    monoid.fail_combine = True
    with pytest.raises(RuntimeError, match="combine failed"):
        daba.insert(11)
    monoid.fail_combine = False
    assert daba.aggregate == 55
    assert daba.validate_structure() == before

    monoid.fail_combine = True
    with pytest.raises(RuntimeError, match="combine failed"):
        daba.evict()
    monoid.fail_combine = False
    assert daba.aggregate == 55
    assert daba.validate_structure() == before

    monoid.fail_identity = True
    with pytest.raises(RuntimeError, match="identity failed"):
        daba.clear()
    monoid.fail_identity = False
    assert daba.aggregate == 55
    assert daba.validate_structure() == before


def test_daba_empty_clear_and_eviction_contracts() -> None:
    daba = DabaLite(_StringMonoid())
    assert daba.is_empty and daba.aggregate == ""
    assert not daba.try_evict()
    with pytest.raises(IndexError, match="empty"):
        daba.evict()
    daba.clear()
    assert daba.validate_structure().count == 0
    daba.insert("a")
    daba.clear()
    assert daba.is_empty and daba.aggregate == ""
    assert daba.validate_structure().block_count == 1


@settings(max_examples=60, deadline=None)
@given(
    st.lists(
        st.one_of(
            st.text(min_size=0, max_size=4).map(lambda value: (True, value)), st.just((False, ""))
        ),
        max_size=500,
    )
)
def test_daba_random_histories_match_fifo_model(
    operations: list[tuple[bool, str]],
) -> None:
    daba = DabaLite(_StringMonoid())
    model: deque[str] = deque()
    for insert, value in operations:
        if insert:
            daba.insert(value)
            model.append(value)
        elif model:
            assert daba.try_evict()
            model.popleft()
        else:
            assert not daba.try_evict()
        assert daba.aggregate == "".join(model)
    assert daba.validate_structure().count == len(model)
