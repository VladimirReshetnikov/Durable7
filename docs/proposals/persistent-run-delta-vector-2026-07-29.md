# Persistent Run-Delta Vector: Branchable Checkpoints With Run-Sensitive Change Discovery

- Status: Experimental C# reference prototype; scoped ADT/API synthesis, not a priority claim
- Created: 2026-07-29
- Base repository HEAD: `67a67f9f553a9194db9d8dd3cf9c7bd670f9b981`
- Branch: `experimental/persistent-run-delta-vector`
- Prototype:
  [`PersistentRunDeltaVector.cs`](../../src/CSharp/src/Durable7.FingerTree/PersistentRunDeltaVector.cs)
- Tests:
  [`PersistentRunDeltaVectorTests.cs`](../../src/CSharp/tests/Durable7.FingerTree.Tests/PersistentRunDeltaVectorTests.cs)
- Scope: one fixed-length C# research implementation; no cross-language shipment commitment

## Result

`PersistentRunDeltaVector<T>` is a fully persistent fixed-length vector with two logical views:
the current values and a checkpoint. Every version also maintains the exact maximal contiguous
runs at which those views differ under one retained equality comparer.

Let:

- `n` be the fixed vector length;
- `k` be the number of dirty positions; and
- `r` be the number of maximal dirty runs, so `0 <= r <= k <= n`.

Against an explicit state-only RRB checkpoint baseline, the structure has this strict result:

> Exact dirty-run descriptor discovery is output-optimal `Theta(r)`, rather than worst-case
> `Theta(n)` from unindexed current/checkpoint RRB roots. All common operations retain the
> baseline's asymptotic time and total-space bounds.

The gap is unbounded. If an `n`-position vector differs from its checkpoint on one contiguous
block, then `k = n` and `r = 1`: state-only roots must inspect all `n` positions to rule out a clean
hole, while this structure emits one descriptor in constant time. Relative to an explicit
point-delta map, the maintained metadata also occupies `Theta(r)` rather than `Theta(k)` live
records. A straightforward ordered scan coalesces those point records in `Theta(k)`, but that is not
a lower bound for every order-statistic point index. Emitting the changed payload values is still
`Omega(k)` and is not claimed to be faster.

This is deliberately a narrow result. The interval component is an instance of the established
Discrete Interval Encoding Tree idea, and a persistent RRB plus a balanced DIET can recreate the
construction and match every bound. The proposal claims a useful aggregate ADT and API not found in
the targeted search: branchable immutable versions, equality-relative cancellation, constant-time
whole checkpoint/rollback, exact run discovery, and selective hunk acceptance/reversion. It does
not claim a new interval algorithm, a new persistent-array technique, universal Pareto superiority,
or historical priority.

## Workload And Surface

The target is fixed-shape state that is edited speculatively along many retained branches:
simulation parameters, editor or IDE property grids, game-state slabs, page tables, build-state
vectors, and reviewable configuration snapshots. Callers often need to answer “which contiguous
regions changed?”, accept one region into the baseline, reject another, then keep both the source
and result versions.

The public surface includes:

```csharp
public readonly record struct PersistentDirtyRun(int Start, int Length)
{
    public int EndExclusive { get; }
}

public sealed class PersistentRunDeltaVector<T> : IReadOnlyList<T>
{
    public static PersistentRunDeltaVector<T> Empty { get; }
    public static PersistentRunDeltaVector<T> Create(IEqualityComparer<T>? valueComparer = null);
    public static PersistentRunDeltaVector<T> CreateRange(
        IEnumerable<T> items,
        IEqualityComparer<T>? comparer = null);

    public int Count { get; }
    public bool IsEmpty { get; }
    public IEqualityComparer<T> ValueComparer { get; }
    public T this[int index] { get; }
    public T GetCheckpointValue(int index);

    public int DirtyCount { get; }
    public int DirtyRunCount { get; }
    public bool HasChanges { get; }
    public bool IsDirty(int index);
    public bool TryGetDirtyRunContaining(int index, out PersistentDirtyRun run);
    public PersistentDirtyRun GetDirtyRunAt(int rank);
    public IEnumerable<PersistentDirtyRun> EnumerateDirtyRuns();

    public PersistentRunDeltaVector<T> SetItem(int index, T value);
    public PersistentRunDeltaVector<T> ResetItem(int index);
    public PersistentRunDeltaVector<T> Checkpoint();
    public PersistentRunDeltaVector<T> Rollback();
    public PersistentRunDeltaVector<T> AcceptDirtyRunAt(int rank);
    public PersistentRunDeltaVector<T> RevertDirtyRunAt(int rank);
}
```

