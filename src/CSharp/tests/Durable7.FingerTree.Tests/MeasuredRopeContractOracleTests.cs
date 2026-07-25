using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Locks the measured-rope contracts used as the correctness and representation oracle for Axis 2 cursor work.
/// </summary>
public sealed class MeasuredRopeContractOracleTests
{
    /// <summary>Bulk construction preserves logical order and produces the declared bounded, proportional chunk layout.</summary>
    [Fact]
    public void BulkConstruction_PreservesSequenceAndCanonicalChunkLayout()
    {
        var count = (RopeChunking.MaxChunkSize * 5) + 17;
        var values = Enumerable.Range(0, count).ToArray();

        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(values);
        var diagnostics = rope.GetStructureDiagnostics();
        var chunkLengths = rope.GetChunkLengthsForDiagnostics();

        Assert.Equal(values, rope.ToArray());
        Assert.Equal(values.Sum(), rope.Measure);
        Assert.Equal(count, diagnostics.ElementCount);
        Assert.Equal(6, diagnostics.ChunkCount);
        Assert.Equal([2048, 2048, 2048, 2048, 2048, 17], chunkLengths);
        Assert.Equal(chunkLengths.Length, diagnostics.BackingStoreCount);
        Assert.Equal(chunkLengths.Min(), diagnostics.MinimumChunkLength);
        Assert.Equal(chunkLengths.Max(), diagnostics.MaximumChunkLength);
        Assert.True(diagnostics.EstimatedChunkStorageBytes > 0);
        Assert.All(chunkLengths, length => Assert.InRange(length, 1, RopeChunking.MaxChunkSize));
        Assert.Equal(count, chunkLengths.Sum());

        var edited = rope.SetItem(RopeChunking.MaxChunkSize * 2, -1);
        var sliced = edited.Slice(1, edited.Count - 2);
        Assert.Equal(values.Skip(1).SkipLast(1).Select((value, index) =>
            index == (RopeChunking.MaxChunkSize * 2) - 1 ? -1 : value), sliced.ToArray());
        Assert.Equal(sliced.Sum(), sliced.Measure);
        AssertProportionalChunkCount(sliced);
    }

    /// <summary>Public imports copy caller storage while persistent slicing and local edits share only rope-owned stores.</summary>
    [Fact]
    public void ImportsAndPersistentEdits_IsolateCallersAndShareUntouchedStores()
    {
        var count = (RopeChunking.MaxChunkSize * 5) + 17;
        var callerArray = Enumerable.Range(0, count).ToArray();
        var expected = callerArray.ToArray();
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(callerArray);

        callerArray[0] = -1;
        callerArray[RopeChunking.MaxChunkSize * 3] = -2;
        Assert.Equal(expected, rope.ToArray());

        var slice = rope.Slice(1, rope.Count - 2);
        Assert.Equal(rope.GetStructureDiagnostics().BackingStoreCount, rope.CountSharedBackingStoresForDiagnostics(slice));

        var edited = rope.SetItem((RopeChunking.MaxChunkSize * 2) + 7, -3);
        Assert.Equal(expected, rope.ToArray());
        Assert.Equal(
            rope.GetStructureDiagnostics().BackingStoreCount - 1,
            rope.CountSharedBackingStoresForDiagnostics(edited));

        var compacted = slice.Compact();
        Assert.Equal(slice.ToArray(), compacted.ToArray());
        Assert.Equal(slice.Measure, compacted.Measure);
        Assert.Equal(0, slice.CountSharedBackingStoresForDiagnostics(compacted));
    }

