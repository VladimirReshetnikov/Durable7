"""Cursor contracts for the independently owned ordered collections."""

from vladimir_reshetnikov.data_structures.ordered import (
    OrderedMapEntry,
    OrderedMultimapEntry,
    PersistentOrderedMap,
    PersistentOrderedMultimap,
    PersistentOrderedSet,
)


def test_set_cursor_edits_explicit_gaps_and_distinguishes_none() -> None:
    """Set cursors retain snapshots, gap position, and presence for stored None."""

    source = PersistentOrderedSet.from_values(["a", None, "c"])
    cursor = source.cursor_at(1)
    assert cursor.peek_previous().value == "a"
    assert cursor.peek_next().found and cursor.peek_next().value is None

    cursor = cursor.insert("x")
    assert cursor.position == 2
    assert tuple(cursor.set) == ("a", "x", None, "c")
    assert cursor.insert(None) is cursor
    cursor = cursor.delete_previous()
    assert cursor.position == 1
    assert tuple(cursor.set) == ("a", None, "c")
    assert tuple(source) == ("a", None, "c")
    located = source.find_cursor(None)
    assert located.found and located.cursor.position == 1


def test_map_cursor_inserts_updates_and_deletes_in_explicit_order() -> None:
    """Map cursors preserve source versions while editing the focused gap."""

    source = PersistentOrderedMap.from_items([("a", 1), ("b", 2), ("c", 3)])
    cursor = source.cursor_at(1).insert("x", 9)
    assert cursor.position == 2
    assert tuple(cursor.map.keys()) == ("a", "x", "b", "c")
    cursor = cursor.set_next_value(20)
    assert cursor.peek_next() == OrderedMapEntry("b", 20)
    inserted, duplicate = cursor.try_insert("b", 200)
    assert not inserted and duplicate.position == 2
    cursor = cursor.delete_previous()
    assert tuple(cursor.map.keys()) == ("a", "b", "c")
    assert source["b"] == 2


def test_multimap_cursor_uses_flattened_grouped_pair_ranks() -> None:
    """Multimap cursor edits follow key-grouped pair enumeration."""

    source = PersistentOrderedMultimap.from_items([("b", 2), ("a", 9), ("b", 1), ("c", 7)])
    located = source.find_cursor("b", 1)
    assert located.found and located.cursor.position == 1
    cursor = located.cursor.add("b", 3)
    assert cursor.position == 3
    assert tuple(cursor.map) == (
        OrderedMultimapEntry("b", 2),
        OrderedMultimapEntry("b", 1),
        OrderedMultimapEntry("b", 3),
        OrderedMultimapEntry("a", 9),
        OrderedMultimapEntry("c", 7),
    )
    assert cursor.add("b", 3) is cursor
    cursor = cursor.delete_previous().delete_next()
    assert cursor.position == 2
    assert cursor.peek_next() == OrderedMultimapEntry("c", 7)
    assert tuple(cursor.map) == (
        OrderedMultimapEntry("b", 2),
        OrderedMultimapEntry("b", 1),
        OrderedMultimapEntry("c", 7),
    )
    assert source.pair_count == 4
    group = source.find_group_cursor("a")
    assert group.found and group.cursor.position == 2
