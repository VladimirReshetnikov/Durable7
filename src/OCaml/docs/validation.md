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
or `Ordered` as a focused workspace.

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

Routine validation covers compilation, formatting, documentation, focused Alcotest cases, and
QCheck histories in HAMT, FingerTree, and Ordered suites. It includes exact
single-entry `MST2` bytes, a pinned `ZZT2` HMAC rank vector, and multi-block authenticated
persistence/proof checks. It does not include
benchmarks, performance claims, or byte-level cross-process fixtures beyond the pinned Merkle golden
vector.

See the [test map](../tests/README.md) for suite ownership and coverage.

## The seven research-derived collections

Seven groups cover the collections added on 2026-08-05, plus one for the level-ancestor seam they
share. The seam's group is the one worth naming: its load-bearing case bounds the maximum ancestor
hop count by `4 * ceil(log2 (M + 1))` over a 32,768-node chain, because Myers' coalesced jump links
are the entire reason that backend exists over a plain parent array and removing them leaves every
ancestor *answer* correct while turning each query into an `O(depth)` walk. Only a hop-count
assertion can catch that. The same group pins the odd-block square layout against a multiply-only
oracle, every documented error contract, and the guarantee that a rejected addition publishes no
node and leaves every counter untouched.

The collection groups assert exact backend query-count profiles rather than ceilings — an
eight-entry indexing profile, a full nine-row slice table, and a split profile — so a regression that
stayed under a documented ceiling would still fail. Counting relations are paired with positive
controls proving the counter is live before a zero-callback claim is asserted, and the
checkpoint-differential groups pin representative identity through a boxed cell, which is what keeps
the "a clean position reuses its exact checkpoint slot" invariant from degenerating into value
equality on immediate payloads.

Every group was mutation-tested during development: reversing a tag composition direction, dropping
an expose, flipping a minimum tie-break, replacing a range seek with a filter over all changes,
removing run merging or canonicalization, and substituting a fresh-but-equal cell on cancellation
were each confirmed to fail a named case.

Two environment limits apply to this workspace and are not claims about the code: `ocamlformat` and
`odoc` are absent from the switch here, so the `@fmt` and `@doc` gates cannot be exercised locally.
A green `dune build @doc` in that state is vacuous — the alias has nothing to run — and must not be
read as a passing documentation gate.
