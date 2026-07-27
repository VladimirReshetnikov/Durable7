# Durable7

**Persistent data structures and authenticated collections — implemented nine times, with the same
semantics in every language.**

Durable7 is a library of immutable, structurally shared collections ported to
**C, C++, C#, Haskell, Kotlin, OCaml, Python, Rust, and TypeScript**. Every port implements the same
contracts: the same ordering rules, the same failure behavior, the same complexity guarantees — and,
for the authenticated tier, the same bytes on the wire.

[![License: MIT-0](https://img.shields.io/badge/License-MIT--0-blue.svg)](LICENSE)
![Languages](https://img.shields.io/badge/languages-9-informational)
![Status](https://img.shields.io/badge/status-pre--release%200.1.0-orange)

---

## What "persistent" means here

Every operation returns a **new version** and leaves the old one untouched and still valid. Versions
share structure, so an update copies a path rather than the whole collection:

```csharp
var v1 = PersistentHashMap<int, string>.Empty.SetItem(1, "one");
var v2 = v1.SetItem(2, "two");
var v3 = v2.SetItem(1, "uno");

// v1, v2, and v3 are three independent, live snapshots.
// They share almost all of their internal nodes.
```

That makes undo/redo, branching history, snapshot isolation, and lock-free reader concurrency fall
out of the data structure itself rather than out of a locking discipline. Mutation is confined to a
few explicitly named places — builders, one-way transient sessions, the concurrent-trie facade, and
`DabaLite` — and those are documented as mutable wherever they appear.

## Why nine implementations

Most collection libraries exist once, and a "port" is a loose family resemblance. Here, semantic
parity is the product:

- **One contract, nine languages.** Ordering, first-representative retention, no-op identity,
  failure atomicity, and complexity bounds are specified once and upheld by every port. Where a port
  genuinely differs, that difference is written down in its API notes instead of being discovered
  later.
- **Byte-identical authenticated data.** The Merkle search tree uses policy identity
  `mst-sha256-b16-v2` with SHA-256 and a canonical `MST2` block format. A tree built in Rust
  verifies against a proof produced in TypeScript. `MSP2` membership, absence, and range proofs are
  exact across ports.
- **Language-native, not transliterated.** Rust uses `Arc` and forbids `unsafe`; C# exposes
  `IReadOnlyList<T>`; OCaml returns `option`/`result`; C is type-erased with explicit ownership and
  fallible allocators. The semantics match; the idioms do not have to.

## What's inside

| Family | Highlights |
| --- | --- |
| **Hash maps & sets** | CHAMP maps/sets, hash bags, strict bimaps, set-valued multimaps, bidirectional relations, map patches, directed graphs, multi-index maps |
| **Integer & authenticated** | 32/64-bit Patricia maps and sets, lock-free snapshotting Ctrie, Merkle search trees with bounded verification, proofs, synchronization, and three-way merge |
| **Sequences** | Catenable deques, monoid-measured trees, RRB vectors, reversible deques, and a lazily range-updating implicit-AVL sequence |
| **Ordered & sorted** | Sorted bags/sets/maps, canonical zip-zip sets, insertion-ordered set/map/multimap with explicit movement and stable sorting |
| **Priority & interval** | Brodal–Okasaki heaps, measured priority queues, winner-cached priority-search queues, interval trees, and payload-bearing interval maps |
| **Text & bits** | Positional and measured ropes, UTF-aware text ropes, sparse rank/select chunked bit sets |
| **Cursors** | Immutable, version-bound gap and search cursors over the maps, sets, sequences, and ropes that have a stable neighbor axis |

The [data-structure catalog](docs/reference/data-structure-catalog.md) lists every public entry point
per language. For the long version — what each structure actually is, how it is represented, and when
to reach for it — read the field guide below.

## 📖 The field guide

**[Persistent Data Structures: The Durable7 Field Guide](docs/book/durable7-data-structures.pdf)**
(PDF, 133 pages) is a single long-form tour of every collection in this repository.

It covers CHAMP and the nine families composed over it, Patricia integer tries, the concurrent trie,
finger trees and the measure framework that turns one tree into a dozen collections, RRB vectors,
ropes and text, the range-update sequence, zip-zip canonical sets, the three priority structures,
interval trees, Merkle search trees with their proofs and synchronisation, and cursors — plus a
complexity table for the whole library, a nine-language name index, and the design decisions the
project deliberately declined.

It is written to be read, not merely consulted, and it is candid about where each guarantee stops.
[LaTeX source](docs/book/durable7-data-structures.tex) and
[build instructions](docs/book/README.md) are committed alongside it.

## A quick look

```python
from durable7 import PersistentHashMap, PersistentHashBag, PersistentBiMap, TextRope

snapshot = PersistentHashMap.empty().put("answer", 42)
bag      = PersistentHashBag.from_values(["alpha", "alpha", "beta"])
bimap    = PersistentBiMap.empty().add("answer", 42)
text     = TextRope.from_text("alpha\nbeta")

bimap.inverse[42]      # "answer"
bag.count_of("alpha")  # 2
```

```ts
import { PersistentBiMap, PersistentHashMap, Rope } from "durable7";

const map    = PersistentHashMap.empty<string, number>().put("answer", 42);
const bimap  = PersistentBiMap.empty<string, number>().add("answer", 42);
const cached = map.getOrAdd("second answer", () => 43);
const edited = Rope.fromText("abc").getCursor(1).insert("X").snapshot();
```

## Getting started

> **Note** — Durable7 is not yet published to npm, PyPI, crates.io, NuGet, Hackage, or opam. Build
> from source for now. Each language workspace is self-contained: you only need the toolchain for the
> port you care about.

```bash
git clone https://github.com/VladimirReshetnikov/Durable7.git
```

Then build and test the port you want:

| Language | Workspace | Build and test |
| --- | --- | --- |
| C# | [`src/CSharp`](src/CSharp/README.md) | `dotnet build` then `.\test.ps1` |
| C | [`src/C`](src/C/README.md) | `.\build.ps1 -Workspace Hamt -RunTests` |
| C++ | [`src/Cpp`](src/Cpp/README.md) | `.\build.ps1 -Workspace Hamt -RunTests` |
| Rust | [`src/Rust`](src/Rust/README.md) | `.\test.ps1` (or `cargo test`) |
| TypeScript | [`src/TypeScript`](src/TypeScript/README.md) | `npm ci && npm run validate` |
| Python | [`src/Python`](src/Python/README.md) | `.\test.ps1` |
| Haskell | [`src/Haskell`](src/Haskell/README.md) | `.\test.ps1` (or `cabal test all`) |
| Kotlin | [`src/Kotlin`](src/Kotlin/README.md) | `.\build.ps1` (bootstraps its own JDK) |
| OCaml | [`src/OCaml`](src/OCaml/README.md) | `dune build @check && dune runtest` |

The C and C++ wrappers also accept `-Workspace FingerTree` and `-Workspace Ordered`. Toolchain
versions are listed under [Prerequisites](docs/guides/build-and-validation.md#prerequisites); the
full matrix, including the single-worker validation policy, is in the
[build and validation guide](docs/guides/build-and-validation.md).

## Project status

**Pre-release (0.1.0).** The collection families are implemented and tested across all nine ports,
but the API is not frozen and nothing is published to a package registry yet.

Every port ships an executable test suite, and the gates run single-worker for determinism:

| Port | Suite |
| --- | --- |
| C# | 1,158 tests, zero-warning Debug and Release builds |
| TypeScript | 252 tests (Vitest + fast-check) |
| Python | 234 tests (pytest + Hypothesis), plus Ruff, strict Mypy, and installed-wheel gates |
| Kotlin | 194 tests |
| OCaml | 48 Alcotest/QCheck cases under strict warnings, ocamlformat, and odoc |
| C / C++ | CTest suites per workspace, MSVC Debug and Release |
| Haskell | Per-package executable suites |
| Rust | Crate unit and integration tests, `#![forbid(unsafe_code)]` |

Benchmarks exist for the C# workspace but are deliberately excluded from routine validation, and no
performance numbers are published as shipment evidence.

## Documentation

| If you want to… | Read |
| --- | --- |
| Understand every structure in depth, end to end | [The field guide](docs/book/durable7-data-structures.pdf) (PDF) |
| Compare families and find entry points | [Data-structure catalog](docs/reference/data-structure-catalog.md) |
| Understand the shared behavior contracts | [Semantic contracts](docs/reference/semantic-contracts.md) |
| Find the doc that owns a topic | [Navigation matrix](docs/reference/navigation-matrix.md) |
| Get oriented before contributing | [Repository onboarding](docs/guides/repository-onboarding.md) |
| Build or validate a change | [Build and validation](docs/guides/build-and-validation.md) |
| Keep ports semantically aligned | [Porting and semantic parity](docs/guides/porting-and-semantic-parity.md) |
| See the repository layout | [Workspace map](docs/reference/workspace-map.md), [source index](src/README.md) |

## Repository layout

```text
docs/   Repository-wide guides, reference material, proposals, and review reports
eng/    Shared build and test tooling
src/    One directory per language, each a self-contained workspace
```

## Contributing

Start with the [onboarding guide](docs/guides/repository-onboarding.md). The short version: a change
to a public contract belongs in every port that exposes it, documentation is part of the change
rather than a follow-up, and each affected workspace's gate should pass before the work is called
done.

## License

[MIT No Attribution (MIT-0)](LICENSE) — use it, fork it, ship it; no attribution required.
