# TypeScript data structures and numerics

- Created (UTC): 2026-07-15T00:12:55Z
- Repository HEAD: 6bf20605073b1750d871d4bd53ef75fcfe25484c
- Runtime: Node.js 24 or newer
- Package: `@vladimir-reshetnikov/data-structures`

This workspace is the strict TypeScript/ESM port of the repository-owned persistent collections,
streaming structures, Tungsten collections, and fixed-width numerics. It targets modern JavaScript
runtimes without native addons and publishes declaration files alongside ES modules.

## Public families

| Import | Main types |
| --- | --- |
| `@vladimir-reshetnikov/data-structures/hamt` | `PersistentHashMap` with one-descent factory updates, `PersistentHashSet`, `PersistentHashBag`, reusable `HashMapBulkBuilder`, map/set single-owner transients, `ConcurrentHashTrie`, 32/64-bit Patricia maps and sets, the exact-wire `MerkleSearchTree`, codecs, stores, packs, finite verification budgets, `MSP2` proofs, synchronization, and typed merge |
| `@vladimir-reshetnikov/data-structures/finger-tree` | `PersistentDeque`, general measured `FingerTree`, `ReversibleDeque`, `RrbVector`, sorted bag/set/map, canonical zip-zip set, measured and Brodal–Okasaki priority queues, priority-search queue, interval tree, rope/measured-rope/text cursors, and `DabaLite` |
| `@vladimir-reshetnikov/data-structures/ordered` | independent insertion-ordered `PersistentOrderedSet` with positional movement/ranges, stable one-shot sorting, receiver-policy algebra, and first-representative retention |
| `@vladimir-reshetnikov/data-structures/tungsten` | `PersistentList` and insertion-ordered `PersistentAssociation` |
| `@vladimir-reshetnikov/data-structures/numerics` | signed and unsigned 256/512/1024-bit integers, `SparseInteger`, and `BitConverterEx` |

The root import re-exports all five families. See [API and semantic notes](docs/api-notes.md) and the
[ordered-set notes](docs/ordered.md) for language-specific mappings and deliberately different
performance claims.

## Build and test

```powershell
cd src/TypeScript
npm ci
npm run validate
```

`validate` performs strict type checking, runs the Vitest and fast-check model/property suites,
cleans generated output, and builds ESM plus declaration/source maps under `dist`. The test inventory
and focused commands are in [test/README.md](test/README.md); validation expectations are in
[docs/validation.md](docs/validation.md).

## Package use

```ts
import { PersistentHashMap, Rope, UInt256 } from "@vladimir-reshetnikov/data-structures";

const map = PersistentHashMap.empty<string, number>().put("answer", 42);
const cached = map.getOrAdd("second answer", () => 43);
const edited = Rope.fromText("abc").getCursor(1).insert("X").snapshot();
const wrapped = UInt256.maxValue.add(new UInt256(1));
```

All persistent values are immutable. Mutating APIs are confined to explicitly mutable builders,
`DabaLite`, the isolate-local concurrent-trie facade, and one-way transient sessions.
