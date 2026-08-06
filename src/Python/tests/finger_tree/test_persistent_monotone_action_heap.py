"""Tests for the persistent monotone-action Brodal-Okasaki heap.

The suite concentrates on the places where a wrong-but-plausible implementation stays silent:
the direction of action composition, the collapse cases of the shipped clamp algebra, the
temporal rule that a later insertion or meld is never retroactively transformed, the two
tie-breaks, the split-forest decomposition, and the absence of deep recursion.  It also pins the
tagged heap's structural shape against the untagged sibling it is derived from, so a divergence in
the skew-binomial kernel itself cannot hide behind the tagging.
"""

from __future__ import annotations

import sys
from typing import TypeVar

import pytest
from hypothesis import given, settings
from hypothesis import strategies as st

from durable7.finger_tree.brodal_okasaki_heap import BrodalOkasakiHeap
from durable7.finger_tree.ordering import Comparator, default_comparator
from durable7.finger_tree.persistent_monotone_action_heap import (
    ComparatorBoundMonotoneHeapAction,
    MonotoneHeapAction,
    MonotoneHeapEntry,
    OrderClamp,
    OrderClampPolicy,
    PersistentMonotoneActionHeap,
    _Forest,
    _Tree,
)

E = TypeVar("E")

Clamp = OrderClamp[int]
IntHeap = PersistentMonotoneActionHeap[str, int, Clamp]
Spine = _Forest[str, int, Clamp] | None

_KeyedPriority = tuple[int, str]


def _keyed_comparator(left: _KeyedPriority, right: _KeyedPriority) -> int:
    """Compare only the key half, so distinct representatives can compare equal."""

    return -1 if left[0] < right[0] else 1 if left[0] > right[0] else 0


class ShiftPolicy:
    """An additive monotone action family that names no comparator.

    Used to exercise the unbound-policy branch of heap creation and to check that the action
    abstraction really is a structural protocol rather than a closed clamp hierarchy.
    """

    @property
    def identity(self) -> int:
        """The zero shift."""

        return 0

    def is_identity(self, action: int) -> bool:
        """Whether the shift moves nothing."""

        return action == 0

    def compose(self, outer: int, inner: int) -> int:
        """Shifts commute, so composition is plain addition."""

        return outer + inner

    def apply(self, action: int, priority: int) -> int:
        """Shift one priority."""

        return action + priority


def _heap(policy: OrderClampPolicy[int]) -> IntHeap:
    return PersistentMonotoneActionHeap.empty(policy)


def _from_pairs(pairs: list[tuple[str, int]], policy: OrderClampPolicy[int]) -> IntHeap:
    heap: IntHeap = PersistentMonotoneActionHeap.from_entries(pairs, policy)
    return heap


def _drain(heap: PersistentMonotoneActionHeap[E, int, Clamp]) -> list[tuple[E, int]]:
    """Remove every entry in ascending priority order, validating the shape at each step."""

    output: list[tuple[E, int]] = []
    while not heap.is_empty:
        heap.validate_structure()
        view = heap.minimum_view()
        assert view is not None
        output.append((view.minimum.element, view.minimum.priority))
        heap = view.remainder
    return output


def _priorities(heap: PersistentMonotoneActionHeap[E, int, Clamp]) -> list[int]:
    return sorted(entry.priority for entry in heap)


def _synthetic(policy: OrderClampPolicy[int], rank: int, name: str) -> _Tree[str, int, Clamp]:
    """A childless tree of a chosen rank; the forest decomposition reads only ranks."""

    return _Tree(rank, name, 0, None, policy.identity)


def _spine(heap: IntHeap, *trees: _Tree[str, int, Clamp]) -> Spine:
    """Build an identity-tagged spine holding ``trees`` in the order given."""

    result: Spine = None
    for tree in reversed(trees):
        result = heap._cons(tree, result)
    return result


def _names(heap: IntHeap, forest: Spine) -> list[str]:
    """The element names of a spine's logical trees, in spine order."""

    return [tree.element for tree in heap._forest_to_list(forest)]


# ---------------------------------------------------------------------------------------------
# Kernel equivalence with the untagged sibling
# ---------------------------------------------------------------------------------------------


