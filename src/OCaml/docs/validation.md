# OCaml Validation

- Created (UTC): 2026-07-17T22:45:00Z
- Repository HEAD: 87dc70271d808e50b52c868cc4956a8da69b2504
- Audience: Maintainers validating the OCaml package
- Scope: Toolchain, one-worker commands, warning policy, and evidence boundaries

## Requirements

- OCaml 4.14 or newer
- opam 2.1 or newer
- Dune 3.20 or newer
- Zarith, Digestif, and Uutf at runtime
- Alcotest and QCheck for tests; ocamlformat and odoc for repository validation

The package constraints are authoritative in `durable7.opam`. The checked-in
`test.ps1` wrapper forces one opam/Dune job and accepts `Common`, `Hamt`, `FingerTree`,
`Ordered`, or `Tungsten` as a focused workspace.

## Full Gate

From `src/OCaml`:

```powershell
opam install . --deps-only --with-test --with-doc --with-dev-setup
opam exec -- dune clean
opam exec -- dune build -j 1 @check @fmt @doc
opam exec -- dune runtest -j 1 --force
```

On Windows, the canonical test invocation is also:

```powershell
.\test.ps1
```

The library and tests compile with safe strings, strict sequencing/formats, all practical warnings
enabled, and those warnings treated as errors. `@fmt` checks ocamlformat output; `@doc` builds odoc
pages for every public interface.

## Evidence Boundary

Routine validation covers compilation, formatting, documentation, 49 focused Alcotest cases, and
QCheck histories in HAMT, FingerTree, Ordered, and Tungsten suites. It includes exact
single-entry `MST2` bytes, a pinned `ZZT2` HMAC rank vector, and multi-block authenticated
persistence/proof checks. It does not include
benchmarks, performance claims, or byte-level cross-process fixtures beyond the pinned Merkle golden
vector.

See the [test map](../tests/README.md) for suite ownership and coverage.
