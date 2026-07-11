# C FingerTree Benchmarks

- Created (UTC): 2026-07-02T21:01:27Z
- Repository HEAD: bfb22d419ea9ab57f1150439a3e8cffc73403110
- Audience: Maintainers running quick local timing checks for the C FingerTree port
- Scope: Dependency-light benchmark harness under `src/C/FingerTree/benchmarks`

`fingertree_c_benchmarks` is a small C timing harness for local sanity checks. It is not a
statistically rigorous benchmark suite: it uses `clock()`, prints CSV-style rows, and is intended to
confirm broad shape and catch obvious local regressions before deeper investigation.

The harness is built when `FINGERTREE_C_BUILD_BENCHMARKS` is enabled, which is the default in the
workspace CMake presets.

## Workloads

The executable currently reports:

- `daba_lite_slide_aggregate`: build a sum window, replace half of it through FIFO slides, and query
  the final aggregate;
- `deque_push_index`: repeated `ft_persistent_deque_push_back` followed by sparse indexed probes;
- `rope_build_split_concat`: `ft_rope_from_array`, midpoint split, and concat;
- `rrb_build_index_split_concat`: packed RRB construction, sparse uniform indexing, midpoint split,
  and boundary-spine concat;
- `rrb_builder_freeze`: append-builder staging and immutable snapshot publication;
- `sorted_set_insert`: descending inserts into `ft_sorted_set`.

The first command-line argument is the size parameter. If it is omitted or parsed as zero, the harness
uses `10000`.

## Build And Run

From `src/C/FingerTree`, build the Release preset and run the harness:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-release && ""$cmakeDir\cmake.exe"" --build --preset msvc-release"
.\out\build\msvc-release\benchmarks\fingertree_c_benchmarks.exe 10000
```

Expected output shape:

```text
benchmark,count,elapsed_ms,check
daba_lite_slide_aggregate,10000,...
deque_push_index,10000,...
rope_build_split_concat,10000,...
rrb_build_index_split_concat,10000,...
rrb_builder_freeze,10000,...
sorted_set_insert,5000,...
```

Run the executable in Release configuration for meaningful local timing. Keep curated or comparative
performance claims in higher-level benchmark documentation; this harness is deliberately lightweight.

Use the workspace [validation guide](../docs/validation.md) for full Debug/Release commands,
warning policy, generated-output locations, and test coverage.
