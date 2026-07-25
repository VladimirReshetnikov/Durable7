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
| [Hamt](Hamt/README.md) | `durable7-hamt` | `Durable7.Hamt`; CHAMP map/set/bag/bimap/multimap/relation, strict map patches, directed graphs, indexed maps, transients, Patricia, and Merkle families |
| [FingerTree](FingerTree/README.md) | `durable7-fingertree` | `Durable7.FingerTree`, genuine measured-tree deque/core, sparse chunked bit set, ropes/cursors, interval and sorted collections, and priority cores |
| [Ordered](Ordered/README.md) | `durable7-ordered` | Neutral persistent ordered set, map, and grouped multimap over the public CHAMP and finger-tree substrates |

Build and test all four packages from this directory:

```powershell
.\test.ps1
```

Use `-Workspace Hamt`, `-Workspace FingerTree`, or `-Workspace Ordered` for a focused run. The wrapper
invokes Cabal after enabling inherited non-interactive Windows error handling, so assertion, runtime,
loader, and crash failures stay on the console and return a nonzero exit instead of opening modal UI.
Additional Cabal options can be passed with `-CabalArguments`; the wrapper appends `--jobs=1`, so
each invocation remains a single Cabal build job even if a caller profile requests more workers.

The port intentionally depends only on packages bundled with the local GHC distribution and sibling
workspace packages, plus each package's own test executable.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and checkpoint obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
