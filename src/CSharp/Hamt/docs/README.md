# Hamt Documentation

- Status: Informational
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers and implementers working on the C# HAMT collection
- Scope: Index of design references and local specifications for `src/CSharp/Hamt`

## Current Documents

- [API Specification](api-specification.md) defines the public C# API shape, semantic contracts,
  persistence behavior, and complexity targets for `PersistentHashMap<TKey, TValue>` and
  `PersistentHashSet<T>`.
- [Usage guide](usage.md) shows namespace setup, persistent update patterns, comparer behavior,
  stored equivalent key/item recovery, iteration, and set algebra for the C# HAMT collections.
- [Validation](validation.md) records the local .NET restore/build/test commands, workspace warning
  policy, generated XML-documentation gate, and xUnit/CsCheck coverage.
- [Tests README](../tests/Tools.DataStructures.Hamt.Tests/README.md) maps the xUnit/CsCheck test project,
  source files, filter commands, and model/property coverage.
- [Implementation Review 2026-07-02](hamt-implementation-review-2026-07-02__afa84237ef48.md)
  records the adversarially-verified multi-agent review of the initial implementation, the
  improvements applied in response (annotations, `IReadOnlySet<T>`, single-pass adds,
  allocation-free copy-safe enumerators, key/item recovery APIs, doc-contract fixes, and a
  20-to-47-test suite expansion), and the declined findings with rationale.
