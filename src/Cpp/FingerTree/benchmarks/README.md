# C++ FingerTree Benchmarks

- Status: Active dependency-free benchmark harness
- Created (UTC): 2026-07-10T19:40:22Z
- Repository HEAD: 82a19b89405110255d76b848e6dff8a8f8d73bee
- Audience: Maintainers validating persistent-complexity and constant-factor behavior
- Scope: Native benchmark cases and execution policy under `src/Cpp/FingerTree/benchmarks`

`fingertree_benchmarks` is a small repository-owned harness: it needs only the C++ standard library and the
header-first FingerTree target. It prints CSV-shaped observations with an anti-elision checksum. Run Release
builds for performance evidence; Debug runs are useful only as functional smoke checks.

The cases cover every Milestone 8 family:

- `persistence_branching`: branch an endpoint update repeatedly from one retained, fully forced version at
  sizes 100, 10,000, and 1,000,000; report allocations and bytes per update and fail if allocation cost is not
  size-flat;
- `deque_endpoint` (persistent endpoint updates), `deque_endpoint_read`, `deque_indexed_read`, and
  `deque_catenation`;
- `reversible_reverse`, plus `reversible_endpoint`, `reversible_endpoint_read`, and `reversible_catenation` for
  direct comparison with the ordinary-deque cases at the same sizes and iteration tiers;
- `weighted_selection` and `sorted_search`;
- `rope_edit`, `rope_split`, `rope_slice`, measured `rope_navigation`, and a linear navigation baseline;
- `priority_meld` and `interval_overlap_query`.

The harness measures immutable updates that retain their source version. A mutable standard-library baseline can
update storage in place and therefore has a different contract; the harness says so in every run rather than
presenting unlike operations as interchangeable.

```powershell
cmake --build --preset msvc-release --target fingertree_benchmarks
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --short
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --filter=persistence_branching
.\out\build\msvc-release\benchmarks\fingertree_benchmarks.exe --list
```

`--short` reduces repetitions but preserves the branching-flatness size ladder. `--filter` accepts a case-name
substring and fails if it matches no case, preventing a mistyped CI filter from silently measuring nothing.
Allocation columns are `n/a` when `FINGERTREE_ENABLE_ALLOCATION_TRACKING=OFF`, as required for sanitizer builds.
