"""Persistent Brodal-Okasaki heap with lazily composable monotone priority actions.

This is the action-tagged sibling of :mod:`durable7.finger_tree.brodal_okasaki_heap`.  It keeps
that module's bootstrapped skew-binomial representation -- a global minimum root above a
skew-binomial forest -- and adds one immutable *pending action* to every tree and every forest
spine cell.  A tree tag applies to that tree and all of its descendants; a forest tag applies
uniformly to every tree in that suffix.

The point of the tags is :meth:`PersistentMonotoneActionHeap.transform_all`, which applies one
monotone action to every priority *currently* in a version by composing a single tag at the root.
It costs constant worst-case time and allocates constant new structure.  Its semantics are
deliberately temporal: entries inserted later, and entries arriving through a later meld, are not
retroactively transformed.  Two heaps transformed independently still meld in constant worst-case
time, including when the actions have no inverse -- a clamp does not.

The ordinary heap bounds are unchanged: insert, minimum, and meld are worst-case constant time;
delete-minimum is worst-case logarithmic; enumeration and :meth:`validate_structure` are linear.
Those bounds include the constant-time action-policy calls and priority comparisons they perform,
so a policy whose ``compose``/``apply``/``is_identity`` is not constant time, or whose composed
actions are not of fixed size, invalidates them.

Composition is directional throughout: ``compose(outer, inner)`` denotes ``outer(inner(priority))``,
so the *newer* action is always the outer one.  Every tag-pushdown site in this module composes a
newer action over an older one in that order.

The action policy is trusted.  Violating its identity, associative-composition, monotonicity, or
constant-time contract invalidates both the semantic and the complexity guarantees.  Every update
is persistent and leaves all source versions usable; a failed operation publishes nothing.
"""

from __future__ import annotations

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Generic, Protocol, TypeVar, cast, runtime_checkable

from .ordering import Comparator, default_comparator

E = TypeVar("E")
P = TypeVar("P")
A = TypeVar("A")
T = TypeVar("T")


class MonotoneHeapAction(Protocol[P, A]):
    """Constant-time-composable monotone action family over heap priorities.

    Implementations must supply a semantic identity and an associative composition where
    ``compose(outer, inner)`` denotes ``outer(inner(priority))``.  Every action must be monotone
    for the comparator retained by its heap: if ``p <= q`` then ``apply(a, p) <= apply(a, q)``.
    Identity testing, composition, and application are part of the heap's advertised bounds and
    must each take constant time over fixed-size actions.  Results must stay deterministic and
    semantically stable for the lifetime of every heap version.
    """

    @property
    def identity(self) -> A:
        """The semantic identity action.

        Not a sentinel: :meth:`is_identity` decides whether an action acts as the identity, and
        other action values may qualify.
        """
        ...

    def is_identity(self, action: A) -> bool:
        """Whether ``action`` acts as the semantic identity, so storing it would be pointless."""
        ...

    def compose(self, outer: A, inner: A) -> A:
        """The action equivalent to ``outer(inner(priority))``.

        Directional: composition is not assumed commutative, so the argument order matters.  The
        heap always passes the newer action as ``outer``.
        """
        ...

    def apply(self, action: A, priority: P) -> P:
        """Apply one action to one priority."""
        ...


@runtime_checkable
class ComparatorBoundMonotoneHeapAction(MonotoneHeapAction[P, A], Protocol[P, A]):
    """An action policy that names the exact comparator its actions are monotone for.

    A heap created from such a policy adopts that comparator, and rejects any other one.  The
    check is structural rather than nominal: any policy exposing a ``comparator`` attribute is
    treated as bound.
    """

    @property
    def comparator(self) -> Comparator[P]:
        """The exact comparator this policy promises monotonicity for."""
        ...


@dataclass(frozen=True, slots=True)
class MonotoneHeapEntry(Generic[E, P]):
    """A payload paired with its current logical priority, every pending action applied."""

    element: E
    priority: P


