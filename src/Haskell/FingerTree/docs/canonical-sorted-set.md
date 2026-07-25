# Canonical zip-zip sorted set

- Created (UTC): 2026-07-12T00:23:48Z
- Repository HEAD: 334e18db9c21711bc982c7719dc78875a0ad06bd
- Audience: Haskell callers and maintainers of the canonical sorted-set port
- Scope: `Durable7.FingerTree.CanonicalSortedSet`

`CanonicalSortedSet a` is an immutable sorted set whose tree shape is a function of the represented
equivalence classes and one retained `ZipTreeRankPolicy a`. It uses a binary-search tree ordered by
the policy comparator and a max heap ordered by a deterministic zip-zip rank. Stable bulk sorting
and incremental updates therefore converge on the same Cartesian-tree topology regardless of
construction history.

## The policy boundary

All policy factories are explicitly in `IO`, while every operation on an existing set is pure. The
effectful boundary has two purposes:

1. `newRandomPolicy` and `newRandomPolicyBy` obtain a fresh 32-byte secret key from `crypton`'s
   system-entropy source.
2. Every factory allocates an opaque `Data.Unique` family identity. Haskell functions do not have
   decidable equality and ordinary immutable records do not expose reference identity, so an
   explicit unique token is the honest equivalent of the C# policy object's identity.

The seeded and keyed factories are effectful only for that identity token. A public seed or a
caller-retained key still reproduces ranks, shape, validation statistics, and digest across
separately created policies. Those separate policies compare by mathematical contents, but set
algebra rejects them because they do not belong to the same policy family. Derive all versions that
must participate in `union`, `intersection`, or `difference` from one policy value.

```haskell
import Data.Word (Word32, Word64)
import qualified Durable7.FingerTree.CanonicalSortedSet as Canonical

int32Hash :: Int -> Word64
int32Hash value = fromIntegral (fromIntegral value :: Word32)

example :: IO (Either Canonical.CanonicalSetError [Int])
example = do
  policy <- Canonical.newSeededPolicy int32Hash 0x5eed
  pure $ do
    left <- Canonical.fromList policy [1, 2, 4]
    right <- Canonical.fromList policy [2, 3, 5]
    Canonical.toAscList <$> Canonical.union left right
```

There is intentionally no implicit global default policy and no hidden `unsafePerformIO`. Haskell
also has no base-library analogue of .NET's general-purpose equality hash, so the caller always
supplies the equivalence-class hash. For an explicit comparator, that hash must be constant whenever
the comparator returns `EQ`. Bulk and duplicate insertion paths check this condition for the
representatives they observe and return `InconsistentRankHash` on a violation.

## Exact rank derivation

The public-seed path matches the C# reference byte for byte:

1. encode the ASCII prefix `ZZT2` followed by the `Word64` seed in big-endian order;
2. derive the 32-byte rank key as SHA-256 of that material;
3. encode the caller's `Word64` rank hash in big-endian order;
4. compute HMAC-SHA-256 of that eight-byte source with the retained rank key;
5. read the first three digest words in big-endian order.

The first word contributes its leading-zero count, the second is compared as an unsigned `Word64`,
and the third feeds the cached tree digest. `newKeyedPolicy` retains the complete caller key and
rejects keys shorter than 32 bytes. Cryptographic operations and entropy come from the maintained
`crypton` package; the port contains no local SHA-256 or HMAC implementation.

Priority compares the geometric component first, then the unsigned secondary word, both larger
first. A complete rank tie is broken by the comparator-smaller item. Consequently even a constant
rank hash has one deterministic representation: an ascending right chain. It loses expected balance
but not correctness or stack safety.

## Persistence and operations

`insert` and `delete` walk and rebuild an explicit path. Split, merge, bulk Cartesian construction,
ordered enumeration, equality, digest validation, and statistics all use explicit worklists. There
is no recursion proportional to tree height, so a fully colliding 4,096-node test tree exercises
lookup, removal, reinsertion, enumeration, digest access, and validation without consuming the
native call stack.

Expected lookup and update cost is O(log n), with O(log n) fresh nodes. Adversarial rank collisions
can degrade it to O(n). `fromList` costs O(n log n) for stable sorting and O(n) for deduplication plus
Cartesian construction. It preserves the first input representative in each comparator-equivalent
class. Enumeration and validation are O(n). `contentHash` is O(1): each immutable node eagerly
caches the same non-cryptographic mixing formula used by the sibling ports.

No-op duplicate insertion and absent deletion retain the current root. Changed versions share all
off-path nodes. `sharesRootWith` and `sharedNodeCount` are IO-only `StableName` diagnostics for tests;
they do not affect set semantics or representation.

## Equality, relations, and validation

Same-family equality rejects by count and digest before comparing canonical topology. Cross-policy
equality sorts and deduplicates the right operand under the left set's comparator, so comparison is
deliberately receiver-defined. This matters when, for example, a case-insensitive one-element set is
compared with a case-sensitive two-element set containing two spellings: equality can be true in
the insensitive direction and false in the sensitive direction. Subset, superset, and overlap
relations use the same receiver rule.

`validateStructure` independently checks strict search-tree bounds, parent/child heap order, rank
reproducibility, cached count and height, the cached digest, and root-level count/height agreement.
It reports `CanonicalSortedSetStatistics`, including maximum geometric rank and repeated
geometric/secondary priority pairs. The test suite mutates a deliberately hostile test-only hash
closure after construction to prove that validation detects a non-reproducible rank; constructors
remain private, and production callers cannot forge nodes or policy identity.
