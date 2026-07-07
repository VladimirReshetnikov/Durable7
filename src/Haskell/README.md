# Haskell Workspaces

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents working on Haskell ports of repository-owned data structures
- Scope: Haskell packages under `src/Haskell`

The Haskell root contains dependency-light ports of the repository-owned persistent data structures.
The packages use idiomatic immutable Haskell values while preserving the observable contracts of the
managed C# projects where the language surfaces line up.

| Workspace | Package | Public modules |
| --- | --- | --- |
| [Hamt](Hamt/README.md) | `tools-data-structures-hamt` | `Data.Structures.Hamt`, `Data.Structures.Hamt.HashMap`, `Data.Structures.Hamt.HashSet` |
| [FingerTree](FingerTree/README.md) | `tools-data-structures-fingertree` | `Data.Structures.FingerTree`, deque, measured tree, sorted collections, priority queue, intervals, ropes, and text helpers |
| [Tungsten](Tungsten/README.md) | `tools-data-structures-tungsten` | `Data.Structures.Tungsten`, `Data.Structures.Tungsten.List`, `Data.Structures.Tungsten.Association` |

Build and test both packages from this directory:

```powershell
cabal test all
```

The port intentionally depends only on packages bundled with the local GHC distribution and sibling
workspace packages, plus each package's own test executable.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and checkpoint obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