    /// <summary>Builder snapshots preserve promised reference identity, isolate earlier versions, and adopt source roots.</summary>
    [Fact]
    public void BuilderSnapshots_CacheByReferenceAndRetainPriorVersions()
    {
        var builder = MeasuredRope<int, int, SumMeasure<int>>.CreateBuilder();
        Assert.Same(MeasuredRope<int, int, SumMeasure<int>>.Empty, builder.ToImmutable());

        var firstValues = Enumerable.Range(0, RopeChunking.MaxChunkSize * 2).ToArray();
        builder.AddRange(firstValues);
        var first = builder.ToImmutable();

        Assert.Same(first, builder.ToImmutable());
        Assert.Same(first.RootIdentityForDiagnostics, builder.ToImmutable().RootIdentityForDiagnostics);

        var appended = Enumerable.Range(firstValues.Length, RopeChunking.MaxChunkSize).ToArray();
        builder.AddRange(appended);
        var second = builder.ToImmutable();

        Assert.NotSame(first, second);
        Assert.NotSame(first.RootIdentityForDiagnostics, second.RootIdentityForDiagnostics);
        Assert.Equal(firstValues, first.ToArray());
        Assert.Equal(firstValues.Concat(appended), second.ToArray());
        Assert.Equal(firstValues.Sum(), first.Measure);
        Assert.Equal(firstValues.Sum() + appended.Sum(), second.Measure);
        Assert.Equal(
            first.GetStructureDiagnostics().BackingStoreCount,
            first.CountSharedBackingStoresForDiagnostics(second));

        var adopted = first.ToBuilder();
        Assert.Same(first, adopted.ToImmutable());
        Assert.Same(first.RootIdentityForDiagnostics, adopted.ToImmutable().RootIdentityForDiagnostics);
    }

    /// <summary>A noncommutative monoid is combined in exact logical sequence order across chunks and edits.</summary>
    [Fact]
    public void NoncommutativeMeasure_PreservesLogicalOrderAcrossChunksAndEdits()
    {
        var values = Enumerable.Range(0, RopeChunking.MaxChunkSize + 31)
            .Select(index => (char)('a' + (index % 26)))
            .ToList();
        var rope = MeasuredRope<char, string, OrderedTextMeasure>.Create([.. values]);

        Assert.Equal(string.Concat(values), rope.Measure);
        Assert.Equal(string.Concat(values.Take(RopeChunking.MaxChunkSize + 7)),
            rope.PrefixMeasure(RopeChunking.MaxChunkSize + 7));

        values.Insert(RopeChunking.MaxChunkSize - 1, '#');
        rope = rope.Insert(RopeChunking.MaxChunkSize - 1, '#');
        values.RemoveAt(17);
        rope = rope.RemoveAt(17);
        values[values.Count - 3] = '!';
        rope = rope.SetItem(rope.Count - 3, '!');

        var suffix = new[] { 'X', 'Y', 'Z' };
        values.AddRange(suffix);
        rope = rope.Concat(MeasuredRope<char, string, OrderedTextMeasure>.Create(suffix));

        Assert.Equal(values, rope.ToArray());
        Assert.Equal(string.Concat(values), rope.Measure);
        Assert.Equal(string.Concat(values.Take(rope.Count - 5)), rope.PrefixMeasure(rope.Count - 5));
        rope.ValidateInvariants();
    }

