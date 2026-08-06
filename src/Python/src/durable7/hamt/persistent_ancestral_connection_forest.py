"""Fully branching persistent insertion-only connectivity forest with a first-connection query.

:class:`PersistentAncestralConnectionForest` is a persistent union-find over the fixed integer
vertex universe ``0 .. vertex_count - 1``. Each successful :meth:`link` performs union by size
*without* path compression and labels the one new root-parent edge with the child
:class:`AncestralConnectionVersion`. A redundant (already connected) link still publishes a
distinct child history version, but shares the complete connectivity index, which
:meth:`shares_connectivity_root_with` can confirm.

The parent cells live sparsely in a :class:`~durable7.hamt.persistent_hamt.PersistentHashMap`. An
absent cell denotes a singleton root, so :meth:`create` is O(1) rather than O(``vertex_count``).
Union by size limits every parent path to O(log n) cells. :meth:`first_connected` answers "at which
ancestor version did these two vertices first become connected?" by comparing the two current
parent paths and taking the latest union-edge version strictly below their forest lowest common
ancestor. It never searches the version history, so its cost is independent of history depth.

Instances and version tokens are immutable snapshots safe for concurrent readers. Deletion,
confluent merging, retroactive updates, and path compression are deliberately outside this module's
contract. The O(log n) parent-path factor is worst-case logarithmic in component size rather than
the inverse-Ackermann amortized bound of an ephemeral linear-history union-find.

Why the CHAMP factor is unconditional here
------------------------------------------

Throughout this module w denotes the cost of one parent-cell lookup or insertion in the backing
CHAMP map. That substrate narrows every hash-policy result to a signed 32-bit word, consumes five
hash bits per trie level, and parks keys that share a full 32-bit hash in a collision node whose
bucket is scanned linearly. A collision bucket can therefore hold two distinct keys, and under a
truncating or colliding hash w would be bounded only *in expectation* -- which is exactly why the
Rust and Haskell ports document expected bounds.

This forest removes that caveat by pinning its own vertex hash instead of accepting one. The pinned
function is the MurmurHash3 32-bit finalizer applied to the vertex's 32-bit two's-complement
pattern. Each of its steps is a bijection of the 32-bit word -- ``h ^= h >> k`` with ``k > 0``
leaves the top ``k`` bits fixed and is undone by the same shape of expression, and multiplication
by an odd constant is invertible modulo 2**32 (both constants are odd) -- and the substrate's own
narrowing to the signed 32-bit range is a bijection too. Vertices are validated into
``0 .. vertex_count - 1`` before any cell is touched and :data:`MAXIMUM_VERTEX_COUNT` caps that
universe at 2**31 - 1, so the whole composition is *injective* over every key the map can ever see.
No collision bucket can hold two distinct vertices, and two distinct vertices must diverge within
32 significant hash bits, that is within ``ceil(32 / 5) = 7`` levels of 32-way nodes. One cell read
or write is therefore O(1) in the worst case, and every bound stated here is unconditional, matching
the C++ and OCaml ports rather than the Rust and Haskell ones. Only the hash policy differs; the
forest algorithm and the union-by-size height argument are identical.

The workspace default policy would in fact also be injective here, because CPython's ``hash`` is
the identity on integers below 2**61 - 1 and ``PYTHONHASHSEED`` perturbs only string and bytes
hashing. Pinning makes the complexity claim a property of this module rather than of CPython's
integer-hash contract, and the finalizer's avalanche additionally spreads clustered vertex numbering
across the low-order trie levels the map consumes first.

Recursion
---------

Every walk in this module -- version chains, parent paths, and the audit -- is an explicit loop, so
a history hundreds of thousands of links deep costs no interpreter stack. The CHAMP map's own
descent recurses, but over at most seven levels.
"""

from __future__ import annotations

from dataclasses import dataclass

from .hash_policy import HashPolicy, create_hash_policy
from .persistent_hamt import PersistentHashMap

MAXIMUM_VERTEX_COUNT = (1 << 31) - 1
"""Largest permitted vertex universe.

The reference implementation's universe is a 32-bit ``int``, and this bound is also what keeps the
pinned vertex hash injective after the CHAMP map narrows it to a signed 32-bit word.
"""


