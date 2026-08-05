# Contextual Rank Sequence: Persistent Rank/Select Over Finite Left Context

- Status: Experimental C# reference prototype; scoped novelty candidate, not a priority claim
- Created: 2026-07-25
- Base repository HEAD: `67a67f9f553a9194db9d8dd3cf9c7bd670f9b981`
- Branch: `experimental`
- Namespace: `Durable7.FingerTree`
- Prototype:
  [`ContextualRankSequence.cs`](../../src/CSharp/src/Durable7.FingerTree/ContextualRankSequence.cs)
- Tests:
  [`ContextualRankSequenceTests.cs`](../../src/CSharp/tests/Durable7.FingerTree.Tests/ContextualRankSequenceTests.cs)
- Scope: one C# research implementation; no cross-language shipment commitment

## Result

`ContextualRankSequence<TElement, TMachine>` is a fully persistent sequence that ranks and selects
events whose occurrence is not an intrinsic property of one element. Instead, a finite
deterministic machine decides the event count from the state immediately before that element.

Examples include:

- delimiters outside quoted regions;
- token or record boundaries in a finite-state lexer;
- frame boundaries in an escaped byte stream;
- transitions into a selected protocol or workflow state; and
- Unicode boundary policies after compilation to a finite-state machine.

A normal sum-measured tree can select locally weighted elements, but it cannot assign one fixed
weight to a comma that is a separator in one prefix context and quoted data in another. A normal
automaton scan retains that context but must replay a prefix after an edit. The new structure caches
the complete effect of each subtree for every possible incoming state.

For a fixed machine with `s` states and a sequence of `n` elements:

- full-sequence final state and event count are O(1);
- contextual prefix evaluation, event rank, and event select are O(s log n) amortized;
- indexed access and arbitrary persistent edits are O(s log n) amortized;
- endpoint updates are O(s) amortized;
- concatenation is O(s log(min(n,m))) amortized; and
- a changed version retains O(s log n) new summary storage while sharing untouched structure.

When the machine is fixed, `s` is a constant. The contextual operations are therefore O(log n),
instead of the Θ(n) worst-case replay required by a state-free persistent sequence. Ordinary
sequence operations keep the underlying finger tree's asymptotic bounds after ignoring the fixed
`s` factor. The trade is O(s n), rather than O(n), summary space.

## The Lifted Summary

The machine supplies, for every state `q` and element `x`, one transition:

```text
step(q, x) = (next state, nonnegative event count)
```

A subtree summary `A` stores two functions over the finite state set:

```text
A.next(q)   = state after consuming A from q
A.events(q) = events emitted while consuming A from q
```

It also stores the ordinary element count used for positional search. For adjacent summaries `A`
and `B`, ordered composition is:

```text
(A ⊗ B).next(q)   = B.next(A.next(q))
(A ⊗ B).events(q) = A.events(q) + B.events(A.next(q))
(A ⊗ B).count     = A.count + B.count
```

The empty summary maps every state to itself, emits zero events, and has zero elements. The C#
prototype represents both functions as arrays of length `s`.

### Associativity

Let `A`, `B`, and `C` be adjacent summaries. Both parenthesizations end in
`C.next(B.next(A.next(q)))`. Their event counts are also identical:

```text
((A ⊗ B) ⊗ C).events(q)
  = A.events(q)
  + B.events(A.next(q))
  + C.events(B.next(A.next(q)))

(A ⊗ (B ⊗ C)).events(q)
  = A.events(q)
  + B.events(A.next(q))
  + C.events(B.next(A.next(q)))
```

Thus the lifted object is an ordered monoid and can be used as a measured finger-tree annotation.
This is the same algebraic shape as composing deterministic additive transducers; the contribution
is not a new associativity theorem.

### Why select is one tree descent

Fix an initial state `q0` and requested zero-based event rank `r`. For any prefix summary `P`, use
the predicate:

```text
P.events(q0) > r
```

Transition event counts are nonnegative, so the predicate is false and then true as prefixes grow.
The finger tree's measure-guided locate finds the first element whose inclusion makes it true. The
summary immediately before that element provides:

- the element index (`before.count`);
- the incoming state (`before.next(q0)`); and
- the number of earlier events (`before.events(q0)`).

The event's ordinal within the transition is `r - before.events(q0)`. No binary search and no
prefix replay is needed.

## Public C# Surface

The machine policy is static, so a closed generic type owns one stable finite automaton:

```csharp
public interface IContextualEventMachine<TElement>
{
    static abstract int StateCount { get; }
    static abstract ContextualEventTransition Transition(int state, TElement element);
}
```

The transition returns a valid next state and a nonnegative `long` event count. The sequence surface
is intentionally positional and compact:

