# FingerTree

- Status: Implemented workspace
- Created (UTC): 2026-04-27T18:33:25Z
- Repository HEAD: df8ea08345ca22ba76e6f4fc7e92d0fd41686de3
- Audience: Maintainers implementing and reviewing the finger-tree deque
- Scope: Project layout and validation entry points for `src/DataStructures/FingerTree`

`src/DataStructures/FingerTree` contains the .NET 10 C# preview workspace for `Tools.DataStructures.FingerTree`, a persistent catenable deque backed by a simplified finger tree.

This workspace provides two public finger-tree types. `FingerTree<TElement, TMeasure, TMeasureOps>` is the general Hinze–Paterson **measured** finger tree — a persistent sequence annotated by an arbitrary monoidal measure (supplied through the static-abstract `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` interfaces), whose monotone-predicate `Split` specializes it into a positional sequence (`SizeMeasure<T>`), a mergeable priority queue, an ordered search tree, or an order-statistic tree depending on the measure. It is strict (eager cached measures); see [docs/api-specification.md](docs/api-specification.md#the-general-measured-finger-tree) for its contract and the strictness boundary versus the deque.

`FingerTreeDeque<T>` is the individually tuned sequence/deque (the analogue of Haskell's `Data.Sequence`, kept separate from the general core just as Haskell keeps it separate from `Data.FingerTree`): an immutable `IReadOnlyList<T>` with O(1) endpoint reads, O(log n) worst-case / O(1) amortized endpoint insertion and removal, concatenation logarithmic in the smaller operand (amortized), indexed access and splitting logarithmic in the distance from the nearer end (amortized), and comparer-based sorted search over rightmost-element signposts with a worst-case near-bound comparer-call count. The representation follows the simplified finger tree of Claessen's *Finger Trees Explained Anew, and Slightly Simplified* (digits of one through three elements, middle nodes of two or three children), with element height encoded through polymorphic recursion, leaf counts plus rightmost-leaf signposts cached per node, and the middle subtree of every deep node held behind a memoize-on-first-force suspension — the strict-language strategy from Hinze and Paterson's original paper that makes the amortized bounds hold under fully persistent (branching) version use. The normative API and complexity contract is [docs/api-specification.md](docs/api-specification.md); its complexity columns were realigned with the source papers' amortized claims (worst-case O(log n), amortized sharp under branching persistence, O(1) worst-case endpoint reads) following an adjudicated [spec defect report](docs/api-specification-defect-report-complexity-guarantees.md).

## Layout

- `FingerTree.sln` is the solution entry point.
- `src/Tools.DataStructures.FingerTree/` contains the public library.
  - `Measures.cs` — the `IMonoid<TMeasure>` / `IMeasure<TElement, TMeasure>` static-abstract measure interfaces and the built-in `SizeMeasure<T>`.
  - `FingerTree.cs` — the public general measured finger tree `FingerTree<TElement, TMeasure, TMeasureOps>`.
  - `Internal/Measured/` — the general measured core: `MeasuredElements.cs` (element contract, leaf wrapper, grouping nodes), `MeasuredTree.cs` (the abstract level with all shared operations), and `MeasuredTreeLevels.cs` (empty/single/deep levels).
  - `FingerTreeDeque.cs` — public deque type, argument validation, and the struct enumerator.
  - `FingerTreeDequeResults.cs` — split and pop result record structs.
  - `Internal/` — the deque's tuned finger-tree core: `TreeElement.cs` (measured-element contract and the struct `Leaf<T>`), `Digit.cs`, `Node.cs`, `Tree.cs` (empty/single/deep levels), `MiddleTree.cs` (memoized middle-subtree suspensions and their pending operations), and `TreeOperations.cs` (smart deep constructors, pulls with the paper's `chop`, and concatenation).
- `tests/Tools.DataStructures.FingerTree.Tests/` contains the xUnit suite: for the deque — API contract tests, invariant tests (through `InternalsVisibleTo`), branching-persistence tests, sorted-search edge tests, enumeration/copy tests, randomized model comparisons that validate internal invariants after every step, and complexity-guard tests that pin the near-bound comparer-call counts, the O(1) endpoint allocation behavior, and the flat marginal cost of replaying operations on retained versions; for the general measured tree (`MeasuredFingerTreeTests.cs`) — positional-split-versus-list-slice laws, concatenation order/measure/associativity, and the priority-queue, ordered-search, and order-statistic specializations driven through `Split`.
- `docs/` contains API and algorithm design references.

## Validation

Use the local .NET SDK:

```powershell
dotnet test .\FingerTree.sln
```

The solution builds warning-free with `CS1591`/`CS1573` as errors and the full test suite is expected to pass.
