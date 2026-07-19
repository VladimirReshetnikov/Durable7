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


def test_ordered_cursors_expose_non_consuming_snapshots() -> None:
    """Every neutral Ordered cursor publishes its retained version without consuming itself."""

    set_source = PersistentOrderedSet.from_values(["a", "b", "c"])
    set_cursor = set_source.cursor_at(1)
    assert set_cursor.snapshot() is set_source
    edited_set = set_cursor.insert("x")
    assert edited_set.snapshot() is edited_set.set
    assert tuple(edited_set.snapshot()) == ("a", "x", "b", "c")
    assert set_cursor.snapshot() is set_source
    assert set_cursor.peek_next().value == "b"

    map_source = PersistentOrderedMap.from_items([("a", 1), ("b", 2)])
    map_cursor = map_source.cursor_at(1)
    assert map_cursor.snapshot() is map_source
    edited_map = map_cursor.insert("x", 9)
    assert edited_map.snapshot() is edited_map.map
    assert tuple(edited_map.snapshot().keys()) == ("a", "x", "b")
    assert map_cursor.snapshot() is map_source
    assert map_cursor.peek_next() == OrderedMapEntry("b", 2)

    multimap_source = PersistentOrderedMultimap.from_items([("a", 1), ("b", 2)])
    multimap_cursor = multimap_source.cursor_at(1)
    assert multimap_cursor.snapshot() is multimap_source
    edited_multimap = multimap_cursor.add("a", 3)
    assert edited_multimap.snapshot() is edited_multimap.map
    assert tuple(edited_multimap.snapshot()) == (
        OrderedMultimapEntry("a", 1),
        OrderedMultimapEntry("a", 3),
        OrderedMultimapEntry("b", 2),
    )
    assert multimap_cursor.snapshot() is multimap_source
    assert multimap_cursor.peek_next() == OrderedMultimapEntry("b", 2)
