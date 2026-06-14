# DataStructures

- Status: Informational
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers working on repository-owned data-structure projects
- Scope: Index for `src/DataStructures`

`src/DataStructures` contains repository-owned data-structure workspaces and design references.

## Workspaces

- [FingerTree](FingerTree/README.md) is a .NET 10 persistent finger-tree library: two engine cores (a tuned catenable deque and a general monoid-measured tree), a full collection family (sorted bag/set/dictionary, priority queue, interval tree, reversible deque), product/sum/built-in measures with a closure-free predicate API, and a rope family (positional, measured, and text). It ships a navigable design-notes document ([FingerTree-Design-Notes.pdf](FingerTree/docs/FingerTree-Design-Notes.pdf), with `.tex` source and a rebuild script alongside), a BenchmarkDotNet harness, two runnable samples, and a three-tier (example + property + model-based command) test suite plus tearable-struct concurrency stress tests.
