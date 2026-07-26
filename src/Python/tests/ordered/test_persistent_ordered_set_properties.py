"""Property-based tests for the persistent insertion-ordered set.

Generates branching edit histories and checks construction and set algebra against an
independent ordered-list model that keeps first representatives, covering behavior that
hand-written cases are unlikely to reach.
"""

from __future__ import annotations

from dataclasses import dataclass

from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.hamt import HashPolicy, create_hash_policy
from durable7.ordered import PersistentOrderedSet


@dataclass(frozen=True, slots=True, eq=False)
class _Value:
    equivalence_class: int
    identity: int


_POLICY: HashPolicy[_Value] = create_hash_policy(
    lambda value: value.equivalence_class % 5,
    lambda left, right: left.equivalence_class == right.equivalence_class,
)


def _find(model: list[_Value], value: _Value) -> int:
    for index, stored in enumerate(model):
        if stored.equivalence_class == value.equivalence_class:
            return index
    return -1


def _normalize(values: list[_Value]) -> list[_Value]:
    result: list[_Value] = []
    for value in values:
        if _find(result, value) < 0:
            result.append(value)
    return result


def _assert_state(model: list[_Value], actual: PersistentOrderedSet[_Value]) -> None:
    observed = actual.to_list()
    assert len(observed) == len(model)
    assert all(expected is value for expected, value in zip(model, observed, strict=True))
    assert actual.policy is _POLICY
    for index, value in enumerate(model):
        probe = _Value(value.equivalence_class, -1)
        assert actual.contains(probe)
        assert actual.index_of(probe) == index
        recovered = actual.try_get_value(probe)
        assert recovered.found and recovered.value is value
    actual._validate_invariants()


@settings(max_examples=80)
@given(st.lists(st.tuples(st.integers(-15, 15), st.integers()), max_size=80))
def test_construction_and_algebra_match_first_representative_models(
    raw: list[tuple[int, int]],
) -> None:
    values = [_Value(equivalence_class, identity) for equivalence_class, identity in raw]
    actual = PersistentOrderedSet.from_values(values, _POLICY)
    expected = _normalize(values)
    _assert_state(expected, actual)

    argument = [
        _Value(equivalence_class + 2, identity ^ 0x55AA)
        for equivalence_class, identity in reversed(raw)
    ]
    normalized = _normalize(argument)
    union_model = [*expected]
    for value in normalized:
        if _find(union_model, value) < 0:
            union_model.append(value)
    intersect_model = [value for value in expected if _find(normalized, value) >= 0]
    except_model = [value for value in expected if _find(normalized, value) < 0]
    symmetric_model = [
        *except_model,
        *(value for value in normalized if _find(expected, value) < 0),
    ]
    _assert_state(union_model, actual.union(argument))
    _assert_state(intersect_model, actual.intersect(argument))
    _assert_state(except_model, actual.except_(argument))
    _assert_state(symmetric_model, actual.symmetric_except(argument))


@settings(max_examples=35, deadline=None)
@given(
    st.lists(
        st.tuples(
            st.integers(0, 13),
            st.integers(-12, 12),
            st.integers(),
            st.integers(),
        ),
        max_size=45,
    )
)
def test_generated_branching_histories_match_an_independent_ordered_list_model(
    commands: list[tuple[int, int, int, int]],
) -> None:
    versions: list[tuple[PersistentOrderedSet[_Value], list[_Value]]] = [
        (PersistentOrderedSet.empty(_POLICY), [])
    ]
    for step, (operation, equivalence_class, identity, position) in enumerate(commands):
        source, retained_model = versions[abs(identity) % len(versions)]
        model = list(retained_model)
        value = _Value(equivalence_class, identity ^ step)
        changed = True

        if operation == 0:
            if _find(model, value) < 0:
                model.append(value)
            else:
                changed = False
            actual = source.add(value)
        elif operation == 1:
            if _find(model, value) < 0:
                model.insert(0, value)
            else:
                changed = False
            actual = source.add_first(value)
        elif operation == 2:
            index = abs(position) % (len(model) + 1)
            if _find(model, value) < 0:
                model.insert(index, value)
            else:
                changed = False
            actual = source.insert(index, value)
        elif operation == 3 and model:
            old_index = abs(equivalence_class) % len(model)
            probe = _Value(model[old_index].equivalence_class, identity)
            final_index = abs(position) % len(model)
            stored = model.pop(old_index)
            model.insert(final_index, stored)
            changed = old_index != final_index
            actual = source.move_to(final_index, probe)
        elif operation == 4:
            found = _find(model, value)
            if found < 0:
                changed = False
            else:
                model.pop(found)
            actual = source.remove(value)
        elif operation == 5 and model:
            index = abs(position) % len(model)
            model.pop(index)
            actual = source.remove_at(index)
        elif operation == 6:
            index = abs(position) % (len(model) + 1)
            count = abs(identity) % (len(model) - index + 1)
            full = index == 0 and count == len(model)
            model = model[index : index + count]
            changed = not full
            actual = source.get_range(index, count)
        elif operation == 7:
            changed = len(model) > 1
            model.reverse()
            actual = source.reverse()
        elif operation == 8:
            before = list(model)
            model.sort(key=lambda item: item.equivalence_class % 4)
            changed = any(left is not right for left, right in zip(before, model, strict=True))
            actual = source.sort(
                lambda left, right: (left.equivalence_class % 4) - (right.equivalence_class % 4)
            )
        elif operation == 9:
            argument = [
                value,
                _Value(value.equivalence_class, identity + 1),
                _Value(equivalence_class + 20, identity),
            ]
            before_size = len(model)
            for candidate in _normalize(argument):
                if _find(model, candidate) < 0:
                    model.append(candidate)
            changed = len(model) != before_size
            actual = source.union(argument)
        elif operation == 10:
            argument = _normalize([value, _Value(equivalence_class + 1, identity)])
            before = list(model)
            model = [candidate for candidate in model if _find(argument, candidate) < 0]
            changed = len(model) != len(before)
            actual = source.except_(argument)
        elif operation == 11:
            argument = _normalize([value, _Value(equivalence_class + 1, identity)])
            before = list(model)
            model = [candidate for candidate in model if _find(argument, candidate) >= 0]
            changed = len(model) != len(before)
            actual = source.intersect(argument)
        elif operation == 12:
            argument = _normalize([value, _Value(equivalence_class + 1, identity)])
            receiver = list(model)
            model = [candidate for candidate in receiver if _find(argument, candidate) < 0]
            model.extend(candidate for candidate in argument if _find(receiver, candidate) < 0)
            changed = bool(argument)
            actual = source.symmetric_except(argument)
        else:
            changed = bool(model)
            model.clear()
            actual = source.clear()

        _assert_state(model, actual)
        assert (actual is source) == (not changed)
        versions.append((actual, model))
        for retained_actual, retained in versions[-6:]:
            _assert_state(retained, retained_actual)

    for actual, model in versions:
        _assert_state(model, actual)
