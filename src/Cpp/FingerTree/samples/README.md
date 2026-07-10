# C++ FingerTree Samples

- Status: Active runnable samples
- Created (UTC): 2026-07-10T19:40:22Z
- Repository HEAD: 82a19b89405110255d76b848e6dff8a8f8d73bee
- Audience: C++ consumers and maintainers exploring the persistent collection family
- Scope: Deterministic sample entry points and smoke validation under `src/Cpp/FingerTree/samples`

The workspace builds two narrated, deterministic samples:

- `fingertree_showcase` demonstrates priority-queue meld/drain order, cumulative-weight selection,
  order-statistic sorted collections, interval overlap queries, and the O(1) reversible-deque view.
- `fingertree_persistent_snapshots` is the text-buffer tour: retained measured-rope versions support undo/redo,
  newline measures provide logarithmic line/column navigation, and an atomic `shared_ptr` publishes complete
  immutable snapshots from one writer to a concurrent reader.

The publication act is data-race-safe and needs no application mutex, but makes no lock-free progress claim.
`std::atomic<std::shared_ptr<T>>` may serialize internally on MSVC or libstdc++; measure the deployment standard
library before making throughput claims.

The substantive bodies are `showcase::run(std::ostream&)` and
`persistent_snapshots::run(std::ostream&)`; each executable's `main()` only forwards `std::cout`. The
`fingertree.samples` CTest captures each run twice, proves byte-for-byte determinism, and checks the main story
markers. Native test processes also opt into the repository's headless Windows error mode, so failures are
reported to the terminal instead of modal dialogs.

Build and run from the workspace root after configuring a preset:

```powershell
cmake --build --preset msvc-release --target fingertree_showcase fingertree_persistent_snapshots
.\out\build\msvc-release\samples\fingertree_showcase.exe
.\out\build\msvc-release\samples\fingertree_persistent_snapshots.exe
ctest --preset msvc-release -R '^fingertree\.samples$' --output-on-failure
```