    /// <summary>Overflowing ranges are rejected without replacing or otherwise disturbing the immutable root.</summary>
    [Fact]
    public void OverflowingRanges_AreRejectedWithoutChangingTheSource()
    {
        var rope = MeasuredRope<int, int, SumMeasure<int>>.Create(1, 2, 3, 4);
        var root = rope.RootIdentityForDiagnostics;

        Assert.Throws<ArgumentOutOfRangeException>(() => rope.RemoveRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.Slice(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.GetRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CopyTo(2, new int[3]));

        Assert.Same(root, rope.RootIdentityForDiagnostics);
        Assert.Equal([1, 2, 3, 4], rope.ToArray());
        Assert.Equal(10, rope.Measure);
    }

    /// <summary>Element-measure exceptions propagate while immutable and builder receivers retain their exact prior state.</summary>
    [Fact]
    public void ThrowingMeasureCallback_LeavesReceiversUnchangedAndRetryable()
    {
        InjectedMeasure.Reset();
        var source = MeasuredRope<int, string, InjectedMeasure>.Create(1, 2, 3);
        _ = source.Count;
        _ = source.Measure;
        var root = source.RootIdentityForDiagnostics;

        InjectedMeasure.ThrowOnMeasure(1);
        AssertInjectedCallbackFailure(() => source.SetItem(1, 9));

        Assert.Same(root, source.RootIdentityForDiagnostics);
        Assert.Equal([1, 2, 3], source.ToArray());
        Assert.Equal("1|2|3|", source.Measure);
        Assert.Equal([1, 9, 3], source.SetItem(1, 9).ToArray());

        var builder = source.ToBuilder();
        InjectedMeasure.ThrowOnMeasure(1);
        AssertInjectedCallbackFailure(() => builder.Add(4));

        Assert.Equal(3, builder.Count);
        Assert.Same(source, builder.ToImmutable());
        builder.Add(4);
        Assert.Equal([1, 2, 3, 4], builder.ToImmutable().ToArray());
    }

    /// <summary>Monoid-combine exceptions propagate while immutable and builder receivers retain their exact prior state.</summary>
    [Fact]
    public void ThrowingCombineCallback_LeavesReceiversUnchangedAndRetryable()
    {
        InjectedMeasure.Reset();
        var source = MeasuredRope<int, string, InjectedMeasure>.Create(1, 2, 3);
        _ = source.Count;
        _ = source.Measure;
        var root = source.RootIdentityForDiagnostics;

        InjectedMeasure.ThrowOnCombine(1);
        AssertInjectedCallbackFailure(() => source.Insert(1, 9));

        Assert.Same(root, source.RootIdentityForDiagnostics);
        Assert.Equal([1, 2, 3], source.ToArray());
        Assert.Equal("1|2|3|", source.Measure);
        Assert.Equal([1, 9, 2, 3], source.Insert(1, 9).ToArray());

        var builder = source.ToBuilder();
        InjectedMeasure.ThrowOnCombine(1);
        AssertInjectedCallbackFailure(() => builder.Add(4));

        Assert.Equal(3, builder.Count);
        Assert.Same(source, builder.ToImmutable());
        builder.Add(4);
        Assert.Equal([1, 2, 3, 4], builder.ToImmutable().ToArray());
    }

    private static void AssertProportionalChunkCount<T, TMeasure, TMeasureOps>(
        MeasuredRope<T, TMeasure, TMeasureOps> rope)
        where TMeasureOps : IMeasure<T, TMeasure>
    {
        var diagnostics = rope.GetStructureDiagnostics();
        var maximumForPackedChunksAndTwoBoundaryFragments =
            ((rope.Count + RopeChunking.MaxChunkSize - 1) / RopeChunking.MaxChunkSize) + 2;

        Assert.InRange(diagnostics.ChunkCount, 1, maximumForPackedChunksAndTwoBoundaryFragments);
        Assert.All(rope.GetChunkLengthsForDiagnostics(), length =>
            Assert.InRange(length, 1, RopeChunking.MaxChunkSize));
    }

    private static void AssertInjectedCallbackFailure(Action action)
    {
        try
        {
            Assert.Throws<InjectedCallbackException>(action);
        }
        finally
        {
            InjectedMeasure.Reset();
        }
    }

    private readonly struct OrderedTextMeasure : IMeasure<char, string>
    {
        public static string Empty => string.Empty;

        public static string Measure(char element) => element.ToString();

        public static string Combine(string left, string right) => string.Concat(left, right);
    }

    private readonly struct InjectedMeasure : IMeasure<int, string>
    {
        private static int s_measureCount;
        private static int s_combineCount;
        private static int s_throwOnMeasure;
        private static int s_throwOnCombine;

        public static string Empty => string.Empty;

        public static string Measure(int element)
        {
            if (++s_measureCount == s_throwOnMeasure)
                throw new InjectedCallbackException("Measure");
            return $"{element}|";
        }

        public static string Combine(string left, string right)
        {
            if (++s_combineCount == s_throwOnCombine)
                throw new InjectedCallbackException("Combine");
            return string.Concat(left, right);
        }

        public static void Reset()
        {
            s_measureCount = 0;
            s_combineCount = 0;
            s_throwOnMeasure = 0;
            s_throwOnCombine = 0;
        }

        public static void ThrowOnMeasure(int ordinal)
        {
            Reset();
            s_throwOnMeasure = ordinal;
        }

        public static void ThrowOnCombine(int ordinal)
        {
            Reset();
            s_throwOnCombine = ordinal;
        }
    }

    private sealed class InjectedCallbackException(string callback) : Exception(callback);
}
