using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Locks absolute measured seek, identity-boundary, miss, and predicate-overload semantics.</summary>
public sealed class MeasuredRopeCursorMeasureSeekTests
{
    /// <summary>Factory and cursor seeks find the element whose inclusive prefix first satisfies the predicate.</summary>
    [Theory]
    [InlineData(0, true, 0, 0)]
    [InlineData(1, true, 0, 0)]
    [InlineData(2, true, 0, 0)]
    [InlineData(3, true, 1, 2)]
    [InlineData(5, true, 1, 2)]
    [InlineData(6, true, 2, 5)]
    [InlineData(10, true, 2, 5)]
    [InlineData(11, true, 3, 10)]
    [InlineData(17, true, 3, 10)]
    [InlineData(18, false, 4, 17)]
    [InlineData(int.MaxValue, false, 4, 17)]
    public void InclusivePrefixBoundary_DelegateAndStructFactoriesAndSeeksAgree(
        int threshold,
        bool expectedFound,
        int expectedPosition,
        int expectedBefore)
    {
        var source = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3, 5, 7);
        var receiver = source.GetCursor(3);

        var delegateFactoryFound = source.TryGetCursorByMeasure(
            sum => sum >= threshold,
            out var delegateFactory);
        var structFactoryFound = source.TryGetCursorByMeasure(
            new AtLeast(threshold),
            out var structFactory);
        var delegateSeekFound = receiver.TrySeekByMeasure(
            sum => sum >= threshold,
            out var delegateSeek);
        var structSeekFound = receiver.TrySeekByMeasure(
            new AtLeast(threshold),
            out var structSeek);

        Assert.Equal(expectedFound, delegateFactoryFound);
        Assert.Equal(expectedFound, structFactoryFound);
        Assert.Equal(expectedFound, delegateSeekFound);
        Assert.Equal(expectedFound, structSeekFound);

