from __future__ import annotations

from dataclasses import dataclass

from vladimir_reshetnikov.data_structures.finger_tree import (
    CanonicalSortedSet,
    Interval,
    IntervalTree,
    PersistentChunkedBitSet,
    PersistentIntervalMap,
    PrioritySearchEntry,
    PrioritySearchQueue,
    SortedBag,
    SortedMap,
    SortedSet,
    ZipTreeRankPolicy,
)


@dataclass(frozen=True)
class _Ranked:
    rank: int
    name: str


def _ranked_comparator(left: _Ranked, right: _Ranked) -> int:
    return left.rank - right.rank


def _compare_text(left: str, right: str) -> int:
    return (left.lower() > right.lower()) - (left.lower() < right.lower())


def test_sorted_cursors_preserve_occurrences_representatives_and_strictness() -> None:
    first = _Ranked(2, "first")
    second = _Ranked(2, "second")
    third = _Ranked(2, "third")
    bag = SortedBag.from_iterable([_Ranked(1, "low"), first, second, third], _ranked_comparator)
    cursor = bag.cursor_at(2)
    candidate = cursor.peek_next()
    assert candidate is not None and candidate.value is second
    assert cursor.delete_next().snapshot().to_list() == [_Ranked(1, "low"), first, third]
    assert bag.cursor_at_upper_bound(_Ranked(2, "probe")).position == 4

    set_ = SortedSet.from_iterable(["a", "c"], _compare_text)
    miss = set_.find_cursor("b")
    assert not miss.found and miss.cursor.position == 1
    assert miss.cursor.add("A").snapshot() is set_
    assert miss.cursor.add("b").snapshot().to_list() == ["a", "b", "c"]

    map_ = SortedMap.from_iterable([(1, "one"), (3, "three")])
    inserted = map_.cursor_at_lower_bound(2).insert(2, "two")
    assert inserted.position == 2
    assert inserted.move_previous().set_next_value("TWO").snapshot().get(2) == "TWO"
    assert not map_.cursor_at().try_insert(1, "duplicate").found


def test_canonical_and_priority_search_cursors_use_ordinary_repairs() -> None:
    policy: ZipTreeRankPolicy[int] = ZipTreeRankPolicy.create(seed=42)
    canonical = CanonicalSortedSet.from_iterable([1, 4, 7], policy)
    edited = canonical.cursor_at_lower_bound(4).add(3).delete_previous().snapshot()
    expected = canonical.add(3).remove(3)
    assert edited.shape_signature() == expected.shape_signature()
    assert edited.policy is policy
    edited.validate_structure()

    queue = PrioritySearchQueue.from_iterable(
        [
            PrioritySearchEntry(1, 30, "one"),
            PrioritySearchEntry(2, 10, "two"),
            PrioritySearchEntry(3, 20, "three"),
        ]
    )
    winner = queue.cursor_at_minimum_priority()
    winner_entry = winner.peek_next()
    assert winner_entry is not None and winner_entry.value.key == 2
    updated = winner.set_next(40, "TWO")
    assert updated.snapshot().minimum.key == 3
    updated.snapshot().validate_structure()


def test_interval_cursors_delete_exact_occurrences_and_continue_exclusively() -> None:
    first = Interval(1, 5)
    middle = Interval(3, 8)
    second = Interval(1, 5)
    tree: IntervalTree[int] = IntervalTree.empty()
    tree = tree.insert(first).insert(middle).insert(second)
    assert tree.to_list() == [second, first, middle]
    assert tree.cursor_at(1).delete_next().snapshot().to_list() == [second, middle]

    overlap = tree.find_overlap_cursor(Interval(4, 4))
    assert overlap.found
    continuation = overlap.cursor.seek_next_overlap(Interval(4, 4))
    assert continuation.found and continuation.cursor.position > overlap.cursor.position

    map_ = PersistentIntervalMap.from_items(
        [(Interval(1, 4), "a"), (Interval(3, 7), "b"), (Interval(9, 10), "c")]
    )
    exact = map_.find_cursor(Interval(3, 7))
    assert exact.found
    assert exact.cursor.set_next_value("B").snapshot().get(Interval(3, 7)) == "B"
    map_overlap = map_.find_overlap_cursor(Interval(4, 9))
    assert map_overlap.cursor.seek_next_overlap(Interval(4, 9)).found


def test_chunked_bit_cursor_uses_population_rank_and_present_identity() -> None:
    bits = PersistentChunkedBitSet.from_values([1, 63, 64, 130])
    cursor = bits.cursor_at_or_after(62)
    assert cursor.position == 1
    next_bit = cursor.peek_next()
    assert next_bit is not None and next_bit.value == 63
    assert not bits.find_cursor(62).found
    added = cursor.add(62)
    assert list(added.snapshot()) == [1, 62, 63, 64, 130]
    assert list(added.delete_previous().snapshot()) == [1, 63, 64, 130]
    assert cursor.add(63) is cursor
