# C# Ordered Collections Documentation

- Status: Current documentation index
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Users and maintainers of `Tools.DataStructures.Ordered`
- Scope: The independently owned C# insertion-ordered persistent-set workspace

`Tools.DataStructures.Ordered` is the neutral owner of `PersistentOrderedSet<T>`, an immutable
insertion-ordered set composed from the public C# HAMT and FingerTree libraries. It is a general
collection project: neither its production contract nor its tests depend on the application-specific
Tungsten collections.

| Document | Use it for |
| --- | --- |
| [Overview](overview.md) | Representation, ownership, invariants, capability boundaries, and headline complexity |
| [Usage](usage.md) | Construction, representative recovery, explicit movement, slicing, sorting, algebra, and identity examples |
| [API specification](api-specification.md) | Normative member-by-member semantics, ordering, comparer normalization, failure behavior, enumeration, and complexity |
| [Validation](validation.md) | Single-worker restore/build/test commands, coverage map, dependency audit, and benchmark boundary |

Primary code and tests:

- [library project](../../src/Tools.DataStructures.Ordered/Tools.DataStructures.Ordered.csproj)
- [`PersistentOrderedSet<T>` source](../../src/Tools.DataStructures.Ordered/PersistentOrderedSet.cs)
- [algebra and relations](../../src/Tools.DataStructures.Ordered/PersistentOrderedSet.Algebra.cs)
- [invariant diagnostics](../../src/Tools.DataStructures.Ordered/PersistentOrderedSet.Diagnostics.cs)
- [test project](../../tests/Tools.DataStructures.Ordered.Tests/Tools.DataStructures.Ordered.Tests.csproj)
- [test-suite map](../../tests/Tools.DataStructures.Ordered.Tests/README.md)

The authoritative execution rationale is the repository-level
[benchmark-independent structures proposal](../../../../docs/proposals/benchmark-independent-next-structures-2026-07-14.md).
The normative ownership rule is the
[Tungsten application-leaf boundary](../../../../docs/reference/tungsten-application-leaf-boundary.md).
