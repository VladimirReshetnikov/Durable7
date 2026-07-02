# HamtCpp

- Status: Initial port workspace
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing the native C++ HAMT port
- Scope: Project layout and validation entry points for `HamtCpp`

`HamtCpp` is the C++20 port of the C# `Hamt` workspace. It provides immutable unordered
collections backed by a hash-array mapped trie:

- `tools::data_structures::hamt::persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>`
- `tools::data_structures::hamt::persistent_hash_set<T, Hash, KeyEqual>`

The implementation preserves the C# library's core shape: 32-way logical branching, five hash bits
per trie level, compact bitmap-indexed branch nodes, immutable equal-hash collision buckets, custom
hash/equality policy objects, structural sharing across versions, first equivalent key/item
retention, and no-op root reuse. Because C++ collections use value semantics, identity guarantees
are expressed as shared root identity rather than object reference identity.

## Layout

- `include/Tools/DataStructures/Hamt/persistent_hash_map.hpp` contains the template map
  implementation.
- `include/Tools/DataStructures/Hamt/persistent_hash_set.hpp` contains the set wrapper and set
  algebra.
- `tests/persistent_hamt_tests.cpp` contains deterministic unit and randomized model tests.
- `build.ps1` imports the MSVC toolchain through Scriptorium and compiles the test executable.
- `docs/api-specification.md` documents the C++ API adaptation and complexity guarantees.

## Validation

Use the local MSVC toolchain:

```powershell
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

Build outputs are written under `build/<Configuration>/`, which is ignored by the repository.
