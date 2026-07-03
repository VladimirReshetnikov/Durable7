# Rust HAMT Validation

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers validating the Rust HAMT workspace
- Scope: Cargo commands, warning policy, and test coverage

Run from `src/Rust`:

```powershell
cargo test -p tools-data-structures-hamt
```

If Cargo is available under rustup but not on `PATH`, use:

```powershell
& $env:USERPROFILE\.cargo\bin\cargo.exe test -p tools-data-structures-hamt
```

The crate uses `#![forbid(unsafe_code)]`. The unit tests are inline in `Hamt/src/lib.rs` and cover:

- persistent snapshot preservation;
- no-op root sharing for equal-value replacement and absent removal;
- duplicate rejection through `try_add` and `add`;
- same-hash collision insertion, lookup, and removal;
- streaming iterator exact-size accounting over collision buckets;
- last-wins bulk map construction while retaining the original stored key;
- persistent set algebra and proper subset/superset relations.