Aliasing any instance is an O(1) fork: every retained version remains readable and can be edited
into an independent branch. There is intentionally no length-changing operation, merge of unrelated
branches, range assignment, arbitrary rebase, or automatic payload diff.

## Representation

A version stores:

```text
V = (current, checkpoint, runs, dirtyCount, equality)
```

- `current` and `checkpoint` are persistent `RrbVector<Cell>` roots of equal length.
- `runs` is a persistent ordered map `start -> endExclusive`, with one record for every maximal
  dirty half-open interval.
- `dirtyCount` is the sum of the interval lengths.
- `equality` is the stable value relation used by every descendant version.

`Cell` is a private reference-identity wrapper around `T`. It is load-bearing: `RrbVector.SetItem`
uses default equality to recognize no-ops, while this ADT accepts a custom comparer. A genuine edit
therefore installs a fresh `Cell`, and a cancellation installs the exact checkpoint `Cell`. Every
clean position reuses the same cell object in both roots.

The invariant is:

```text
runs = maximalIntervals({ i | !equality(current[i], checkpoint[i]) })
dirtyCount = sum(end - start for (start, end) in runs)
runs is ordered, disjoint, and nonadjacent
runs is empty => current and checkpoint have the same root identity
```

The implementation's internal validator checks the RRB roots, interval ordering/maximality,
cardinality, comparer-relative truth at every dirty position, and exact cell identity at every
clean position.

## Point-Edit Algorithm

`SetItem(i, x)` first compares `x` with the current value. A comparer-equal replacement returns the
receiver. Otherwise it compares `x` with the checkpoint value before allocating or publishing any
successor, so a comparer exception leaves every input version unchanged.

Only four logical transitions remain:

| Before | After | Run-index action |
| --- | --- | --- |
| clean | dirty | insert `i`, merging the left and/or right adjacent runs |
| dirty | clean | remove `i`, deleting, shrinking, or splitting its containing run |
| dirty | dirty | leave the run index unchanged |
| comparer-equal current value | unchanged | return the receiver |

At most two neighboring interval records change. The RRB copies one root-to-leaf path; the ordered
run map performs a constant number of logarithmic predecessor/successor and update operations.
Because `r <= n`, the total is `O(log n)` amortized time and fresh nodes. If removing the final dirty
position empties the run map, the current root is canonicalized to the exact checkpoint root.

`ResetItem(i)` is `SetItem(i, checkpoint[i])`, so it follows the same cancellation path.

## Checkpoint, Rollback, And Selected Hunks

Whole-version operations replace roots rather than visiting values:

```text
Checkpoint(current, checkpoint, runs) = (current, current, empty)
Rollback(current, checkpoint, runs)   = (checkpoint, checkpoint, empty)
```

Both are O(1) and return the receiver when already clean.

For one selected run `[a,b)`, acceptance replaces that range of the checkpoint root with the
corresponding current slice; reversion replaces the current range with the checkpoint slice. The
prototype composes four RRB splits and two boundary-spine concats, removes one run record, and
subtracts its length. This costs `O(log n)` independently of `b-a` and shares untouched
off-boundary subtrees; boundary leaves and spines may be rebuilt. This splice bound is inherited
from RRB trees and is not itself a novelty claim.

## Complexity And The Exact Comparator Family

The no-regression statement compares three fixed-length structures built over the same repository
RRB operations:

1. **State-only RRB pair:** current and checkpoint roots, with no maintained relational index.
2. **Point-delta RRB:** the same roots plus a persistent ordered set/map containing all `k` dirty
   positions.
3. **Run-delta vector:** the same roots plus `r` maximal intervals.

| Operation | State-only RRB pair | RRB + point-delta map | Run-delta vector |
| --- | ---: | ---: | ---: |
| Create `n` values | `Theta(n)` | `Theta(n)` | `Theta(n)` |
| Read current/checkpoint value | `O(log n)` | `O(log n)` | `O(log n)` |
| Persistent point edit | `O(log n)` | `O(log n)` | `O(log n)` amortized |
| Fork by retaining a handle | `O(1)` | `O(1)` | `O(1)` |
| Whole checkpoint / rollback | `O(1)` | `O(1)` | `O(1)` |
| Enumerate current values | `Theta(n)` | `Theta(n)` | `Theta(n)` |
| Dirty membership | not indexed | `O(log(k + 2))` | `O(log(r + 2))` amortized |
| Exact dirty-run descriptors | worst-case `Theta(n)` | `O(min(k, r log^2(k + 2)))` by scan/rank search; not a lower bound | `Theta(r)` |
| Select dirty run by rank | not indexed | not directly stored | `O(log(r + 2))` amortized |
| Live delta metadata | none | `Theta(k)` | `Theta(r)` |
| Accept/revert a known run | `O(log n)` splice | `O(log n)` splice | `O(log n)` amortized |
| Total live structure | `O(n)` | `O(n)` | `O(n)` |
| Fresh structure per point edit | `O(log n)` | `O(log n)` | `O(log n)` amortized |

