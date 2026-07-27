# C# Ordered Collections Documentation

- Status: Current documentation index
- Created (UTC): 2026-07-15T01:28:46Z
- Repository HEAD: 5fd1a85c5ec58886f0dbabe805552bd37ec40871
- Audience: Users and maintainers of `Durable7.Ordered`
- Scope: The independently owned C# insertion-ordered persistent map/set/multimap workspace

`Durable7.Ordered` is the neutral owner of `PersistentOrderedMap<TKey, TValue>`,
`PersistentOrderedSet<T>`, and `PersistentOrderedMultimap<TKey, TValue>`, immutable
insertion-ordered collections composed from the public C# HAMT and FingerTree libraries. It is a
general collection project: its production contract and its tests are defined against the semantics
documented here, not derived from any single application's collection.

| Document | Use it for |
| --- | --- |
| [Overview](overview.md) | Representation, ownership, invariants, capability boundaries, and headline complexity |
| [Usage](usage.md) | Construction, representative recovery, explicit movement, slicing, sorting, algebra, and identity examples |
| [API specification](api-specification.md) | Normative member-by-member semantics, ordering, comparer normalization, failure behavior, enumeration, and complexity |
| [Persistent ordered multimap](persistent-ordered-multimap.md) | Normative grouped-order, representative, API, and complexity contract for the set-valued ordered multimap |
| [Validation](validation.md) | Single-worker restore/build/test commands, coverage map, dependency audit, and benchmark boundary |

Primary code and tests:

- [library project](../../src/Durable7.Ordered/Durable7.Ordered.csproj)
- [`PersistentOrderedMap<TKey, TValue>` source](../../src/Durable7.Ordered/PersistentOrderedMap.cs)
- [`PersistentOrderedSet<T>` source](../../src/Durable7.Ordered/PersistentOrderedSet.cs)
- [`PersistentOrderedMultimap<TKey, TValue>` source](../../src/Durable7.Ordered/PersistentOrderedMultimap.cs)
- [algebra and relations](../../src/Durable7.Ordered/PersistentOrderedSet.Algebra.cs)
- [invariant diagnostics](../../src/Durable7.Ordered/PersistentOrderedSet.Diagnostics.cs)
- [test project](../../tests/Durable7.Ordered.Tests/Durable7.Ordered.Tests.csproj)
- [test-suite map](../../tests/Durable7.Ordered.Tests/README.md)

The authoritative execution rationale is the repository-level
[benchmark-independent structures proposal](../../../../docs/proposals/benchmark-independent-next-structures-2026-07-14.md).
The normative cross-language behavior obligations are the
[insertion-ordered set](../../../../docs/reference/semantic-contracts.md#insertion-ordered-persistent-set)
and [insertion-ordered map](../../../../docs/reference/semantic-contracts.md#insertion-ordered-persistent-map)
sections of the semantic contracts reference; the eight sibling ports listed in the
[data-structure catalog](../../../../docs/reference/data-structure-catalog.md) implement the same
contract in their own languages.
