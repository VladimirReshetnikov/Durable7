# C++ Merkle Persistence, Proofs, Synchronization, And Merge

- Status: Current persistence specification
- Created (UTC): 2026-07-12T22:15:00Z
- Repository HEAD: d1ad0f774e9e356b9967c428ac0eef16c25f9562
- Audience: C++ consumers, maintainers, security reviewers, and cross-language port reviewers
- Scope: Verified `MST2` storage/import, exact `MSP2`, bounded verification, synchronization, and three-way merge

The C++20 persistence tier is header-first and byte-compatible with the C, C#, Haskell,
Kotlin/JVM, and Rust sibling implementations. Include
[`merkle_persistence.hpp`](../include/durable7/hamt/merkle_persistence.hpp) for blocks,
stores, packs, bounded load/import, and synchronization. Include
[`merkle_proofs.hpp`](../include/durable7/hamt/merkle_proofs.hpp) for proof creation,
proof verification, and three-way merge. The aggregate
[`hamt.hpp`](../include/durable7/hamt/hamt.hpp) includes both.

The persistence layer never treats bytes as trusted merely because they came from a block store.
Every load/import/proof path authenticates addresses, parses strict canonical encodings, enforces
policy and separator invariants, applies finite resource limits, and reconstructs through the same
canonical node factory used by ordinary tree updates.

## Immutable Transfer Values

`merkle_block` owns immutable bytes behind shared storage and pairs them with a claimed
`merkle_digest`. Construction deliberately does not recompute the digest: stores are format
agnostic, while verified operations authenticate the pair. `content()` returns
`std::span<const std::byte>` and `to_bytes()` returns a mutable copy.

`merkle_block_pack` owns a deterministic ordered block vector and rejects duplicate addresses. It
records the algorithm ID, policy-domain digest, target root, total bytes, and whether the pack
contains the root. Packs may be complete closures or partial transfers; import can resolve missing
blocks through a destination/fallback store.

`merkle_proof`, `merkle_proof_step`, and `merkle_sync_plan` likewise own their vectors privately and
expose only const spans. Callers cannot mutate indexes, steps, queries, blocks, or requested
frontiers after validation.

Names under `merkle_persistence_detail` are unstable implementation machinery, not supported API.
Consumers must not name them: in particular, the internal friend bridge exists only so the public
persistence operations can reconstruct already-validated canonical nodes without exposing mutable
node or entry constructors. The aggregate-header consumer guards against restoring the former
top-level access-bridge name.

## Concurrent Block Store

`merkle_block_store` defines immutable snapshot reads and idempotent content-addressed writes.
Writing identical bytes under an existing address returns `false`. Writing different bytes under an
existing address throws `merkle_verification_error` with
`merkle_verification_failure_kind::conflicting_block`.

`in_memory_merkle_block_store` implements that contract with `std::shared_mutex` and a sorted
`std::map`. Concurrent `get`, `contains`, and address snapshots take shared locks; writes, removals,
and clear take exclusive locks. Returned `merkle_block` values retain immutable shared bytes after a
store mutation.

## Export, Save, Load, And Import

- `export_merkle_pack(tree)` returns the complete closure in deterministic preorder.
- `export_merkle_pack(tree, digests)` returns unique requested blocks in caller order and rejects
  duplicate or foreign addresses.
- `save_merkle_tree(tree, store)` preflights every destination conflict before the first write, then
  stores the complete closure and returns the number of newly added addresses.
- `load_merkle_tree(root, policy, store, budget)` resolves and verifies the complete reachable
  closure.
- `import_merkle_pack(pack, policy, destination, budget)` verifies every supplied block, resolves
  the named root through a staged-pack overlay plus optional destination store, preflights all
  destination conflicts, and only then publishes supplied blocks.

Empty roots require no blocks. A partial pack succeeds only when the overlay/fallback union contains
the complete named closure. Extra supplied blocks are still authenticated and canonically decoded;
they are not silently accepted as opaque data.

Verified reconstruction checks:

1. supported algorithm and exact policy domain;
2. claimed digest against complete bytes;
3. `MST2`, node tag, domain, level, signed counts, and exact end-of-block;
4. child-reference and minimum-length feasibility before vector allocation;
5. canonical codec decode/re-encode equality for every key and value;
6. hash-derived levels and strict comparator order;
7. child level, separator interval, closure count, and cycle constraints;
8. reconstruction through the core node factory; and
9. exact bytes/digest plus final deep `validate_structure()`.

Failures throw `merkle_verification_error` with a stable failure kind and, when known, the offending
or missing digest. Codec implementations are required to report malformed values with
`merkle_codec_error` or `std::invalid_argument`; unrelated application exceptions propagate and no
destination publication begins.

## Seven Verification Limits

`merkle_verification_budget` validates seven positive finite limits:

1. unique block count;
2. query plus unique block bytes;
3. bytes in one block;
4. root-to-leaf expansion depth;
5. cumulative decoded entries;
6. child references in one block; and
7. bytes in one `MSP2` query descriptor.

