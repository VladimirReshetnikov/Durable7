// Tests for the rope append builder.

using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Validates append-only mutable builders for positional and measured ropes.</summary>
public sealed class RopeAppendBuilderTests
{
    /// <summary>Verifies positional rope builder appends, caching, and snapshot isolation.</summary>
    [Fact]
    public void RopeBuilder_AppendsFreezeAndCacheCorrectly()
    {
        var builder = Rope<int>.CreateBuilder();
        var empty = builder.ToImmutable();

        Assert.Same(Rope<int>.Empty, empty);
        Assert.Same(empty, builder.ToImmutable());

        builder.Add(1);
        builder.AddRange(new[] { 2, 3 }.AsSpan());
        builder.AddRange(new SingleUseEnumerable<int>(new[] { 4, 5 }));

        var first = builder.ToImmutable();
        first.ValidateInvariants();
        Assert.Equal(new[] { 1, 2, 3, 4, 5 }, first.ToArray());
        Assert.Same(first, builder.ToImmutable());

        builder.Add(6);
        var second = builder.ToImmutable();
        second.ValidateInvariants();

        Assert.Equal(new[] { 1, 2, 3, 4, 5 }, first.ToArray());
        Assert.Equal(new[] { 1, 2, 3, 4, 5, 6 }, second.ToArray());
    }

    /// <summary>Verifies positional rope ToBuilder adopts the source snapshot and Clear resets to the canonical empty rope.</summary>
    [Fact]
    public void RopeBuilder_ToBuilderAdoptsPrefixAndClearResets()
    {
        var source = Rope<int>.Create(1, 2, 3);
        var builder = source.ToBuilder();

        Assert.Equal(3, builder.Count);
        Assert.Same(source, builder.ToImmutable());

        builder.Clear();
        var empty = builder.ToImmutable();

        Assert.Same(Rope<int>.Empty, empty);
        Assert.Empty(empty);
        Assert.Equal(new[] { 1, 2, 3 }, source.ToArray());
    }

    /// <summary>Verifies positional rope builder enumeration is fail-fast for mutations but not empty appends.</summary>
    [Fact]
    public void RopeBuilder_EnumerationIsFailFast()
    {
        var builder = Rope<int>.CreateBuilder();
        builder.AddRange(new[] { 1, 2, 3 });

        using var enumerator = builder.GetEnumerator();
        builder.AddRange(ReadOnlySpan<int>.Empty);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current);

        builder.Add(4);
        Assert.Throws<InvalidOperationException>(() => enumerator.MoveNext());
    }

    /// <summary>Verifies repeated small snapshots keep producing valid positional ropes.</summary>
    [Fact]
    public void RopeBuilder_SnapshotLoopKeepsValidRopes()
    {
        var builder = Rope<int>.CreateBuilder();
        var snapshots = new List<Rope<int>>();
        for (var i = 0; i < 200; i++)
        {
            builder.Add(i);
            var snapshot = builder.ToImmutable();
            snapshot.ValidateInvariants();
            snapshots.Add(snapshot);
        }

        Assert.Equal(Enumerable.Range(0, 200).ToArray(), snapshots[^1].ToArray());
        Assert.Equal(new[] { 0 }, snapshots[0].ToArray());
        Assert.Equal(Enumerable.Range(0, 100).ToArray(), snapshots[99].ToArray());
    }

    /// <summary>Verifies measured rope builder tracks the live measure and freezes valid snapshots.</summary>
    [Fact]
    public void MeasuredRopeBuilder_AppendsMeasureAndFreezeAreCorrect()
    {
        var builder = MeasuredRope<int, int, SumMeasure<int>>.CreateBuilder();

        Assert.Empty(builder);
        Assert.Equal(0, builder.Measure);
        Assert.Same(MeasuredRope<int, int, SumMeasure<int>>.Empty, builder.ToImmutable());

        builder.Add(5);
        builder.AddRange(new[] { 1, 4 }.AsSpan());
        Assert.Equal(3, builder.Count);
        Assert.Equal(10, builder.Measure);

        var first = builder.ToImmutable();
        first.ValidateInvariants();
        Assert.Equal(10, first.Measure);
        Assert.Same(first, builder.ToImmutable());

        builder.AddRange(new SingleUseEnumerable<int>(new[] { 2, 8 }));
        Assert.Equal(20, builder.Measure);

        var second = builder.ToImmutable();
        second.ValidateInvariants();
        Assert.Equal(new[] { 5, 1, 4, 2, 8 }, second.ToArray());
        Assert.Equal(new[] { 5, 1, 4 }, first.ToArray());
    }

    /// <summary>Verifies measured span appends bulk-copy across partial and full chunk boundaries without losing measure state.</summary>
    [Fact]
    public void MeasuredRopeBuilder_SpanAppendAcrossChunkBoundariesPreservesContentsAndMeasure()
    {
        var builder = MeasuredRope<int, int, SumMeasure<int>>.CreateBuilder();
        var values = Enumerable.Range(1, (RopeChunking.MaxChunkSize * 2) + 17).ToArray();

        builder.Add(-1);
        builder.AddRange(values.AsSpan());

        Assert.Equal(values.Length + 1, builder.Count);
        Assert.Equal(-1 + values.Sum(), builder.Measure);

        var frozen = builder.ToImmutable();
        frozen.ValidateInvariants();
        Assert.Equal(new[] { -1 }.Concat(values), frozen);
    }

    /// <summary>Verifies measured rope ToBuilder preserves the source measure and Clear resets the live aggregate.</summary>
    [Fact]
    public void MeasuredRopeBuilder_ToBuilderPreservesMeasureAndClearResets()
    {
        var source = MeasuredRope<int, int, SumMeasure<int>>.Create(2, 3, 5);
        var builder = source.ToBuilder();

        Assert.Equal(3, builder.Count);
        Assert.Equal(10, builder.Measure);
        Assert.Same(source, builder.ToImmutable());

        builder.Clear();

        Assert.Empty(builder);
        Assert.Equal(0, builder.Measure);
        Assert.Same(MeasuredRope<int, int, SumMeasure<int>>.Empty, builder.ToImmutable());
    }

    /// <summary>Verifies measured rope builder enumeration is fail-fast for mutations but not empty appends.</summary>
    [Fact]
    public void MeasuredRopeBuilder_EnumerationIsFailFast()
    {
        var builder = MeasuredRope<int, int, SumMeasure<int>>.CreateBuilder();
        builder.AddRange(new[] { 1, 2, 3 });

        using var enumerator = builder.GetEnumerator();
        builder.AddRange(ReadOnlySpan<int>.Empty);
        Assert.True(enumerator.MoveNext());
        Assert.Equal(1, enumerator.Current);

        builder.Add(4);
        Assert.Throws<InvalidOperationException>(() => enumerator.MoveNext());
    }
}