def test_untagged_use_matches_the_sibling_skew_binomial_heap_exactly() -> None:
    values = [(value * 7919) % 1021 for value in range(400)]
    sibling = BrodalOkasakiHeap.from_iterable(values)
    tagged = _from_pairs([(str(value), value) for value in values], OrderClampPolicy[int]())

    sibling_shape = sibling.validate_structure()
    tagged_shape = tagged.validate_structure()
    assert tagged_shape.count == sibling_shape.count
    assert tagged_shape.root_forest_length == sibling_shape.root_forest_length
    assert tagged_shape.maximum_rank == sibling_shape.maximum_rank
    assert tagged_shape.maximum_depth == sibling_shape.maximum_depth
    assert tagged_shape.tagged_component_count == 0

    sibling_drain: list[int] = []
    while not sibling.is_empty:
        view = sibling.minimum_view()
        assert view is not None
        sibling_drain.append(view.minimum)
        sibling = view.remainder
    assert [priority for _, priority in _drain(tagged)] == sibling_drain
    assert sibling_drain == sorted(values)


# ---------------------------------------------------------------------------------------------
# Composition direction
# ---------------------------------------------------------------------------------------------


def test_clamp_composition_treats_the_outer_action_as_the_later_one() -> None:
    policy = OrderClampPolicy[int]()
    floor = policy.at_least(10)
    cap = policy.at_most(5)

    # cap(floor(x)) is 5 for every x; floor(cap(x)) is 10 for every x.  The two disagree on every
    # input, so this pins the direction rather than merely exercising it.
    assert policy.compose(cap, floor) == policy.constant(5)
    assert policy.compose(floor, cap) == policy.constant(10)
    for sample in (-100, 0, 4, 5, 7, 10, 11, 1000):
        assert policy.apply(policy.compose(cap, floor), sample) == 5
        assert policy.apply(policy.compose(floor, cap), sample) == 10


def test_transform_all_composes_the_newer_action_outermost() -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(chr(97 + index), index) for index in range(6)], policy)

    later_cap = heap.transform_all(policy.at_least(10)).transform_all(policy.at_most(5))
    later_floor = heap.transform_all(policy.at_most(5)).transform_all(policy.at_least(10))
    assert _priorities(later_cap) == [5] * 6
    assert _priorities(later_floor) == [10] * 6
    assert later_cap.validate_structure().count == 6
    assert later_floor.validate_structure().count == 6
    assert _priorities(heap) == list(range(6))


def test_composition_direction_survives_every_tag_pushdown_site() -> None:
    """A drain forces exposure, forest tagging, skew links, and ranked links to compose tags.

    Transforming between deletions means the surviving structure carries tags at tree nodes and
    at forest spine cells simultaneously, so a reversed composition at any one site shows up as a
    wrong drained priority.
    """

    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(str(value), value) for value in range(40)], policy)
    model = list(range(40))
    output: list[int] = []
    step = 0
    while not heap.is_empty:
        heap.validate_structure()
        if step % 3 == 0:
            # A collapsing floor/cap pair, applied newest last. Composition here is *not*
            # commutative: composed in the correct order the pair collapses to constant(base),
            # while a reversal at either pushdown site yields constant(base + 1000). A pure floor
            # would be commutative and would leave both reversals invisible.
            base = 100 * (step + 1)
            for action in (policy.at_least(base + 1000), policy.at_most(base)):
                heap = heap.transform_all(action)
                model = [policy.apply(action, priority) for priority in model]
        view = heap.minimum_view()
        assert view is not None
        # The model applies each action immediately and never composes, so it is the ground truth
        # a mis-ordered composition disagrees with.
        assert view.minimum.priority == min(model)
        model.remove(min(model))
        output.append(view.minimum.priority)
        heap = view.remainder
        assert _priorities(heap) == sorted(model)
        step += 1
    assert output == sorted(output)


# ---------------------------------------------------------------------------------------------
# Clamp collapse cases
# ---------------------------------------------------------------------------------------------


def test_clamp_composition_short_circuits_on_either_identity() -> None:
    policy = OrderClampPolicy[int]()
    identity = policy.identity
    floor = policy.at_least(3)
    assert policy.is_identity(identity)
    assert policy.compose(identity, floor) is floor
    assert policy.compose(floor, identity) is floor
    assert policy.compose(identity, identity) == identity


def test_an_outer_constant_wins_outright() -> None:
    policy = OrderClampPolicy[int]()
    constant = policy.constant(7)
    for inner in (policy.at_least(100), policy.at_most(-100), policy.between(0, 1)):
        assert policy.compose(constant, inner) is constant


def test_an_inner_constant_receives_the_outer_action() -> None:
    policy = OrderClampPolicy[int]()
    assert policy.compose(policy.at_least(10), policy.constant(3)) == policy.constant(10)
    assert policy.compose(policy.at_most(2), policy.constant(3)) == policy.constant(2)
    assert policy.compose(policy.between(0, 9), policy.constant(3)) == policy.constant(3)


