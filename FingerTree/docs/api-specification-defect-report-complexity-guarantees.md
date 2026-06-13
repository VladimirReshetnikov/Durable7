# Spec Defect Report: Complexity Guarantee Columns In The Finger Tree Deque API Specification

- Status: Adjudicated — approved (R2) with an endpoint-access caveat; specification amended accordingly
- Created (UTC): 2026-06-12T04:26:32Z
- Repository HEAD: 18a0d238487bf978e847af54a738a446699e2f16
- Audience: Specification owner and maintainers of `Tools.DataStructures.FingerTree`
- Scope: Defects in the [API specification](api-specification.md) complexity model and their remediation
- Related docs:
  - [Finger Tree Deque API Specification](api-specification.md) (the document under review)
  - [Finger trees: a simple general-purpose data structure](<Finger trees - a simple general-purpose data structure/Finger trees - a simple general-purpose data structure.md>) (Hinze and Paterson 2006, "the original paper")
  - [Finger Trees Explained Anew, and Slightly Simplified](<Finger Trees Explained Anew, and Slightly Simplified.md>) (Claessen 2020, "the simplified paper")
  - [Haskell containers 0.8 `Data.Sequence`](containers-0.8/src/Data/Sequence.hs)

## Summary

The specification's complexity table places the sharp "distance to the nearer end" bounds for the split family (`SplitAt`, `SplitItemAt`, `InsertAt`, `RemoveAt`, `SplitAtSortedLowerBound`, `SplitAtSortedUpperBound`) and the log-of-smaller-operand bound for `Concat` in the **worst-case** column. Three independent arguments show those placements are defective:

1. They are **unsatisfiable by the representation the specification itself prescribes** (a strict simplified finger tree maintaining the paper's potential discipline), and an adversarial multi-lens review of the initial implementation confirmed the violations empirically on shapes produced by ordinary public use.
2. They are **internally inconsistent with the specification's own endpoint rows**: `RemoveAt(0)` cannot be cheaper than `RemoveFirst`, which the specification caps at worst-case O(log n) and defends at length in "Why Endpoint Insertions And Removals Are Not Worst-Case Constant".
3. They are **stronger than the claims of the source papers being matched**: both the original paper and `Data.Sequence` state these bounds as *amortized*, with worst-case Θ(log n), and the original paper obtains persistence-robust amortization specifically through memoized lazy suspension of the middle subtrees.

The recommended remediation is twofold: adopt the original paper's strict-language strategy in the implementation (memoize-on-first-force suspension of the middle subtree of each deep node) so the amortized bounds hold **even under branching persistent use**, exactly as the original paper claims; and re-scope the affected table cells so that the sharp bounds appear in the amortized column with worst-case Θ(log n), exactly as the source papers state them. With both parts adopted, the deque matches the theoretical complexity of the original paper, including its persistence claim, with no downgrade.

## Defect 1: The Worst-Case Column Is Unsatisfiable For The Prescribed Strict Representation

The specification's "Internal Design Obligations" section prescribes the simplified-paper structure with strict nodes and the exact amortization discipline of the simplified paper: endpoint overflow must leave a `Two` digit and push a `Node2`; underflow from a `One` digit must pull from the middle, chopping a pulled `Node3` without recursion. That discipline is what *creates* the shapes that defeat the sharp worst-case bounds:

- A deque built only with `AddLast` (which includes every `Create`/`CreateRange` result) has a `One` prefix digit at every level, and every middle contains only `Node2` nodes, because `Snoc` overflow never touches the prefix and only ever pushes `Node2` nodes. On that shape, `SplitItemAt(0)`, `RemoveAt(0)`, and `SplitAt(1)` empty the top prefix digit, and the smart constructor (`deepL`) must pull the head node from the middle; with a `One` prefix and a `Node2` head at every level, that pull cascades through the entire spine: Θ(log n) time and allocation where the table requires O(1 + log nearIndex(0, n)) = O(1).
- Symmetrically, an `AddFirst`-only history periodically reaches states in which every spine level carries a `Three` prefix (the binary-counter all-ones states). Concatenating a singleton onto such a deque bottoms out the `glue` recursion after one level and then pushes carried nodes with `Cons`, which cascades through every consecutive `Three` prefix: Θ(log n) for min(n, m) = 1 where the table requires O(log(min(n, m) + 1)) = O(1).

These are not contrived adversarial inputs; they are the default shapes of the structure under the most common construction patterns. Empirical measurements on the initial implementation (which follows the prescribed discipline exactly and passes all functional contract tests) confirm linear-in-depth growth: `SplitItemAt(0)` and `RemoveAt(0)` allocations grow from 568 B at n = 2^8 to 2,104 B at n = 2^24 (≈96 B per level), and worst-observed singleton `Concat` allocations grow ≈136 B per level peaking at counts of the form 2^k − 4, while the control `SetItem(0)` — whose O(1) worst-case claim is genuinely satisfiable and satisfied — stays flat at 88 B across all sizes.

Two facts sharpen the defect. First, the *descent* phase of split and search does meet the sharp bound worst-case; only the *reconstruction* phase (pulls from suspended-empty boundary digits, and `glue` carry pushes) cascades — and those cascades telescope, so each call is O(log n) total, never O(log² n). Second, the cascades are exactly the operations the amortization discipline pays for: each cascade level converts a dangerous digit (`One`/`Three`) to a safe `Two`, so the sharp bounds hold *amortized* over linear version histories. The defect is purely that the table asserts them as per-call worst-case.

## Defect 2: The Table Is Internally Inconsistent

`RemoveAt(0)` and `RemoveFirst()` are the same abstract operation. The table assigns `RemoveAt` worst-case O(1 + log nearIndex(i, n)), which is O(1) at i = 0, while assigning `RemoveFirst` worst-case O(log n) — and the section "Why Endpoint Insertions And Removals Are Not Worst-Case Constant" argues at length that worst-case O(1) endpoint removal is out of scope for this representation. Both cells cannot be right; the endpoint rows and the prose section are correct, so the sharp `RemoveAt`/`SplitItemAt`/`InsertAt`/`SplitAt` worst-case cells are wrong at and near the boundary indexes. The same argument applies to `SplitAt(1)` versus `RemoveFirst`, and to the sorted bound-split rows at boundary result indexes.

## Defect 3: The Sharp Worst-Case Claims Exceed The Source Papers

The stated project goal is to match the theoretical complexity of the original paper. The original paper does not claim these bounds as worst-case:

- Abstract: "access to the ends in **amortized** constant time, and concatenation and splitting in time logarithmic in the size of the smaller piece" — and the complexity treatment (§3.2 and the appendix) is a debit analysis in Okasaki's framework, i.e., amortized.
- §3.2: "Each deque operation may recurse down the spine of the finger tree, and thus take **Θ(log n) time in the worst case**. However, it can be shown that these operations take only Θ(1) **amortized** time, **even in a persistent setting**."
- §3.2: "The same bounds hold in a persistent setting **if subtrees are suspended using lazy evaluation**." The persistence claim is conditional on memoized laziness; it is not a property of the plain strict structure.
- `Data.Sequence` (the production realization of the paper) states at the top of its module documentation: "An **amortized** running time is given for each operation" — every bound in that module, including `splitAt`'s O(log(min(i, n − i))) and `(><)`'s O(log(min(n₁, n₂))), is an amortized bound.
- The simplified paper proves the sequential amortized bounds for the 1–3 digit variant and explicitly defers the persistent claim: "We believe the same argument holds for the simpler version of finger trees presented here, but just like Hinze and Paterson, we do not provide a proof here either."

A table cell that demands worst-case O(1) near-boundary splits therefore asks for more than the papers deliver even in Haskell. Achieving such bounds genuinely worst-case is the domain of the Kaplan–Tarjan-style structures that the specification's own scope section excludes.

## Can Strict C# Match Lazy Haskell Here? Yes — By The Paper's Own Recipe

No fundamental gap separates an eager language from a lazy one for this data structure, because the laziness that the analysis needs is not pervasive call-by-need; it is a bounded, local technique — the *memoized suspension* — and the foundational treatments already live in a strict language: Okasaki's framework and implementations are written in Standard ML with explicit `$`-suspensions. The original paper says precisely how much laziness is required (§3.2):

> "In a strict language that provides a lazy evaluation primitive, we need only suspend the middle subtree of each Deep node, so only Θ(log n) suspensions are required in a tree of size n."

C# provides everything needed to build that primitive: a one-field cell holding either the computed middle tree or a pending-operation object, forced on first use and published with an interlocked compare-exchange. Memoization is what makes amortized bounds survive *branching* persistence: once any version forces a shared suspension, every other version sharing it reads the cached result. Racing readers may duplicate a bounded amount of work, which the specification's thread-safety section already permits.

What a **fully strict** representation (no suspensions, no benign internal mutation) cannot do, in C# or any language, is make amortized bounds robust under branching persistence: a pure, strict, deterministic operation re-applied to the same retained version necessarily repeats the same work, so an adversary replaying the expensive operation on a saved instance pays the worst case every time. This is the precise technical content behind the specification's existing option of a "downgraded public guarantee... amortized only for single-threaded linear use of versions". The choice is therefore not C# versus Haskell; it is suspension-with-memoization versus none.

One genuine trade-off should be recorded: the strict representation delivers the sharp near-index bounds for *read-only* operations (indexer, `TryGetItem`, sorted bound queries) as true worst-case bounds, because no reconstruction is involved. With suspended middles, a read may additionally force pending spine work, so those time bounds become amortized as well (the comparer-call counts of the sorted helpers remain worst-case sharp, since forcing performs no comparisons). This matches `Data.Sequence`, whose blanket qualifier is amortized for exactly this reason.

## Remediation Options

### R1: Re-scope the table only (keep the fully strict implementation)

Move the sharp split/concat bounds to the amortized column with an explicit "single-threaded linear use of versions" qualifier; set the worst-case cells to O(log n) (O(log(n + m)) for `Concat`). Cheapest change; leaves the deque short of the original paper's persistence claim, which conflicts with the stated project goal.

### R2 (recommended): Adopt the paper's strict-language laziness recipe, and re-scope the table to the papers' own claims

Implementation part — exactly the original paper's prescription plus the memoization needed for persistence:

- Represent the middle subtree of each deep node as a suspension cell: either an already-computed `Tree` or a small immutable pending-operation object (`push front/back of a node onto a suspended middle`, `pop front/back of a computed middle`). Forcing runs the pending operation and publishes the result with `Interlocked.CompareExchange`; losers of a benign race adopt the winner's result, so all sharers converge on one canonical subtree. Θ(log n) suspensions per tree, as the paper states.
- Thread sizes arithmetically: every deep node stores its total leaf count computed from known quantities (never by forcing the middle), so `Count`, indexing arithmetic, and emptiness checks never force. Endpoint reads, endpoint digit replacement (`SetItem` at 0 and Count − 1), and signpost reads from digits remain force-free and keep their O(1) worst-case guarantees.
- Suspension points: endpoint overflow pushes, the recursive pop inside underflow pulls (the `Node2` arm of the paper's `more0`; the `Node3` chop arm stays eager because it is O(1)), and the pulls performed by split reconstruction. The `glue` recursion itself can stay eager: its per-level work is O(1) over min-depth levels, and its carry pushes ride the now-lazy `Cons`/`Snoc`.
- Suspension creators must force their source middle first and defer only the single new operation, following the original paper's note "forcing the middle subtree m in the recursive case of '◁' ... to avoid building a chain of suspensions. According to the above debit analysis, this suspension has been paid for by this time, so the amortized bounds would be unaffected." This keeps every suspension exactly one deferred operation deep and bounds forcing recursion by tree depth; a bare-suspension variant that skips the force would make `AddFirst`/`AddLast` O(1) worst-case but would let unread push runs build O(n)-deep suspension chains whose first force overflows the call stack. `AddFirst`/`AddLast` therefore remain O(log n) worst-case and O(1) amortized, matching the specification's existing endpoint rows.
- Debit accounting transfers from the original paper (§3.2: middle suspension of each deep node carries as many debits as the node has safe digits) with the simplified paper's safe/dangerous classification (`Two` safe; `One`/`Three` dangerous). The simplified paper records the persistent argument as believed-but-unproven in the literature; the specification's option 1 therefore applies: ship the memoized-suspension strategy together with branching-persistence stress tests, including allocation-flatness guards that fail if a replayed operation on a retained version stops being O(1) marginal.

Specification part — phrase the affected cells exactly as the source papers do:

| Operation | Worst-case time | Amortized time (persistent) |
| --- | ---: | ---: |
| `First`, `Last`, `TryPeekFirst`, `TryPeekLast` | O(1) | O(1) |
| `AddFirst`, `AddLast` | O(log n) | O(1) |
| `RemoveFirst`, `RemoveLast`, `PopFirst`, `PopLast` | O(log n) | O(1) |
| `Concat` / `AddRange(FingerTreeDeque<T>)` | O(log(n + m)) | O(log(min(n, m) + 1)) |
| `InsertAt`, `SplitAt` | O(log n) | O(1 + log(nearSplit + 1)) |
| `RemoveAt`, `SplitItemAt` | O(log n) | O(1 + log nearIndex) |
| `SplitAtSortedLowerBound`, `SplitAtSortedUpperBound` | O(log n) | O(1 + log nearBound) |
| Indexer get, `TryGetItem`, `SetItem`, `UpdateAt` | O(log n); O(1) at i ∈ {0, n − 1} | O(1 + log nearIndex) |
| `SortedLowerBound` et al. (comparer calls) | O(1 + log nearBound) | same |

with endpoint reads and endpoint replacement keeping O(1) worst-case (the first and last elements live in the top digits and the middle leaf count is cached arithmetically, so they never force a suspension), the amortized column holding under fully persistent (branching) use per the original paper, and a note that read-only operations were worst-case sharp in the fully strict variant and are amortized-sharp in the suspended variant.

### R3: Worst-case structures (rejected)

Kaplan–Tarjan-style recursive-slowdown catenable deques achieve worst-case bounds in strict languages with no laziness, but the specification's scope section already excludes them as a different engineering contract, and they sacrifice the measured-split simplicity this project exists to provide.

## Requested Decision

Approve R2 (implementation already proceeding under the stated project goal of matching the original paper), and approve the corresponding re-scoping of the specification's complexity table and of the "Worst-Case Versus Amortized Guarantees" section so the table's columns state exactly the bounds the original paper proves and `Data.Sequence` documents.

## Adjudication (2026-06-12)

The owner approved R2 with one caveat: **accessing — as opposed to inserting or removing — the first and last elements must be O(1) worst-case.** The implementation already satisfies this (endpoint reads resolve within the top prefix/suffix digits and the arithmetically cached middle leaf count, never forcing a suspension), and a deterministic allocation-flatness guard test (`EndpointAccess_NeverForcesSuspendedSpine`) now pins it. The specification has been amended accordingly: the complexity table marks the endpoint-access O(1) worst-case guarantee with footnote ‡ on `First`/`Last`/`TryPeek*` and on the indexer/`TryGetItem`/`SetItem`/`UpdateAt` rows, the split-family and `Concat` rows carry the amortized sharp bound with O(log n) worst-case (footnote †), the sorted-search rows state the worst-case comparer-call count with amortized forcing noted (footnote §), and the "Worst-Case Versus Amortized Guarantees" and "Internal Design Obligations" sections record the memoized middle-subtree suspension strategy as the realization of the first option for advertising persistent amortized bounds. This report is retained as the rationale of record for those amendments.
