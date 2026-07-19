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

## Set-Bit Cursor

`PersistentChunkedBitSetCursor` is a non-generic `public readonly struct` gap cursor over **present
set bits**, not over a dense Boolean sequence extending to `int.MaxValue`. Its position is a
population rank, so `Count` and `Position` are `long` while bit indexes remain `int`. It is a
**Profile R snapshot-plus-rank checkpoint** under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md):
the value is a retained set reference plus a validated rank, and every edit delegates to the ordinary
`Add`/`Remove` operations. It claims none of the C# rope tier's focused representation, memo cell,
callback ceiling, allocation bound, or amortized-locality properties, and chunk boundaries never
enter the public contract.

```csharp
public PersistentChunkedBitSetCursor GetCursor(long position = 0);
public PersistentChunkedBitSetCursor GetCursorAtOrAfter(int bitIndex);
public bool TryGetCursor(int bitIndex, out PersistentChunkedBitSetCursor cursor);

public readonly struct PersistentChunkedBitSetCursor
{
    public long Count { get; }
    public long Position { get; }
    public bool IsAtStart { get; }
    public bool IsAtEnd { get; }
    public bool TryPeekPrevious(out int bitIndex);
    public bool TryPeekNext(out int bitIndex);
    public PersistentChunkedBitSetCursor MovePrevious();
    public PersistentChunkedBitSetCursor MoveNext();
    public PersistentChunkedBitSetCursor SeekRank(long position);
    public PersistentChunkedBitSetCursor Add(int bitIndex);
    public PersistentChunkedBitSetCursor DeletePrevious();
    public PersistentChunkedBitSetCursor DeleteNext();
    public PersistentChunkedBitSet Snapshot();
}
```

- Cursor rank accepts `0 .. Count`, where `Count` is the end gap. This deliberately differs from
  `Select`, whose domain is `0 .. Count - 1`.
- `GetCursorAtOrAfter(bitIndex)` places the gap before the first present bit at or after the given
  index. `TryGetCursor(bitIndex, out cursor)` is the exact form: it reports the hit through its
  `bool` return and still publishes a usable at-or-after gap on a miss.
- `Add(bitIndex)` on a present bit is an identity no-op returning the receiver cursor; a missing bit
  updates or inserts its word and returns the gap after the new bit. `DeleteNext` and
  `DeletePrevious` clear the exact neighboring bit, and clearing a word's last bit removes the chunk
  so no publishable version stores a zero word.
- There is no cursor-level set algebra: `Union`, `Intersect`, `Except`, and `SymmetricExcept` remain
  sparse word-stream operations on the collection.
- `Snapshot()` on a clean cursor returns the exact source instance, and a present-bit `Add`
  preserves that reference identity.
- Positions outside `0 .. Count` throw `ArgumentOutOfRangeException`; boundary violations throw
  `InvalidOperationException`; the invalid `default` value throws `InvalidOperationException` from
  every member, including `Position`, `IsAtStart`, and the `SeekRank(Position)` identity shortcut.

Honest cost: creation, `SeekRank`, and movement are O(1) integer work. A peek resolves through
`Select`, which is an O(log `c`) measured descent **plus a bounded in-word loop of up to 63
clear-lowest-set-bit iterations** — asymptotically O(log `c`), but every peek pays that constant, and
it is a loop rather than a bit-deposit intrinsic. `GetCursorAtOrAfter` and `Add` resolve through
`Rank`, a clean O(log `c`) descent plus one population count. Because the cursor retains no path, a
complete ascending walk by move-plus-peek is O(`Count` · log `c`); use enumeration for whole-set
traversal.

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