@dataclass(frozen=True, slots=True)
class OrderClamp(Generic[T]):
    """A lower bound, an upper bound, both bounds, an exact constant, or the identity.

    Values are normally created by :class:`OrderClampPolicy`, so that two-sided clamps are
    validated with the same comparator later used for application and composition.  A missing
    bound denotes negative or positive infinity.  Each bound is carried as an explicit
    presence flag plus a raw slot rather than as a nullable value, so a legitimately ``None``
    priority stays distinguishable from an absent bound.

    The policy never produces an action that is both constant and bounded; such a value is
    representable only so that the ``constant -> lower -> upper`` application order stays
    observable.
    """

    is_constant: bool = False
    constant_or_none: T | None = None
    has_lower_bound: bool = False
    lower_bound_or_none: T | None = None
    has_upper_bound: bool = False
    upper_bound_or_none: T | None = None

    def __post_init__(self) -> None:
        """Reject a raw slot carrying a value its presence flag denies.

        Equality compares the raw slots directly, so an absent component must always be ``None``
        for two semantically equal clamps to compare equal.
        """

        if not self.is_constant and self.constant_or_none is not None:
            raise ValueError("A non-constant clamp must not carry a constant value.")
        if not self.has_lower_bound and self.lower_bound_or_none is not None:
            raise ValueError("A clamp without a lower bound must not carry a lower value.")
        if not self.has_upper_bound and self.upper_bound_or_none is not None:
            raise ValueError("A clamp without an upper bound must not carry an upper value.")

    @property
    def constant_value(self) -> T:
        """The exact constant result, raising :class:`ValueError` when the clamp is not constant."""

        if not self.is_constant:
            raise ValueError("The clamp is not constant.")
        return cast(T, self.constant_or_none)

    @property
    def lower_bound(self) -> T:
        """The lower bound, raising :class:`ValueError` when the clamp has none."""

        if not self.has_lower_bound:
            raise ValueError("The clamp has no lower bound.")
        return cast(T, self.lower_bound_or_none)

    @property
    def upper_bound(self) -> T:
        """The upper bound, raising :class:`ValueError` when the clamp has none."""

        if not self.has_upper_bound:
            raise ValueError("The clamp has no upper bound.")
        return cast(T, self.upper_bound_or_none)


class OrderClampPolicy(Generic[T]):
    """The closed monotone action family ``x -> clamp(x, lower, upper)``.

    Missing bounds denote negative or positive infinity, and composition always yields either one
    constant-sized clamp or an explicit constant function, so ``compose`` and ``apply`` are
    constant time.  The explicit constant case preserves exact results even when the comparator
    treats distinct priority values as equal: an inclusive ``[k, k]`` clamp preserves an input
    equivalent to ``k``, whereas the constant function must return the exact ``k`` representative.

    Composition of two overlapping clamps intersects them and keeps the *inner* (older)
    representative when two boundary values compare equal; disjoint clamps collapse to a constant
    carrying the *outer* (newer) boundary, which is the only value the composite can ever return.

    The retained comparator is the exact comparator this family is monotone for, so a heap created
    from this policy adopts it and rejects any other one.
    """

    __slots__ = ("comparator",)

    comparator: Comparator[T]

    def __init__(self, comparator: Comparator[T] = default_comparator) -> None:
        """Create a clamp family monotone for ``comparator``, retained by object identity."""

        self.comparator = comparator

    @property
    def identity(self) -> OrderClamp[T]:
        """The identity clamp, which has neither bound and is not constant."""

        return OrderClamp()

    def at_least(self, lower_bound: T) -> OrderClamp[T]:
        """A floor action ``x -> max(x, lower_bound)``."""

        return OrderClamp(has_lower_bound=True, lower_bound_or_none=lower_bound)

    def at_most(self, upper_bound: T) -> OrderClamp[T]:
        """A cap action ``x -> min(x, upper_bound)``."""

        return OrderClamp(has_upper_bound=True, upper_bound_or_none=upper_bound)

    def constant(self, value: T) -> OrderClamp[T]:
        """An exact constant action ``x -> value``, returning that exact representative."""

        return OrderClamp(is_constant=True, constant_or_none=value)

    def between(self, lower_bound: T, upper_bound: T) -> OrderClamp[T]:
        """A two-sided clamp, raising :class:`ValueError` when the bounds cross."""

        if self.comparator(lower_bound, upper_bound) > 0:
            raise ValueError("The lower bound must not compare greater than the upper bound.")
        return OrderClamp(
            has_lower_bound=True,
            lower_bound_or_none=lower_bound,
            has_upper_bound=True,
            upper_bound_or_none=upper_bound,
        )

    def is_identity(self, action: OrderClamp[T]) -> bool:
        """Whether ``action`` is neither constant nor bounded, and so changes nothing."""

        return not action.is_constant and not action.has_lower_bound and not action.has_upper_bound

    def compose(self, outer: OrderClamp[T], inner: OrderClamp[T]) -> OrderClamp[T]:
        """The clamp equivalent to applying ``inner`` first and then ``outer``.

        Non-commutative, and deliberately so: a cap composed over a floor whose bounds are
        disjoint collapses to the cap's boundary, whereas the opposite order collapses to the
        floor's.  That asymmetry is what pins the composition direction.
        """

        if self.is_identity(outer):
            return inner
        if self.is_identity(inner):
            return outer
        if outer.is_constant:
            return outer
        if inner.is_constant:
            return self.constant(self.apply(outer, inner.constant_value))

        # Disjoint bounds collapse to an explicit constant carrying the newer (outer) boundary.
        if (
            inner.has_upper_bound
            and outer.has_lower_bound
            and self.comparator(inner.upper_bound, outer.lower_bound) < 0
        ):
            return self.constant(outer.lower_bound)
        if (
            inner.has_lower_bound
            and outer.has_upper_bound
            and self.comparator(inner.lower_bound, outer.upper_bound) > 0
        ):
            return self.constant(outer.upper_bound)

        # Overlapping bounds intersect; equal boundaries keep the older (inner) representative.
        has_lower = inner.has_lower_bound or outer.has_lower_bound
        lower: T | None = None
        if has_lower:
            if not inner.has_lower_bound:
                lower = outer.lower_bound
            elif (
                not outer.has_lower_bound
                or self.comparator(inner.lower_bound, outer.lower_bound) >= 0
            ):
                lower = inner.lower_bound
            else:
                lower = outer.lower_bound

        has_upper = inner.has_upper_bound or outer.has_upper_bound
        upper: T | None = None
        if has_upper:
            if not inner.has_upper_bound:
                upper = outer.upper_bound
            elif (
                not outer.has_upper_bound
                or self.comparator(inner.upper_bound, outer.upper_bound) <= 0
            ):
                upper = inner.upper_bound
            else:
                upper = outer.upper_bound

        return OrderClamp(
            has_lower_bound=has_lower,
            lower_bound_or_none=lower,
            has_upper_bound=has_upper,
            upper_bound_or_none=upper,
        )

    def apply(self, action: OrderClamp[T], priority: T) -> T:
        """Apply one clamp, testing the constant, then the lower bound, then the upper bound."""

        if action.is_constant:
            return action.constant_value
        if action.has_lower_bound and self.comparator(priority, action.lower_bound) < 0:
            return action.lower_bound
        if action.has_upper_bound and self.comparator(priority, action.upper_bound) > 0:
            return action.upper_bound
        return priority


