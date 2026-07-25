using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Bounded concurrent-read and immutable snapshot-publication tests.</summary>
public sealed class PersistentOrderedSetConcurrencyTests
{
    /// <summary>Verifies separate readers observe one retained snapshot consistently.</summary>
    [Fact]
    public void ConcurrentReaders_ObserveConsistentOrderMembershipAndPositions()
    {
        var set = PersistentOrderedSet<int>.CreateRange(Enumerable.Range(0, 512));

        Parallel.For(0, 4, _ =>
        {
            for (var pass = 0; pass < 24; pass++)
                AssertPrefixSnapshot(set, 512);
        });

        OrderedSetAssert.Matches(Enumerable.Range(0, 512).ToArray(), set);
    }

    /// <summary>Verifies volatile publication exposes only fully formed immutable prefix versions.</summary>
    [Fact]
    public async Task LockFreePublication_ReadersSeeOnlyCompleteSnapshots()
    {
        var published = PersistentOrderedSet<int>.Empty;
        var start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var writer = Task.Run(async () =>
        {
            await start.Task;
            var current = PersistentOrderedSet<int>.Empty;
            for (var item = 0; item < 256; item++)
            {
                current = current.Add(item);
                Volatile.Write(ref published, current);
            }
        });
        var readers = Enumerable.Range(0, 2).Select(_ => Task.Run(async () =>
        {
            await start.Task;
            for (var pass = 0; pass < 400; pass++)
            {
                var snapshot = Volatile.Read(ref published);
                AssertPrefixSnapshot(snapshot, 256);
                if ((pass & 15) == 0)
                    await Task.Yield();
            }
        })).ToArray();

        start.SetResult();
        await Task.WhenAll(readers.Append(writer));
        AssertPrefixSnapshot(Volatile.Read(ref published), 256);
    }

    /// <summary>Verifies deriving many successors concurrently never mutates a retained branch source.</summary>
    [Fact]
    public async Task ConcurrentDerivation_LeavesRetainedBranchReadable()
    {
        var source = PersistentOrderedSet<int>.CreateRange(Enumerable.Range(0, 256));
        var writer = Task.Run(() =>
        {
            var current = source;
            for (var item = 256; item < 512; item++)
                current = current.AddFirst(item).MoveToLast(item);
            return current;
        });
        var readers = Enumerable.Range(0, 2).Select(_ => Task.Run(() =>
        {
            for (var pass = 0; pass < 64; pass++)
                AssertPrefixSnapshot(source, 256);
        })).ToArray();

        await Task.WhenAll(readers.Append(writer));
        var successor = await writer;
        AssertPrefixSnapshot(source, 256);
        Assert.Equal(512, successor.Count);
        successor.ValidateInvariants();
    }

    private static void AssertPrefixSnapshot(PersistentOrderedSet<int> set, int maximumCount)
    {
        Assert.InRange(set.Count, 0, maximumCount);
        var array = set.ToArray();
        Assert.Equal(set.Count, array.Length);
        for (var index = 0; index < array.Length; index++)
        {
            Assert.Equal(index, array[index]);
            Assert.Equal(index, set[index]);
            Assert.Equal(index, set.IndexOf(index));
            Assert.True(set.Contains(index));
            Assert.True(set.TryGetValue(index, out var stored));
            Assert.Equal(index, stored);
        }
        set.ValidateInvariants();
    }
}
