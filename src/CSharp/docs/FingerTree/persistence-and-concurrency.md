# Persistence and concurrency

- Created (UTC): 2026-06-13T00:00:00Z
- Repository HEAD: b71609d90
- Audience: Users adopting the library for snapshotting, undo, or multi-threaded workloads
- Scope: How immutability gives every type in `Durable7.FingerTree` cheap snapshots and lock-free concurrency

Every public type in this library — the deque, the reversible deque, the general measured tree, the sorted
collections, the priority queue, the interval tree, and both ropes — is **immutable and persistent**: an
operation never changes its receiver; it returns a *new* version that shares almost all of its structure with
the old one. Two properties fall out of that for free, and this document shows both as worked patterns. Each
pattern is a runnable test in
[`PersistenceConcurrencyExamplesTests.cs`](../../tests/Durable7.FingerTree.Tests/PersistenceConcurrencyExamplesTests.cs),
so the code below is guaranteed to compile and behave as described.

## Snapshots are free; edits share structure

Keeping a snapshot is just keeping a reference — O(1), zero allocation — and it can never be invalidated, because
nothing mutates it. An edit allocates only the O(log n) of the structure that actually changes; the rest is
shared with the version it came from.

```csharp
var original = Rope<int>.Create(Enumerable.Range(0, 1_000_000).ToArray());
var snapshot = original;                 // O(1): just a reference

var edited = original.Insert(500_000, -1);   // allocates kilobytes, not a 4 MB copy

// edited has the change; snapshot (and original) are untouched and still valid.
```

Measured: inserting into a 1,000,000-element rope allocates a few kilobytes (it rebuilds only the path to the
edited chunk), not the megabytes a defensive copy would cost. Holding a thousand historical versions costs a
thousand small deltas, not a thousand full copies.

## Undo and redo are a cursor over retained versions

Because each edit yields a new version and old versions stay valid, an undo history needs no diffing and no
inverse operations — just a list of versions and a cursor. Redo is moving the cursor forward; a new edit after
an undo drops the redo branch.

```csharp
var history = new List<MeasuredRope<char, int, NewlineMeasure>> { "".ToTextRope() };
var cursor = 0;

void Apply(Func<...> edit)
{
    var next = edit(history[cursor]);
    history.RemoveRange(cursor + 1, history.Count - cursor - 1);   // drop the redo branch
    history.Add(next);
    cursor++;
}

string Undo() => history[cursor = Math.Max(0, cursor - 1)].AsString();
string Redo() => history[cursor = Math.Min(history.Count - 1, cursor + 1)].AsString();
```

The same shape works for any type in the library — a `SortedSet<T>` of filters, a `FingerTreeDeque<T>` of events,
a `MeasuredRope<char, …>` document — because they are all persistent. (A production editor would cap or coalesce
history; the structural sharing keeps even an unbounded history affordable.)

## Many threads can read one version with no locks

An immutable value has no mutable state to protect, so any number of threads can read the same version
concurrently — indexed reads, enumeration, slicing, measure queries — with no locks, no copies, and no chance of
a data race.

```csharp
var rope = Rope<int>.Create(/* … */);
Parallel.For(0, 64, _ =>
{
    _ = rope[50_000];          // indexed read
    _ = rope.ToArray();        // full enumeration
    _ = rope.Slice(40_000, 2_000);   // sub-rope (shares structure)
});
```

This is the headline advantage of persistent data structures for concurrent software: sharing read-only data
across threads needs no synchronization at all.

## Publishing new versions is lock-free

When one producer evolves the data and many consumers read it, you do not need a lock around the structure — only
an atomic publish of a single reference. The producer builds the next immutable version privately and publishes
it with one volatile (or interlocked) write; consumers read that reference and operate on the complete snapshot
they captured. A consumer never observes a half-applied edit, because a version is fully built before it is ever
published and is never mutated afterward.

```csharp
var cell = new[] { Rope<int>.Empty };   // one shared, atomically published reference

// producer
var current = Rope<int>.Empty;
for (var i = 0; i < n; i++)
{
    current = current.AddLast(i);
    Volatile.Write(ref cell[0], current);   // publish atomically
}

// any number of consumers, concurrently
var snapshot = Volatile.Read(ref cell[0]);   // a complete, consistent version
Process(snapshot);                           // read freely; it will never change underfoot
```

The example test runs four readers against a writer publishing 4,000 successive versions and asserts that every
snapshot a reader observes is a complete, consistent prefix — never a torn intermediate state. For multi-writer
scenarios, compose with `Interlocked.CompareExchange` on the same reference (read-modify-publish, retrying on
contention); the structures themselves need no further synchronization.

## Even first reads are thread-safe

The general measured tree and both ropes use a *lazy-memoized spine*: a deep node's middle subtree and its
combined measure are computed on first access and cached. That caching is done with atomic compare-exchange, so a
freshly built structure read concurrently from many threads is still safe — racing "first" reads recompute a
bounded amount of work independently and converge on a single published result, and no reader can observe a
partially initialized node.

```csharp
var fresh = Rope<int>.CreateRange(/* … */);   // never read yet
Parallel.For(0, 32, _ => Assert.Equal(expected, fresh.ToArray()));   // all threads agree
```

In other words, immutability here is not merely "safe to share once warmed up" — it is safe to share *immediately*,
including the lazy work the persistent amortized bounds depend on.

## Summary

| Property | What it gives you | Cost |
|----------|-------------------|------|
| Snapshot = reference | unlimited cheap versions, undo/redo, time travel | O(1), zero allocation |
| Edit shares structure | a thousand versions ≈ a thousand small deltas | O(log n) per edit |
| Immutable reads | lock-free concurrent reading across any number of threads | none |
| Atomic publish | lock-free producer/consumer over evolving data | one volatile/interlocked write |
| CAS-memoized spine | concurrent *first* reads of a fresh structure are safe | bounded duplicated work under a race |
