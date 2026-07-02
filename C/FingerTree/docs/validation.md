# C FingerTree Validation

- Status: Initial validation guide
- Created (UTC): 2026-07-02T18:12:21Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers validating the C port
- Scope: Local build and test commands for `C/FingerTree`

## Debug Build And Tests

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
& "$cmakeDir\cmake.exe" --preset msvc-debug
& "$cmakeDir\cmake.exe" --build --preset msvc-debug
& "$cmakeDir\ctest.exe" --preset msvc-debug
```

## Release Build And Tests

```powershell
$cmakeDir = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
& "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
& "$cmakeDir\cmake.exe" --preset msvc-release
& "$cmakeDir\cmake.exe" --build --preset msvc-release
& "$cmakeDir\ctest.exe" --preset msvc-release
```

## Current Coverage

The bootstrap test executable covers:

- endpoint, index, split, concat, and persistence behavior for the generic tree/deque surface;
- size-measure `locate` and measure-guided split behavior;
- reversible-deque logical reversal, endpoint edits, and persistence;
- sorted set uniqueness, sorted multiset duplicates, rank access, removal, and traversal;
- sorted map insert/set/remove, duplicate rejection, lookup, rank access, traversal, and persistence;
- chunked rope construction across chunk boundaries, cumulative indexing, traversal, split/reconcat, insertion,
  removal, append, and persistence;
- priority queue minimum-first drain order and FIFO stability for equal priorities;
- signed 64-bit interval insertion, ordering, containment, removal, first-overlap, and overlap counting;
- text rope construction, editing, indexing, line count, line/column navigation, and traversal.

Both `msvc-debug` and `msvc-release` are expected to build warning-free under `/W4 /WX`.
