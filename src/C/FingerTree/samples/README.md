# C FingerTree Samples

- Created (UTC): 2026-07-02T21:01:27Z
- Repository HEAD: bfb22d419ea9ab57f1150439a3e8cffc73403110
- Audience: Maintainers and users exploring the C FingerTree sample executables
- Scope: Runnable C sample programs under `src/C/FingerTree/samples`

These samples are deterministic console programs that exercise the public C API without adding a
test-framework dependency. They are built when `FINGERTREE_C_BUILD_SAMPLES` is enabled, which is the
default in the workspace CMake presets, and both programs are registered as CTest smoke tests.

## Programs

- `fingertree_c_showcase` (`showcase.c`) demonstrates the priority queue, sorted set, signed-64-bit
  interval tree, and text-rope line/column navigation.
- `fingertree_c_snapshots` (`persistent_snapshots.c`) demonstrates retained text-rope snapshots,
  edit/restore behavior, and independent disposal of each persistent version.

## Build And Run

From `src/C/FingerTree`, build and smoke-test the samples through the ordinary Debug preset:

```powershell
$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat"
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

cmd.exe /d /c "call ""$vsDevCmd"" -arch=x64 -host_arch=x64 && ""$cmakeDir\cmake.exe"" --preset msvc-debug && ""$cmakeDir\cmake.exe"" --build --preset msvc-debug --parallel 1 && ""$cmakeDir\ctest.exe"" --preset msvc-debug --parallel 1 --output-on-failure -R ""fingertree_c\.sample"""
```

Run the built executables directly when changing transcript text or manual-demo behavior:

```powershell
.\out\build\msvc-debug\samples\fingertree_c_showcase.exe
.\out\build\msvc-debug\samples\fingertree_c_snapshots.exe
```

## Expected Transcript Markers

`fingertree_c_showcase` prints one line per facade:

```text
priority: 10@1 20@2 30@3 40@4
sorted-set: 1 2 3 5
interval-overlaps: 2
text: 11 chars, 3 lines, offset8=(1,2)
```

`fingertree_c_snapshots` prints the retained-version sizes and first-character check:

```text
original: 14 chars, 4 lines
snapshot: 14 chars, 4 lines
edited: 15 chars, 4 lines
restored: 14 chars, 4 lines
first-chars: o # o
```

Use the workspace [validation guide](../docs/validation.md) for full Debug/Release commands,
warning policy, generated-output locations, and test coverage.