The state-only lower bound is an adversary argument at the black-box RRB API boundary. Until a
position is inspected, its values can be equal or unequal without changing any observed result, so
an exact set of run boundaries can require all `n` comparisons. Even an internal identity-pruned
walk has a linear worst case after edits and cancellations have copied paths throughout the vector.

The run-delta output itself has `r` records, proving the matching `Omega(r)` lower bound and
therefore `Theta(r)` optimality. The point-delta table's `Theta(k)` entry describes the ordinary
linear coalescing scan, not a representation lower bound. For example, `Count`, minimum, and maximum
certify one contiguous point set in O(1); with order-statistic access, the monotone sequence
`key[j] - j` permits block-boundary searches without consuming every record. With O(log k)
`EntryAt`, at most `r` binary searches give the table's O(r log^2(k + 2)) upper bound; a linear scan
is better when `k` is smaller. An augmented point tree can go further, and a balanced DIET matches
this proposal exactly. The unconditional
point-delta comparison is therefore the `Theta(r)` versus `Theta(k)` live metadata, while the strict
descriptor-time theorem rests on the state-only RRB baseline.

A single live version occupies `O(n)` total structure, including `O(r)` delta records. Starting
from one `n`-element version, `m` retained effective point edits use `O(n + m log n)` worst-case
total structural space. The proposal does not collapse that history bound to `O(n)`.

## Prior Art And Novelty Boundary

The building blocks and adjacent systems are established:

- Erwig's [*Diets for Fat Sets*](https://doi.org/10.1017/S0956796898003116) introduced Discrete
  Interval Encoding Trees: maximal adjacent subsets of a discrete set stored as intervals.
  Friedmann and Lange's
  [*More on Balanced Diets*](https://doi.org/10.1017/S0956796810000328) develops balanced DIETs and
  set operations. The run index here is an application of that representation, not a replacement.
- Stucki et al.'s
  [*RRB Vector: A Practical General Purpose Immutable Sequence*](https://doi.org/10.1145/2784731.2784739)
  supplies the persistent vector and structural split/concat family.
- Driscoll, Sarnak, Sleator, and Tarjan's
  [*Making Data Structures Persistent*](https://doi.org/10.1016/0022-0000(89)90034-2) supplies the
  standard full-persistence framework.
- Fuchsia's
  [`zx_pager_query_dirty_ranges`](https://fuchsia.dev/reference/syscalls/pager_query_dirty_ranges)
  exposes contiguous dirty-page ranges, but its state is mutable, write-dirty rather than
  equality-relative, and not an immutable branchable value.
- [Concordia](https://arxiv.org/abs/2606.23521) contrasts sparse dirty-page records with run-length
  encoding for contiguous checkpoint pages, but produces checkpoint records rather than this
  branchable cancellation-aware value.
- [Roaring bitmaps](https://arxiv.org/abs/1603.06549) include run containers and copy-on-write
  sharing; they do not provide the two-root checkpoint and selective-hunk semantics.
- [Snapshottable Stores](https://doi.org/10.1145/3674637) provides persistent capture/restore and
  transactional commit/rollback, without a relational dirty-run index.
- Git's [interactive staging](https://git-scm.com/docs/git-add) demonstrates the usefulness of
  accepting or rejecting individual hunks, but computes textual diffs over mutable repository
  states rather than maintaining this fixed-vector index.

Targeted searches on 2026-07-25 through 2026-07-29 combined “fully persistent/immutable vector or
array,” “checkpoint/snapshot/rollback,” “dirty interval/range/run,” “delta/diff,” “hunk
accept/revert,” “DIET,” and citation traversal from the sources above. They found the components and
close applications, but not a named aggregate with all of these properties:

- any retained version can branch;
- equality-relative dirtiness cancels exactly when a value returns to its checkpoint class;
- whole checkpoint and rollback are constant-time root changes;
- maximal dirty runs are continuously available by rank and output-linear enumeration; and
- one selected run can be accepted or reverted without touching its payload length.

The defensible novelty claim is therefore:

> The searched sources did not expose this exact persistent checkpoint/run-delta ADT and selective
> hunk API. Descriptor discovery is strictly faster than the named state-only RRB baseline without
> worsening its common asymptotic operations; a persistent RRB plus balanced DIET is an equal-bound
> construction of this same design.

This negative search is not proof of first publication, patentability, or broad novelty.

### Why this is not globally Pareto-optimal

Dietz's [*Fully Persistent Arrays*](http://hdl.handle.net/1802/5695) gives expected amortized
`O(log log m)` operations, and Straka's
[*Fully Persistent Arrays with Optimal Worst-Case*](https://ufal.mff.cuni.cz/~straka/papers/2009-perarray.pdf)
gives `O(log log min(n,m))` worst-case lookup/update with `O(n+m)` space. Those structures beat this
RRB prototype's `O(log n)` point operations and history space. They do not expose dirty-run
semantics, but they disprove a universal “no worse than every persistent array” claim. The theorem
in this proposal applies only to the declared RRB comparator family.

## Correctness

### Exact run index

Induct on successful edits. The empty/created vector has no unequal positions and an empty run map.
An edit can change dirty membership only at its target `i`. Insertion joins exactly those existing
runs ending at `i` or starting at `i+1`; deletion modifies only the unique containing run and uses
the four singleton/left-edge/right-edge/interior cases. Every other position and adjacency is
unchanged. The result is therefore exactly the maximal-interval encoding of the new dirty set.

### Whole-version operations

After `Checkpoint`, both logical views name the old current root. After `Rollback`, both name the
old checkpoint root. In either case every position is equal by identity, so the empty run map and
zero count are exact.

### Selected-run operations

Suppose `[a,b)` is a maximal dirty run. Acceptance changes only the checkpoint slice on `[a,b)` to
the exact current cells, making precisely those positions clean; reversion symmetrically changes
the current slice to checkpoint cells. Positions outside the interval retain both roots. Removing
the selected interval and subtracting its length therefore restores the invariant without changing
any other run.

### Persistence and failure atomicity

RRB roots, ordered-map roots, cells, and wrappers are immutable. Producing operations return a new
wrapper and do not mutate any input; every retained version can therefore be edited again. All
user-comparer calls in `SetItem` occur before successor construction. A thrown comparer publishes
no partial version. Allocation failures likewise cannot mutate an existing root.

Concurrent reads and independent branch derivations are safe when the comparer is safe for
concurrent calls. The comparer must remain one stable equivalence relation, and values retained in
versions must not mutate comparer-observable state; otherwise any cached relational index can
become stale.

## Validation

The focused xUnit class covers:

- empty, factory, bounds, no-op identity, and equality policies both coarser and finer than default
  equality, including exact representative restoration;
- every local interval insertion, merge, shrink, split, and cancellation case;
- selected-run acceptance/reversion with zero value-comparer calls while all source versions remain
  valid;
- 5,000 randomized operations from retained arbitrary parent versions against independent current,
  checkpoint, and maximal-run models;
- the strict clustered witness with 8,191 dirty positions represented by two descriptors;
- comparer failure before successor publication; and
- concurrent readers while workers derive and validate independent branches.

The randomized oracle compares every current value, checkpoint value, membership result, dirty
count, run rank, and run enumeration, then invokes the internal structural validator. The clustered
test is executable evidence of `k >> r`; the output-size proof supplies the asymptotic result.

Final serialized branch validation passed:

- focused `PersistentRunDeltaVectorTests`: 7/7 in Debug and 7/7 in Release;
- complete FingerTree project: 731/731 in Debug and 731/731 in Release;
- complete C# solution: 1,537/1,537 in Debug and 1,537/1,537 in Release;
- clean full-solution builds: zero warnings and zero errors in both configurations;
- `dotnet format` over both changed C# files; and
- every repository-owned Markdown link plus `git diff --check`.

The serialized commands and project totals are recorded in the FingerTree
[validation guide](../../src/CSharp/docs/FingerTree/validation.md). Benchmarks were not run because
the result is asymptotic and no elapsed-time claim is part of this experiment.

## Deliberate Limits

- Length is fixed after construction. Insert, delete, concat, and split would require a position
  transformation policy for checkpoint correspondence and are outside this ADT.
- There is one checkpoint per version. Rebasing onto an unrelated vector and three-way merging are
  not defined.
- Dirtiness is equality-relative, not write-relative. Writing a comparer-equal value is a no-op.
- Only descriptors are run-sensitive. Enumerating, hashing, serializing, or applying all changed
  payloads still costs at least `Omega(k)`.
- `AcceptDirtyRunAt` and `RevertDirtyRunAt` inherit an `O(log n)` RRB splice; no faster hunk-update
  novelty is claimed.
- Retained mutable reference values can invalidate the index if their equality-observable state is
  changed outside `SetItem`.
- Custom comparers may have side effects; those external effects cannot be rolled back.
- This branch is experimental and does not merge or modify `main`.
