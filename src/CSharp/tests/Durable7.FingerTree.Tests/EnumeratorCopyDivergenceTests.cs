// Tests for the enumerator copy divergence.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies that value copies of an in-progress struct enumerator fail fast instead of silently
/// skipping elements. Copies share one traversal stack, so after any copy advances, every other
/// copy's next <c>MoveNext</c> must throw <see cref="InvalidOperationException"/>.
/// </summary>
public sealed class EnumeratorCopyDivergenceTests
{
    /// <summary>
    /// Verifies the deque enumerator: a copy that falls behind another copy's advance throws instead
    /// of skipping the element the other copy consumed.
    /// </summary>
    [Fact]
    public void Deque_AdvancedCopy_InvalidatesOtherCopies()
    {
        var deque = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, 100));

        var first = deque.GetEnumerator();
        for (var i = 0; i < 10; i++)
            Assert.True(first.MoveNext());
        Assert.Equal(9, first.Current);

        var second = first;
        Assert.True(first.MoveNext());
        Assert.Equal(10, first.Current);

        Assert.Throws<InvalidOperationException>(() => second.MoveNext());

        // The advanced copy and fresh enumerators are unaffected.
        Assert.True(first.MoveNext());
        Assert.Equal(11, first.Current);
        Assert.Equal(Enumerable.Range(0, 100), deque.AsEnumerable());
    }

    /// <summary>
    /// Verifies that copying an enumerator and advancing only the copy is fine, and that the copy
    /// yields the full remaining sequence.
    /// </summary>
    [Fact]
    public void Deque_SingleActiveCopy_YieldsRemainingElements()
    {
        var deque = FingerTreeDeque<int>.CreateRange(Enumerable.Range(0, 50));

        var original = deque.GetEnumerator();
        for (var i = 0; i < 5; i++)
            Assert.True(original.MoveNext());

        var copy = original;
        var observed = new List<int>();
        while (copy.MoveNext())
            observed.Add(copy.Current);

        Assert.Equal(Enumerable.Range(5, 45), observed);
    }

    /// <summary>
    /// Verifies the measured-tree enumerator applies the same fail-fast copy contract.
    /// </summary>
    [Fact]
    public void MeasuredTree_AdvancedCopy_InvalidatesOtherCopies()
    {
        var tree = FingerTree<int, Optional<int>, MaxMeasure<int>>.CreateRange(Enumerable.Range(0, 100));

        var first = tree.GetEnumerator();
        Assert.True(first.MoveNext());

        var second = first;
        Assert.True(first.MoveNext());

        Assert.Throws<InvalidOperationException>(() => second.MoveNext());
    }

    /// <summary>
    /// Verifies the reversible-deque enumerator applies the same fail-fast copy contract, including
    /// on a reversed snapshot.
    /// </summary>
    [Fact]
    public void ReversibleDeque_AdvancedCopy_InvalidatesOtherCopies()
    {
        var deque = ReversibleDeque<int>.CreateRange(Enumerable.Range(0, 100)).Reverse();

        var first = deque.GetEnumerator();
        Assert.True(first.MoveNext());
        Assert.Equal(99, first.Current);

        var second = first;
        Assert.True(first.MoveNext());
        Assert.Equal(98, first.Current);

        Assert.Throws<InvalidOperationException>(() => second.MoveNext());
    }

    /// <summary>
    /// Verifies copies of an exhausted or empty enumerator keep returning <see langword="false"/>:
    /// only elements consumed by another copy trigger the fail-fast contract.
    /// </summary>
    [Fact]
    public void ExhaustedAndEmptyEnumerators_CopiesStayIdempotent()
    {
        var deque = FingerTreeDeque<int>.Create(1, 2);
        var enumerator = deque.GetEnumerator();
        Assert.True(enumerator.MoveNext());
        Assert.True(enumerator.MoveNext());
        Assert.False(enumerator.MoveNext());

        var copyAfterExhaustion = enumerator;
        Assert.False(copyAfterExhaustion.MoveNext());
        Assert.False(enumerator.MoveNext());

        var empty = FingerTreeDeque<int>.Empty.GetEnumerator();
        var emptyCopy = empty;
        Assert.False(empty.MoveNext());
        Assert.False(emptyCopy.MoveNext());
    }
}
