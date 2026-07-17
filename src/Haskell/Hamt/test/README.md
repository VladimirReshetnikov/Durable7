# Haskell HAMT Tests

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents validating the Haskell HAMT port
- Scope: `tools-data-structures-hamt` test executable

Run from `src/Haskell`:

```powershell
.\test.ps1 -Workspace Hamt
```

The self-contained executable covers map last-wins construction, duplicate rejection, explicit
collision buckets, custom hash/equality policy behavior, original-key recovery, set algebra, and
`forkIO` concurrent readers over shared immutable snapshots. It also locks in collision-to-leaf
canonicalization, receiver-policy set relations, one-pass adjustment behavior, strict value mapping,
independent-history CHAMP equality, typed diff classification, partially shared same-policy
lockstep equality/diff callback ceilings, semantic fallback across compatible policies with
different hash functions, and 100,000-entry bulk construction.
The persistent factory tier checks exact one-hash and selected-callback counts for hits and misses,
leaf, collision, inline-bitmap, and child-bitmap routes; caller-key delivery; stored key/value
representative retention; equal-value root identity; a non-strict bottom-valued stored hit; present
`Nothing`; and `Either`-reported add or update failures with no successor. Injected hash,
key-equality, pure-factory, and value-equality
exceptions likewise leave every source version intact. These are operation-count tests rather than
elapsed-time measurements.
The derived-collection scenario covers multimap key/pair counts, nonempty groups, pair removal,
retained snapshots and validation, plus relation forward/reverse lookup, inversion, whole-left
removal, and exact inverse validation.
It additionally covers strict patch apply/invert/compose and conflict isolation, graph endpoint and
reverse-adjacency maintenance, indexed-map group movement, retained snapshots, and composite
invariants.
The hash-bag tier covers first-representative construction, explicit `Int` distinct and `Int64`
expanded counts, positive checked `Int32` multiplicities, negative/zero pre-hash behavior,
one-descent addition, saturated removal, nullable-style `Maybe` representatives, expanded/distinct/
entry traversal, and internal CHAMP/count invariants. Its algebra tables pin maximum union, minimum
intersection, saturated difference, checked sum, self/empty root identities, eager receiver-policy
normalization, checked collapsed classes, receiver representative precedence, and first argument-
order representatives for new classes. A 1,000-operation all-collision model compares every class,
total, expanded sequence, and invariant at deterministic checkpoints.
The strict-bimap tier covers key-first two-domain conflicts, independent key/value policies,
stored representatives, non-displacing replacement, symmetric lookup/removal, a removed
`Nothing` versus a miss, two-root clear/inversion identity, structural validation, injected hash
failure atomicity, `forkIO` readers, retained versions, and a 2,000-step two-map model.
The CHAMP diagnostics additionally validate full hash-prefix routing through terminal shift-30
fragments and compare collision key sets independently of insertion order under the map's policy.
The CHAMP algebra tier additionally checks right-valued map union with left key representatives,
all four structural map/set operations, cached-cardinality validity, zero-rehash shared ancestry,
receiver-policy normalization, and collision-heavy deterministic histories.
The transient tier covers clean-source root identity after duplicate/equal/missing no-ops,
case-insensitive stored-key and stored-item representatives, edited snapshot isolation, direct map/set
clear semantics, all six set relations under the receiver policy, duplicate errors, consumed map/set
sessions, a 192-key deterministic model history, canonical publication, and a throwing hash callback
that leaves the session active and the source root unchanged.
The Patricia tier covers explicit 32/64-bit signed extrema, ascending enumeration, a 10,000-step
map history, right-biased map algebra, left/right/key-aware combining algebra, cached-subtree
cardinality validation, and set union/intersection/difference.

The Merkle core/wire tier pins the standard SHA-256 vector and the shared C#/Rust policy domain,
empty digest, root digest, and complete `MST2` block bytes. It also covers strict integer/UTF-8/
nullable codecs, malformed decoding, stable first-key/last-value equivalence, opposite-history
preorder block equality, wide blocks, exact replacement no-ops, retained versions, off-path block
sharing, typed diff, inclusive and reversed ranges, a 10,000-operation ordered model, full
re-encoding validation, and `forkIO` readers.

Eight persistence groups extend that checkpoint with the shared golden block through pack export;
complete save/load/import round trips; immutable-store idempotence; empty roots; malformed,
noncanonical, foreign-domain, unsupported, missing-closure, digest-tamper, and late-conflict
failures; independent enforcement and construction validation for all seven finite budgets; exact
query-first and shape-second proof admission using same-domain bomb codecs; accounted failure
diagnostics; canonical membership, nonmembership, inclusive-range, and empty-root `MSP2` proofs;
query/block/step/expansion/omission/extra-step tampering; complete, partial, and iterative frontier
synchronization; disjoint, identical, unresolved, and resolved typed merges; present `Nothing`
versus deletion; retained roots in one content store; and a 2,000-operation save/load model. Local
proof and sync construction is separately exercised with decoders that fail if forced.