```csharp
public sealed class ContextualRankSequence<TElement, TMachine> : IReadOnlyList<TElement>
    where TMachine : IContextualEventMachine<TElement>
{
    public static ContextualRankSequence<TElement, TMachine> Empty { get; }
    public static int StateCount { get; }
    public static ContextualRankSequence<TElement, TMachine> Create(ReadOnlySpan<TElement> items);
    public static ContextualRankSequence<TElement, TMachine> CreateRange(IEnumerable<TElement> items);

    public int Count { get; }
    public bool IsEmpty { get; }
    public TElement this[int index] { get; }

    public ContextualPrefixSummary Evaluate(int initialState);
    public ContextualPrefixSummary EvaluatePrefix(int elementCount, int initialState);
    public long EventRank(int elementCount, int initialState);
    public bool TrySelectEvent(long eventIndex, int initialState,
        out ContextualEventLocation location);

    public ContextualRankSequence<TElement, TMachine> Prepend(TElement item);
    public ContextualRankSequence<TElement, TMachine> Append(TElement item);
    public ContextualRankSequence<TElement, TMachine> Insert(int index, TElement item);
    public ContextualRankSequence<TElement, TMachine> SetItem(int index, TElement item);
    public ContextualRankSequence<TElement, TMachine> RemoveAt(int index);
    public ContextualRankSequence<TElement, TMachine> Concat(
        ContextualRankSequence<TElement, TMachine> other);
    public (ContextualRankSequence<TElement, TMachine> Left,
        ContextualRankSequence<TElement, TMachine> Right) SplitAt(int index);
    public ContextualRankSequence<TElement, TMachine> GetRange(int index, int count);
}
```

`TrySelectEvent` supports multiple events from one transition. Its result includes the input index,
the event ordinal within that transition, the states before and after it, and the transition's total
event count.

### Quoted-delimiter example

```csharp
readonly struct QuotedComma : IContextualEventMachine<char>
{
    public static int StateCount => 2; // 0 outside, 1 inside quotes

    public static ContextualEventTransition Transition(int state, char value) => value switch
    {
        '"' => new(1 - state, 0),
        ',' when state == 0 => new(state, 1),
        _ => new(state, 0)
    };
}

var text = ContextualRankSequence<char, QuotedComma>
    .Create("\"a,b\",c,d".AsSpan());

var total = text.Evaluate(initialState: 0).EventCount; // 2
text.TrySelectEvent(0, initialState: 0, out var first);
// first.ElementIndex == 5; the comma at index 2 is quoted data.

var edited = text.Insert(0, '"');
// text remains unchanged; edited has different contextual ranks after index 0.
```

## Comparison With Alternatives

The table compares shared operations. `s` is the number of machine states and is fixed for the
headline bounds. `p` is the inspected prefix length.

| Operation | Persistent sequence + machine replay | Local-weight measured tree | Dynamic-word product tree | Contextual Rank Sequence |
| --- | ---: | ---: | ---: | ---: |
| Persistent endpoint edit | O(1) amortized | O(1) amortized | usually fixed length | O(s) amortized |
| Persistent positional edit | O(log n) | O(log n) | substitution-focused | O(s log n) amortized |
| Concat/split | O(log n) | O(log n) | generally absent | O(s log n) amortized |
| Final state and total contextual events | Θ(n) | not expressible by local scalar weights | state product available | O(1) |
| Contextual event rank at prefix `p` | Θ(p) | not expressible by local scalar weights | prefix product, not event rank | O(s log n) amortized |
| Select contextual event | Θ(n) worst case | not expressible by local scalar weights | not in the searched surface | O(s log n) amortized |
| Space | O(n) | O(n) | O(n) for fixed monoid | O(s n) |

For fixed `s`, every shared sequence operation is asymptotically no worse than the underlying
persistent measured sequence, ignoring constants, while the contextual queries improve from linear
replay to constant or logarithmic time. If `s` is treated as input rather than a fixed policy, the
structure does not dominate: its edits and storage explicitly carry the `s` factor.

The comparison does **not** claim an improvement over a hypothetical tree already storing exactly
the same lifted arrays and weighted select predicate. Such a tree is this construction under another
name. Nor does it claim that all regular-language dynamic problems need logarithmic updates; the
dynamic-word literature classifies faster bounds for special finite monoids and machine models.

## Prior-Art Audit And Novelty Boundary

The design deliberately combines established components:

- Driscoll, Sarnak, Sleator, and Tarjan formalized persistent data structures and general
  persistence transformations in
  [*Making Data Structures Persistent*](https://www.cs.cmu.edu/~sleator/papers/making-data-structures-persistent.pdf).
- Hinze and Paterson's
  [*Finger Trees: A Simple General-Purpose Data Structure*](https://doi.org/10.1017/S0956796805005769)
  supplies the persistent measured sequence and monotone measure-guided split.
- Frandsen, Miltersen, and Skyum's
  [*Dynamic Word Problems*](https://doi.org/10.7146/dpb.v22i438.6755) studies updates and products
  in a fixed finite monoid, including dynamic prefix products.
- Mohri's
  [*Weighted Finite-State Transducer Algorithms: An Overview*](https://cs.nyu.edu/~mohri/pub/fla.pdf)
  surveys weighted transducer definitions, composition, and applications.
- Unicode Standard Annex #29 explains that text-boundary rules can be compiled to finite-state
  machines and calls out the random-access difficulty explicitly in
  [its text-boundary and random-access sections](https://www.unicode.org/reports/tr29/).

Searches performed on 2026-07-25 included combinations of “persistent sequence/rope,” “dynamic
word,” “finite automaton/transducer,” “weighted,” “rank,” “select,” “event,” “grapheme,” and
“random access,” plus citation traversal from the sources above. They found the ingredients and
adjacent dynamic-language results, but not this exact public synthesis: a fully persistent,
insert/delete/concat-capable sequence with all-start-state additive summaries and one-descent
contextual event rank/select returning the emitting input position.

The defensible novelty claim is therefore narrow:

> The searched primary sources did not expose this exact persistent contextual rank/select
> structure and API combination.

That is a reproducible negative search result, not a historical-priority, patentability, or broad
novelty assertion. The lift is mathematically natural once weighted deterministic transitions and
measured trees are placed side by side. An earlier or differently named instance may exist.

## Correctness And Persistence

### Summary correctness

Induct on the measured tree. A leaf stores exactly the machine's transition for each incoming
state. The empty summary is correct for the empty word. If the summaries for adjacent words `A`
and `B` are correct, executing `AB` from `q` first produces `A.next(q)` and `A.events(q)`, then
executes `B` from that middle state. The composition equations therefore store exactly the result
for `AB`. Every root and prefix summary is correct.

### Rank/select inverse

For a valid event rank `r`, the complete summary emits more than `r` events. Nonnegative weights
make the select predicate monotone, so locate returns the unique first input element whose inclusive
prefix count exceeds `r`. The preceding prefix emits at most `r` events, and adding the selected
transition emits more than `r`; hence the reported ordinal is within that transition. Conversely,
adding the preceding event count to the ordinal reconstructs `r`.

### Full persistence and failure atomicity

All exposed update methods return new finger-tree roots. They never mutate an input version, and
any retained version can be edited to form a new branch. The repository's memoized measured spine
is designed so its amortized bounds survive branching histories; memoization changes only private
cache publication, not logical values.

Leaf transitions are validated while their summary is constructed. The wrapper forces the new
root summary before publishing a successor, so invalid next states, negative event counts, and
checked `long` overflow throw first. Previously reachable versions remain unchanged. Policy side
effects cannot be rolled back and violate the documented purity requirement.

## Validation

The focused xUnit class includes:

- a quoted-delimiter example where identical comma elements have different event status;
- exhaustive enumeration of every word of length zero through seven over quote/comma/ordinary
  input, from both incoming states;
- 4,000 randomized operations selected from retained branches, modeled by independent lists and
  direct machine scans for every state, prefix, and event;
- split/concat/slice and boundary-identity checks;
- transitions that emit multiple events;
- deterministic callback counters: cached prefix ranks invoke zero transitions and each successful
  select invokes exactly one transition after summaries are prepared;
- invalid-state, negative-event, and overflow failure atomicity; and
- concurrent cached readers while independent writers derive branches; and
- ordinary argument and null validation.

The callback counter is complexity evidence, not an asymptotic proof. The monoid derivation and
finger-tree bound transfer supply the proof; tests catch implementation departures.

Final serialized branch validation passed:

- focused `ContextualRankSequence` lane: 7/7 in Debug and 7/7 in Release;
- complete FingerTree project: 731/731 in Debug and 731/731 in Release;
- complete C# solution: 1,537/1,537 in Debug and 1,537/1,537 in Release;
- `dotnet format` over both changed C# files;
- every repository-owned Markdown link; and
- `git diff --check`.

## Deliberate Limits

- Context must be finite. Parenthesis depth, arbitrary indentation stacks, and general parsers do
  not fit unless their state is explicitly bounded.
- Context flows left to right. A right-context rule must be compiled into a streaming machine,
  delayed, or paired with a reverse machine; the prototype does not hide that transformation.
- Event weights must be nonnegative so select remains monotone.
- `long` event totals are checked. A version whose total would exceed `long.MaxValue` is rejected.
- Machine types and semantics must match exactly for concatenation; the closed generic type enforces
  this mechanically.
- The prototype stores one input element per measured leaf. It is not a chunked text rope and makes
  no cache-density claim.
- There is no arbitrary change of machine policy on an existing root. Rebuilding under another
  closed machine type costs O(s n).
- This branch is experimental and does not merge or modify `main`.
