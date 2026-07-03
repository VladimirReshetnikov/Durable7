# C++ Finger Tree Port — Editorial Notes

- Status: Review companion to `port-plan.md`
- Created (UTC): 2026-06-30T03:24:30Z
- Reviewed (UTC): 2026-06-30T16:54:50Z
- Repository HEAD: c9aef9636783ee5d4be6cee7819bb9a4ad70fb5a
- Audience: Maintainers and AI agents implementing the C++ port; reviewers of `port-plan.md`
- Scope: Non-obvious C#→C++ porting hazards found by reviewing the original `Tools.DataStructures.FingerTree`
  source against the plan, and the rationale for the edits applied to `port-plan.md`

## What this document is

These notes back the revisions made to [`port-plan.md`](port-plan.md). They were produced by reading the C#
library under [`FingerTree/src/Tools.DataStructures.FingerTree`](../../../CSharp/src/Tools.DataStructures.FingerTree)
against the plan and recording every place the proposed C++ design would diverge in **behavior**,
**complexity**, **thread-safety**, or **compilability**, plus places the plan was silent on something that will
bite during porting. Each item cites the C# evidence by `file:line` so an implementer can confirm it.

The plan was already strong: it correctly keeps the tuned deque separate from the general measured tree, picks
`shared_ptr`-based persistence, maps static-abstract interfaces to concepts/policies, keeps the closure-free
struct-predicate fast path, and is honest about amortized-vs-worst-case bounds. The first ten sections below
record the original review findings; section 11 records the follow-up edits from the 2026-06-30 comprehensive
review that expanded the plan into a more executable implementation checklist.

