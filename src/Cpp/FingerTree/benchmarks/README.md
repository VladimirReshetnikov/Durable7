# C++ FingerTree Benchmarks

- Status: Active dependency-free benchmark harness
- Created (UTC): 2026-07-10T19:40:22Z
- Repository HEAD: 82a19b89405110255d76b848e6dff8a8f8d73bee
- Updated (UTC): 2026-07-11T21:45:54Z
- Updated against repository HEAD: ee5f888b47fc8d4317fb0209546cb5c9f808039d
- Audience: Maintainers validating persistent-complexity and constant-factor behavior
- Scope: Native benchmark cases and execution policy under `src/Cpp/FingerTree/benchmarks`

`fingertree_benchmarks` is a small repository-owned harness: it needs only the C++ standard library and the
header-first FingerTree target. It prints CSV-shaped observations with an anti-elision checksum. Run Release
builds for performance evidence; Debug runs are useful only as functional smoke checks.

The cases cover every Milestone 8 family plus the RRB-vector benchmark gate:

- `persistence_branching`: branch an endpoint update repeatedly from one retained, fully forced version at
  sizes 100, 10,000, and 1,000,000; report allocations and bytes per update and fail if allocation cost is not
  size-flat;
- `deque_endpoint` (persistent endpoint updates), `deque_endpoint_read`, `deque_indexed_read`, and
  `deque_catenation`;
- `rrb_indexed_read` and `rrb_catenation`, paired at identical sizes and iteration tiers with
  `rope_indexed_read` and `rope_catenation`; these are the catalog's benchmark gate for the new 32-way RRB core;
- `daba_slide_and_query`, paired with `deque_slide_and_reaggregate` at 63, 64, 65, 1,000, and 100,000 values,
  plus `daba_validate_structure`. The first pair compares identical mutable slide/query workloads while exposing
  the algorithmic difference between DABA's bounded update and a naive full-window fold; the validator is kept
  separate because its O(n + c) diagnostic walk is not a hot-path operation;
- `reversible_reverse`, plus `reversible_endpoint`, `reversible_endpoint_read`, and `reversible_catenation` for
  direct comparison with the ordinary-deque cases at the same sizes and iteration tiers;
- `weighted_selection` and `sorted_search`;
- `rope_edit`, `rope_split`, `rope_slice`, measured `rope_navigation`, and a linear navigation baseline;
- `priority_meld` and `interval_overlap_query`.

Most cases measure immutable updates that retain their source version. A mutable standard-library baseline can
update storage in place and therefore has a different contract; the harness says so rather than presenting unlike
operations as interchangeable. DABA Lite and its `std::deque` baseline are the explicit exception: both are
mutable streaming windows, and the paired operations evict, insert, and then query the same fixed-size window.

```powershell
cmake --build --preset msvc-release --target fingertree_benchmarks
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --short
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --filter=persistence_branching
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --filter=daba
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --list
```

`--short` reduces repetitions but preserves the branching-flatness size ladder. `--filter` accepts a case-name
substring and fails if it matches no case, preventing a mistyped CI filter from silently measuring nothing.
Allocation columns are `n/a` when `FINGERTREE_ENABLE_ALLOCATION_TRACKING=OFF`, as required for sanitizer builds.
