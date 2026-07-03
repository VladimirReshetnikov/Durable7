# Rust HAMT Tests

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers navigating Rust HAMT test coverage
- Scope: Test location, command, and coverage map

Tests currently live inline in [`../src/lib.rs`](../src/lib.rs). Run them from `src/Rust`:

```powershell
cargo test -p tools-data-structures-hamt
```

Coverage groups:

- map persistence and version isolation;
- root-sharing no-op behavior;
- duplicate-key rejection;
- equal-hash collision buckets;
- last-wins replacement and original-key retention;
- set algebra.