Severity convention: **H** = make-or-break (wrong result, data race, or won't compile); **M** = behavioral or
complexity fidelity / a real footgun; **L** = clarity, hygiene, or test/doc coverage.

---

## 1. The lazy-memoized spine and the C++ memory model (highest risk)

The single most dangerous area is the lazy-memoized spine, because the C# implementation leans on **CLR memory
guarantees that simply do not exist in C++**. The plan's "Lazy Memoization And Thread Safety" section handles the
*middle suspension* well but is silent on several load-bearing details. These are the issues most likely to
produce a port that passes every functional test yet harbors a rare data race.

### 1.1 There are *two* memoization cells per deep node, not one (H)

A deep node of the general measured tree memoizes **two independent things**, each with its own one-shot cell:

1. The **middle subtree** — `MeasuredMiddle` / `LazyMeasuredMiddle._state`, published with
   `Interlocked.CompareExchange` on an `object` reference
   ([`MeasuredMiddle.cs:101-110`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs)).
2. The **deep node's own combined measure** — `DeepMeasuredTree._measureBox`, a *separate* field, published with a
   *separate* `Interlocked.CompareExchange`
   ([`MeasuredTreeLevels.cs:165-197`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs)).

The plan only discusses the middle suspension. An implementer who reads the plan and ports only the middle cell
will store the measure as a plain mutable field and read it racily. Both cells must be ported.

### 1.2 The measure is **boxed** specifically to make it tear-free — and C++ has no boxing (H)

`_measureBox` is typed `object?`, not `TMeasure`
([`MeasuredTreeLevels.cs:165-166`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs)).
This is deliberate. `TMeasure` can be an arbitrary user value type — including a 32-byte struct: the stress suite
uses a 4×`long` `Tearable` as *both element and measure* via `TearableSumMeasure`
([`TearableConcurrencyStressTests.cs:242-262`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/TearableConcurrencyStressTests.cs)).
Boxing reduces the publication of an arbitrarily large measure to a **single atomic reference write**, so a racing
first-reader sees either `null` (recompute — benign) or a pointer to a fully-constructed immutable measure. It can
never see a half-written 32-byte value.

C++ has no boxing. The naive translations are all wrong:

- `std::atomic<TMeasure> _measure;` — for a non-trivially-lock-free measure this is **not lock-free** (a hidden
  mutex, silently defeating the lock-free claim) and is **ill-formed** for measures that are not trivially
  copyable (e.g. a measure containing `std::string`, a `boost::multiprecision` integer, or an `Optional<T>` of
  such).
- `mutable TMeasure _measure;` written non-atomically while another thread reads it — a **data race / UB**, and it
  *tears* for any measure wider than a lock-free word. This is exactly the `Tearable` scenario the stress test
  exists to catch.

The faithful C++ analogue of boxing is to publish a **pointer**, fully constructing the measure first:
`std::atomic<std::shared_ptr<const measure_type>>` (or a `std::shared_ptr<const measure_type>` accessed only via
`std::atomic_load`/`atomic_store`). The `nullptr` state is the "not computed" sentinel that C#'s `null` box
provides. Plan §"Lazy Memoization And Thread Safety" now states this explicitly and forbids the two broken forms.

### 1.3 A plain `std::shared_ptr` is **not** safe to race on (H)

The whole C# concurrency story rests on the CLR guarantee that **reference reads/writes are atomic**: the
publication doc's lock-free patterns use `Volatile.Write(ref cell[0], current)` / `Volatile.Read`
([`persistence-and-concurrency.md:88-123`](../../../CSharp/docs/FingerTree/persistence-and-concurrency.md)), and the
suspension cells use `Volatile.Read` + `Interlocked.CompareExchange` on `object` references.

In C++ `std::shared_ptr` makes only its **control-block refcount** thread-safe. The `shared_ptr` *object itself*
(pointer + control-block pointer) is **not** atomically readable while another thread reassigns it. A producer
doing `cell = next;` while readers do `auto snap = cell;` **on the same `shared_ptr`** is a data race even though
refcounting is internally safe. Every `shared_ptr` that is concurrently published-and-read — the lazy cells, the
measure box, and the publication cell in the `persistent_snapshots` sample / concurrency tests — must be
`std::atomic<std::shared_ptr<T>>` (C++20) or accessed only through `std::atomic_load`/`atomic_store`. A plain
`shared_ptr` field is safe to copy concurrently only *after* a happens-before publication and never *while* it is
being reassigned. The C# `Volatile.Write`/`Volatile.Read` cell maps to an atomic `shared_ptr`, not a plain one.

### 1.4 Memory ordering must be acquire/release, never relaxed (M)

C#'s `Volatile.Read` (acquire) on the fast path and `Interlocked.CompareExchange` (full barrier) on publication
supply the fences that make the *fully-constructed* pointee visible before its pointer
([`MeasuredMiddle.cs:103,108`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs);
[`MeasuredTreeLevels.cs:187,194`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTreeLevels.cs)).
`std::atomic`'s defaults are `seq_cst`, which is correct, but an implementer "optimizing" with
`memory_order_relaxed` would drop the happens-before and let a reader observe a non-null pointer to a
partially-constructed object. The fast-path load and the publishing CAS/store must form an **acquire/release (or
seq_cst) pair**; in particular do not let the fast-path read degrade to a relaxed load, which drops the acquire
edge even when the CAS is seq_cst.

### 1.5 The unstated tear-free invariant (M)

Tear-freedom of large elements/measures does not come from atomics on the *element* storage — it comes from the
fact that immutable storage is **never overwritten**. Every chunk edit allocates a *fresh* array
([`RopeChunk.cs:42-80`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/RopeChunk.cs), remark at
9-13: "backing memory is never mutated after construction"); node children and measures are construction-time
`readonly` fields. The load-bearing invariant the plan should state outright:

> All node, chunk, and measure storage is fully written before its owning `shared_ptr` is observable, and is
> never mutated after publication; each new version is published through a single atomic pointer write.

In C++ this is easy to violate — a `compact()` that resizes a buffer in place, a copy-on-write that mutates a
still-shared buffer, or filling an array element-by-element after its `shared_ptr` is visible would all
reintroduce tearing. `compact()` and copy-on-write must allocate fresh buffers.

### 1.6 Push answers its measure arithmetically; pop must force (M)

This asymmetry is *why* the measure box exists, and it is worth porting consciously. A push suspension can report
its measure in O(1) as `combine(pushed, source.measure)` without forcing the structure; a pop suspension cannot,
because a general monoid has no inverse — there is nothing to subtract
([`MeasuredMiddle.cs:117-129,184-188,214-218,248-252,280-284`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs)).
The pending-operation type must therefore expose a `try_measure_without_forcing(measure&) -> bool` equivalent:
`true` for push (arithmetic), `false` for pop (force then read). An implementer unaware of this will either force
on every measure read (destroying endpoint O(1)) or assume the measure is always cheaply recoverable. This is also
exactly why the tuned deque needs **no** measure box (see §1.8) and is the concrete reason `measure()` on the
general tree is O(1) **amortized**, not worst-case
([`FingerTree.cs:27-37`](../../../CSharp/src/Tools.DataStructures.FingerTree/FingerTree.cs)).

### 1.7 Prefer a CAS'd `shared_ptr` cell over `std::call_once` (M)

After publication the C# cell overwrites `_state` with the computed tree, so the pending operation **and the
already-forced `source` subtree it captured** become unreachable and collectable
([`MiddleTree.cs:77`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/MiddleTree.cs);
[`MeasuredMiddle.cs:108`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredMiddle.cs)).
A `std::once_flag` + stored callable design *keeps the callable alive for the cell's lifetime*, pinning the forced
source subtree. Under branching persistence with many retained versions this is a real space leak. The plan listed
`call_once` and a CAS cell as co-equal; the editorial recommendation makes the `std::shared_ptr<const state_base>`
compare-exchange cell the default, because only it can drop the captured source on publication, and demotes
`call_once` to "considered and rejected for the memoized middle."

### 1.8 The deque is different: eager, invertible size — no measure box, no CAS (M)

Do **not** copy the measure-box machinery into `persistent_deque`. The deque caches its leaf count as a plain
`int` set in the `DeepTree` constructor and never mutated; every construction site derives the new count
arithmetically (size has an inverse — subtraction), so size queries never force the middle and never need a CAS
([`Tree.cs:34,360-372,378`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Tree.cs)). Its only
shared-mutable cell is the `LazyMiddle` suspension. In C++ the deque's size is a plain immutable `std::size_t`
field — safe to read concurrently because it is written-before-publish and never modified (it still relies on the
§1.5 invariant, but needs no atomic). This asymmetry — invertible size measure (deque) vs. non-invertible general
monoid (measured tree) — is the core engineering reason the two cores are kept separate, and reinforces the plan's
insistence that the deque is not a thin alias over `finger_tree<T, size_measure>`.

---

## 2. Polymorphic recursion forces type erasure — it is not optional (H)

Both cores use **polymorphic recursion**: a level's middle is one level deeper in the *type*. For the measured
tree, `MeasuredTree<TElement, TChild, ...>`'s middle is
`MeasuredTree<TElement, MeasuredNode<TElement, TChild, ...>, ...>`
([`MeasuredTree.cs:41-42,298-305`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTree.cs);
[`MeasuredElements.cs:50-57`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredElements.cs)).
For the deque, `Tree<T, TChild>`'s middle is `Tree<T, Node<T, TChild>>`
([`Tree.cs:11-14,344`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Tree.cs)). Each deeper
level instantiates a *fresh, distinct* type while the measure type stays fixed.

In .NET this is fine — generic types are reified lazily at runtime, and a tree of size *n* only ever realizes
O(log *n*) levels. In C++, templates are instantiated at **compile time**: a literal port
(`node<node<node<…>>>`) is **unbounded recursive instantiation and will not compile**.

The plan offers type erasure as "a practical initial design" and lists a `std::variant` representation as an
interchangeable alternative under Open Design Questions. Two corrections:

- **Type erasure is mandatory, not discretionary.** Below the public `finger_tree<T, MeasurePolicy>` boundary,
  every level must operate on a *single erased child type* (e.g. `shared_ptr<const measured_element>` carrying a
  cached `measure_type`). The measure type is level-invariant; only the child shape deepens, and that deepening is
  what must be erased. The phrase "operations monomorphic at the public template boundary" is true of the
  measure/predicate *policy* but must not be read as permission to keep the internal level structure templated.
- **The `std::variant` fallback does not solve this.** A `variant<empty, single<…>, deep<…>>` whose `deep`
  alternative recursively names the next level's node type is the same non-terminating chain. `variant` can
  discriminate the empty/single/deep *shape* at a fixed level, but it does not erase the *element type* the way
  `shared_ptr<const impl>` + a non-template base does. The plan's Open Design Question is reworded accordingly.

This decision propagates: the internal RTTI downcasts the reversible core relies on
(`(RevNode<T>)node`, `(RevLeaf<T>)head` —
[`ReversibleTree.cs:386,491,505`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleTree.cs);
[`ReversibleDeque.cs:145,163`](../../../CSharp/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs)) become
`static_pointer_cast` (fast, invariant-guaranteed) over the erased base, or `std::get` if a per-level variant is
chosen.

---

## 3. The comparator story has three regimes, not one (M)

The plan says, for the derived collections, "store the runtime comparer as part of the wrapper" and "assume the
same comparator type and semantically equivalent comparator state." That is right for the *sorted collections* but
conflates them with the priority queue and interval tree, which work differently. There are three regimes, and the
C++ `Compare` parameter means something different in each:

1. **Order-independent measure + runtime comparer in the predicate** — `SortedBag/Set/Dictionary`. The measure is
   `OrderStatisticMeasure`, whose `Combine` is `(left.Count + right.Count, right.Key ?? left.Key)` — it **never
   touches the comparer** ([`BuiltInMeasures.cs:158-169`](../../../CSharp/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs)).
   The comparer is a *runtime* `IComparer<T>` stored on the wrapper and consulted **only inside the query
   predicate** (e.g. `new KeyAtLeastPredicate<T>(_comparer, item)`,
   [`SortedSet.cs:45,123,353`](../../../CSharp/src/Tools.DataStructures.FingerTree/SortedSet.cs)). This is what
   lets C# choose the order at *runtime* — including `Comparer<T>.Create(lambda)` — without changing the measure
   type. In C++ this means: keep `Compare` out of the measure entirely; the predicate object holds the comparator.
   A template `sorted_set<T, Compare = std::less<T>>` is fine for the common case, but if you want to match C#'s
   runtime-chosen order you must allow a runtime comparator stored on the wrapper and captured by the predicate —
   *without* changing the measure type (which keeps structural sharing across differently-ordered instances of the
   same static type).

2. **Comparison baked into the measure monoid** — `PriorityQueue` and `IntervalTree`. Here `Combine` itself calls
   the comparer: the priority queue's running-min (`MinPriority` uses `Comparer<TPriority>.Default`,
   [`PriorityQueue.cs:36-43`](../../../CSharp/src/Tools.DataStructures.FingerTree/PriorityQueue.cs)) and the
   interval tree's max-high (`CombineMax` uses `Comparer<T>.Default`,
   [`IntervalTree.cs:73-80`](../../../CSharp/src/Tools.DataStructures.FingerTree/IntervalTree.cs)). C# is
   explicit that "a min-priority queue's `Combine` depends on the order, so unlike the sorted collections it
   **cannot accept a runtime comparer**" ([`PriorityQueue.cs:60-63`](../../../CSharp/src/Tools.DataStructures.FingerTree/PriorityQueue.cs)).
   In C++ the `Compare` for these two must be a **compile-time, stateless, default-constructible measure-policy
   ingredient**, reachable from `combine`, and identical in type on both operands of `meld`/`concat`. The same
   comparator must be used by `combine` *and* by the query predicates; if they diverge, max-high accumulation and
   the stabbing search become inconsistent.

