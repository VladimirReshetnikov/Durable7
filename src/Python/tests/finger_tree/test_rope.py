"""Persistent positional, measured, and text rope tests."""

from __future__ import annotations

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from vladimir_reshetnikov.data_structures.finger_tree.measures import (
    IntegerSumMeasure,
    NumberSumMeasure,
    SizeMeasure,
)
from vladimir_reshetnikov.data_structures.finger_tree.rope import (
    LineColumn,
    MeasuredRope,
    MeasuredRopeCursorSearch,
    MeasuredRopeSplit,
    Rope,
    RopeBuilder,
    RopeCursorPeek,
    TextRope,
    TextRopeCursorSearch,
)


def test_positional_and_measured_edits_preserve_snapshots() -> None:
    basis = Rope.from_iterable([1, 2, 3, 4])
    inserted = basis.insert_range(2, [8, 9])
    assert inserted is not None
    edited = inserted.remove_range(1, 2)
    assert edited is not None
    assert basis.to_list() == [1, 2, 3, 4]
    assert edited.to_list() == [1, 9, 3, 4]

    measure = NumberSumMeasure()
    measured = MeasuredRope.from_iterable([2, 3, 5, 7], measure)
    assert measured.measure == 17
    located = measured.locate_by_measure(lambda total: total >= 8)
    assert (located.index, located.measure_before, located.value, located.found) == (2, 5, 5, True)
    sliced = measured.slice(1, 2)
    assert sliced is not None and sliced.measure == 8
    split = measured.split_at(2)
    assert isinstance(split, MeasuredRopeSplit)
    assert (split.left.to_list(), split.right.to_list()) == ([2, 3], [5, 7])


def test_measured_rope_concat_rejects_policy_mismatch_even_for_empty_operands() -> None:
    left_policy = IntegerSumMeasure()
    right_policy = IntegerSumMeasure()
    left = MeasuredRope.from_iterable([1], left_policy)
    right = MeasuredRope.from_iterable([2], right_policy)

    with pytest.raises(TypeError):
        MeasuredRope.empty(left_policy).concat(right)
    with pytest.raises(TypeError):
        left.concat(MeasuredRope.empty(right_policy))


def test_text_rope_maps_unicode_code_point_offsets_and_lines() -> None:
    text = TextRope.from_text("alpha\nbeta\n🙂x")
    assert text.line_count() == 3
    assert text.lines() == ["alpha", "beta", "🙂x"]
    assert text.line_column_of(8) == LineColumn(1, 2)
    assert text.offset_of(2, 2) == 13
    assert len(text) == 13


def test_builder_publishes_isolated_snapshots() -> None:
    builder = RopeBuilder().append("a").append_line("b")
    first = builder.to_text_rope()
    builder.append("c")
    assert first.as_string() == "ab\n"
    assert builder.as_string() == "ab\nc"


def test_persistent_gap_cursors_branch_from_retained_versions() -> None:
    source = Rope.from_text("abcdef")
    clean = source.get_cursor(3)
    assert clean.snapshot() is source
    assert (clean.peek_previous(), clean.peek_next()) == ("c", "d")
    left = clean.insert_range("XY").delete_next()
    right = clean.delete_previous().replace_next("Z")
    assert "".join(left.snapshot()) == "abcXYef"
    assert "".join(right.snapshot()) == "abZef"
    assert "".join(source) == "abcdef"


def test_cursor_entry_peeks_distinguish_stored_none_from_boundaries() -> None:
    source = Rope.from_iterable([None])
    start = source.get_cursor()
    assert start.peek_next() is None
    assert start.peek_previous_entry() is None
    assert start.peek_next_entry() == RopeCursorPeek(None)

    end = start.move_next()
    assert end.peek_previous() is None
    assert end.peek_previous_entry() == RopeCursorPeek(None)
    assert end.peek_next_entry() is None


def test_measured_cursor_entry_peeks_distinguish_stored_none_from_boundaries() -> None:
    source = MeasuredRope.from_iterable([None], SizeMeasure())
    start = source.get_cursor()
    assert start.peek_next() is None
    assert start.peek_previous_entry() is None
    assert start.peek_next_entry() == RopeCursorPeek(None)

    end = start.move_next()
    assert end.peek_previous() is None
    assert end.peek_previous_entry() == RopeCursorPeek(None)
    assert end.peek_next_entry() is None