def test_disjoint_bounds_collapse_to_a_constant_carrying_the_outer_boundary() -> None:
    policy = OrderClampPolicy[int]()
    # inner caps below the outer floor: only the outer floor's value can ever come out.
    assert policy.compose(policy.at_least(10), policy.at_most(5)) == policy.constant(10)
    # inner floors above the outer cap: only the outer cap's value can ever come out.
    assert policy.compose(policy.at_most(5), policy.at_least(10)) == policy.constant(5)
    assert policy.compose(policy.between(20, 30), policy.between(0, 10)) == policy.constant(20)
    assert policy.compose(policy.between(0, 10), policy.between(20, 30)) == policy.constant(10)


def test_overlapping_bounds_intersect_and_keep_the_inner_representative() -> None:
    policy: OrderClampPolicy[_KeyedPriority] = OrderClampPolicy(_keyed_comparator)
    inner = policy.between((0, "inner"), (10, "inner"))
    outer = policy.between((0, "outer"), (10, "outer"))
    composed = policy.compose(outer, inner)
    assert not composed.is_constant
    assert composed.lower_bound == (0, "inner")
    assert composed.upper_bound == (10, "inner")

    narrower = policy.compose(policy.between((3, "outer"), (7, "outer")), inner)
    assert narrower.lower_bound == (3, "outer")
    assert narrower.upper_bound == (7, "outer")

    numeric = OrderClampPolicy[int]()
    assert numeric.compose(numeric.at_most(4), numeric.at_least(1)) == numeric.between(1, 4)
    assert numeric.compose(numeric.at_least(1), numeric.at_most(4)) == numeric.between(1, 4)


def test_clamp_application_tests_constant_then_lower_then_upper() -> None:
    policy = OrderClampPolicy[int]()
    # An inverted clamp is not producible through the policy, but it is representable, and it is
    # the only way to observe that the lower bound is consulted before the upper one.
    inverted = OrderClamp(
        has_lower_bound=True,
        lower_bound_or_none=5,
        has_upper_bound=True,
        upper_bound_or_none=3,
    )
    # Between the two crossed bounds both branches are live, and only the order decides.
    assert policy.apply(inverted, 4) == 5
    assert policy.apply(inverted, 0) == 5
    assert policy.apply(inverted, 100) == 3

    constant_with_bounds = OrderClamp(
        is_constant=True,
        constant_or_none=42,
        has_lower_bound=True,
        lower_bound_or_none=5,
        has_upper_bound=True,
        upper_bound_or_none=7,
    )
    assert policy.apply(constant_with_bounds, 0) == 42


def test_a_constant_action_returns_its_exact_representative() -> None:
    policy: OrderClampPolicy[_KeyedPriority] = OrderClampPolicy(_keyed_comparator)
    assert policy.apply(policy.constant((5, "exact")), (5, "other")) == (5, "exact")
    # An inclusive two-sided clamp is not the same function: it preserves an equivalent input.
    assert policy.apply(policy.between((5, "low"), (5, "high")), (5, "other")) == (5, "other")


def test_clamp_construction_rejects_crossed_and_inconsistent_values() -> None:
    policy = OrderClampPolicy[int]()
    with pytest.raises(ValueError, match="must not compare greater"):
        policy.between(4, 3)
    assert policy.between(3, 3) == policy.between(3, 3)

    with pytest.raises(ValueError, match="non-constant clamp"):
        OrderClamp(constant_or_none=1)
    with pytest.raises(ValueError, match="lower value"):
        OrderClamp(lower_bound_or_none=1)
    with pytest.raises(ValueError, match="upper value"):
        OrderClamp(upper_bound_or_none=1)

    with pytest.raises(ValueError, match="not constant"):
        _ = policy.at_least(1).constant_value
    with pytest.raises(ValueError, match="no lower bound"):
        _ = policy.at_most(1).lower_bound
    with pytest.raises(ValueError, match="no upper bound"):
        _ = policy.at_least(1).upper_bound


# ---------------------------------------------------------------------------------------------
# Temporal semantics
# ---------------------------------------------------------------------------------------------


def test_an_insertion_after_a_transform_is_not_retroactively_transformed() -> None:
    policy = OrderClampPolicy[int]()
    base = _from_pairs([(str(value), value) for value in range(8)], policy)
    raised = base.transform_all(policy.at_least(50))
    assert _priorities(raised) == [50] * 8

    # The new entry loses the root comparison, so the old root must be exposed before the
    # singleton is skew-inserted.  Skipping that exposure would surface the untransformed
    # priorities of the old root and of every tree in its forest.
    losing = raised.insert("late", 90)
    assert _priorities(losing) == [50] * 8 + [90]
    assert losing.minimum.priority == 50
    losing.validate_structure()

    # The new entry wins the root comparison, and must still keep its own untransformed priority.
    winning = raised.insert("early", 10)
    assert _priorities(winning) == [10] + [50] * 8
    assert winning.minimum == MonotoneHeapEntry("early", 10)
    winning.validate_structure()

    # A second transform reaches both generations, applying to their current logical priorities.
    capped = losing.transform_all(policy.at_most(70))
    assert _priorities(capped) == [50] * 8 + [70]
    assert _priorities(base) == list(range(8))