3. **Static comparison policy** — the `MaxMeasure<T, TComparison>` / `MinMeasure<T, TComparison>` family and the
   `IComparison<T>` strategy with `DefaultComparison`/`ReverseComparison` adapters
   ([`BuiltInMeasures.cs:178-222`](../../../CSharp/src/Tools.DataStructures.FingerTree/BuiltInMeasures.cs);
   [`Comparisons.cs`](../../../CSharp/src/Tools.DataStructures.FingerTree/Comparisons.cs)). These map directly
   to a C++ `Compare` template policy.

Two notes that fall out: (a) the plan's `priority_queue<…, Compare>` and `interval_tree<T, Compare>` are a
*deliberate enhancement* over the C#, which hard-wires `Comparer<T>.Default` (a max-priority queue in C# is
obtained by reversing the priority type's natural order). Passing `std::greater` for a max-priority queue is the
idiomatic C++ way and should be documented as such. (b) `Comparer<T>.Default.Compare` is itself a *runtime virtual
call* inside those C# measures; C++ `std::less<T>` inlines, so the C++ port is actually faster here — there is no
fidelity cost to making it a static policy.

---

## 4. The reversible deque is strict — no lazy spine (M)

The reversible core is explicitly **strict**: "Size-measured simplified finger tree …, strict (no lazy spine)"
([`ReversibleTree.cs:13-17`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleTree.cs);
[`ReversibleDeque.cs:22`](../../../CSharp/src/Tools.DataStructures.FingerTree/ReversibleDeque.cs)). Its deep
node holds a fully-computed child tree, not a suspension. Consequences for the port:

- **Do not add lazy cells, pending objects, or atomic publication to this subsystem.** The plan's lazy-memoization
  section reads as a cross-cutting requirement for "each deep middle"; it must be scoped to `finger_tree` and
  `persistent_deque` only. Adding suspensions here imports complexity the C# does not have.
- **Its amortized bound is weaker.** Because it is strict, the O(1) amortized endpoint bound holds only for
  single-threaded *linear* use; under branching/persistent reuse of a retained version the endpoint bound is
  O(log *n*) worst case. The C# docs are careful here; the plan was silent. C++ docs/tests must not assert
  O(1)-amortized-under-branching for `reversible_deque`.
- **O(1) `reverse()` depends on *sharing* children, not deep-copying them.** `Mirror()` builds a new node that
  reuses the same prefix/middle/suffix references and just flips a bit
  ([`ReversibleTree.cs:288`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleTree.cs);
  [`ReversibleElements.cs:66,130`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleElements.cs)).
  If the C++ node stores children by value (`std::array<node, N>` / `std::vector<node>`), `Mirror` deep-copies and
  silently makes `reverse()` O(*n*) per touched node. Children must be shared `shared_ptr<const …>` handles. Add a
  Milestone-6 guard asserting `reverse()` is O(1) in allocations and node touches.
- **Orientation-aware accessors allocate on the reversed path.** `LogicalChild`/`LogicalChildren` under reversal
  materialize a fresh mirrored node (and array) per access
  ([`ReversibleElements.cs:134-144`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleElements.cs)).
  The forward path must avoid that allocation entirely (the plan's "check the common orientation first" guidance),
  and the reversed path's cost should be acknowledged — prefer a small by-value "oriented element" carrying an
  orientation bit alongside the handle over allocating a fresh `shared_ptr<node>` per access.
- **Endpoint reads use an opposite-digit identity, not `Mirror`.** Reversed `First`/`Last` read cached
  forward-first/forward-last scalars via the algebraic identity (logical-first of a reversed element = underlying
  logical-last), never materializing a mirror
  ([`ReversibleTree.cs:273-276`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleTree.cs);
  [`ReversibleElements.cs:124-127`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Reversible/ReversibleElements.cs)).
  Implementing reversed `first()` as "mirror the tree then read prefix[0]" would lose O(1)/allocation-free
  endpoints.

(Mixed-orientation concat via the logical accessors — `Glue` building the bridge from `LogicalSuffix`/`LogicalPrefix`
and recursing on `LogicalMiddle` — was reviewed and is faithfully captured by the plan; no change needed.)

---

## 5. Rope chunk ownership and measured-rope two-level navigation (M)

### 5.1 Chunk storage aliasing and `from_chunks` immutability

`Chunk<T>` wraps `ReadOnlyMemory<T>`; `Slice` shares the backing array in O(1)
([`RopeChunk.cs:14-37`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/RopeChunk.cs)). The C++
mapping is `shared_ptr<const T[]>` (or `shared_ptr<const std::vector<T>>`) plus offset/length, sharing the
allocation via the aliasing constructor. Two ownership subtleties GC hid in C#:

- **A one-element slice pins the whole backing buffer** until `compact()` rebuilds it. This is the *reason*
  `compact()` exists; the plan should name it. Guidance: a slice far smaller than its backing buffer may warrant
  an eager copy.
- **`FromChunks` deliberately does not copy** caller memory ([`Rope.cs:347-364`](../../../CSharp/src/Tools.DataStructures.FingerTree/Rope.cs)).
  In C# `ReadOnlyMemory<T>` is a *read-only view*; the caller could still mutate the underlying array. A C++
  `from_chunks` must take ownership of `shared_ptr<const T[]>` under a documented no-external-mutation precondition
  and **must not** accept a bare `std::span`/pointer for retained storage (the persistent rope outlives the call —
  dangling — and `span` gives no immutability guarantee). Provide a copying `from_span`/`create` overload as the
  safe default.

### 5.2 Measured navigation is O(log n) tree descent **plus a bounded in-chunk linear scan**

This is a correctness-shaped subtlety, not just a constant factor. A measured chunk caches its *total* user
measure, but that cache is **not prefix-summable**: a sub-slice cannot derive its measure in O(1) — `Slice`
re-walks the slice to recompute it ([`MeasuredRopeChunk.cs:23-27,52,74-81`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/MeasuredRopeChunk.cs)).
`PrefixMeasure`, the measure-split, and locate-by-measure all isolate the boundary *chunk* via an O(log *n*) tree
descent and then **linearly accumulate the user measure element-by-element within that chunk**
([`MeasuredRope.cs:480-502`](../../../CSharp/src/Tools.DataStructures.FingerTree/MeasuredRope.cs)). An
implementer who assumes the cached chunk measure suffices and stops the split at chunk granularity will get the
wrong boundary element and the wrong measure-before. The plan now states the two-level contract.

### 5.3 `RopeBuilder` is a text builder, not a generic `rope_builder<T>`

The plan frames the builder as generic with a char specialization as a fallback. In the C# there is no generic
builder: `RopeBuilder` is `StringBuilder`-backed, `Rune`-aware, newline-aware, and its primary output is
`MeasuredRope<char, int, NewlineMeasure>` ([`RopeBuilder.cs:21-83`](../../../CSharp/src/Tools.DataStructures.FingerTree/RopeBuilder.cs)).
`NewlineMeasure` and the basic line helpers live in `RopeText.cs`; they are ordinary measured-rope operations, not
the editor-grade extras. So `to_text_rope()` and line/column helpers stay in scope; only the Unicode
scalar/grapheme/newline-style/TextReader conveniences are out. Milestone 7's builder bullet is reworded to a
char/text builder with `to_rope() -> rope<char>` and `to_text_rope() -> measured_rope<char, newline_measure>`.

---

## 6. Counts, widths, and overflow: `int` → `size_t` is not a no-op (M/L)

Every count-bearing measure is 32-bit `int` in C#: `SizeMeasure.Combine` is `left + right` with no checked
arithmetic ([`Measures.cs:81`](../../../CSharp/src/Tools.DataStructures.FingerTree/Measures.cs)), and the
`int Count` recurs in `RankedKey`, `OrderStatisticMeasure`, `IntervalAnnotation`, and `PriorityAggregate`. The
plan says "prefer `std::size_t` … add overflow checks where constructing result sizes could exceed `std::size_t`."
Three things to get right:

- **Signed → unsigned is a hazard, not a free widening.** Audit every count predicate that compares a running
  count to a user index (`count > rank`, `accumulated > k`) and every distance computation (`size - index`,
  `index + 1`, `count_at_most - count_less_than`) for unsigned underflow and signed/unsigned comparison surprises.
  The C# leans on `int` arithmetic and the `(uint)index >= (uint)Count` bounds idiom; a `size_t` port changes the
  value domain.
- **Overflow behavior does not transfer.** C# `SizeMeasure` silently *wraps* in the general tree but the deque
  *throws* `OverflowException`. A uniform `size_t`-with-checks design matches neither unless each `combine` is
  individually guarded. Reframe: pick `size_t` for all counts, treat overflow as a precondition violation
  (the structures cannot hold 2⁶⁴ elements anyway), and do not promise C# wrap/throw parity.
- **Ranks that can be absent need `std::optional<std::size_t>`, not a `-1` sentinel.** `IndexOf`/`IndexOfKey`
  return `-1` when absent ([`SortedSet.cs:122-126`](../../../CSharp/src/Tools.DataStructures.FingerTree/SortedSet.cs));
  `size_t` cannot hold `-1`, and the plan mandates `size_t` for ranks. Map to `std::optional<std::size_t>`.

---

## 7. API-shape mappings the plan under-specifies (M/L)

- **`Try…(out …)` → optional / total result.** The read API is bool-returning `Try` methods with `out` values set
  to `default!` on the false path ([`FingerTree.cs:137-167,259-346`](../../../CSharp/src/Tools.DataStructures.FingerTree/FingerTree.cs)).
  Most map to `std::optional<T>` / `std::optional<result_struct>`. **Exception:** `TryLocate` sets `measureBefore`
  to the **whole-tree measure** on the false path — a meaningful value (a rank query reads the full count when
  nothing matches, [`FingerTree.cs:335-339`](../../../CSharp/src/Tools.DataStructures.FingerTree/FingerTree.cs)).
  A bare `std::optional<locate_result>` would discard it. Use a *total* result that always carries
  `measure_before`, with the boundary element as the optional part:
  `struct locate_result<T, M> { M measure_before; std::optional<T> found; }`. The plan's `try_locate` signature is
  reconciled with this.
- **Value carriers need `operator==` (and sometimes `std::hash`); containers must not.** The public
  `readonly record struct` carriers — `MeasurePair`, `RankedKey`, `Interval`, `IntervalAnnotation`,
  `PriorityAggregate`, the deque result structs, `Optional<T>` — get compiler-generated structural equality and
  hashing, and users key on / compare them. Give the C++ equivalents a defaulted
  `bool operator==(const X&) const = default;` (and `std::hash` only for carriers a user would realistically key
  on, e.g. `interval`). Conversely, the container types (`persistent_deque`, etc.) deliberately omit value
  equality ([`FingerTreeDeque.cs:38-40`](../../../CSharp/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs));
  a defaulted C++ `operator==` on a `shared_ptr`-backed container would compare *pointers*, silently breaking the
  "do not use reference identity for logical equality" contract. Do not default `operator==` on containers.
- **Indexable types keep `operator[]`/`at`.** `FingerTreeDeque<T>` is `IReadOnlyList<T>` with an O(log min(i+1,
  n−i)) indexer, and the sorted collections expose O(log *n*) order-statistic indexing
  ([`FingerTreeDeque.cs:56,112-120`](../../../CSharp/src/Tools.DataStructures.FingerTree/FingerTreeDeque.cs);
  [`SortedSet.cs:109-117`](../../../CSharp/src/Tools.DataStructures.FingerTree/SortedSet.cs)). The plan's
  "start with forward iterators" decision is about *iterator category* and must not be read as dropping indexed
  access; add `operator[]`/`at` to the naming guidelines for the indexable types.
- **The named-operation convenience layer has no home.** Three public static classes —
  `FingerTreeMeasureExtensions` (peek/extract max/min, lower/upper-bound split, split-at-index),
  `FingerTreeProductExtensions` (component-projecting splits/finds), `FingerTreeSumExtensions`
  (`SplitByCumulativeWeight`, `TrySelectByCumulativeWeight`) — wrap the raw `split`/`try_split_find`/`try_locate`
  primitives into the library's actual headline ergonomics. The plan modeled only the primitives. These map to a
  free-function layer co-located in `built_in_measures.hpp` / `product_measure.hpp` / `sum_measure.hpp` and need a
  milestone deliverable, or they will silently be dropped.

---

## 8. Iteration is bimodal — and the lazy walk is a hand-written stack (M)

The plan picks forward iterators and avoids coroutines. Two facts to record:

- **Most of the library iterates by eager materialization, not a lazy walk.** `FingerTree.GetEnumerator` and
  `ToArray` `Flatten` the whole tree into a `List` and return its enumerator
  ([`FingerTree.cs:350-367`](../../../CSharp/src/Tools.DataStructures.FingerTree/FingerTree.cs)), and the
  sorted/priority/interval collections and both ropes delegate to that. So a chunk-by-chunk *lazy* C++ iterator is
  a deliberate *improvement*, not a like-for-like port; and set algebra's "linear merge" actually materializes
  both operands first ([`SortedSet.cs:355-403`](../../../CSharp/src/Tools.DataStructures.FingerTree/SortedSet.cs))
  — its "preserve structural sharing" is a single-element/range property (split+concat), not a set-algebra one.
- **The one genuinely lazy walk is hand-written.** Only the deque has a lazy struct enumerator over an explicit
  O(log *n*) frame stack. A C++ lazy forward iterator over a persistent `shared_ptr` spine must reproduce that
  stack by hand (no `yield return` to lower it) **and** be copyable/comparable for `begin()==end()` multipass —
  strictly harder than the C# struct enumerator. Decide per family whether `begin()` walks lazily or returns
  iterators over a materialized buffer, and document the allocation/complexity contract either way.

---

## 9. Build/toolchain and documentation hygiene (L)

- **`/WX` over `/std:c++latest` is itself a stability risk.** The library is intentionally template- and
  concept-heavy. Combining warnings-as-errors with a draft C++26 mode on an Insiders MSVC promotes
  standard-library *header* warnings (outside the project's control, and changing across toolchain bumps) to hard
  errors. Keep `/WX` but exclude system/vcpkg headers from it via MSVC external-header flags
  (`/external:anglebrackets /external:W0`, `/external:I <vcpkg-include>`), so `/WX` gates the project's own code
  only.
- **Stale build path in the earlier draft.** The original command used an absolute Codex worktree path
  (`C:\Users\vresh\.codex\worktrees\23eb\...`) that was not portable to this worktree. The plan now uses a
  repository-relative `cd src\Cpp\FingerTree` plus absolute paths only for the Visual Studio-bundled tools.
- The original provenance `Repository HEAD: 8f20c4d…` was correct for the first draft because it recorded the
  parent commit the plan was authored against. This review updates the plan and editorial headers to the current
  pre-edit repository head (`c9aef963…`) and adds an explicit `Reviewed (UTC)` field, so future edits can
  distinguish creation provenance from later review provenance.

---

## 10. Validation, benchmark, and sample gaps (M/L)

The C# suite is a three-tier example + property + model-based-command design with dedicated concurrency,
allocation, and complexity-guard families. The plan's validation lists are lighter. Specific gaps:

- **Tearable concurrent-first-read test (H-adjacent).** Milestone 3 says only "concurrent first-force read tests."
  Replace with an explicit `Tearable` analogue: a multi-word struct with an `is_intact()` tear detector used as
  *both element and measure*, with (a) many-thread reads of one immutable tree, (b) concurrent *first* reads of a
  fresh, never-forced tree validating the forced spine measure **and** every element are intact, (c) lock-free
  single-producer/multi-consumer publication over an atomic `shared_ptr`, and (d) a branching+reading soak off a
  retained base ([`TearableConcurrencyStressTests.cs`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/TearableConcurrencyStressTests.cs)).
  This is the only test that exercises §1's atomic-measure-publication and tear-freedom invariants.
- **Model-based command tests.** `ModelBasedCommandTests` generates *sequences of operations* and shrinks to a
  minimal failing *program* — the most powerful tier and the one most likely to surface lazy-spine reconstruction
  bugs. The plan tops out at "randomized model histories" (data shrinking). Commit to a stateful/command testing
  library (RapidCheck state machine, or equivalent) and add the tier to Milestones 3–7.
- **Complexity guards** (allocation-flatness, no-force endpoint reads, comparer-call counts, size-independent
  marginal cost on a retained version): these are the *normative* evidence for the amortized-under-branching claim
  ([`FingerTreeDequeComplexityGuardTests.cs`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/FingerTreeDequeComplexityGuardTests.cs);
  [`AllocationFreeReadTests.cs`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/AllocationFreeReadTests.cs);
  [`ZeroClosureNamedOpTests.cs`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/ZeroClosureNamedOpTests.cs)).
  Add a counting `operator new`/`delete` and an operation counter in Milestone 1, and make these guards
  non-optional for the general tree (M3) too, not just the deque.
- **`try_locate` vs `try_split_find` equivalence across every threshold** ([`TryLocateTests.cs`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/TryLocateTests.cs))
  — the read-only locate path must return the same boundary and measure-before as the reconstructing split.
- **Benchmarks omitted:** `PersistenceBenchmarks` (branching-flatness — the highest-value guard for the whole
  lazy-suspension design), `MeasuredRopeBenchmarks` (offset↔line navigation), `ReversibleOverheadBenchmarks` (the
  constant-factor cost that justifies keeping `reversible_deque` a separate type), and an interval-tree query
  benchmark.
- **Samples:** C# ships three (Tour, Showcase, Editor). Editor is correctly dropped (text extras). But Tour ≠ the
  plan's abstract `persistent_snapshots`: Tour is a rope/measured-rope text-buffer persistence-and-concurrency
  demonstration (undo/redo cursor, line/column navigation, lock-free snapshotting), and rope/measured_rope are in
  scope. Make `persistent_snapshots.cpp` concretely the Tour. Each sample should expose a testable
  `run(std::ostream&)` seam smoke-tested via CTest ([`SampleSmokeTests.cs`](../../../CSharp/tests/Tools.DataStructures.FingerTree.Tests/SampleSmokeTests.cs)).

---

## 11. Follow-up edits from the comprehensive plan rewrite (M/L)

These edits were made after re-reading the existing plan, the C# public surface, `RopeText`, the Tour sample, and
the current local toolchain state.

### 11.1 `std::once_flag` was still listed as acceptable despite being rejected later (M)

The plan's dependency section said to use "`std::atomic`, `std::once_flag`, or a small custom atomic lazy cell" for
memoized suspensions, while §1.7 above rejects `std::call_once` for the middle suspension because it retains the
callable and therefore the already-forced source subtree captured by a pending operation. That was not merely a
wording nit: an implementer reading top-down could reasonably choose `std::once_flag` before reaching the later
rejection. The plan now makes the compare-exchange `atomic<shared_ptr<const state_base>>` lazy cell the only
recommended middle-suspension design, and says explicitly that `std::once_flag`/`std::call_once` are not for this
cell.

### 11.2 Toolchain discovery needed to match this worktree, not an earlier snapshot (L)

The previous baseline said CMake and Ninja were available through Visual Studio, but the current plain PowerShell
has `cl.exe` on `PATH` and does **not** have `cmake.exe` or `ninja.exe` on `PATH`. The bundled tools do exist under
Visual Studio 18 Insiders:

- `...\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- `...\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`

The plan now gives absolute-path build commands and tells presets to set `CMAKE_MAKE_PROGRAM`. This removes a
first-command failure mode without changing the compiler decision.

### 11.3 The text scope boundary was too coarse: line helpers are in scope (M)

The initial plan correctly excluded editor-grade text extras, but its wording risked excluding all text
navigation. That would undercut the C# Tour sample and an important measured-rope use case. `RopeText.cs`
separates a simple newline-count measure and line/column helpers from `RopeTextExtras.cs`: `LineCount`,
`LineOfOffset`, `LineStartOffset`, `LineColumnOf`, `OffsetOf`, `GetLine`, and `Lines` are all ordinary
measured-rope operations over `MeasuredRope<char, int, NewlineMeasure>`
([`RopeText.cs:91-174`](../../../CSharp/src/Tools.DataStructures.FingerTree/RopeText.cs)). The Tour sample
uses exactly those operations for its second act
([`TourProgram.cs:57-67`](../../../CSharp/samples/Tools.DataStructures.FingerTree.Tour/TourProgram.cs)).

By contrast, Unicode scalar indexing, grapheme segmentation, newline-style detection, CR-stripping
`GetLineText`/`LinesText`, and `TextReader` adapters are the editor/convenience layer that can wait
([`RopeTextExtras.cs`](../../../CSharp/src/Tools.DataStructures.FingerTree/RopeTextExtras.cs);
[`RopeText.cs:73-89`](../../../CSharp/src/Tools.DataStructures.FingerTree/RopeText.cs)). The plan now keeps
`newline_measure`, the basic line helpers, and the char/text builder in Milestone 7, while explicitly leaving the
extras out.

### 11.4 The plan needed a public API checklist, not only architecture (M/L)

The architecture sections were detailed, but a C++ implementer still had to infer public coverage from the C#
source. The rewrite adds a "Public Surface Checklist" with the expected first-wave surface for:

- the raw measured tree;
- the tuned deque and its sorted-search helpers;
- sorted bag/set/map, including optional ranks instead of `-1`;
- priority queue, including FIFO equal-priority behavior without an insertion ordinal;
- interval tree duplicate/removal semantics;
- rope, measured rope, and text helpers.

This was added because the C# surface is broad and easy to accidentally narrow. The relevant source files are the
public wrappers (`FingerTree.cs`, `FingerTreeDeque.cs`, `SortedBag.cs`, `SortedSet.cs`, `SortedDictionary.cs`,
`PriorityQueue.cs`, `IntervalTree.cs`, `Rope.cs`, `MeasuredRope.cs`, and `ReversibleDeque.cs`), whose member lists
show that the port is not just the two cores plus a couple of examples.

### 11.5 Iterator, exception, and allocator policy moved out of "open questions" (M/L)

The previous "Open Design Questions" section left iterator allocation behavior, exception guarantees, and
allocator support too vague. The rewrite does three things:

- Resolves the iterator category to forward iterators, keeps indexed access separate from iterator category, and
  requires a genuinely lazy deque iterator because the C# deque has an explicit stack enumerator while most other
  families currently materialize.
- Sets strong exception safety as the target for public operations. This follows from immutable snapshots but must
  be restated for lazy cells: compute fully before CAS publication, and if forcing throws, leave the cell pending
  for a retry rather than publishing a partial state.
- Defers public allocator customization but requires allocation helper choke points (`make_node`, `make_leaf`,
  `make_lazy_cell`) so the implementation does not paint itself into a corner.

These are not algorithmic changes; they prevent first-wave implementation choices from silently defining weak API
contracts that would be hard to tighten later.

---

## Appendix: concerns checked and dismissed

For the record, these candidate issues were investigated and found *not* to require a plan change:

- **"Concat forces both middles, breaking the lazy bound"** — `Glue` does force both operand middles
  ([`MeasuredTree.cs:139`](../../../CSharp/src/Tools.DataStructures.FingerTree/Internal/Measured/MeasuredTree.cs)),
  but that is the intended `app3`/glue behavior; descent depth is bounded by the shallower operand. Not a defect.
- **Static-init-order (SIOF) for the per-level `Empty` singletons** — with the mandated type erasure the per-level
  empties collapse, and a Meyers-singleton/function-local-`static` idiom is the standard, SIOF-safe mapping. No
  special plan handling needed beyond using function-local statics.
- **"Sorted collections' template `Compare` can't express a runtime comparer"** — already adequately covered by
  the plan's "store the runtime comparer as part of the wrapper" guidance; §3 above sharpens *why* (the measure is
  comparer-independent) rather than contradicting it.
- **Component-projection closure-free predicate API** — `FingerTreeProductExtensions` already ships both a `Func`
  overload and a `struct TPredicate` overload; the plan's dual-path predicate model covers it.
- **Internal struct-predicate library needs its own header** — the plan's `detail/` and `measure_predicates.hpp`
  already provide a home; the internal predicates are an implementation detail of the named-op layer.
