# C FingerTree

- Status: Initial C workspace
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers implementing and reviewing the C port
- Scope: Build entry points, layout, validation, and current port boundaries for `src/C/FingerTree`

This workspace contains the C port of the native FingerTree work. It starts from the completed
[`src/Cpp/FingerTree`](../../Cpp/FingerTree/README.md) port and exposes a C11 API centered on a generic measured
finger-tree core.

The C workspace is intentionally dependency-light: the library is ordinary C, builds as a static library, and uses
a small local C test executable registered with CTest. The core preserves immutable structural sharing through
atomic reference-counted tree reps, shared lazy middle cells, lazy deep-measure publication, digits, 2/3 nodes,
concatenation, split, locate, and endpoint operations. The related C-facing surfaces currently included are:

- `ft_persistent_deque`, an alias over the size-measured tree;
- `ft_reversible_deque`, a logical-orientation facade with O(1) `reverse` over shared snapshots;
- `ft_sorted_set`, `ft_sorted_multiset`, and `ft_sorted_map`, persistent sorted wrappers over the deque/tree
  surface;
- `ft_priority_queue`, a generic persistent minimum-priority queue with FIFO tie-breaking for equal priorities;
- `ft_interval_tree`, a generic closed-interval tree facade over caller-supplied endpoint policies;
- `ft_interval_tree_i64`, a convenience closed-interval facade for signed 64-bit endpoints;
- `ft_rope`, a generic persistent chunked positional sequence backed by measured chunk leaves;
- `ft_measured_rope`, a generic persistent chunked sequence with cached user measures and cumulative-measure
  navigation;
- `ft_text_rope`, a character-rope facade backed by `ft_rope`, with insertion, removal, indexing, line count, and
  line/column navigation.

The central C++ lazy-middle publication machinery is now present in the C core: endpoint overflow and boundary
pop repairs share memoized middle cells across persistent versions, and independently held immutable handles may
be used concurrently under normal handle-lifetime rules.

## Build

From this directory:

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
& "$cmakeDir\cmake.exe" --preset msvc-debug
& "$cmakeDir\cmake.exe" --build --preset msvc-debug
& "$cmakeDir\ctest.exe" --preset msvc-debug
```

Use `msvc-release` for the optimized configuration. The presets use Visual Studio's bundled Ninja by absolute
path, so CMake and Ninja do not need to be on `PATH`.

## Layout

- `include/tools/data_structures/finger_tree/fingertree.h` contains the public C API.
- `src/fingertree.c` contains the measured-tree implementation and the current wrappers.
- `tests/` contains the bootstrap CTest executable.
- `samples/` contains deterministic C sample executables that are also registered as CTest smoke tests.
- `benchmarks/` contains a dependency-light timing harness for quick local comparisons.
- `docs/` contains C-specific API and validation notes.