def test_a_meld_after_a_transform_leaves_each_operand_history_intact() -> None:
    policy = OrderClampPolicy[int]()
    left = _from_pairs([(f"l{value}", value) for value in range(6)], policy)
    right = _from_pairs([(f"r{value}", value + 100) for value in range(6)], policy)

    raised = left.transform_all(policy.at_least(50))
    merged = raised.meld(right)
    merged.validate_structure()
    assert _priorities(merged) == [50] * 6 + [100, 101, 102, 103, 104, 105]
    assert _priorities(raised) == [50] * 6
    assert _priorities(right) == [100, 101, 102, 103, 104, 105]

    # The receiver loses the root comparison here, so the other operand's root is the one exposed.
    other = right.transform_all(policy.at_most(0))
    reversed_merge = raised.meld(other)
    reversed_merge.validate_structure()
    assert _priorities(reversed_merge) == [0] * 6 + [50] * 6

    # A transform applied to the melded version reaches both histories at once.
    assert _priorities(merged.transform_all(policy.constant(9))) == [9] * 12


def test_transform_all_allocates_constant_structure_and_is_inert_when_it_can_be() -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(str(value), value) for value in range(64)], policy)

    transformed = heap.transform_all(policy.at_least(5))
    assert transformed._root_children_identity is heap._root_children_identity
    assert transformed._root_identity is not heap._root_identity
    assert len(transformed) == len(heap)
    assert transformed.shared_tree_count(heap) > 0
    # One tag is retained, but auditing it means exposing it onto every logical view below the
    # root, so the reported count measures observations rather than retained objects.
    assert heap.validate_structure().tagged_component_count == 0
    assert transformed.validate_structure().tagged_component_count > 0

    assert heap.transform_all(policy.identity) is heap
    empty: IntHeap = PersistentMonotoneActionHeap.empty(policy)
    assert empty.transform_all(policy.at_least(5)) is empty


# ---------------------------------------------------------------------------------------------
# Tie-breaking, self-meld, and iteration
# ---------------------------------------------------------------------------------------------


def test_an_insert_root_tie_favours_the_new_entry() -> None:
    policy = OrderClampPolicy[int]()
    heap = _heap(policy).insert("first", 5).insert("second", 5).insert("third", 5)
    assert heap.minimum == MonotoneHeapEntry("third", 5)
    assert heap.try_minimum() == MonotoneHeapEntry("third", 5)
    # A strictly larger key must not displace the root.
    assert heap.insert("bigger", 6).minimum == MonotoneHeapEntry("third", 5)
    assert heap.insert("smaller", 4).minimum == MonotoneHeapEntry("smaller", 4)


def test_the_forest_minimum_favours_the_earliest_spine_occurrence() -> None:
    policy = OrderClampPolicy[int]()
    heap = _heap(policy)
    forest = heap._cons(
        _synthetic(policy, 0, "a"),
        heap._cons(
            _synthetic(policy, 0, "b"),
            heap._cons(_synthetic(policy, 0, "c"), None),
        ),
    )
    # Tie between "b" and "c"; the earlier spine position must win.
    tied = _Tree(0, "b", 3, None, policy.identity)
    later = _Tree(0, "c", 3, None, policy.identity)
    forest = heap._cons(
        _Tree(0, "a", 5, None, policy.identity), heap._cons(tied, heap._cons(later, None))
    )
    minimum, remainder = heap._get_minimum(forest)
    assert minimum.element == "b"
    assert [tree.element for tree in heap._forest_to_list(remainder)] == ["a", "c"]

    # And the same tie-break is observable through a public drain.
    entries = [("a", 1), ("b", 1), ("c", 2), ("d", 2), ("e", 3), ("f", 3), ("g", 4), ("h", 4)]
    drained = _drain(_from_pairs(entries, policy))
    assert [priority for _, priority in drained] == [1, 1, 2, 2, 3, 3, 4, 4]
    assert drained == [
        ("b", 1),
        ("a", 1),
        ("c", 2),
        ("d", 2),
        ("f", 3),
        ("e", 3),
        ("h", 4),
        ("g", 4),
    ]


