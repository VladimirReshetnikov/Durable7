# C# Tungsten Collections Documentation

- Created (UTC): 2026-07-07T15:05:40Z
- Repository HEAD: 754f2e474caf2419bfabd5f88565341ddadbf449
- Audience: Maintainers, consumers, and porters of the C# Tungsten-collections library
- Scope: Documentation index for `src/CSharp/src/Tools.DataStructures.Tungsten`

`Tools.DataStructures.Tungsten` provides persistent collections shaped for representing Tungsten
Language `List` and `Association` expressions: `PersistentList<T>` and
`PersistentAssociation<TKey, TValue>`. The primary external client is the Tungsten engine
(`C:\Smithereens\src\Tungsten`, a kernel-free Tungsten Language automation workspace); the library
itself is client-agnostic and generic. This C# implementation is the reference for ports to the
repository's other language workspaces.

## Documents

- [overview.md](overview.md) - what the library is, the composition it uses, the semantics it
  guarantees, and the design decisions behind it.
- [usage.md](usage.md) - task-oriented examples for both types, including the Tungsten operation
  correspondence.
- [api-specification.md](api-specification.md) - the public surface with contracts, ordering
  rules, complexity, and allocation behavior.
- [validation.md](validation.md) - how to build and test, and what the test suite covers.

## Related Material

- [Tests README](../../tests/Tools.DataStructures.Tungsten.Tests/README.md)
- [Derived structure catalog](../../../../docs/reference/derived-structure-catalog.md) - the
  verified composition rules (`PersistentOrderedMap` pattern) this library instantiates.
- [Data structure catalog](../../../../docs/reference/data-structure-catalog.md) - shipped
  cross-language surface.
- Tungsten design study (external, in the Smithereens repository):
  `src/Tungsten/docs/reports/2026-07-03-list-association-persistent-backends.md` - the
  kernel-verified Tungsten semantics and the adversarially reviewed backend designs.