@dataclass(frozen=True, slots=True)
class _Tree(Generic[E, P, A]):
    """One skew-binomial tree carrying a pending action for itself and all of its descendants."""

    rank: int
    element: E
    #: Raw priority; the logical priority is ``apply(action, priority)``.
    priority: P
    #: Raw children; the logical children are ``tag_forest(children, action)``.
    children: _Forest[E, P, A] | None
    action: A


@dataclass(frozen=True, slots=True)
class _Forest(Generic[E, P, A]):
    """One forest spine cell carrying a pending action for its whole suffix."""

    #: Raw head; the logical head is ``tag_tree(head, action)``.
    head: _Tree[E, P, A]
    #: Raw tail; the logical tail is ``tag_forest(tail, action)``.
    tail: _Forest[E, P, A] | None
    action: A


@dataclass(frozen=True, slots=True)
class _SplitForest(Generic[E, P, A]):
    """The three components a minimum tree's fused children decompose into."""

    zeros: _Forest[E, P, A] | None
    trees: _Forest[E, P, A] | None
    embedded_forest: _Forest[E, P, A] | None


@dataclass(frozen=True, slots=True)
class PersistentMonotoneActionHeapStatistics:
    """Shape measurements returned by a successful structural audit."""

    count: int
    root_forest_length: int
    maximum_rank: int
    maximum_depth: int
    #: Pending tree- and forest-tag observations made during the audit.  Not a distinct-object
    #: count: exposing one uniform forest tag produces several tagged logical views of the same
    #: retained cells.
    tagged_component_count: int


@dataclass(frozen=True, slots=True)
class MonotoneActionMinimumView(Generic[E, P, A]):
    """The minimum entry together with the heap that remains once it is removed."""

    minimum: MonotoneHeapEntry[E, P]
    remainder: PersistentMonotoneActionHeap[E, P, A]