def test_melding_a_heap_with_itself_duplicates_every_logical_occurrence() -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([("a", 1), ("b", 2), ("c", 2), ("d", 5)], policy)
    doubled = heap.meld(heap)
    assert doubled.validate_structure().count == 8
    assert _priorities(doubled) == [1, 1, 2, 2, 2, 2, 5, 5]
    assert sorted(entry.element for entry in doubled) == list("aabbccdd")

    transformed = heap.transform_all(policy.at_least(3))
    both = transformed.meld(transformed)
    assert _priorities(both) == [3, 3, 3, 3, 3, 3, 5, 5]
    assert _priorities(heap) == [1, 2, 2, 5]


def test_iteration_applies_every_pending_action_exactly_once() -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(str(value), value) for value in range(32)], policy)
    staged = heap.transform_all(policy.at_least(4)).transform_all(policy.at_most(20))
    expected = sorted(min(max(value, 4), 20) for value in range(32))
    assert _priorities(staged) == expected
    assert sorted(entry.element for entry in staged) == sorted(str(v) for v in range(32))
    # Draining and iterating must agree entry for entry.
    assert sorted(priority for _, priority in _drain(staged)) == expected


# ---------------------------------------------------------------------------------------------
# Split-forest decomposition
# ---------------------------------------------------------------------------------------------


def test_split_forest_covers_every_decomposition_branch() -> None:
    policy = OrderClampPolicy[int]()
    heap = _heap(policy)

    def spine(*trees: _Tree[str, int, Clamp]) -> Spine:
        return _spine(heap, *trees)

    zero = _synthetic(policy, 0, "z")
    other_zero = _synthetic(policy, 0, "y")
    rank_one = _synthetic(policy, 1, "o")
    other_one = _synthetic(policy, 1, "p")
    rank_two = _synthetic(policy, 2, "t")

    # rank one, singleton spine: the only tree becomes a ranked tree.
    split = heap._split_forest(1, spine(zero))
    assert _names(heap, split.zeros) == []
    assert _names(heap, split.trees) == ["z"]
    assert split.embedded_forest is None

    # rank one, second child also rank zero: the ambiguity resolves toward the structural prefix.
    split = heap._split_forest(1, spine(zero, other_zero, rank_one))
    assert _names(heap, split.zeros) == ["z"]
    assert _names(heap, split.trees) == ["y"]
    assert _names(heap, split.embedded_forest) == ["o"]

    # rank one, second child ranked: nothing is a zero and the second child stays embedded.
    split = heap._split_forest(1, spine(zero, rank_one, other_one))
    assert _names(heap, split.zeros) == []
    assert _names(heap, split.trees) == ["z"]
    assert _names(heap, split.embedded_forest) == ["o", "p"]

    # equal leading ranks: a skew link is undone in one step.
    split = heap._split_forest(2, spine(rank_one, other_one, zero))
    assert _names(heap, split.zeros) == []
    assert _names(heap, split.trees) == ["o", "p"]
    assert _names(heap, split.embedded_forest) == ["z"]

    # leading rank zero at rank two: the zero is peeled off and the walk continues.
    split = heap._split_forest(2, spine(zero, rank_one, other_zero))
    assert _names(heap, split.zeros) == ["z"]
    assert _names(heap, split.trees) == ["y", "o"]
    assert split.embedded_forest is None

    # leading ranked child at rank two: the second child is pushed back onto the walk.
    split = heap._split_forest(2, spine(rank_one, zero, other_zero))
    assert _names(heap, split.zeros) == ["z"]
    assert _names(heap, split.trees) == ["y", "o"]
    assert split.embedded_forest is None

    # rank zero returns the whole forest untouched.
    split = heap._split_forest(0, spine(zero, rank_one))
    assert split.zeros is None
    assert split.trees is None
    assert _names(heap, split.embedded_forest) == ["z", "o"]

    with pytest.raises(ValueError, match="skew-binomial forest invariant"):
        heap._split_forest(1, None)
    with pytest.raises(ValueError, match="skew-binomial forest invariant"):
        heap._split_forest(2, spine(rank_two))