def _vertex_hash(vertex: int) -> int:
    """Return the MurmurHash3 32-bit finalizer of ``vertex``, a bijection of the 32-bit word."""

    bits = vertex & 0xFFFF_FFFF
    bits ^= bits >> 16
    bits = (bits * 0x85EB_CA6B) & 0xFFFF_FFFF
    bits ^= bits >> 13
    bits = (bits * 0xC2B2_AE35) & 0xFFFF_FFFF
    return bits ^ (bits >> 16)


def _vertex_equivalent(left: int, right: int) -> bool:
    """Return whether two vertex numbers denote the same vertex."""

    return left == right


VERTEX_HASH_POLICY: HashPolicy[int] = create_hash_policy(_vertex_hash, _vertex_equivalent)
"""The hash policy this forest pins into its parent-cell map.

Part of the type's complexity contract rather than a tuning detail: its injectivity over the vertex
universe is what makes the CHAMP factor a worst-case rather than an expected constant.
"""


class AncestralConnectionVersion:
    """Identifies one immutable version in a :class:`PersistentAncestralConnectionForest` history.

    Tokens use *reference* identity: ``==`` is Python's inherited identity comparison, so two
    versions at equal depths in sibling branches are never equal, and ``hash`` is the inherited
    identity hash, so tokens may be put in sets and dictionaries. This is deliberately **not** a
    dataclass. A token records only a depth, a parent, and a root, so the structural ``__eq__`` a
    dataclass generates would make two sibling branches that split at the same depth compare equal
    and let a witness from one branch masquerade as a witness from the other; it would also recurse
    without bound, because a history root is its own root and a deep chain is thousands of parents
    long. Use :meth:`is_same_version` when the intent should be explicit in the reading.

    The parent chain contains history identities, not mutable connectivity state.
    """

    __slots__ = ("_depth", "_parent", "_root")

    def __init__(self, parent: AncestralConnectionVersion | None = None) -> None:
        """Create a history root when ``parent`` is ``None``, otherwise a child one link deeper.

        Published only by :meth:`PersistentAncestralConnectionForest.link` and
        :meth:`PersistentAncestralConnectionForest.create`.
        """

        self._parent: AncestralConnectionVersion | None = parent
        self._depth: int = 0 if parent is None else parent._depth + 1
        self._root: AncestralConnectionVersion = self if parent is None else parent._root

    @property
    def parent(self) -> AncestralConnectionVersion | None:
        """This version's parent, or ``None`` at the history root. O(1)."""

        return self._parent

    @property
    def depth(self) -> int:
        """The number of link events from the history root. O(1)."""

        return self._depth

    @property
    def root(self) -> AncestralConnectionVersion:
        """The root identity of this version tree. O(1)."""

        return self._root

    def is_same_version(self, other: AncestralConnectionVersion) -> bool:
        """Whether two handles denote the very same version. O(1), and identical to ``is``."""

        return self is other

    def __repr__(self) -> str:
        """Show only the depth; printing the parent chain would walk the whole history."""

        return f"AncestralConnectionVersion(depth={self._depth})"


@dataclass(frozen=True, slots=True)
class AncestralConnectionForestStatistics:
    """Shape measurements returned by a successful structural audit."""

    vertex_count: int
    component_count: int
    stored_cell_count: int
    maximum_parent_path_length: int
    history_depth: int


@dataclass(frozen=True, slots=True)
class _Cell:
    """One sparse parent record.

    ``size`` is meaningful only for a root, whose ``parent`` is the vertex itself, and ``joined_at``
    is present only on a non-root edge. An absent cell canonically means "singleton root of size 1".
    """

    parent: int
    size: int
    joined_at: AncestralConnectionVersion | None


@dataclass(frozen=True, slots=True)
class _PathStep:
    """One vertex on a parent path together with the union-edge tag leaving it."""

    vertex: int
    joined_at: AncestralConnectionVersion | None


def _later(
    left: AncestralConnectionVersion | None,
    right: AncestralConnectionVersion | None,
) -> AncestralConnectionVersion | None:
    """Return the deeper of two candidate versions, keeping ``left`` on an equal depth."""

    if right is not None and (left is None or right.depth > left.depth):
        return right
    return left


