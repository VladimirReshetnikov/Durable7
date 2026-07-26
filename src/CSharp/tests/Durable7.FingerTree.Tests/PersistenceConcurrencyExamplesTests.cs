// Tests for the persistence concurrency examples.

using System.Collections.Concurrent;
using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Worked, verified examples of the two properties immutable persistent structures give for free: cheap
/// snapshots with structural-sharing undo/redo, and lock-free multi-threaded access. Every example here is a
/// runnable test, so the patterns shown in <c>docs/persistence-and-concurrency.md</c> are guaranteed to compile
/// and behave as documented. The patterns apply to every type in the library; the rope is used as the concrete
/// subject.
/// </summary>
public sealed class PersistenceConcurrencyExamplesTests
{
    /// <summary>
    /// Demonstrates that taking a snapshot is free and an edit shares structure with the version it came from:
    /// editing a 1,000,000-element rope allocates only kilobytes (O(log n)), not a fresh megabyte-scale copy,
    /// and the original version is untouched.
    /// </summary>
    [Fact]
    public void Snapshots_AreCheapAndStructurallyShared()
    {
        var original = Rope<int>.Create(Enumerable.Range(0, 1_000_000).ToArray());

        // A "snapshot" is just keeping the reference — O(1), zero allocation. Edits never mutate it.
        var snapshot = original;

        original.Insert(500_000, -1);   // warm up the JIT before measuring
        var before = GC.GetAllocatedBytesForCurrentThread();
        var edited = original.Insert(500_000, -1);
        var editAllocation = GC.GetAllocatedBytesForCurrentThread() - before;

        // The edit reuses almost all of the original's structure: kilobytes, not the ~4 MB a full copy would cost.
        Assert.InRange(editAllocation, 1, 256 * 1024);
        Assert.Equal(1_000_001, edited.Count);
        Assert.Equal(-1, edited[500_000]);

        // The retained snapshot is unchanged and still valid.
        Assert.Equal(1_000_000, snapshot.Count);
        Assert.Equal(500_000, snapshot[500_000]);
    }

    /// <summary>
    /// Demonstrates an undo/redo history over a text buffer: because every edit yields a new immutable version,
    /// undo and redo are just moving a cursor over a list of retained versions — no diffing, no inverse
    /// operations.
    /// </summary>
    [Fact]
    public void UndoRedo_OnATextBuffer()
    {
        var history = new List<MeasuredRope<char, int, NewlineMeasure>> { "".ToTextRope() };
        var cursor = 0;

        void Apply(Func<MeasuredRope<char, int, NewlineMeasure>, MeasuredRope<char, int, NewlineMeasure>> edit)
        {
            var next = edit(history[cursor]);
            history.RemoveRange(cursor + 1, history.Count - cursor - 1);   // drop any redo branch
            history.Add(next);
            cursor++;
        }

        string Undo() => history[cursor = Math.Max(0, cursor - 1)].AsString();
        string Redo() => history[cursor = Math.Min(history.Count - 1, cursor + 1)].AsString();
        string Current() => history[cursor].AsString();

        Apply(r => r.InsertRange(0, "hello".AsSpan()));
        Apply(r => r.InsertRange(r.Count, " world".AsSpan()));
        Apply(r => r.InsertRange(5, ",\nthere".AsSpan()));
        Assert.Equal("hello,\nthere world", Current());

        Assert.Equal("hello world", Undo());
        Assert.Equal("hello", Undo());
        Assert.Equal("hello world", Redo());

        // Editing after an undo discards the redo branch.
        Apply(r => r.InsertRange(r.Count, "!".AsSpan()));
        Assert.Equal("hello world!", Current());
        Assert.Equal("hello world!", Redo());   // nothing to redo
    }

    /// <summary>
    /// Demonstrates that many threads can read one shared immutable rope concurrently with no locks and no risk:
    /// every reader sees the same complete, consistent sequence.
    /// </summary>
    [Fact]
    public void ConcurrentReaders_OnAnImmutableRope_AreConsistent()
    {
        var rope = Rope<int>.Create(Enumerable.Range(0, 100_000).ToArray());
        var expected = Enumerable.Range(0, 100_000).ToArray();

        Parallel.For(0, 64, _ =>
        {
            // Indexed reads, enumeration, and slicing all run lock-free against the shared instance.
            Assert.Equal(0, rope[0]);
            Assert.Equal(99_999, rope[99_999]);
            Assert.Equal(50_000, rope[50_000]);
            Assert.Equal(expected, rope.ToArray());
            Assert.Equal(2_000, rope.Slice(40_000, 2_000).Count);
        });
    }

    /// <summary>
    /// Demonstrates lock-free snapshot publication: a single writer publishes successive immutable versions
    /// through one volatile reference while readers grab the current version and read it. Because each version is
    /// fully built before it is published and never mutated afterward, readers only ever observe a complete,
    /// consistent snapshot — every observed version is exactly the prefix it should be, never a torn state.
    /// </summary>
    [Fact]
    public async Task LockFreePublication_ReadersSeeConsistentSnapshots()
    {
        const int writes = 4000;
        var cell = new[] { Rope<int>.Empty };   // the single shared, atomically published reference
        var failures = new ConcurrentQueue<string>();

        var writer = Task.Run(() =>
        {
            var current = Rope<int>.Empty;
            for (var i = 0; i < writes; i++)
            {
                current = current.AddLast(i);            // build the next version off the last
                Volatile.Write(ref cell[0], current);    // publish it atomically
            }
        });

        var readers = Enumerable.Range(0, 4).Select(_ => Task.Run(() =>
        {
            for (var iteration = 0; iteration < 2000; iteration++)
            {
                var snapshot = Volatile.Read(ref cell[0]);   // a complete, immutable version
                var array = snapshot.ToArray();
                for (var j = 0; j < array.Length; j++)
                {
                    if (array[j] != j)   // a torn read would break the [0, k) prefix shape
                    {
                        failures.Enqueue($"inconsistent snapshot at {j}: {array[j]}");
                        break;
                    }
                }
            }
        })).ToArray();

        await Task.WhenAll([writer, .. readers]);

        Assert.Empty(failures);
        Assert.Equal(Enumerable.Range(0, writes), Volatile.Read(ref cell[0]).ToArray());
    }

    /// <summary>
    /// Demonstrates that even a freshly built structure is safe to read from many threads at once: the
    /// lazy-memoized spine forces its deferred work through atomic compare-exchange, so concurrent first reads
    /// race harmlessly and every thread observes the same content.
    /// </summary>
    [Fact]
    public void ConcurrentFirstReads_OfAFreshStructure_Converge()
    {
        var fresh = Rope<int>.CreateRange(Enumerable.Range(0, 1 << 16));   // not read before this point
        var expected = Enumerable.Range(0, 1 << 16).ToArray();
        var observations = new ConcurrentBag<int>();

        Parallel.For(0, 32, _ =>
        {
            observations.Add(fresh.Count);   // first reads force the lazy measure concurrently
            Assert.Equal(expected, fresh.ToArray());
        });

        Assert.All(observations, count => Assert.Equal(1 << 16, count));
    }
}