def test_the_forest_spine_composes_a_newer_tag_outermost() -> None:
    """Pin the forest-side pushdown directly, with two *stacked* non-identity spine tags.

    ``_tag_forest`` is the one composition site a whole-heap drain barely reaches: ``_cons`` builds
    identity-tagged cells, and composing against the identity is commutative, so a reversal there
    stays invisible until two non-identity tags meet on one spine. This builds that shape by hand -
    an inner cell already carrying a floor, then an outer cap pushed onto it - so a reversed
    ``compose`` at line 687 changes the answer rather than merely reordering equal work.

    Correct order caps last and collapses to 10; reversed, the floor wins and every priority
    reads 1000.
    """

    policy = OrderClampPolicy[int]()
    heap = _heap(policy)
    floor = policy.at_least(1000)
    cap = policy.at_most(10)

    # An inner cell whose pending action is the floor, so it is not the identity.
    inner = _Forest(_synthetic(policy, 0, "inner"), None, floor)
    # The outer cell carries the cap and, through _forest_tail, pushes it onto the inner cell.
    outer = _Forest(_synthetic(policy, 0, "outer"), inner, cap)

    tail = heap._forest_tail(outer)
    assert tail is not None

    # Tagging composes but does not apply; the pending action is what the reversal corrupts.
    assert policy.apply(tail.action, tail.head.priority) == 10, "the cap is newer, so it wins"
    assert tail.action == policy.constant(10)

    # The same pair composed by hand, as an independent statement of the expected direction.
    assert policy.compose(cap, floor) == policy.constant(10)
    assert policy.compose(floor, cap) == policy.constant(1000)


def test_a_meld_tie_between_the_two_roots_favours_the_receiver() -> None:
    """``meld`` documents that a root tie favours the receiver; nothing else observes it.

    Every other meld in this file has strictly ordered roots, and ``heap.meld(heap)`` compares a
    root against itself, so the tie rule was a free mutation: relaxing ``_le`` to a strict ``<``
    left the whole file green. Both directions are asserted, because one alone pins an element
    rather than receiver-favouring.
    """

    policy = OrderClampPolicy[int]()
    left = _from_pairs([("L0", 5), ("L1", 9)], policy)
    right = _from_pairs([("R0", 5), ("R1", 7)], policy)

    assert left.meld(right).minimum == MonotoneHeapEntry("L0", 5)
    assert right.meld(left).minimum == MonotoneHeapEntry("R0", 5)


def test_split_forest_collects_zeros_so_they_reinsert_in_first_encountered_order() -> None:
    policy = OrderClampPolicy[int]()
    heap = _heap(policy)

    split = heap._split_forest(
        3,
        _spine(
            heap,
            _synthetic(policy, 0, "first"),
            _synthetic(policy, 2, "t2"),
            _synthetic(policy, 0, "second"),
            _synthetic(policy, 1, "t1"),
            _synthetic(policy, 0, "tail"),
        ),
    )
    # The accumulator conses, so the retained spine is newest-first; delete-minimum reverses it
    # before skew-inserting, which is what restores first-encountered order.
    assert _names(heap, split.zeros) == ["second", "first"]
    assert _names(heap, split.trees) == ["tail", "t1", "t2"]


# ---------------------------------------------------------------------------------------------
# Empty heaps, policies, and validation
# ---------------------------------------------------------------------------------------------


def test_empty_heap_operations_report_absence_without_guessing() -> None:
    policy = OrderClampPolicy[int]()
    empty: IntHeap = PersistentMonotoneActionHeap.empty(policy)
    assert empty.is_empty
    assert len(empty) == 0
    assert empty.count == 0
    assert empty.try_minimum() is None
    assert empty.minimum_view() is None
    assert list(empty) == []
    shape = empty.validate_structure()
    assert (shape.count, shape.root_forest_length, shape.maximum_rank) == (0, 0, 0)
    assert (shape.maximum_depth, shape.tagged_component_count) == (0, 0)

    with pytest.raises(IndexError, match="empty"):
        _ = empty.minimum
    with pytest.raises(IndexError, match="empty"):
        empty.delete_minimum()

    populated = empty.insert("a", 1)
    assert empty.meld(populated) is populated
    assert populated.meld(empty) is populated
    view = populated.minimum_view()
    assert view is not None
    assert view.minimum == MonotoneHeapEntry("a", 1)
    assert view.remainder.is_empty
    assert view.remainder.try_minimum() is None
    assert empty.is_empty


def test_melding_requires_the_same_comparator_and_action_policy_objects() -> None:
    first = OrderClampPolicy[int]()
    second = OrderClampPolicy[int]()
    left = _from_pairs([("a", 1)], first)
    right = _from_pairs([("b", 2)], second)
    assert first.comparator is second.comparator

    with pytest.raises(TypeError, match="same action-policy object"):
        left.meld(right)

    other_comparator: Comparator[int] = lambda a, b: a - b  # noqa: E731
    bound = OrderClampPolicy[int](other_comparator)
    with pytest.raises(TypeError, match="same comparator object"):
        left.meld(_from_pairs([("c", 3)], bound))

    # Same policy object, so melding is allowed and both sources stay usable.
    sibling = _from_pairs([("d", 4)], first)
    merged = left.meld(sibling)
    assert merged.validate_structure().count == 2
    assert _priorities(left) == [1]
    assert _priorities(sibling) == [4]


