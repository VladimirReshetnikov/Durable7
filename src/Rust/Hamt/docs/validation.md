# Rust HAMT Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust HAMT workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```

The wrapper locates Cargo on `PATH` or under the default rustup profile and applies inherited,
non-interactive Windows error handling before Cargo starts the test executable.

The crate uses `#![forbid(unsafe_code)]`. The unit tests are inline in `Hamt/src/lib.rs` and cover:

- persistent snapshot preservation;
- no-op root sharing for equal-value replacement and absent removal;
- duplicate rejection through `try_add` and `add`;
- same-hash collision insertion, lookup, and removal;
- streaming iterator exact-size accounting over collision buckets;
- last-wins bulk map construction while retaining the original stored key;
- persistent set algebra and proper subset/superset relations;
- transient bulk-builder snapshot detachment, first-key/last-value duplicate identity,
  final-hash-level splitting, and collision-heavy/branch-heavy differential agreement with
  incremental construction.
