# src/Cpp/Hamt

- Status: Active port workspace
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing the native C++ HAMT port
- Scope: Project layout and validation entry points for `src/Cpp/Hamt`

`src/Cpp/Hamt` is the C++20 port of the C# HAMT project under `src/CSharp/src/Tools.DataStructures.Hamt`.
It provides immutable unordered
collections backed by a hash-array mapped trie:

- `tools::data_structures::hamt::persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>`
- `tools::data_structures::hamt::persistent_hash_set<T, Hash, KeyEqual>`
- `persistent_int_map<T>` / `persistent_long_map<T>` and the corresponding explicit-width
  `persistent_int_set` / `persistent_long_set` types.

The implementation preserves the C# library's core shape: 32-way logical branching, five hash bits
per trie level, canonical CHAMP branches with separate data/node maps, compact inline payload and
child-only vectors, immutable equal-hash collision buckets, custom
hash/equality policy objects, structural sharing across versions, first equivalent key/item
retention, and no-op root reuse. A transient `bulk_builder` (mirroring the C# reference's bulk
construction) mutates unpublished nodes in place and freezes them into detached persistent maps;
`create_range` and set intersection build through it. Maps also expose `map_equals` and owned typed
added/removed/changed diff. Because C++ collections use value semantics,
identity guarantees are expressed as shared root identity rather than object reference identity.

The integer family is a separate big-endian Patricia core. It sign-flips keys for ascending signed
iteration, compresses unary prefixes, caches subtree cardinality, and aligns prefixes for structural
union/intersection/difference. Map union/intersection have resolver overloads for overlapping values;
all algebra preserves shared roots when the semantic result is unchanged.

## Layout

- `include/Tools/DataStructures/Hamt/persistent_hash_map.hpp` contains the template map
  implementation.
- `include/Tools/DataStructures/Hamt/persistent_hash_set.hpp` contains the set wrapper and set
  algebra.
- `include/Tools/DataStructures/Hamt/persistent_int_map.hpp` contains both widths of Patricia maps
  and sets.
- `tests/` contains the [deterministic native test executable](tests/README.md).
- `build.ps1` imports the MSVC toolchain through Scriptorium and compiles the test executable.
- `docs/api-specification.md` documents the C++ API adaptation and complexity guarantees.
- `docs/usage.md` provides practical include, value-semantics, map/set, policy, iteration, and set-algebra examples.
- `docs/validation.md` records the local build script, warning policy, Debug/Release commands, and
  native test coverage.

## Validation

Use the local MSVC toolchain:

```powershell
.\build.ps1 -RunTests
.\build.ps1 -Configuration Release -RunTests
```

Build outputs are written under `build/<Configuration>/`, which is ignored by the repository. See
[`docs/validation.md`](docs/validation.md) for compiler flags, generated outputs, and test coverage.
