# OCaml Workspace

- Created (UTC): 2026-07-17T17:58:43Z
- Repository HEAD: b2312bd763b0ad7e9bc1628963dcea5ec43b5e68
- Audience: Maintainers and AI agents working on OCaml ports of repository-owned data structures
- Scope: OCaml package layout, policy foundations, build entry points, and validation

This workspace is the OCaml port of the repository-owned persistent data structures and numerics.
It uses immutable OCaml values for published snapshots, runtime policy records where the source
families retain hashing or comparison behavior, and separately identified mutable builders,
editing sessions, cursors, and streaming cores.

The general-purpose modules under `lib/numerics`, `lib/hamt`, `lib/finger_tree`, and `lib/ordered`
must not depend on `lib/tungsten`. Tungsten is an application-specific leaf and may consume the
general libraries only in that direction.

## Layout

| Path | Responsibility |
| --- | --- |
| `lib/common` | Runtime hashing and comparison policies shared by the general families |
| `lib/numerics` | Fixed-width and sparse integer values |
| `lib/hamt` | Persistent hash, Patricia, concurrent-snapshot, and authenticated-map families |
| `lib/finger_tree` | Persistent sequence, sorted, priority, interval, rope, and streaming families |
| `lib/ordered` | Independently owned insertion-ordered set, map, and grouped multimap |
| `lib/tungsten` | Application-specific persistent List and Association collections |
| `tests` | Alcotest examples, regression tests, models, and QCheck properties by family |

## Toolchain

The workspace requires OCaml 4.14 or newer, Dune 3.20 or newer, Zarith, and Digestif. Alcotest and
QCheck are test-only dependencies; odoc is the documentation dependency. The package constraints
are recorded in `tools-data-structures.opam`.

## Build And Test

From `src/OCaml`:

```powershell
opam install . --deps-only --with-test --with-doc
opam exec -- dune build -j 1
.\test.ps1
opam exec -- dune build @doc -j 1
```

Use `-Workspace Common`, `Numerics`, `Hamt`, `FingerTree`, `Ordered`, or `Tungsten` for a focused
test run. The launcher enforces one opam/Dune job and imports the repository's noninteractive test
failure handling.

The shipped numerics modules expose `UInt256`/`Int256`, `UInt512`/`Int512`,
`UInt1024`/`Int1024`, `Bit_converter_ex`, and non-negative `Sparse_integer` values. The fixed-width
modules preserve modulo arithmetic, checked overflow, signed truncating division, width-constrained
bit operations, and exact 32/64/128-byte two's-complement conversion. Zarith is an implementation
substrate only; the public fixed-width contract never widens at runtime.

Each remaining collection-family checkpoint adds its public modules and corresponding focused tests
before the repository-level indexes claim that family as shipped.