        foreach (var cursor in new[] { delegateFactory, structFactory, delegateSeek, structSeek })
        {
            Assert.Equal(expectedPosition, cursor.Position);
            Assert.Equal(expectedBefore, cursor.MeasureBefore);
            Assert.Equal(17 - expectedBefore, cursor.MeasureAfter);
            Assert.Same(source, cursor.Snapshot());
            AssertCursorBoundary(cursor, source.ToArray());
        }
    }

    /// <summary>A predicate true at the monoid identity succeeds at zero only for a nonempty rope.</summary>
    [Fact]
    public void TrueAtEmpty_NonemptyReturnsStartButEmptyReturnsFalseEnd()
    {
        var nonempty = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3);

        Assert.True(nonempty.TryGetCursorByMeasure(static _ => true, out var delegateStart));
        Assert.True(nonempty.TryGetCursorByMeasure(new AlwaysTrue(), out var structStart));
        Assert.Equal(0, delegateStart.Position);
        Assert.Equal(0, structStart.Position);
        Assert.Equal(0, delegateStart.MeasureBefore);
        Assert.Equal(5, delegateStart.MeasureAfter);
        Assert.Same(nonempty, delegateStart.Snapshot());
        Assert.Same(nonempty, structStart.Snapshot());

        var empty = MeasuredRope<int, int, PositiveWeightMeasure>.Empty;
        Assert.False(empty.TryGetCursorByMeasure(static _ => true, out var delegateEnd));
        Assert.False(empty.TryGetCursorByMeasure(new AlwaysTrue(), out var structEnd));
        Assert.Equal(0, delegateEnd.Position);
        Assert.Equal(0, structEnd.Position);
        Assert.True(delegateEnd.IsAtStart);
        Assert.True(delegateEnd.IsAtEnd);
        Assert.Equal(0, delegateEnd.MeasureBefore);
        Assert.Equal(0, delegateEnd.MeasureAfter);
        Assert.Same(empty, delegateEnd.Snapshot());
        Assert.Same(empty, structEnd.Snapshot());

        var emptyCursor = empty.GetCursor();
        Assert.False(emptyCursor.TrySeekByMeasure(static _ => true, out var delegateSeekEnd));
        Assert.False(emptyCursor.TrySeekByMeasure(new AlwaysTrue(), out var structSeekEnd));
        Assert.True(emptyCursor.HasSameContextStateForDiagnostics(delegateSeekEnd));
        Assert.True(emptyCursor.HasSameContextStateForDiagnostics(structSeekEnd));
        Assert.Same(emptyCursor.VersionIdentityForDiagnostics, delegateSeekEnd.VersionIdentityForDiagnostics);
        Assert.Same(emptyCursor.VersionIdentityForDiagnostics, structSeekEnd.VersionIdentityForDiagnostics);
    }

    /// <summary>Cursor measure seek is absolute from the whole version, not relative to the current gap.</summary>
    [Fact]
    public void CursorSeek_IsAbsoluteAndPreservesLogicalVersion()
    {
        var source = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3, 5, 7);
        var receiver = source.GetCursor(4);
        var version = receiver.VersionIdentityForDiagnostics;

        Assert.True(receiver.TrySeekByMeasure(new AtLeast(3), out var beforeSecond));

        Assert.Equal(1, beforeSecond.Position);
        Assert.Equal(2, beforeSecond.MeasureBefore);
        Assert.Equal(15, beforeSecond.MeasureAfter);
        Assert.Same(version, beforeSecond.VersionIdentityForDiagnostics);
        Assert.Same(source, beforeSecond.Snapshot());
        Assert.False(receiver.HasSameContextStateForDiagnostics(beforeSecond));
    }

    /// <summary>Insert, delete, and replacement versions support exact absolute hit, miss, and no-op seeks.</summary>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    public void DirtyVersion_DelegateAndStructSeeksCoverHitMissAndSamePosition(int editKind)
    {
        var source = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3, 5, 7, 11);
        var receiver = source.GetCursor(2);
        var (dirty, expected) = editKind switch
        {
            0 => (receiver.Insert(13), new[] { 2, 3, 13, 5, 7, 11 }),
            1 => (receiver.DeletePrevious(), new[] { 2, 5, 7, 11 }),
            2 => (receiver.ReplaceNext(13), new[] { 2, 3, 13, 7, 11 }),
            _ => throw new ArgumentOutOfRangeException(nameof(editKind)),
        };
        var version = dirty.VersionIdentityForDiagnostics;

        Assert.True(dirty.TrySeekByMeasure(sum => sum >= 1, out var delegateHit));
        Assert.True(dirty.TrySeekByMeasure(new AtLeast(1), out var structHit));
        AssertDirtySeek(expected, version, delegateHit, 0);
        AssertDirtySeek(expected, version, structHit, 0);

        var sameThreshold = expected.Take(dirty.Position + 1).Sum();
        Assert.True(dirty.TrySeekByMeasure(sum => sum >= sameThreshold, out var delegateSame));
        Assert.True(dirty.TrySeekByMeasure(new AtLeast(sameThreshold), out var structSame));
        Assert.True(dirty.HasSameContextStateForDiagnostics(delegateSame));
        Assert.True(dirty.HasSameContextStateForDiagnostics(structSame));
        AssertDirtySeek(expected, version, delegateSame, dirty.Position);
        AssertDirtySeek(expected, version, structSame, dirty.Position);

        var beyondTotal = checked(expected.Sum() + 1);
        Assert.False(dirty.TrySeekByMeasure(sum => sum >= beyondTotal, out var delegateMiss));
        Assert.False(dirty.TrySeekByMeasure(new AtLeast(beyondTotal), out var structMiss));
        AssertDirtySeek(expected, version, delegateMiss, expected.Length);
        AssertDirtySeek(expected, version, structMiss, expected.Length);

        Assert.Equal(expected, dirty.Snapshot().ToArray());
        Assert.NotSame(source, dirty.Snapshot());
    }

    /// <summary>A measured seek resolving to the current gap is the same context-state no-op as positional Seek.</summary>
    [Theory]
    [InlineData(0)]
    [InlineData(15)]
    [InlineData(16)]
    [InlineData(255)]
    [InlineData(256)]
    [InlineData(257)]
    [InlineData(2047)]
    [InlineData(2048)]
    [InlineData(2049)]
    public void SamePositionSeek_PreservesExactVersionAndContextState(int position)
    {
        var values = Enumerable.Range(0, 4098).ToArray();
        var source = MeasuredRope<int, int, CountMeasure>.Create(values);
        var receiver = source.GetCursor(position);

        Assert.True(receiver.TrySeekByMeasure(new CountAbove(position), out var sameStruct));
        Assert.True(receiver.TrySeekByMeasure(count => count > position, out var sameDelegate));

        Assert.Equal(position, sameStruct.Position);
        Assert.Equal(position, sameDelegate.Position);
        Assert.Same(receiver.VersionIdentityForDiagnostics, sameStruct.VersionIdentityForDiagnostics);
        Assert.Same(receiver.VersionIdentityForDiagnostics, sameDelegate.VersionIdentityForDiagnostics);
        Assert.True(receiver.HasSameContextStateForDiagnostics(sameStruct));
        Assert.True(receiver.HasSameContextStateForDiagnostics(sameDelegate));
        Assert.Same(source, sameStruct.Snapshot());
        Assert.Same(source, sameDelegate.Snapshot());
    }

    /// <summary>Misses return an initialized end cursor over the same version and exact total measure.</summary>
    [Fact]
    public void Miss_ReturnsEndCursorWithTotalMeasureAndSharedSnapshot()
    {
        var source = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3, 5, 7);
        var receiver = source.GetCursor(1);

        Assert.False(receiver.TrySeekByMeasure(new AtLeast(18), out var miss));

        Assert.Equal(source.Count, miss.Position);
        Assert.True(miss.IsAtEnd);
        Assert.False(miss.IsAtStart);
        Assert.Equal(source.Measure, miss.MeasureBefore);
        Assert.Equal(0, miss.MeasureAfter);
        Assert.Same(receiver.VersionIdentityForDiagnostics, miss.VersionIdentityForDiagnostics);
        Assert.Same(source, miss.Snapshot());
        Assert.False(miss.TryPeekNext(out _));
        Assert.True(miss.TryPeekPrevious(out var previous));
        Assert.Equal(7, previous);
    }

    /// <summary>Measure search is exact at focus/carry/ordinary-chunk boundaries through 2,048 elements.</summary>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(15)]
    [InlineData(16)]
    [InlineData(255)]
    [InlineData(256)]
    [InlineData(257)]
    [InlineData(2047)]
    [InlineData(2048)]
    [InlineData(2049)]
    [InlineData(4097)]
    public void CountMeasure_SeeksToExactNamedBoundary(int position)
    {
        var values = Enumerable.Range(0, 4098).ToArray();
        var source = MeasuredRope<int, int, CountMeasure>.Create(values);
        var receiver = source.GetCursor(Math.Min(4098, position + 37));

        Assert.True(source.TryGetCursorByMeasure(new CountAbove(position), out var factory));
        Assert.True(receiver.TrySeekByMeasure(new CountAbove(position), out var seek));

        Assert.Equal(position, factory.Position);
        Assert.Equal(position, seek.Position);
        Assert.Equal(position, factory.MeasureBefore);
        Assert.Equal(values.Length - position, factory.MeasureAfter);
        Assert.Equal(factory.Position, seek.Position);
        Assert.Equal(factory.MeasureBefore, seek.MeasureBefore);
        Assert.Equal(factory.MeasureAfter, seek.MeasureAfter);
        Assert.Same(source, factory.Snapshot());
        Assert.Same(source, seek.Snapshot());
    }

    /// <summary>A located chunk remains exact before and after its first navigation/edit preparation.</summary>
    [Fact]
    public void LocatedChunk_NoncommutativeMeasuresSurviveDeferredNavigationAndEditing()
    {
        var source = MeasuredRope<char, string, ConcatenatingMeasure>.Create("abcdef".ToCharArray());

        Assert.True(source.TryGetCursorByMeasure(new LengthAtLeast(4), out var located));

        Assert.Equal(3, located.Position);
        Assert.Equal("abc", located.MeasureBefore);
        Assert.Equal("def", located.MeasureAfter);
        Assert.Equal(0, located.GetDiagnostics().ActiveLength);
        Assert.True(located.TryPeekPrevious(out var previous));
        Assert.Equal('c', previous);
        Assert.True(located.TryPeekNext(out var next));
        Assert.Equal('d', next);
        located.Validate();

        var moved = located.MoveNext();
        Assert.Equal(4, moved.Position);
        Assert.Equal("abcd", moved.MeasureBefore);
        Assert.Equal("ef", moved.MeasureAfter);
        Assert.Same(source, moved.Snapshot());
        moved.Validate();

        var edited = located.Insert('X');
        Assert.Equal(4, edited.Position);
        Assert.Equal("abcX", edited.MeasureBefore);
        Assert.Equal("def", edited.MeasureAfter);
        Assert.Equal("abcXdef", new string(edited.Snapshot().ToArray()));
        edited.Validate();

        Assert.Equal(3, located.Position);
        Assert.Equal("abc", located.MeasureBefore);
        Assert.Equal("def", located.MeasureAfter);
        Assert.Same(source, located.Snapshot());
    }

    /// <summary>Null delegate overloads reject null before navigating or changing cursor state.</summary>
    [Fact]
    public void NullDelegate_IsRejectedWithoutChangingState()
    {
        var source = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3, 5);
        var cursor = source.GetCursor(1);
        Func<int, bool> predicate = null!;

        Assert.Throws<ArgumentNullException>(() => source.TryGetCursorByMeasure(predicate, out _));
        Assert.Throws<ArgumentNullException>(() => cursor.TrySeekByMeasure(predicate, out _));
        Assert.Equal(1, cursor.Position);
        Assert.Same(source, cursor.Snapshot());
    }

    /// <summary>Predicate exceptions propagate while the immutable receiver stays exact and retryable.</summary>
    [Fact]
    public void PredicateException_LeavesReceiverUnchangedAndRetryable()
    {
        var source = MeasuredRope<int, int, PositiveWeightMeasure>.Create(2, 3, 5);
        var cursor = source.GetCursor(2);
        var version = cursor.VersionIdentityForDiagnostics;

        Assert.Throws<PredicateException>(() =>
            cursor.TrySeekByMeasure(static _ => throw new PredicateException(), out _));

        Assert.Same(version, cursor.VersionIdentityForDiagnostics);
        Assert.Equal(2, cursor.Position);
        Assert.Same(source, cursor.Snapshot());
        Assert.True(cursor.TrySeekByMeasure(new AtLeast(4), out var retry));
        Assert.Equal(1, retry.Position);
    }

    private static void AssertCursorBoundary(
        MeasuredRopeCursor<int, int, PositiveWeightMeasure> cursor,
        IReadOnlyList<int> expected)
    {
        Assert.Equal(cursor.Position > 0, cursor.TryPeekPrevious(out var previous));
        if (cursor.Position > 0)
            Assert.Equal(expected[cursor.Position - 1], previous);
        Assert.Equal(cursor.Position < cursor.Count, cursor.TryPeekNext(out var next));
        if (cursor.Position < cursor.Count)
            Assert.Equal(expected[cursor.Position], next);
        cursor.Validate();
    }

    private static void AssertDirtySeek(
        IReadOnlyList<int> expected,
        object version,
        MeasuredRopeCursor<int, int, PositiveWeightMeasure> cursor,
        int position)
    {
        Assert.Same(version, cursor.VersionIdentityForDiagnostics);
        Assert.Equal(position, cursor.Position);
        Assert.Equal(expected.Take(position).Sum(), cursor.MeasureBefore);
        Assert.Equal(expected.Skip(position).Sum(), cursor.MeasureAfter);
        AssertCursorBoundary(cursor, expected);
    }

    private readonly struct PositiveWeightMeasure : IMeasure<int, int>
    {
        public static int Empty => 0;

        public static int Measure(int element) => element;

        public static int Combine(int left, int right) => left + right;
    }

    private readonly struct CountMeasure : IMeasure<int, int>
    {
        public static int Empty => 0;

        public static int Measure(int element) => 1;

        public static int Combine(int left, int right) => left + right;
    }

    private readonly struct ConcatenatingMeasure : IMeasure<char, string>
    {
        public static string Empty => string.Empty;

        public static string Measure(char element) => element.ToString();

        public static string Combine(string left, string right) => left + right;
    }

    private readonly struct AtLeast(int threshold) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure >= threshold;
    }

    private readonly struct CountAbove(int count) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure > count;
    }

    private readonly struct LengthAtLeast(int length) : IMeasurePredicate<string>
    {
        public bool Invoke(string measure) => measure.Length >= length;
    }

    private readonly struct AlwaysTrue : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => true;
    }

    private sealed class PredicateException : Exception;
}