The defaults match the sibling ports: 1,000,000 blocks, 1 GiB total bytes, 16 MiB per block,
depth 256, 100,000,000 entries, 65,536 child references, and 16 MiB per proof query. `with_max_*`
members create validated budget copies; changing the block-byte limit also changes the query limit
when those two limits were previously coupled.

Envelope and declared-size checks precede allocations they bound. Block count/size/total limits are
checked before hashing or codecs. Entry and child counts are checked before their vectors are
reserved. Depth is checked before following a reference.

Proof verification has an intentionally observable precedence:

1. account/reject the query byte budget;
2. reject proof step count and every expanded-index count;
3. validate algorithm and domain;
4. preflight per-block and aggregate bytes;
5. allocate lookup maps, hash blocks, invoke codecs, and expand references.

Thus a hostile oversized query reports zero verified bytes/blocks, while a step/expansion-shape
failure accounts only the already accepted query and performs no block, hash, or codec work.

## Exact `MSP2` Queries And Proofs

`create_merkle_proof(tree, key)` emits either membership or nonmembership. A membership query is:

```text
"MSP2" | 0x00 | i32be(keyLength) | keyBytes |
                  i32be(valueLength) | valueBytes
```

A nonmembership query is:

```text
"MSP2" | 0x01 | i32be(keyLength) | keyBytes
```

`create_merkle_range_proof(tree, minimum, maximum)` proves completeness of an inclusive comparator
range and emits:

```text
"MSP2" | 0x02 |
    i32be(minimumLength) | minimumBytes |
    i32be(maximumLength) | maximumBytes
```

Proof steps contain exact `MST2` blocks and sorted child indexes expanded by another step. Point
proofs expand exactly one search interval until presence or an authenticated empty child.
Range proofs expand exactly the nonempty child intervals intersecting the requested range.

`verify_merkle_proof` authenticates every block, canonicalizes the query through the supplied
codecs, checks exact expansions and separator intervals, rejects duplicates, cycles, missing steps,
unreachable extras, and mismatched claims, and returns `merkle_proof_verification_result` rather
than throwing verification failures. The result reports failure kind/message and accounted blocks
and bytes. Local allocation failures and unrelated codec/application exceptions still propagate.

The shared one-entry golden queries are:

```text
membership(42 -> "forty-two"):
4d53503200000000040000002a0000000a01666f7274792d74776f

nonmembership(43):
4d53503201000000040000002b

range(40..44):
4d535032020000000400000028000000040000002c
```

## Closure-Pruned And Iterative Synchronization

`create_merkle_sync_pack(target, receiver)` walks target preorder. If the receiver already contains
a block address, the complete child closure beneath that authenticated address is pruned; otherwise
the block and unknown descendants are emitted.

`plan_merkle_sync(target, local, receiver)` performs one missing-frontier round. It requests the
first absent address on each target path and does not descend through that unknown block. After the
receiver stores the requested round, call it again. Once no addresses are requested, verified load
can publish the target root; a final plan with that loaded local tree reports matching roots.

Stores used for pruning must contain previously verified closures. Mere address presence in an
untrusted format-agnostic store is not itself verification.

## Three-Way Merge

`merge_merkle_trees(base, left, right)` merges two compatible descendants. Root fast paths return a
complete existing tree. Otherwise the algorithm merges ordered entry handles and reuses canonical
entries without re-encoding unchanged keys or values.

`merkle_merge_value<V>` distinguishes absence from presence. If `V` is nullable,
`present(std::nullopt)` is therefore different from deletion. A true conflict contains shared key
and value handles for base, left, and right. A resolver may choose base/left/right, delete, supply a
new move-only value, or leave the conflict unresolved.

The result contains either one complete canonical tree or all unresolved conflicts. It never exposes
the partially accumulated output. Resolver, equality, comparator, or codec exceptions propagate
before any tree is published. Unchanged and selected entries preserve their owning handles; only a
resolver-supplied value is encoded anew.

## Move-Only Values And Lifetime

Loaded keys/values are decoded directly into shared immutable entry state. Proofs retain serialized
blocks rather than copying application values. Merge copies entry handles, not `K` or `V`. All
load/import/proof/merge paths therefore support move-only key and value types when their comparator
and codecs satisfy the ordinary deterministic contracts.

Trees, blocks, packs, proofs, plans, and merge results are immutable after publication and may be
read concurrently. Custom comparers/codecs must themselves support the concurrency allowed by the
caller. Ordinary C++ lifetime rules apply to returned spans: keep the owning pack, proof, plan, or
block alive while using the view.

## Validation

The persistence groups live in
[`merkle_search_tree_tests.cpp`](../tests/merkle_search_tree_tests.cpp). They cover exact shared
goldens, complete/partial round trips, missing/tampered/noncanonical data, count corruption,
destination conflict preflight, all seven budgets, proof precedence and tampering, iterative sync,
present-null and unresolved merge, move-only load/import/proof/merge, and concurrent store/load/
proof/sync. See [validation](validation.md) for the full compiler matrix.
