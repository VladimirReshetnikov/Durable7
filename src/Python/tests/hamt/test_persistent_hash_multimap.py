from dataclasses import dataclass

from vladimir_reshetnikov.data_structures.hamt import (
    HashPolicy,
    PersistentHashMultimap,
    create_hash_policy,
)


@dataclass(frozen=True)
class Box:
    identifier: int
    name: str


BOXES: HashPolicy[Box] = create_hash_policy(
    lambda box: box.identifier,
    lambda left, right: left.identifier == right.identifier,
)


def test_retains_first_representatives_in_both_domains() -> None:
    key = Box(1, "key-first")
    value = Box(2, "value-first")
    multimap = (
        PersistentHashMultimap.empty(BOXES, BOXES)
        .add(key, value)
        .add(Box(1, "key-later"), Box(2, "value-later"))
    )
    assert multimap.key_count == 1
    assert multimap.pair_count == 1
    assert multimap.try_get_key(Box(1, "probe")).key is key
    assert multimap.get_values(key).get(Box(2, "probe")) is value


def test_contracts_final_pair_and_preserves_source() -> None:
    source = PersistentHashMultimap.from_items((("a", 1), ("a", 2), ("b", 3)))
    one_left = source.remove("a", 1)
    contracted = one_left.remove("a", 2)
    assert one_left.key_count == 2
    assert contracted.key_count == 1
    assert contracted.pair_count == 1
    assert not contracted.contains_key("a")
    assert source.pair_count == 3


def test_removes_whole_key_class() -> None:
    source = PersistentHashMultimap.from_items((("a", 1), ("a", 2), ("b", 3)))
    result = source.remove_key("a")
    assert list(result) == [next(entry for entry in result if entry.key == "b")]
    assert result.remove_key("missing") is result


def test_duplicate_and_missing_edits_preserve_identity_and_root() -> None:
    source = PersistentHashMultimap[str, int].empty().add("a", 1)
    duplicate = source.add("a", 1)
    assert duplicate is source
    assert duplicate.shares_root_with(source)
    assert source.remove("a", 2) is source


def test_preserves_independent_policies_and_retained_branches() -> None:
    keys: HashPolicy[str] = create_hash_policy(
        len, lambda left, right: left.casefold() == right.casefold()
    )
    values: HashPolicy[int] = create_hash_policy(
        lambda value: value, lambda left, right: left == right
    )
    source = PersistentHashMultimap.empty(keys, values).add("Alpha", 1)
    branch = source.add("ALPHA", 2)
    assert source.key_policy is keys
    assert source.value_policy is values
    assert source.pair_count == 1
    assert branch.pair_count == 2


def test_validates_counts_and_presence_safe_empty_lookup() -> None:
    multimap = PersistentHashMultimap.from_items((("a", 1), ("a", 2), ("b", 2)))
    lookup = multimap.try_get_values("missing")
    assert not lookup.found
    assert lookup.values.is_empty
    assert multimap.validate_structure()
    assert multimap.clear().validate_structure()
