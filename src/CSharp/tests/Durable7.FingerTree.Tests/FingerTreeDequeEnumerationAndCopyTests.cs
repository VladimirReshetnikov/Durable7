// Tests for the finger tree deque enumeration and copy.

using System.Collections;
using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies enumerator contracts for both the struct-based pattern foreach path and the boxed interface
/// path, plus array-copy edge cases.
/// </summary>
public sealed class FingerTreeDequeEnumerationAndCopyTests
{
    /// <summary>
    /// Verifies the pattern-based struct enumerator yields every element in order on small and deep deques.
    /// </summary>
    [Fact]
    public void StructEnumerator_YieldsAllElementsInOrder()
    {
        foreach (var size in new[] { 0, 1, 2, 3, 12, 1000 })
        {
            var expected = Enumerable.Range(0, size).ToArray();
            var deque = FingerTreeDeque<int>.CreateRange(expected);

            var observed = new List<int>(size);
            foreach (var item in deque)
                observed.Add(item);

            Assert.Equal(expected, observed);
        }
    }

    /// <summary>
    /// Verifies the boxed interface enumeration path matches the struct path.
    /// </summary>
    [Fact]
    public void InterfaceEnumeration_MatchesStructEnumeration()
    {
        var expected = Enumerable.Range(0, 500).ToArray();
        var deque = FingerTreeDeque<int>.CreateRange(expected);

        Assert.Equal(expected, deque.AsEnumerable().ToArray());

        var untyped = new List<object?>();
        foreach (var item in (IEnumerable)deque)
            untyped.Add(item);
        Assert.Equal(expected.Cast<object?>(), untyped);
    }

    /// <summary>
    /// Verifies the enumerator stays exhausted after the final element and that reset is unsupported.
    /// </summary>
    [Fact]
    public void Enumerator_ExhaustionAndResetContracts()
    {
        var deque = FingerTreeDeque<int>.Create(1, 2);
        var enumerator = deque.GetEnumerator();

        Assert.True(enumerator.MoveNext());
        Assert.True(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());

        Assert.Throws<NotSupportedException>(() => ((IEnumerator)deque.GetEnumerator()).Reset());

        var defaultEnumerator = default(FingerTreeDeque<int>.Enumerator);
        Assert.False(defaultEnumerator.MoveNext());
    }

    /// <summary>
    /// Verifies copying accepts boundary destinations: exact fit, trailing space, and the empty deque at the
    /// very end of the destination array.
    /// </summary>
    [Fact]
    public void CopyTo_BoundaryDestinations_Succeed()
    {
        var deque = FingerTreeDeque<int>.Create(1, 2, 3);

        var exact = new int[3];
        deque.CopyTo(exact, 0);
        Assert.Equal([1, 2, 3], exact);

        var offset = new int[5];
        deque.CopyTo(offset, 2);
        Assert.Equal([0, 0, 1, 2, 3], offset);

        var endOfArray = new int[4];
        FingerTreeDeque<int>.Empty.CopyTo(endOfArray, 4);
        Assert.Equal([0, 0, 0, 0], endOfArray);
    }

    /// <summary>
    /// Verifies array conversion on the empty deque and on a deque deep enough to require several traversal
    /// stack levels.
    /// </summary>
    [Fact]
    public void ToArray_EmptyAndDeepDeques_ReturnExpectedContents()
    {
        Assert.Empty(FingerTreeDeque<int>.Empty.ToArray());

        var expected = Enumerable.Range(0, 20_000).ToArray();
        var deque = FingerTreeDeque<int>.CreateRange(expected);
        Assert.Equal(expected, deque.ToArray());
    }
}
