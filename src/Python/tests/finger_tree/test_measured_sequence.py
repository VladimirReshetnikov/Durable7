"""Bound probes for the measured finger-tree core.

These are the inverted forms of the measurements that established the old join-tree core's weaker
bounds. Each counts the policy's ``combine``/``measure`` calls, because asymptotic claims are
invisible to correctness tests: an eager join tree would pass every behavioural case here while
failing every counter.
"""

from __future__ import annotations

import random

from durable7.finger_tree.measured_sequence import MeasuredSequence


class _CountingSize:
    """The size measure with live call counters."""

    identity = 0

    def __init__(self) -> None:
        self.combine_calls = 0
        self.measure_calls = 0

    def combine(self, left: int, right: int) -> int:
        self.combine_calls += 1
        return left + right

    def measure(self, element: object) -> int:
        self.measure_calls += 1
        return 1

    def reset(self) -> int:
        total = self.combine_calls + self.measure_calls
        self.combine_calls = 0
        self.measure_calls = 0
        return total


def _build(count: int, policy: _CountingSize) -> MeasuredSequence[int, int]:
    sequence: MeasuredSequence[int, int] = MeasuredSequence.empty(policy)
    for value in range(count):
        sequence = sequence.append(value)
    return sequence


def test_endpoint_push_cost_is_independent_of_length() -> None:
    """Appends must cost amortized O(1) policy work — the join tree paid Θ(log n) per push."""

    policy = _CountingSize()
    small = _build(1_024, policy)
    policy.reset()
    for value in range(1_000):
        small = small.append(value)
    small_cost = policy.reset()

    large = _build(32_768, policy)
    policy.reset()
    for value in range(1_000):
        large = large.append(value)
    large_cost = policy.reset()

    # ~one node build per three pushes, a few calls each: comfortably under 4 per push, and the
    # 32x larger sequence must not change the per-push cost at all.
    assert small_cost <= 4_000, small_cost
    assert large_cost <= 4_000, large_cost
    assert abs(large_cost - small_cost) <= 200, (small_cost, large_cost)


def test_front_and_back_are_constant_reads() -> None:
    """Digit reads: no policy call, no middle forcing."""

    policy = _CountingSize()
    sequence = _build(8_192, policy)
    policy.reset()
    assert sequence.front() == 0
    assert sequence.back() == 8_191
    assert policy.reset() == 0


def test_concat_immediate_cost_tracks_neither_operand() -> None:
    """Concatenation defers its middle recursion, so the immediate cost is O(1) regrouping.

    The join tree's concat walked the taller operand's spine — its cost grew with the height
    difference. Here concatenating a 16384-element sequence with a 1-element one must cost the
    same handful of calls as with a 1024-element one.
    """

    policy = _CountingSize()
    big = _build(16_384, policy)
    tiny = _build(1, policy)
    mid = _build(1_024, policy)

    policy.reset()
    joined_tiny = big.concat(tiny)
    tiny_cost = policy.reset()
    joined_mid = big.concat(mid)
    mid_cost = policy.reset()

    assert tiny_cost <= 40, tiny_cost
    assert mid_cost <= 40, mid_cost
    assert abs(mid_cost - tiny_cost) <= 20, (tiny_cost, mid_cost)
    assert len(joined_tiny) == 16_385
    assert len(joined_mid) == 17_408


def test_forced_spines_are_memoized_across_persistent_branches() -> None:
    """Work deferred by one version and forced by another must never be repeated.

    This is the property that makes the amortized bounds valid under persistence: the suspended
    spine is shared, so the first full-measure force pays for everyone. Without memoization every
    branch would re-pay Θ(n).
    """

    policy = _CountingSize()
    base = _build(10_000, policy)
    branches = [base.prepend(-value) for value in range(50)]
    policy.reset()

    first_total = branches[0].measure
    assert first_total == 10_001
    first_cost_total = policy.reset()
    # Much of the spine's measure work was already cached in 2-3 nodes at construction; the first
    # full force still pays for the whole remaining suspended spine.
    assert first_cost_total >= 1_000, first_cost_total

    assert all(branch.measure == 10_001 for branch in branches[1:])
    rest_cost = policy.reset()
    # 49 branches, each re-paying only its own unshared top-level digits (~10 calls measured) —
    # not the spine. Without memoization each would re-pay the full first-force cost.
    assert rest_cost < first_cost_total, (first_cost_total, rest_cost)
    assert rest_cost <= 49 * 32, rest_cost


def test_randomized_edits_match_a_list_model() -> None:
    generator = random.Random(20260805)
    policy = _CountingSize()
    sequence: MeasuredSequence[int, int] = MeasuredSequence.empty(policy)
    model: list[int] = []
    for step in range(600):
        choice = generator.randrange(6)
        if choice == 0:
            sequence = sequence.prepend(step)
            model.insert(0, step)
        elif choice == 1:
            sequence = sequence.append(step)
            model.append(step)
        elif choice == 2 and model:
            index = generator.randrange(len(model))
            updated = sequence.set_at(index, -step)
            assert updated is not None
            sequence = updated
            model[index] = -step
        elif choice == 3 and model:
            index = generator.randrange(len(model))
            removed = sequence.remove_at(index)
            assert removed is not None
            sequence = removed
            del model[index]
        elif choice == 4:
            index = generator.randrange(len(model) + 1)
            inserted = sequence.insert_at(index, step)
            assert inserted is not None
            sequence = inserted
            model.insert(index, step)
        else:
            index = generator.randrange(len(model) + 1)
            split = sequence.split_at(index)
            assert split is not None
            assert split.left.to_list() == model[:index]
            assert split.right.to_list() == model[index:]
            sequence = split.left.concat(split.right)
        assert len(sequence) == len(model)
        if model:
            probe = generator.randrange(len(model))
            assert sequence.at(probe) == model[probe]
    assert sequence.to_list() == model
    assert sequence.measure == len(model)


