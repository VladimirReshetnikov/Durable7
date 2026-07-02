# src/C/Hamt

- Status: Active port workspace
- Created (UTC): 2026-07-02T18:18:57Z
- Repository HEAD: 3444f5ee27357d86c43db484993f8f12dfd4887c
- Audience: Maintainers implementing and reviewing the pure C HAMT port
- Scope: Project layout and validation entry points for `src/C/Hamt`

`src/C/Hamt` is the C17 port of the persistent HAMT collection family. It provides a type-erased C API
for immutable unordered collections backed by a hash-array mapped trie:

- `tds_hamt_map`, a persistent map from `void *` keys to `void *` values.
- `tds_hamt_set`, a persistent set wrapper over the map core.

The implementation preserves the C# and C++ libraries' core shape: 32-way logical branching, five
hash bits per trie level, compact bitmap-indexed branch nodes, immutable equal-hash collision
buckets, custom hash/equality policy callbacks, first equivalent key/item retention, no-op root
reuse, and structural sharing across versions. Because this is C, ownership is explicit: maps and
sets are value structs whose roots are reference-counted, and callers use `clone`/`destroy` to manage
version lifetimes.

## Layout

- `include/Tools/DataStructures/Hamt/hamt.h` contains the public C API.
- `src/hamt.c` contains the HAMT implementation.
- `tests/` contains the [deterministic native test executable](tests/README.md).
- `build.ps1` imports the MSVC toolchain through Scriptorium and compiles the test executable.
- `docs/api-specification.md` documents the C API adaptation and complexity guarantees.
- `docs/usage.md` provides practical policy, lifetime, update, iteration, and set-algebra examples.
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
