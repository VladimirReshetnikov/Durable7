using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies that the sorted, order-statistic, priority-queue, and interval collections perform their hot reads
/// with <em>zero</em> heap allocation, now that they route through the non-capturing struct-predicate
/// <c>TryLocate</c> overload instead of closure-allocating lambdas and tree-rebuilding splits. Each test warms
/// the JIT and memoizes the lazy measures, then asserts a tight loop of reads allocates essentially nothing.
/// </summary>
public sealed class AllocationFreeReadTests
{
    // Generous absolute ceiling for an entire read loop: true zero-allocation reads sit at 0, while a single
    // captured-closure per read would push thousands of reads into the tens of kilobytes.
    private const long Budget = 256;

    private static long MeasureLoop(Action<int> read, int iterations)
    {
        for (var i = 0; i < iterations; i++)   // warm up: JIT + memoize the lazy measures along every read path
            read(i);

        var before = GC.GetAllocatedBytesForCurrentThread();
        for (var i = 0; i < iterations; i++)
            read(i);
        return GC.GetAllocatedBytesForCurrentThread() - before;
    }

    /// <summary>Verifies SortedSet membership, rank, indexing, and navigable reads allocate nothing.</summary>
    [Fact]
    public void SortedSetReads_AllocateNothing()
    {
        var set = SortedSet<int>.CreateRange(Enumerable.Range(0, 1 << 14).Select(i => i * 2));
        var sink = 0;
        var bytes = MeasureLoop(i =>
        {
            var key = i % (1 << 15);
            if (set.Contains(key)) sink++;
            sink += set[i % set.Count];
            sink += set.IndexOf(key);
            if (set.TryFloor(key, out var f)) sink += f;
            if (set.TryCeiling(key, out var c)) sink += c;
            if (set.TryLower(key, out var l)) sink += l;
            if (set.TryHigher(key, out var h)) sink += h;
        }, 2000);

        Assert.True(bytes < Budget, $"SortedSet reads allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>Verifies SortedBag membership, counting, and indexing reads allocate nothing.</summary>
    [Fact]
    public void SortedBagReads_AllocateNothing()
    {
        var bag = SortedBag<int>.CreateRange(Enumerable.Range(0, 1 << 13).Select(i => i % 500));
        var sink = 0;
        var bytes = MeasureLoop(i =>
        {
            var key = i % 600;
            if (bag.Contains(key)) sink++;
            sink += bag.CountLessThan(key);
            sink += bag.CountAtMost(key);
            sink += bag.CountOf(key);
            sink += bag[i % bag.Count];
        }, 2000);

        Assert.True(bytes < Budget, $"SortedBag reads allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>Verifies SortedDictionary lookup, indexing, and navigable reads allocate nothing.</summary>
    [Fact]
    public void SortedDictionaryReads_AllocateNothing()
    {
        var dict = SortedDictionary<int, int>.CreateRange(
            Enumerable.Range(0, 1 << 13).Select(i => new KeyValuePair<int, int>(i * 2, i)));
        var sink = 0;
        var bytes = MeasureLoop(i =>
        {
            var key = i % (1 << 14);
            if (dict.ContainsKey(key)) sink++;
            if (dict.TryGetValue(key, out var v)) sink += v;
            sink += dict.IndexOfKey(key);
            sink += dict.EntryAt(i % dict.Count).Value;
            if (dict.TryFloorEntry(key, out var f)) sink += f.Value;
            if (dict.TryHigherEntry(key, out var h)) sink += h.Value;
        }, 2000);

        Assert.True(bytes < Budget, $"SortedDictionary reads allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>Verifies PriorityQueue peek and IntervalTree stabbing reads allocate nothing.</summary>
    [Fact]
    public void PriorityQueueAndIntervalReads_AllocateNothing()
    {
        var queue = PriorityQueue<int, int>.Empty;
        for (var i = 0; i < 1 << 13; i++)
            queue = queue.Enqueue(i, (i * 31) % 1000);

        var intervals = IntervalTree<int>.Empty;
        for (var i = 0; i < 1 << 12; i++)
            intervals = intervals.Insert(new Interval<int>(i * 3, i * 3 + 5));

        var sink = 0;
        var bytes = MeasureLoop(i =>
        {
            if (queue.TryPeek(out var element, out var priority)) sink += element + priority;
            if (intervals.TryFindOverlap(new Interval<int>(i % 12000, i % 12000 + 1), out var match)) sink += match.Low;
        }, 2000);

        Assert.True(bytes < Budget, $"PriorityQueue/IntervalTree reads allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>Verifies the peek named operations (now routed through struct predicates / the memoized measure)
    /// read with no allocation, for both the bare max/min measures and the size+max / size+min products.</summary>
    [Fact]
    public void MeasureExtensionPeeks_AllocateNothing()
    {
        var maxTree = FingerTree<int, Optional<int>, MaxMeasure<int>>.CreateRange(Enumerable.Range(0, 1 << 13).Select(i => (i * 31) % 1000));
        var minTree = FingerTree<int, Optional<int>, MinMeasure<int>>.CreateRange(Enumerable.Range(0, 1 << 13).Select(i => (i * 17) % 1000));
        var sizeMax = ProductMeasures.CreateSizeAndMaxRange(Enumerable.Range(0, 1 << 13).Select(i => (i * 13) % 1000));
        var sizeMin = ProductMeasures.CreateSizeAndMinRange(Enumerable.Range(0, 1 << 13).Select(i => (i * 7) % 1000));
        var sink = 0;
        var bytes = MeasureLoop(_ =>
        {
            if (maxTree.TryPeekMax(out var mx)) sink += mx;
            if (minTree.TryPeekMin(out var mn)) sink += mn;
            if (sizeMax.TryPeekMax(out var smx)) sink += smx;
            if (sizeMin.TryPeekMin(out var smn)) sink += smn;
        }, 2000);

        Assert.True(bytes < Budget, $"Peek ops allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>Verifies the measured rope's value-type-predicate locate runs fully closure-free, including the
    /// within-chunk scan — the headline of the fully-closure-free measure navigation.</summary>
    [Fact]
    public void MeasuredRopeLocateByMeasure_AllocatesNothing()
    {
        var rope = string.Concat(Enumerable.Range(0, 4000).Select(i => $"line {i}\n")).ToTextRope();
        var lineCount = rope.LineCount();
        var sink = 0;
        var bytes = MeasureLoop(i =>
        {
            if (rope.TryLocateByMeasure(new MeasureAtLeast(i % lineCount), out var index, out var before, out var ch))
                sink += index + before + ch;
        }, 2000);

        Assert.True(bytes < Budget, $"MeasuredRope locate allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>Verifies RopeText line/column navigation reads allocate nothing now that LineStartOffset is
    /// closure-free (GetLine, which materializes a string, is intentionally excluded).</summary>
    [Fact]
    public void RopeTextLineNavigation_AllocatesNothing()
    {
        var rope = string.Concat(Enumerable.Range(0, 4000).Select(i => $"line {i}\n")).ToTextRope();
        var count = rope.Count;
        var lineCount = rope.LineCount();
        var sink = 0;
        var bytes = MeasureLoop(i =>
        {
            var offset = i % (count + 1);
            sink += rope.LineOfOffset(offset);
            var (line, column) = rope.LineColumnOf(offset);
            sink += line + column;
            var lineIndex = i % lineCount;
            sink += rope.LineStartOffset(lineIndex);
            sink += rope.OffsetOf(lineIndex, 0);
        }, 2000);

        Assert.True(bytes < Budget, $"RopeText navigation allocated {bytes} bytes over the loop (sink={sink}).");
    }

    /// <summary>A value-type predicate over an <see cref="int"/> measure, driving a closure-free locate.</summary>
    private readonly struct MeasureAtLeast(int target) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure >= target;
    }
}