def test_heap_creation_adopts_or_rejects_the_policy_comparator() -> None:
    other_comparator: Comparator[int] = lambda a, b: a - b  # noqa: E731
    bound = OrderClampPolicy[int](other_comparator)
    assert isinstance(bound, ComparatorBoundMonotoneHeapAction)
    adopted: IntHeap = PersistentMonotoneActionHeap.empty(bound)
    assert adopted.comparator is other_comparator
    explicit: IntHeap = PersistentMonotoneActionHeap.empty(bound, other_comparator)
    assert explicit.comparator is other_comparator
    with pytest.raises(ValueError, match="exact comparator"):
        PersistentMonotoneActionHeap.empty(bound, default_comparator)

    unbound: MonotoneHeapAction[int, int] = ShiftPolicy()
    assert not isinstance(unbound, ComparatorBoundMonotoneHeapAction)
    default_heap: PersistentMonotoneActionHeap[str, int, int] = PersistentMonotoneActionHeap.empty(
        unbound
    )
    assert default_heap.comparator is default_comparator
    chosen: PersistentMonotoneActionHeap[str, int, int] = PersistentMonotoneActionHeap.empty(
        unbound, other_comparator
    )
    assert chosen.comparator is other_comparator


def test_an_unbound_custom_policy_drives_the_heap() -> None:
    policy: MonotoneHeapAction[int, int] = ShiftPolicy()
    heap: PersistentMonotoneActionHeap[str, int, int] = PersistentMonotoneActionHeap.from_entries(
        [MonotoneHeapEntry(str(value), value) for value in range(20)], policy
    )
    shifted = heap.transform_all(5).transform_all(-2)
    assert sorted(entry.priority for entry in shifted) == [value + 3 for value in range(20)]
    assert shifted.insert("late", 0).minimum == MonotoneHeapEntry("late", 0)
    assert shifted.transform_all(0) is shifted
    assert shifted.validate_structure().count == 20

    output: list[int] = []
    while not shifted.is_empty:
        view = shifted.minimum_view()
        assert view is not None
        output.append(view.minimum.priority)
        shifted = view.remainder
    assert output == [value + 3 for value in range(20)]


def test_from_entries_accepts_entries_and_pairs_alike() -> None:
    policy = OrderClampPolicy[int]()
    mixed: IntHeap = PersistentMonotoneActionHeap.from_entries(
        [MonotoneHeapEntry("a", 3), ("b", 1), MonotoneHeapEntry("c", 2)], policy
    )
    assert _drain(mixed) == [("b", 1), ("c", 2), ("a", 3)]


def test_validate_structure_rejects_a_corrupted_representation() -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(str(value), value) for value in range(32)], policy)
    root = heap._root_identity
    assert isinstance(root, _Tree)

    wrong_count: IntHeap = PersistentMonotoneActionHeap(
        root, len(heap) + 1, heap.comparator, policy
    )
    with pytest.raises(ValueError, match="logical count mismatch"):
        wrong_count.validate_structure()

    ranked_root = _Tree(1, root.element, root.priority, root.children, root.action)
    bad_rank: IntHeap = PersistentMonotoneActionHeap(
        ranked_root, len(heap), heap.comparator, policy
    )
    with pytest.raises(ValueError, match="rank zero"):
        bad_rank.validate_structure()

    lifted = _Tree(root.rank, root.element, 10_000, root.children, root.action)
    out_of_order: IntHeap = PersistentMonotoneActionHeap(lifted, len(heap), heap.comparator, policy)
    with pytest.raises(ValueError, match="outranks its parent"):
        out_of_order.validate_structure()

    empty_but_counted: IntHeap = PersistentMonotoneActionHeap(None, 3, heap.comparator, policy)
    with pytest.raises(ValueError, match="count mismatch"):
        empty_but_counted.validate_structure()

    tangled = _Tree(0, "x", 0, heap._cons(_synthetic(policy, 3, "bad"), None), policy.identity)
    broken: IntHeap = PersistentMonotoneActionHeap(tangled, 2, heap.comparator, policy)
    with pytest.raises(ValueError, match="missing children"):
        broken.validate_structure()


def test_validate_structure_counts_tagged_components_through_composed_views() -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(str(value), value) for value in range(16)], policy)
    assert heap.validate_structure().tagged_component_count == 0

    tagged = heap.transform_all(policy.at_least(3))
    shape = tagged.validate_structure()
    assert shape.count == 16
    # One tag at the root, plus one logical view per spine cell the exposure pushes it onto.
    assert shape.tagged_component_count >= 1
    assert shape.maximum_rank == heap.validate_structure().maximum_rank
    assert shape.root_forest_length == heap.validate_structure().root_forest_length


