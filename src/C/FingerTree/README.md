# C FingerTree

- Status: Active C workspace
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
concatenation, split, locate, indexed replacement, and endpoint operations. The related C-facing surfaces currently
included are:

- `ft_persistent_deque`, an alias over the size-measured tree;
- `ft_reversible_deque`, an orientation-aware persistent deque with O(1) `reverse`, concat, split, and indexed
  edits over shared snapshots;
- `ft_sorted_set`, `ft_sorted_multiset`, and `ft_sorted_map`, persistent sorted wrappers over the deque/tree
  surface;
- `ft_priority_queue`, a generic persistent minimum-priority queue with FIFO tie-breaking for equal priorities;
- `ft_interval_tree`, a generic closed-interval tree facade over caller-supplied endpoint policies;
- `ft_interval_tree_i64`, a convenience closed-interval facade for signed 64-bit endpoints;
- `ft_rope`, a generic persistent chunked positional sequence backed by measured chunk leaves;
- `ft_measured_rope`, a generic persistent chunked sequence with cached user measures and cumulative-measure
  navigation;
- `ft_text_rope`, a character-rope facade backed by `ft_measured_rope` with a cached newline measure, insertion,
  removal, indexing, O(1) line count, O(log n) line navigation, and validated line/column-to-offset conversion.

The central C++ lazy-middle publication machinery is now present in the C core: endpoint overflow and boundary
pop repairs share memoized middle cells across persistent versions, and independently held immutable handles may
be used concurrently under normal handle-lifetime rules.

## Build

From this directory:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug && ""$cmakeDir\ctest.exe"" --preset msvc-debug --output-on-failure"
```

Use `msvc-release` for the optimized configuration. The `msvc-*` presets use Visual Studio's bundled Ninja by
absolute path, so CMake and Ninja do not need to be on `PATH` for that route. Keep the Visual Studio environment
setup, configure, build, and CTest run in one `cmd.exe` chain when starting from plain PowerShell; invoking
`VsDevCmd.bat` directly from PowerShell does not persist its environment changes in that process. Host-agnostic
`ninja-debug`, `ninja-release`, and `ninja-asan` presets are also available when CMake, Ninja, and a suitable
compiler are on `PATH`. For release commands, sanitizer validation, benchmark entry points, warning policy, and
generated-output locations, see the [validation guide](docs/validation.md).

## Layout

- `include/tools/data_structures/finger_tree/fingertree.h` contains the public C API.
- `src/fingertree.c` contains the measured-tree implementation and the current wrappers.
- `tests/` contains the [bootstrap CTest executable](tests/README.md).
- `samples/` contains deterministic C sample executables that are also registered as CTest smoke tests; see
  [`samples/README.md`](samples/README.md).
- `benchmarks/` contains a dependency-light timing harness for quick local comparisons; see
  [`benchmarks/README.md`](benchmarks/README.md).
- `docs/` contains C-specific API, usage, and validation notes.
