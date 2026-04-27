using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

internal static class FingerTreeDequeAssert
{
    public static void SequenceEqual<T>(IReadOnlyList<T> expected, FingerTreeDeque<T> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.Count == 0, actual.IsEmpty);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(expected, actual.ToList());

        var copied = new T[expected.Count + 2];
        actual.CopyTo(copied, 1);
        Assert.Equal(expected, copied.Skip(1).Take(expected.Count).ToArray());

        for (var index = 0; index < expected.Count; index++)
        {
            Assert.True(actual.TryGetItem(index, out var value));
            Assert.Equal(expected[index], value);
            Assert.Equal(expected[index], actual[index]);
        }

        Assert.False(actual.TryGetItem(-1, out _));
        Assert.False(actual.TryGetItem(expected.Count, out _));

        if (expected.Count == 0)
        {
            Assert.False(actual.TryPeekFirst(out _));
            Assert.False(actual.TryPeekLast(out _));
        }
        else
        {
            Assert.Equal(expected[0], actual.First);
            Assert.Equal(expected[^1], actual.Last);
            Assert.True(actual.TryPeekFirst(out var first));
            Assert.Equal(expected[0], first);
            Assert.True(actual.TryPeekLast(out var last));
            Assert.Equal(expected[^1], last);
        }
    }

    public static FingerTreeDeque<T> Deque<T>(params T[] items) => FingerTreeDeque<T>.Create(items);
}