def test_sorted_bounds_and_locate_agree_with_the_model() -> None:
    policy = _CountingSize()
    values = sorted(random.Random(7).choices(range(500), k=400))
    sequence = MeasuredSequence.from_iterable(values, policy)

    def compare(left: int, right: int) -> int:
        return left - right

    for probe in range(0, 500, 13):
        expected_lower = next((i for i, v in enumerate(values) if v >= probe), len(values))
        expected_upper = next((i for i, v in enumerate(values) if v > probe), len(values))
        assert sequence.lower_bound(probe, compare) == expected_lower, probe
        assert sequence.upper_bound(probe, compare) == expected_upper, probe

    for count in range(0, 401, 37):
        assert sequence.prefix_measure(count) == count

        def past_count(measured: int, target: int = count) -> bool:
            return measured > target

        located = sequence.locate(past_count)
        if count < 400:
            assert (located.index, located.found) == (count, True)
            assert located.measure_before == count
        else:
            assert not located.found


def test_sharing_probe_is_free_on_a_warmed_spine() -> None:
    """The probe must see nodes hidden inside pending suspensions, so it forces them.

    Forcing replays the deferred construction - including its node-measure work - exactly once,
    memoized. On a spine that has already been forced the probe therefore costs zero policy
    calls, which is the property a bound test cares about: probing cannot re-add work.
    """

    policy = _CountingSize()
    base = _build(4_096, policy)
    branch = base.append(-1)
    assert base.measure == 4_096
    assert branch.measure == 4_097
    policy.reset()
    assert branch.shares_structure_with(base)
    assert policy.reset() == 0

    independent = _build(4_096, policy)
    assert independent.measure == 4_096
    policy.reset()
    assert not independent.shares_structure_with(base)
    assert policy.reset() == 0


def test_reversed_view_is_lazy_and_correct() -> None:
    """Mirroring defers the middle's reversal: O(1) policy work immediately, full order on read."""

    policy = _CountingSize()
    sequence = _build(16_384, policy)
    assert sequence.measure == 16_384
    policy.reset()
    mirrored = sequence.reversed_view()
    assert policy.reset() == 0, "mirroring reuses cached measures and defers the middle"
    assert mirrored.front() == 16_383
    assert mirrored.back() == 0
    assert mirrored.at(0) == 16_383
    assert mirrored.at(16_383) == 0
    assert mirrored.to_list() == list(reversed(range(16_384)))


def test_cross_orientation_concat_is_structural() -> None:
    """The reversible deque's mismatched concat must join structurally, not materialize.

    Measured here at the substrate level: mirror-then-concat of a 16384-element sequence with a
    small one costs the same handful of immediate policy calls as a same-orientation concat -
    the Theta(n + m) materialization this replaced would pay tens of thousands.
    """

    policy = _CountingSize()
    big = _build(16_384, policy)
    small = _build(9, policy)
    assert big.measure == 16_384
    assert small.measure == 9
    policy.reset()
    joined = big.concat(small.reversed_view())
    immediate = policy.reset()
    assert immediate <= 40, immediate
    assert len(joined) == 16_393
    assert joined.at(16_392) == 0


class _Spelling:
    """A deliberately non-commutative measure: concatenation of element spellings.

    Under this monoid a mirrored subtree's summary is NOT its cached summary read backwards, so
    any reversal that reuses cached measures reports the forward spelling for the reversed view.
    Only reversed-order recombination can pass these assertions.
    """

    identity = ""

    def combine(self, left: str, right: str) -> str:
        return left + right

    def measure(self, element: object) -> str:
        return str(element)


def test_reversed_view_is_correct_under_a_non_commutative_measure() -> None:
    policy = _Spelling()
    sequence: MeasuredSequence[str, str] = MeasuredSequence.empty(policy)
    letters = [chr(ord("a") + value % 26) + str(value) for value in range(300)]
    for letter in letters:
        sequence = sequence.append(letter)

    mirrored = sequence.reversed_view()
    assert mirrored.to_list() == list(reversed(letters))
    assert mirrored.measure == "".join(reversed(letters)), "the summary must mirror too"

    # Interior structure must agree as well, not only the root summary: split inside the mirror
    # and check both halves' summaries against the model.
    split = mirrored.split_at(137)
    assert split is not None
    expected = list(reversed(letters))
    assert split.left.measure == "".join(expected[:137])
    assert split.right.measure == "".join(expected[137:])

    # A double reversal restores the forward spelling.
    assert mirrored.reversed_view().measure == "".join(letters)
