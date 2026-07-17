# Persistent Chunked Bit Set Contract

- Status: Implemented normative contract
- Created (UTC): 2026-07-17T00:00:00Z
- Repository HEAD: `0bee5b4e50d0a21d43af88efbce5df6d34516bf9`
- Audience: Consumers and maintainers of `Tools.DataStructures.FingerTree`
- Scope: `PersistentChunkedBitSet` semantics, rank/select, algebra, and complexity

`PersistentChunkedBitSet` is an immutable sparse set over every nonnegative `int` bit index. It
stores only nonzero 64-bit words in ascending word-index order inside a measured finger tree. The
cached measure records chunk count, population count, and the last word index, enabling logarithmic
word location and population selection without allocating on read paths.

## Semantic Contract

- `Count` is the `long` number of set bits; `ChunkCount` is the number of represented nonzero words.
- Enumeration is ascending and yields each bit index once.
- `Contains` returns `false` for a negative index. `Remove` treats a negative or clear index as an
  identity no-op. `Add` and `CreateRange` reject negative indexes.
- `Rank(index)` is inclusive: it returns the number of set bits less than or equal to `index`, and
  returns zero for a negative index.
- `Select(rank)` uses zero-based population order and rejects ranks outside `[0, Count)`.
- Removing a word's final bit removes the chunk. No zero chunk is stored.
- `Union`, `Intersect`, `Except`, and `SymmetricExcept` use mathematical set semantics and merge
  chunk streams rather than scanning the largest bit index.
- Every operation leaves all source versions valid. Algebra results preserve receiver identity when
  their chunk sequence is unchanged; self-difference and self-symmetric-difference return `Empty`.

## Public Surface

```csharp
public sealed class PersistentChunkedBitSet : IEnumerable<int>
{
    public static PersistentChunkedBitSet Empty { get; }
    public static PersistentChunkedBitSet CreateRange(IEnumerable<int> bitIndexes);
    public long Count { get; }
    public int ChunkCount { get; }
    public bool IsEmpty { get; }
    public bool Contains(int bitIndex);
    public PersistentChunkedBitSet Add(int bitIndex);
    public bool TryAdd(int bitIndex, out PersistentChunkedBitSet result);
    public PersistentChunkedBitSet Remove(int bitIndex);
    public bool TryRemove(int bitIndex, out PersistentChunkedBitSet result);
    public long Rank(int bitIndex);
    public int Select(long rank);
    public bool TrySelect(long rank, out int bitIndex);
    public PersistentChunkedBitSet Union(PersistentChunkedBitSet other);
    public PersistentChunkedBitSet Intersect(PersistentChunkedBitSet other);
    public PersistentChunkedBitSet Except(PersistentChunkedBitSet other);
    public PersistentChunkedBitSet SymmetricExcept(PersistentChunkedBitSet other);
    public PersistentChunkedBitSet Clear();
}
```

## Complexity And Allocation

Let `c` be the number of nonzero chunks. Membership, point edits, inclusive rank, and select are
O(log `c`) amortized under the measured finger-tree contract. `Contains`, `Rank`, and `Select` use
closure-free measure predicates and allocate no permanent collection nodes. Construction sorts
distinct word indexes in O(`n log n`) time. Set algebra is O(`c1 + c2`) time and O(`result chunks`)
temporary/output storage. Enumeration is O(`Count + c`) and uses bounded tree traversal state.

## Validation

`PersistentChunkedBitSetTests.cs` covers negative and `int.MaxValue` boundaries, word seams,
deduplication, inclusive rank, select, chunk contraction, all algebra operations, receiver identity,
retained branches, randomized `SortedSet<int>` model parity, and recursive measure invariants.