class PersistentAncestralConnectionForest:
    """A fully branching persistent insertion-only connectivity forest.

    Reports the first ancestor version in which two vertices became connected without searching the
    version history. See the module docstring for the cost model.
    """

    __slots__ = ("_cells", "_component_count", "_version", "_vertex_count")

    def __init__(
        self,
        vertex_count: int,
        component_count: int,
        cells: PersistentHashMap[int, _Cell],
        version: AncestralConnectionVersion,
    ) -> None:
        """Wrap an already-built connectivity index; use :meth:`create` and :meth:`link` instead.

        The caller is responsible for the counts agreeing with ``cells``.
        """

        self._vertex_count = vertex_count
        self._component_count = component_count
        self._cells = cells
        self._version = version

    @classmethod
    def create(cls, vertex_count: int) -> PersistentAncestralConnectionForest:
        """Return an edgeless root version over vertices ``0 .. vertex_count - 1``. O(1).

        Every vertex starts as its own component. Because a singleton is represented by the
        *absence* of a cell, construction costs the same for a universe of two vertices and for one
        of :data:`MAXIMUM_VERTEX_COUNT`.

        Raises :class:`ValueError` when ``vertex_count`` is negative or exceeds
        :data:`MAXIMUM_VERTEX_COUNT`.
        """

        if vertex_count < 0:
            raise ValueError("Vertex count cannot be negative.")
        if vertex_count > MAXIMUM_VERTEX_COUNT:
            raise ValueError(f"Vertex count cannot exceed {MAXIMUM_VERTEX_COUNT}.")

        cells: PersistentHashMap[int, _Cell] = PersistentHashMap.empty(VERTEX_HASH_POLICY)
        return cls(vertex_count, vertex_count, cells, AncestralConnectionVersion())

    @property
    def vertex_count(self) -> int:
        """The fixed number of vertices in this forest. O(1)."""

        return self._vertex_count

    @property
    def component_count(self) -> int:
        """The number of connected components in this version. O(1)."""

        return self._component_count

    @property
    def version(self) -> AncestralConnectionVersion:
        """The identity token for this history version. O(1)."""

        return self._version

    def find(self, vertex: int) -> int:
        """Return the current union-find representative of ``vertex``. O(w log n) worst case.

        The representative is selected by deterministic union-by-size history and is an
        implementation artifact: sibling versions need not agree on it.

        Raises :class:`ValueError` when ``vertex`` is outside the fixed universe.
        """

        self._check_vertex(vertex)
        return self._find_root(vertex)

    def connected(self, left: int, right: int) -> bool:
        """Whether two vertices have the same current root. O(w log n) worst case.

        Raises :class:`ValueError` when either vertex is outside the fixed universe.
        """

        self._check_vertices(left, right)
        return self._find_root(left) == self._find_root(right)

    def component_size(self, vertex: int) -> int:
        """Return the number of vertices in ``vertex``'s component. O(w log n) worst case.

        An isolated vertex is still a singleton root and reports 1. The answer is the union-by-size
        count cached at the component's current root, so the walk reads the structure without
        compressing it; this forest deliberately has no path compression.

        Raises :class:`ValueError` when ``vertex`` is outside the fixed universe.
        """

        self._check_vertex(vertex)
        return self._size_of(self._find_root(vertex))

    def link(self, left: int, right: int) -> PersistentAncestralConnectionForest:
        """Return a child history version connecting two vertices undirectedly.

        The returned version is always distinct. When the endpoints were already connected -- a
        self link included -- its connectivity cells are shared unchanged, which
        :meth:`shares_connectivity_root_with` can confirm; otherwise exactly one union-by-size root
        edge is added and labelled with the child version. On a size tie the first endpoint's root
        stays the representative.

        O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1)
        space when redundant. The receiver is left untouched, including on failure.

        Raises :class:`ValueError` when either endpoint is outside the fixed universe; nothing is
        published in that case. Unlike the ports whose depth counter is a fixed-width integer, this
        one has no history-depth overflow mode, because Python integers are arbitrary precision.
        """

        self._check_vertices(left, right)
        left_root = self._find_root(left)
        right_root = self._find_root(right)
        child_version = AncestralConnectionVersion(self._version)

        if left_root == right_root:
            return PersistentAncestralConnectionForest(
                self._vertex_count, self._component_count, self._cells, child_version
            )

        left_size = self._size_of(left_root)
        right_size = self._size_of(right_root)
        if left_size < right_size:
            left_root, right_root = right_root, left_root
            left_size, right_size = right_size, left_size

        # On a size tie the first endpoint's root remains the representative, making the result
        # deterministic without changing the logarithmic-height proof.
        cells = self._cells.put(left_root, _Cell(left_root, left_size + right_size, None)).put(
            right_root, _Cell(left_root, 0, child_version)
        )
        return PersistentAncestralConnectionForest(
            self._vertex_count, self._component_count - 1, cells, child_version
        )

    def first_connected(self, left: int, right: int) -> AncestralConnectionVersion | None:
        """Return the earliest version on this version's history in which the pair was connected.

        A vertex is connected to itself at the history root. The answer is computed from the two
        current parent paths as the latest union-edge version strictly below their forest lowest
        common ancestor; the version history is never searched. O(w log n) time and O(log n)
        temporary path space, independent of history depth.

        The result is never a version on another branch, and a later merge of the pair's component
        into a bigger one never replaces the pair's earlier witness.

        The two failure modes stay distinguishable: a vertex outside the fixed universe raises
        :class:`ValueError`, while a pair that is merely disconnected in this version returns
        ``None``.
        """

        self._check_vertices(left, right)
        if left == right:
            return self._version.root

        left_path = self._build_path(left)
        right_path = self._build_path(right)
        if left_path[-1].vertex != right_path[-1].vertex:
            return None

        # Delete the common rootward suffix. The remaining steps are precisely the two paths
        # strictly below the forest LCA, and every one of those steps owns a successful-union tag.
        left_last = len(left_path) - 1
        right_last = len(right_path) - 1
        while (
            left_last >= 0
            and right_last >= 0
            and left_path[left_last].vertex == right_path[right_last].vertex
        ):
            left_last -= 1
            right_last -= 1

        latest: AncestralConnectionVersion | None = None
        for index in range(left_last + 1):
            latest = _later(latest, left_path[index].joined_at)
        for index in range(right_last + 1):
            latest = _later(latest, right_path[index].joined_at)

        # Distinct connected vertices have at least one edge strictly below their LCA.
        if latest is None:
            raise AssertionError("A connected pair has no union-edge witness.")
        return latest

    def shares_connectivity_root_with(self, other: PersistentAncestralConnectionForest) -> bool:
        """Whether two versions retain the exact same sparse connectivity index. O(1).

        Neither can then observe a union recorded in the other. A representation test used to
        confirm that a redundant link avoided copying, not an equality test.
        """

        return self._cells.shares_root_with(other._cells)

    def validate_structure(self) -> AncestralConnectionForestStatistics:
        """Audit the whole representation and return its shape measurements.

        Checks the cached counts, the history-token parent chain and root, sparse-cell canonicality,
        union-tag ancestry and order, the union-by-size height ceiling of ``floor(log2 n)``, the
        cached component count, and the cached root sizes. Raises :class:`ValueError` on the first
        violation, matching the other audits in this workspace that return statistics. O(H + m w
        log n), where H is the history depth and m is the explicit-cell count. A defensive audit;
        ordinary operations maintain these invariants.
        """

        if (
            self._vertex_count < 0
            or self._component_count < 0
            or self._component_count > self._vertex_count
        ):
            raise ValueError("Connection-forest counts are invalid.")

        history_root = self._version.root
        ancestors: set[AncestralConnectionVersion] = set()
        expected_depth = self._version.depth
        version: AncestralConnectionVersion | None = self._version
        while version is not None:
            if version.depth != expected_depth or version.root is not history_root:
                raise ValueError("The history-token parent chain is inconsistent.")
            expected_depth -= 1
            ancestors.add(version)
            version = version.parent
        if (
            expected_depth != -1
            or history_root not in ancestors
            or history_root.parent is not None
            or history_root.depth != 0
        ):
            raise ValueError("The history root is inconsistent.")

        for entry in self._cells:
            cell = entry.value
            if not self._is_vertex(entry.key) or not self._is_vertex(cell.parent):
                raise ValueError("A sparse parent cell addresses a vertex outside the universe.")

            if cell.parent == entry.key:
                if cell.size <= 1 or cell.joined_at is not None:
                    raise ValueError("A stored root cell is not a canonical non-singleton root.")
            elif (
                cell.size != 0
                or cell.joined_at is None
                or cell.joined_at not in ancestors
                or not self._cells.contains_key(cell.parent)
            ):
                raise ValueError("A non-root cell has an invalid size or union-version tag.")

        component_sizes: dict[int, int] = {}
        maximum_parent_path_length = 0
        maximum_height = 0 if self._vertex_count <= 1 else self._vertex_count.bit_length() - 1
        for entry in self._cells:
            current = entry.key
            height = 0
            previous_depth = -1
            while True:
                cell = self._cell(current)
                if cell.parent == current:
                    break
                joined_at = cell.joined_at
                if (
                    joined_at is None
                    or joined_at.depth <= previous_depth
                    or joined_at not in ancestors
                ):
                    raise ValueError(
                        "Union-edge versions do not strictly increase toward the root."
                    )

                previous_depth = joined_at.depth
                current = cell.parent
                height += 1
                if height > maximum_height:
                    raise ValueError("A parent path exceeds the union-by-size height bound.")

            maximum_parent_path_length = max(maximum_parent_path_length, height)
            component_sizes[current] = component_sizes.get(current, 0) + 1

        stored_cell_count = self._cells.size
        absent_singletons = self._vertex_count - stored_cell_count
        if (
            absent_singletons < 0
            or absent_singletons + len(component_sizes) != self._component_count
        ):
            raise ValueError("The cached component count disagrees with the parent forest.")
        for root, size in component_sizes.items():
            if self._size_of(root) != size:
                raise ValueError("A root's cached union size is incorrect.")

        return AncestralConnectionForestStatistics(
            self._vertex_count,
            self._component_count,
            stored_cell_count,
            maximum_parent_path_length,
            self._version.depth,
        )

    def __repr__(self) -> str:
        """Show the universe size, the component count, and the history depth."""

        return (
            f"PersistentAncestralConnectionForest(vertex_count={self._vertex_count}, "
            f"component_count={self._component_count}, depth={self._version.depth})"
        )

    def _find_root(self, vertex: int) -> int:
        current = vertex
        while True:
            parent = self._parent_of(current)
            if parent == current:
                return current
            current = parent

    def _build_path(self, vertex: int) -> list[_PathStep]:
        path: list[_PathStep] = []
        current = vertex
        while True:
            cell = self._cells.get(current)
            parent = current if cell is None else cell.parent
            path.append(_PathStep(current, None if cell is None else cell.joined_at))
            if parent == current:
                return path
            current = parent

    def _cell(self, vertex: int) -> _Cell:
        """Read the cell of ``vertex``; an absent cell is canonically its own singleton root."""

        cell = self._cells.get(vertex)
        return _Cell(vertex, 1, None) if cell is None else cell

    def _parent_of(self, vertex: int) -> int:
        cell = self._cells.get(vertex)
        return vertex if cell is None else cell.parent

    def _size_of(self, vertex: int) -> int:
        cell = self._cells.get(vertex)
        return 1 if cell is None else cell.size

    def _is_vertex(self, vertex: int) -> bool:
        return 0 <= vertex < self._vertex_count

    def _check_vertex(self, vertex: int) -> None:
        if not self._is_vertex(vertex):
            raise ValueError(
                f"Vertex {vertex} must lie within the forest's fixed universe of "
                f"{self._vertex_count} vertices."
            )

    def _check_vertices(self, left: int, right: int) -> None:
        self._check_vertex(left)
        self._check_vertex(right)


__all__ = [
    "MAXIMUM_VERTEX_COUNT",
    "VERTEX_HASH_POLICY",
    "AncestralConnectionForestStatistics",
    "AncestralConnectionVersion",
    "PersistentAncestralConnectionForest",
]