class PersistentMonotoneActionHeap(Generic[E, P, A]):
    """Immutable bootstrapped skew-binomial min-heap with pending monotone priority actions.

    Insert, minimum, and meld are worst-case constant time; delete-minimum is worst-case
    logarithmic; :meth:`transform_all` is worst-case constant time and allocates constant new
    structure; enumeration and :meth:`validate_structure` are linear.  Every operation publishes a
    new root and preserves prior versions.
    """

    __slots__ = ("_count", "_root", "action_policy", "comparator")

    comparator: Comparator[P]
    action_policy: MonotoneHeapAction[P, A]

    def __init__(
        self,
        root: _Tree[E, P, A] | None,
        count: int,
        comparator: Comparator[P],
        action_policy: MonotoneHeapAction[P, A],
    ) -> None:
        """Wrap an already-built forest; use :meth:`empty` or :meth:`from_entries` instead.

        The caller is responsible for ``count`` matching ``root`` and for the heap order holding
        through every pending action.
        """

        self._root = root
        self._count = count
        self.comparator = comparator
        self.action_policy = action_policy

    @classmethod
    def empty(
        cls,
        action_policy: MonotoneHeapAction[P, A],
        comparator: Comparator[P] | None = None,
    ) -> PersistentMonotoneActionHeap[E, P, A]:
        """Return an empty heap retaining ``action_policy`` and a priority comparator.

        A policy that names the comparator it is monotone for -- see
        :class:`ComparatorBoundMonotoneHeapAction` -- supplies the default, and any other
        comparator is rejected with :class:`ValueError` because the policy's monotonicity promise
        would not cover it.  Both objects are retained by identity, and only heaps sharing both
        exact objects may meld.
        """

        if isinstance(action_policy, ComparatorBoundMonotoneHeapAction):
            bound: Comparator[P] = action_policy.comparator
            if comparator is None:
                comparator = bound
            elif comparator is not bound:
                raise ValueError(
                    "The comparator must be the exact comparator retained by the action policy."
                )
        elif comparator is None:
            comparator = default_comparator
        return cls(None, 0, comparator, action_policy)

    @classmethod
    def from_entries(
        cls,
        entries: Iterable[MonotoneHeapEntry[E, P] | tuple[E, P]],
        action_policy: MonotoneHeapAction[P, A],
        comparator: Comparator[P] | None = None,
    ) -> PersistentMonotoneActionHeap[E, P, A]:
        """Build a heap in linear time by repeated worst-case-constant-time insertion.

        Entries may be :class:`MonotoneHeapEntry` values or plain ``(element, priority)`` pairs.
        """

        result: PersistentMonotoneActionHeap[E, P, A] = cls.empty(action_policy, comparator)
        for entry in entries:
            if isinstance(entry, MonotoneHeapEntry):
                result = result.insert(entry.element, entry.priority)
            else:
                element, priority = entry
                result = result.insert(element, priority)
        return result

    def __len__(self) -> int:
        """Number of entries, matching :attr:`count`."""

        return self._count

    @property
    def count(self) -> int:
        """Number of entries, counting duplicates separately."""

        return self._count

    @property
    def is_empty(self) -> bool:
        """Whether the heap holds no entries."""

        return self._root is None

    @property
    def minimum(self) -> MonotoneHeapEntry[E, P]:
        """A minimum entry, every pending action applied, in worst-case constant time.

        Reads the bootstrapped root directly rather than searching the forest.  Raises
        :class:`IndexError` when the heap is empty; use :meth:`try_minimum` to test and read at
        once.
        """

        if self._root is None:
            raise IndexError("The heap is empty.")
        return self._entry_of(self._root)

    def try_minimum(self) -> MonotoneHeapEntry[E, P] | None:
        """A minimum entry, or ``None`` when the heap is empty.

        The presence-safe counterpart of :attr:`minimum`, which raises instead.
        """

        return None if self._root is None else self._entry_of(self._root)

    def insert(self, element: E, priority: P) -> PersistentMonotoneActionHeap[E, P, A]:
        """Return a heap with ``element`` added at ``priority``, in worst-case constant time.

        The new entry joins untransformed: actions applied by an earlier :meth:`transform_all`
        never reach it.  A tie against the current root favours the new entry, which becomes the
        new bootstrapped root; otherwise the old root is *exposed* before the new singleton is
        skew-inserted, so the old root's pending action cannot leak onto an entry that did not
        exist when that action was applied.
        """

        singleton = _Tree(0, element, priority, None, self.action_policy.identity)
        root = self._root
        if root is None:
            return self._new(singleton, 1)

        if self._le(singleton, root):
            updated = _Tree(
                0, element, priority, self._cons(root, None), self.action_policy.identity
            )
        else:
            exposed = self._expose(root)
            updated = _Tree(
                0,
                exposed.element,
                exposed.priority,
                self._skew_insert(singleton, exposed.children),
                self.action_policy.identity,
            )
        return self._new(updated, self._count + 1)

    def transform_all(self, action: A) -> PersistentMonotoneActionHeap[E, P, A]:
        """Return a heap with ``action`` applied to every priority *currently* present.

        Worst-case constant time, allocating constant new structure: one tag is composed at the
        root and the whole child forest is reused.  Later insertions and later melds are not
        retroactively transformed.  An empty heap or an action the policy recognizes as the
        identity returns the receiver.
        """

        if self._root is None or self.action_policy.is_identity(action):
            return self
        return self._new(self._tag_tree(self._root, action), self._count)

    def meld(
        self, other: PersistentMonotoneActionHeap[E, P, A]
    ) -> PersistentMonotoneActionHeap[E, P, A]:
        """Return a heap holding every entry of both operands, in worst-case constant time.

        Each operand keeps its own action history: neither heap's pending actions are applied to
        the other's entries, even for actions with no inverse.  Both heaps must retain the same
        comparator and action-policy objects; mixing them raises :class:`TypeError` rather than
        silently combining two ordering rules or two action algebras.  A tie between the two roots
        favours the receiver.  Melding a heap with itself duplicates every logical occurrence.
        """

        if self.comparator is not other.comparator:
            raise TypeError("Heap melding requires the same comparator object.")
        if self.action_policy is not other.action_policy:
            raise TypeError("Heap melding requires the same action-policy object.")
        left_root = self._root
        right_root = other._root
        if left_root is None:
            return other
        if right_root is None:
            return self

        left_wins = self._le(left_root, right_root)
        winner = self._expose(left_root if left_wins else right_root)
        loser = right_root if left_wins else left_root
        root = _Tree(
            0,
            winner.element,
            winner.priority,
            self._skew_insert(loser, winner.children),
            self.action_policy.identity,
        )
        return self._new(root, self._count + other._count)

    def delete_minimum(self) -> PersistentMonotoneActionHeap[E, P, A]:
        """Return the heap without one minimum entry, in worst-case logarithmic time.

        Unlike insertion and melding, this rebuilds the root's forest, so it is the one operation
        whose cost grows with the heap's rank.  Raises :class:`IndexError` when the heap is empty;
        use :meth:`minimum_view` to read and remove at once.
        """

        if self._root is None:
            raise IndexError("The heap is empty.")
        root = self._expose(self._root)
        if root.children is None:
            return self._new(None, 0)

        tagged_minimum, remainder = self._get_minimum(root.children)
        minimum = self._expose(tagged_minimum)
        split = self._split_forest(minimum.rank, minimum.children)
        merged = self._skew_meld(self._skew_meld(split.trees, remainder), split.embedded_forest)
        for zero in reversed(self._forest_to_list(split.zeros)):
            merged = self._skew_insert(zero, merged)
        return self._new(
            _Tree(
                0,
                minimum.element,
                minimum.priority,
                merged,
                self.action_policy.identity,
            ),
            self._count - 1,
        )

    def minimum_view(self) -> MonotoneActionMinimumView[E, P, A] | None:
        """The minimum entry and the remaining heap, or ``None`` when the heap is empty."""

        if self._root is None:
            return None
        return MonotoneActionMinimumView(self._entry_of(self._root), self.delete_minimum())

    def __iter__(self) -> Iterator[MonotoneHeapEntry[E, P]]:
        """Iterate every entry in linear time, in unspecified structural order.

        Every yielded priority is fully composed: children are reached only through the
        tag-composing accessors, so a pending action is never skipped and never applied twice.
        Repeated :meth:`delete_minimum` gives ascending order instead.
        """

        pending: list[_Tree[E, P, A]] = [] if self._root is None else [self._root]
        while pending:
            tree = self._expose(pending.pop())
            yield MonotoneHeapEntry(tree.element, tree.priority)
            forest = tree.children
            while forest is not None:
                pending.append(self._forest_head(forest))
                forest = self._forest_tail(forest)

    def shared_tree_count(self, other: PersistentMonotoneActionHeap[E, P, A]) -> int:
        """Number of retained tree nodes reachable from both heaps by object identity.

        Used by the tests to show that a derived version really does share structure with the
        version it came from, rather than having been rebuilt.
        """

        def collect(root: _Tree[E, P, A] | None) -> set[int]:
            """Gather the identities of every retained tree node reachable from ``root``."""

            destination: set[int] = set()
            pending: list[_Tree[E, P, A]] = [] if root is None else [root]
            while pending:
                tree = pending.pop()
                identity = id(tree)
                if identity in destination:
                    continue
                destination.add(identity)
                forest = tree.children
                while forest is not None:
                    pending.append(forest.head)
                    forest = forest.tail
            return destination

        return len(collect(self._root) & collect(other._root))

    @property
    def _root_identity(self) -> object | None:
        return self._root

    @property
    def _root_children_identity(self) -> object | None:
        return None if self._root is None else self._root.children

    # -- action plumbing ---------------------------------------------------------------------

    def _new(
        self, root: _Tree[E, P, A] | None, count: int
    ) -> PersistentMonotoneActionHeap[E, P, A]:
        return PersistentMonotoneActionHeap(root, count, self.comparator, self.action_policy)

    def _entry_of(self, tree: _Tree[E, P, A]) -> MonotoneHeapEntry[E, P]:
        return MonotoneHeapEntry(tree.element, self.action_policy.apply(tree.action, tree.priority))

    def _le(self, left: _Tree[E, P, A], right: _Tree[E, P, A]) -> bool:
        return (
            self.comparator(
                self.action_policy.apply(left.action, left.priority),
                self.action_policy.apply(right.action, right.priority),
            )
            <= 0
        )

    def _tag_tree(self, tree: _Tree[E, P, A], outer: A) -> _Tree[E, P, A]:
        """Push a newer action onto one tree, composing it over the tree's pending action."""

        if self.action_policy.is_identity(outer):
            return tree
        return _Tree(
            tree.rank,
            tree.element,
            tree.priority,
            tree.children,
            self.action_policy.compose(outer, tree.action),
        )

    def _tag_forest(self, forest: _Forest[E, P, A], outer: A) -> _Forest[E, P, A]:
        """Push a newer action onto one forest suffix, composing it over the suffix's action."""

        if self.action_policy.is_identity(outer):
            return forest
        return _Forest(
            forest.head,
            forest.tail,
            self.action_policy.compose(outer, forest.action),
        )

    def _expose(self, tree: _Tree[E, P, A]) -> _Tree[E, P, A]:
        """Materialize one tree's pending action into its own priority and its child forest."""

        if self.action_policy.is_identity(tree.action):
            return tree
        return _Tree(
            tree.rank,
            tree.element,
            self.action_policy.apply(tree.action, tree.priority),
            None if tree.children is None else self._tag_forest(tree.children, tree.action),
            self.action_policy.identity,
        )

    def _cons(self, head: _Tree[E, P, A], tail: _Forest[E, P, A] | None) -> _Forest[E, P, A]:
        """Build an identity-tagged spine cell, so an attached tree keeps only its own history."""

        return _Forest(head, tail, self.action_policy.identity)

    def _forest_head(self, forest: _Forest[E, P, A]) -> _Tree[E, P, A]:
        return self._tag_tree(forest.head, forest.action)

    def _forest_tail(self, forest: _Forest[E, P, A]) -> _Forest[E, P, A] | None:
        return None if forest.tail is None else self._tag_forest(forest.tail, forest.action)

    def _forest_to_list(self, forest: _Forest[E, P, A] | None) -> list[_Tree[E, P, A]]:
        result: list[_Tree[E, P, A]] = []
        cursor = forest
        while cursor is not None:
            result.append(self._forest_head(cursor))
            cursor = self._forest_tail(cursor)
        return result

    # -- skew-binomial kernel over logical, tag-composed views -------------------------------

    def _skew_insert(
        self, tree: _Tree[E, P, A], forest: _Forest[E, P, A] | None
    ) -> _Forest[E, P, A]:
        # Ranks are tag-invariant, so the raw tail's rank decides the link before any tagged tail
        # cell is materialized.  Only the linking branch pays for the tagged tail.
        if forest is not None:
            raw_tail = forest.tail
            if raw_tail is not None and forest.head.rank == raw_tail.head.rank:
                tail = self._tag_forest(raw_tail, forest.action)
                return self._cons(
                    self._skew_link(tree, self._forest_head(forest), self._forest_head(tail)),
                    self._forest_tail(tail),
                )
        return self._cons(tree, forest)

    def _skew_link(
        self, zero: _Tree[E, P, A], first: _Tree[E, P, A], second: _Tree[E, P, A]
    ) -> _Tree[E, P, A]:
        if first.rank != second.rank:
            raise ValueError("Invalid skew-link ranks.")
        # Only the logical winner is exposed; the losing trees attach through identity-tagged
        # spine cells so they keep their own action histories.
        if self._le(first, zero) and self._le(first, second):
            winner = self._expose(first)
            return _Tree(
                first.rank + 1,
                winner.element,
                winner.priority,
                self._cons(zero, self._cons(second, winner.children)),
                self.action_policy.identity,
            )
        if self._le(second, zero) and self._le(second, first):
            winner = self._expose(second)
            return _Tree(
                second.rank + 1,
                winner.element,
                winner.priority,
                self._cons(zero, self._cons(first, winner.children)),
                self.action_policy.identity,
            )
        exposed_zero = self._expose(zero)
        return _Tree(
            first.rank + 1,
            exposed_zero.element,
            exposed_zero.priority,
            self._cons(first, self._cons(second, exposed_zero.children)),
            self.action_policy.identity,
        )

    def _link(self, first: _Tree[E, P, A], second: _Tree[E, P, A]) -> _Tree[E, P, A]:
        if first.rank != second.rank:
            raise ValueError("Invalid binomial-link ranks.")
        first_wins = self._le(first, second)
        winner = self._expose(first if first_wins else second)
        loser = second if first_wins else first
        return _Tree(
            winner.rank + 1,
            winner.element,
            winner.priority,
            self._cons(loser, winner.children),
            self.action_policy.identity,
        )

    def _get_minimum(
        self, forest: _Forest[E, P, A]
    ) -> tuple[_Tree[E, P, A], _Forest[E, P, A] | None]:
        """The minimum tree of a spine and the spine that remains, iteratively.

        A tie favours the earliest spine occurrence, because the fold runs from the last cell
        back to the first and an earlier head only has to compare less than *or equal*.
        """

        cells: list[tuple[_Tree[E, P, A], _Forest[E, P, A] | None]] = []
        cursor: _Forest[E, P, A] | None = forest
        while cursor is not None:
            head = self._forest_head(cursor)
            tail = self._forest_tail(cursor)
            cells.append((head, tail))
            cursor = tail
        if not cells:
            raise AssertionError("A nonempty forest produced no spine cells.")

        minimum = cells[-1][0]
        remainder: _Forest[E, P, A] | None = None
        for head, tail in reversed(cells[:-1]):
            if self._le(head, minimum):
                minimum = head
                remainder = tail
            else:
                remainder = self._cons(head, remainder)
        return minimum, remainder

    def _split_forest(self, rank: int, initial: _Forest[E, P, A] | None) -> _SplitForest[E, P, A]:
        """Decompose a minimum tree's fused children into zeros, ranked trees, and the rest."""

        current_rank = rank
        zeros: _Forest[E, P, A] | None = None
        trees: _Forest[E, P, A] | None = None
        forest = initial
        while True:
            if current_rank == 0:
                return _SplitForest(zeros, trees, forest)
            if forest is None:
                raise ValueError("Invalid skew-binomial forest invariant.")

            first = self._forest_head(forest)
            tail = self._forest_tail(forest)
            if current_rank == 1 and tail is None:
                return _SplitForest(zeros, self._cons(first, trees), None)
            if tail is None:
                raise ValueError("Invalid skew-binomial forest invariant.")

            second = self._forest_head(tail)
            rest = self._forest_tail(tail)
            if current_rank == 1:
                # The rank-zero ambiguity is resolved in favour of the structural prefix.
                if second.rank == 0:
                    return _SplitForest(self._cons(first, zeros), self._cons(second, trees), rest)
                return _SplitForest(zeros, self._cons(first, trees), self._cons(second, rest))
            if first.rank == second.rank:
                return _SplitForest(zeros, self._cons(first, self._cons(second, trees)), rest)
            if first.rank == 0:
                zeros = self._cons(first, zeros)
                trees = self._cons(second, trees)
                forest = rest
            else:
                trees = self._cons(first, trees)
                forest = self._cons(second, rest)
            current_rank -= 1

    def _skew_meld(
        self, left: _Forest[E, P, A] | None, right: _Forest[E, P, A] | None
    ) -> _Forest[E, P, A] | None:
        return self._union_unique(self._uniquify(left), self._uniquify(right))

    def _uniquify(self, forest: _Forest[E, P, A] | None) -> _Forest[E, P, A] | None:
        # Iterative rather than recursive: a corrupted spine must not be able to exhaust the
        # interpreter stack, and Python's default recursion limit is small.
        heads = self._forest_to_list(forest)
        result: _Forest[E, P, A] | None = None
        for head in reversed(heads):
            result = self._insert_ranked(head, result)
        return result

    def _insert_ranked(
        self, tree: _Tree[E, P, A], forest: _Forest[E, P, A] | None
    ) -> _Forest[E, P, A]:
        carry = tree
        cursor = forest
        while cursor is not None:
            head = self._forest_head(cursor)
            if carry.rank > head.rank:
                raise ValueError("Invalid ranked insertion order.")
            if carry.rank < head.rank:
                return self._cons(carry, cursor)
            linked = self._link(carry, head)
            cursor = self._forest_tail(cursor)
            carry = linked
        return self._cons(carry, None)

    def _union_unique(
        self, left: _Forest[E, P, A] | None, right: _Forest[E, P, A] | None
    ) -> _Forest[E, P, A] | None:
        # The recursive formulation conses onto, or ranked-inserts into, the result of the merge
        # of the two tails.  Recording the pending step for each spine position and replaying it
        # in reverse reproduces that exactly without recursion.
        pending: list[tuple[bool, _Tree[E, P, A]]] = []
        base: _Forest[E, P, A] | None
        while True:
            if left is None:
                base = right
                break
            if right is None:
                base = left
                break
            left_head = self._forest_head(left)
            right_head = self._forest_head(right)
            if left_head.rank < right_head.rank:
                pending.append((False, left_head))
                left = self._forest_tail(left)
            elif left_head.rank > right_head.rank:
                pending.append((False, right_head))
                right = self._forest_tail(right)
            else:
                pending.append((True, self._link(left_head, right_head)))
                left = self._forest_tail(left)
                right = self._forest_tail(right)

        result = base
        for is_carry, tree in reversed(pending):
            result = self._insert_ranked(tree, result) if is_carry else self._cons(tree, result)
        return result

    # -- validation ---------------------------------------------------------------------------

    def validate_structure(self) -> PersistentMonotoneActionHeapStatistics:
        """Walk the whole representation and return its shape measurements.

        Independently checks the bootstrapped root's rank, the fused primitive-child encoding of
        every tree, the skew-binomial forest rank rules (nondecreasing, and only the first two may
        be equal), the logical heap order between every tree and its children *through* their
        composed pending actions, and agreement with the cached count.  Raises :class:`ValueError`
        on the first violation.  The walk uses an explicit worklist, so it cannot exhaust the
        interpreter stack.  A defensive audit, not part of normal use.
        """

        if self._root is None:
            if self._count != 0:
                raise ValueError("Empty action-heap count mismatch.")
            return PersistentMonotoneActionHeapStatistics(0, 0, 0, 0, 0)
        if self._root.rank != 0:
            raise ValueError("Global action-heap root must have rank zero.")

        tagged_components = 0
        normalized_root = self._expose(self._root)
        root_forest_length = self._validate_skew_forest(normalized_root.children)
        pending: list[tuple[_Tree[E, P, A], int]] = [(self._root, 1)]
        count = 0
        maximum_rank = 0
        maximum_depth = 0
        while pending:
            tagged, depth = pending.pop()
            if not self.action_policy.is_identity(tagged.action):
                tagged_components += 1
            tree = self._expose(tagged)
            if tree.rank < 0:
                raise ValueError("An action-heap tree has a negative rank.")
            count += 1
            maximum_rank = max(maximum_rank, tree.rank)
            maximum_depth = max(maximum_depth, depth)
            self._validate_skew_forest(self._validate_fused_children(tree))
            forest = tree.children
            while forest is not None:
                if not self.action_policy.is_identity(forest.action):
                    tagged_components += 1
                child = self._forest_head(forest)
                if not self._le(tree, child):
                    raise ValueError("An action-heap child outranks its parent.")
                pending.append((child, depth + 1))
                forest = self._forest_tail(forest)
        if count != self._count:
            raise ValueError("Action-heap logical count mismatch.")
        return PersistentMonotoneActionHeapStatistics(
            self._count,
            root_forest_length,
            maximum_rank,
            maximum_depth,
            tagged_components,
        )

    def _validate_fused_children(self, tree: _Tree[E, P, A]) -> _Forest[E, P, A] | None:
        rank = tree.rank
        forest = tree.children
        while rank > 0:
            if forest is None:
                raise ValueError("A ranked action-heap tree is missing children.")
            first = self._forest_head(forest)
            tail = self._forest_tail(forest)
            if rank == 1:
                if first.rank != 0:
                    raise ValueError("A rank-one tree must begin with a rank-zero child.")
                if tail is None:
                    return None
                return self._forest_tail(tail) if self._forest_head(tail).rank == 0 else tail
            if tail is None:
                raise ValueError("A ranked action-heap tree has incomplete children.")
            second = self._forest_head(tail)
            if first.rank == second.rank:
                if first.rank != rank - 1:
                    raise ValueError("A skew-linked tree has invalid child ranks.")
                return self._forest_tail(tail)
            if first.rank == 0:
                if second.rank != rank - 1:
                    raise ValueError("A skew-linked tree has an invalid ranked child.")
                forest = self._forest_tail(tail)
            else:
                if first.rank != rank - 1:
                    raise ValueError("A linked tree has an invalid ranked child.")
                forest = tail
            rank -= 1
        return forest

    def _validate_skew_forest(self, initial: _Forest[E, P, A] | None) -> int:
        length = 0
        previous_rank = -1
        duplicate = False
        forest = initial
        while forest is not None:
            # Ranks are tag-invariant, so the raw head answers without materializing a tagged one.
            rank = forest.head.rank
            if rank < 0:
                raise ValueError("An action-heap forest contains a negative rank.")
            if rank < previous_rank:
                raise ValueError("Action-heap forest ranks are not nondecreasing.")
            if rank == previous_rank:
                if length != 1 or duplicate:
                    raise ValueError("Only the first two forest ranks may be equal.")
                duplicate = True
            previous_rank = rank
            length += 1
            forest = self._forest_tail(forest)
        return length


__all__ = [
    "ComparatorBoundMonotoneHeapAction",
    "MonotoneActionMinimumView",
    "MonotoneHeapAction",
    "MonotoneHeapEntry",
    "OrderClamp",
    "OrderClampPolicy",
    "PersistentMonotoneActionHeap",
    "PersistentMonotoneActionHeapStatistics",
]