# ---------------------------------------------------------------------------------------------
# Recursion depth
# ---------------------------------------------------------------------------------------------


def _stack_depth() -> int:
    depth = 0
    frame: object = sys._getframe()
    while frame is not None:
        depth += 1
        frame = getattr(frame, "f_back", None)
    return depth


def test_large_heaps_never_recurse_deeply() -> None:
    """Every forest and spine walk is iterative, so a shallow recursion limit must suffice.

    The limit is lowered around the work itself rather than around the assertions, since a
    failure report needs the stack back.
    """

    policy = OrderClampPolicy[int]()
    values = [(value * 2654435761) % 1_000_003 for value in range(20_000)]
    heap = _from_pairs([(str(value), value) for value in values], policy)

    limit = sys.getrecursionlimit()
    try:
        sys.setrecursionlimit(_stack_depth() + 60)
        shape = heap.validate_structure()
        transformed = heap.transform_all(policy.at_most(500_000))
        transformed_shape = transformed.validate_structure()
        melded = heap.meld(transformed)
        melded_shape = melded.validate_structure()
        iterated = len(list(melded))
        drained: list[int] = []
        cursor = melded
        for _ in range(4_000):
            view = cursor.minimum_view()
            assert view is not None
            drained.append(view.minimum.priority)
            cursor = view.remainder
        remaining_shape = cursor.validate_structure()
    finally:
        sys.setrecursionlimit(limit)

    assert shape.count == 20_000
    assert transformed_shape.count == 20_000
    assert melded_shape.count == 40_000
    assert iterated == 40_000
    assert drained == sorted(drained)
    assert remaining_shape.count == 36_000
    # A degenerate chain would push the depth toward the entry count instead.
    assert shape.maximum_depth < 100


# ---------------------------------------------------------------------------------------------
# Randomized agreement with a reference model
# ---------------------------------------------------------------------------------------------


@settings(max_examples=60, deadline=None)
@given(st.lists(st.integers(min_value=-500, max_value=500), max_size=250))
def test_random_multisets_drain_in_sorted_order(values: list[int]) -> None:
    policy = OrderClampPolicy[int]()
    heap = _from_pairs([(str(index), value) for index, value in enumerate(values)], policy)
    assert heap.validate_structure().count == len(values)
    assert [priority for _, priority in _drain(heap)] == sorted(values)


@settings(max_examples=40, deadline=None)
@given(
    st.lists(st.integers(min_value=-200, max_value=200), max_size=120),
    st.lists(st.integers(min_value=-200, max_value=200), max_size=120),
    st.integers(min_value=-50, max_value=50),
)
def test_random_melds_of_independently_transformed_heaps_match_the_model(
    left_values: list[int], right_values: list[int], floor: int
) -> None:
    policy = OrderClampPolicy[int]()
    left = _from_pairs([(f"l{i}", v) for i, v in enumerate(left_values)], policy)
    right = _from_pairs([(f"r{i}", v) for i, v in enumerate(right_values)], policy)
    raised = left.transform_all(policy.at_least(floor))
    merged = raised.meld(right)

    expected = sorted([max(value, floor) for value in left_values] + right_values)
    assert merged.validate_structure().count == len(expected)
    assert [priority for _, priority in _drain(merged)] == expected
    assert _priorities(left) == sorted(left_values)
    assert _priorities(right) == sorted(right_values)


@settings(max_examples=40, deadline=None)
@given(
    st.lists(
        st.tuples(
            st.sampled_from(("insert", "transform", "delete")),
            st.integers(min_value=-100, max_value=100),
        ),
        max_size=200,
    )
)
def test_interleaved_operations_track_a_list_model(
    operations: list[tuple[str, int]],
) -> None:
    policy = OrderClampPolicy[int]()
    heap: IntHeap = PersistentMonotoneActionHeap.empty(policy)
    model: list[int] = []
    for index, (kind, value) in enumerate(operations):
        if kind == "insert":
            heap = heap.insert(str(index), value)
            model.append(value)
        elif kind == "transform":
            action = policy.at_least(value) if index % 2 == 0 else policy.at_most(value)
            heap = heap.transform_all(action)
            model = [policy.apply(action, priority) for priority in model]
        elif model:
            view = heap.minimum_view()
            assert view is not None
            assert view.minimum.priority == min(model)
            model.remove(min(model))
            heap = view.remainder
        else:
            assert heap.minimum_view() is None
        assert len(heap) == len(model)
        assert _priorities(heap) == sorted(model)
    assert heap.validate_structure().count == len(model)
    assert [priority for _, priority in _drain(heap)] == sorted(model)