def test_cursor_replacement_always_publishes_a_fresh_edit_without_equality() -> None:
    class Value:
        def __eq__(self, other: object) -> bool:
            raise AssertionError("cursor replacement must not compare elements")

    class CountingMeasure:
        identity = 0

        def __init__(self) -> None:
            self.calls = 0

        def combine(self, left: int, right: int) -> int:
            return left + right

        def measure(self, element: Value) -> int:
            self.calls += 1
            return 1

    value = Value()
    positional = Rope.from_iterable([value])
    positional_edit = positional.get_cursor().replace_next(value).snapshot()
    assert positional_edit is not positional
    assert positional_edit.get(0) is value

    policy = CountingMeasure()
    measured = MeasuredRope.from_iterable([value], policy)
    policy.calls = 0
    measured_edit = measured.get_cursor().replace_next(value).snapshot()
    assert measured_edit is not measured
    assert measured_edit.get(0) is value
    assert policy.calls > 0


def test_measured_cursors_retain_ordered_prefix_and_suffix_aggregates() -> None:
    rope = MeasuredRope.from_iterable([2, 3, 5, 7], NumberSumMeasure())
    cursor = rope.get_cursor(2)
    assert (cursor.measure_before, cursor.measure_after) == (5, 12)
    edited = cursor.insert(11).delete_previous()
    assert edited.position == 2
    assert edited.snapshot().to_list() == [2, 3, 5, 7]
    found = rope.get_cursor_by_measure(lambda total: total >= 8)
    assert found is not None and found.position == 2
    search = cursor.search_by_measure(lambda total: total >= 8)
    assert isinstance(search, MeasuredRopeCursorSearch)
    assert search.found and search.cursor.position == 2
    missing = rope.cursor_by_measure(lambda total: total >= 100)
    assert isinstance(missing, MeasuredRopeCursorSearch)
    assert not missing.found and missing.cursor.is_at_end


def test_text_cursors_navigate_line_and_column_without_mutating_old_text() -> None:
    source = TextRope.from_text("alpha\nbeta")
    cursor = source.get_cursor(6)
    assert (cursor.line, cursor.column) == (1, 0)
    edited = cursor.insert("new\n").seek_line_column(2, 2).insert("!")
    assert edited.snapshot().as_string() == "alpha\nnew\nbe!ta"
    assert source.as_string() == "alpha\nbeta"
    search = cursor.search_line_column(1, 2)
    assert isinstance(search, TextRopeCursorSearch)
    assert search.found and search.cursor.position == 8
    missing = cursor.search_line_column(2, 0)
    assert isinstance(missing, TextRopeCursorSearch)
    assert not missing.found and missing.cursor is cursor


@settings(max_examples=100)
@given(
    st.text(max_size=40),
    st.lists(st.tuples(st.integers(min_value=0, max_value=50), st.text(max_size=5)), max_size=30),
)
def test_cursor_insert_histories_agree_with_python_strings(
    initial: str, operations: list[tuple[int, str]]
) -> None:
    cursor = Rope.from_text(initial).get_cursor()
    model = initial
    position = 0
    for raw_position, inserted in operations:
        position = raw_position % (len(model) + 1)
        cursor = cursor.seek(position).insert_range(inserted)
        model = model[:position] + inserted + model[position:]
        position += len(inserted)
        assert "".join(cursor.snapshot()) == model
        assert cursor.position == position


def test_text_cursor_snapshot_reuses_the_path_copied_measured_rope(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    source = TextRope.from_text("alpha\nbeta\ngamma")
    edited = source.get_cursor(6).insert("XY")

    def _refuse(_characters: object) -> TextRope:
        raise AssertionError("A text cursor snapshot must not rebuild from characters.")

    monkeypatch.setattr(TextRope, "from_characters", _refuse)

    snapshot = edited.snapshot()
    assert snapshot is edited.snapshot()
    assert snapshot._characters is edited.cursor.snapshot()
    assert snapshot.as_string() == "alpha\nXYbeta\ngamma"
    assert snapshot.line_count() == 3

    assert (edited.line, edited.column) == (1, 2)
    assert edited.seek_line_column(2, 3).position == 16
    assert not edited.search_line_column(9, 0).found
    assert source.as_string() == "alpha\nbeta\ngamma"
