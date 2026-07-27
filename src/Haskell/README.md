# Haskell Workspaces

- Created (UTC): 2026-07-03T04:37:54Z
- Updated (UTC): 2026-07-27T17:20:00Z
- Repository HEAD: dc8c8f3fa715a02db94a5a9ef3347baeac0b70a0
- Audience: Maintainers and AI agents working on Haskell ports of repository-owned data structures
- Scope: Haskell packages under `src/Haskell`

The Haskell root contains dependency-light ports of the repository-owned persistent data structures.
The packages use idiomatic immutable Haskell values while preserving the observable contracts of the
managed C# projects where the language surfaces line up. Haskell is the port where persistence is
least remarkable — every value is already immutable — so what these packages actually contribute is
the *sharing* discipline: an update copies a path, not a structure, and the cached measures that
make a finger tree logarithmic are forced rather than left as thunks that would defer the cost to an
unpredictable later read.

| Workspace | Package | Public modules |
| --- | --- | --- |
| [Hamt](Hamt/README.md) | `durable7-hamt` | `Durable7.Hamt`; CHAMP map/set/bag/bimap/multimap/relation, strict map patches, directed graphs, indexed maps, transients, Patricia, and Merkle families |
| [FingerTree](FingerTree/README.md) | `durable7-fingertree` | `Durable7.FingerTree`, genuine measured-tree deque/core, sparse chunked bit set, ropes/cursors, interval and sorted collections, and priority cores |
| [Ordered](Ordered/README.md) | `durable7-ordered` | Neutral persistent ordered set, map, and grouped multimap over the public CHAMP and finger-tree substrates |

`durable7-hamt` and `durable7-fingertree` are independent of each other; `durable7-ordered` depends
on both. `cabal.project` lists exactly those three packages and applies `-Wall -Wcompat` to all of
them.

## Dependencies

The port is deliberately close to what a bare GHC installation provides, so that building it does
not turn into a dependency-resolution exercise:

| Package | Beyond `base` |
| --- | --- |
| `durable7-hamt` | `array`, `bytestring`, `containers`, `text` — all shipped with GHC |
| `durable7-fingertree` | the same four, plus `crypton` and `memory` from Hackage |
| `durable7-ordered` | the two sibling packages, nothing else |

`crypton` and `memory` are the one genuine external requirement, and only
`Durable7.FingerTree.CanonicalSortedSet` uses them: the zip-zip set's rank policy needs
HMAC-SHA-256 and a cryptographic random source. The HAMT package needs SHA-256 too, for the Merkle
search tree's `mst-sha256-b16-v2` policy, but carries its own small implementation in
`Durable7.Hamt.MerkleEncoding` rather than take the dependency — the Merkle wire format has to be
byte-identical across nine languages, and an independent implementation is a check on that rather
than a cost.

Each package's test executable depends only on that package and its siblings.

## Build and test

From `src/Haskell`:

```powershell
.\test.ps1
```

Use `-Workspace Hamt`, `-Workspace FingerTree`, or `-Workspace Ordered` for a focused run. The
wrapper invokes Cabal after dot-sourcing
[`eng/Enable-HeadlessTestMode.ps1`](../../eng/Enable-HeadlessTestMode.ps1), so assertion, runtime,
loader, and crash failures stay on the console and return a nonzero exit instead of opening modal
UI. Additional Cabal options can be passed with `-CabalArguments`; the wrapper appends `--jobs=1`,
so each invocation remains a single Cabal build job even if a caller profile requests more workers.

The suites are plain `exitcode-stdio-1.0` executables rather than a test framework, which is why the
test stanzas add no dependency beyond the packages under test: a failure is an ordinary nonzero exit
with its diagnostic on stdout, reproducible by running the built binary directly. Each package's
`test/README.md` maps its cases.

Use the repository [semantic contracts reference](../../docs/reference/semantic-contracts.md) when
checking which persistence, ordering, policy, and checkpoint obligations should align with sibling
ports, and use the [porting guide](../../docs/guides/porting-and-semantic-parity.md) before changing
shared behavior.
