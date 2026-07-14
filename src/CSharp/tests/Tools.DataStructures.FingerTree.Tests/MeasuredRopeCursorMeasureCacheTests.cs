using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>Locks measured-fragment caching, callback ceilings, failure atomicity, and snapshot races.</summary>
public sealed class MeasuredRopeCursorMeasureCacheTests
{
    /// <summary>Every source/cursor and delegate/struct seek measures at most one 2,048 chunk plus a 16 focus.</summary>
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
    [InlineData(4097)]
    public void MeasureSeek_ElementMeasureCallbacksNeverExceedChunkPlusFocus(int position)
    {
        var source = CreateSource(4098);
        var receiver = source.GetCursor(Math.Min(source.Count, position + 31));

        InstrumentedCountMeasure.Reset();
        Assert.True(source.TryGetCursorByMeasure(new CountAbove(position), out var sourceStruct));
        AssertSeekCallbackCeiling(position, sourceStruct);

        InstrumentedCountMeasure.Reset();
        Assert.True(source.TryGetCursorByMeasure(count => count > position, out var sourceDelegate));
        AssertSeekCallbackCeiling(position, sourceDelegate);

        InstrumentedCountMeasure.Reset();
        Assert.True(receiver.TrySeekByMeasure(new CountAbove(position), out var cursorStruct));
        AssertSeekCallbackCeiling(position, cursorStruct);

        InstrumentedCountMeasure.Reset();
        Assert.True(receiver.TrySeekByMeasure(count => count > position, out var cursorDelegate));
        AssertSeekCallbackCeiling(position, cursorDelegate);

        Assert.Equal(sourceStruct.Position, sourceDelegate.Position);
        Assert.Equal(sourceStruct.Position, cursorStruct.Position);
        Assert.Equal(sourceStruct.Position, cursorDelegate.Position);
        Assert.Same(source, sourceStruct.Snapshot());
        Assert.Same(source, cursorStruct.Snapshot());
    }

    /// <summary>Measure seek scans one ordinary chunk once and reuses that preparation for later seeks and focus materialization.</summary>
    [Fact]
    public void MeasureSeek_UsesOneChunkScanAndPreparesOnlyOnFirstNavigation()
    {
        var source = CreateSource(4098);
        var receiver = source.GetCursor(source.Count);

        InstrumentedCountMeasure.Reset();
        Assert.True(receiver.TrySeekByMeasure(new CountAbove(2040), out var located));
        Assert.Equal(2048, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(0, located.GetDiagnostics().ActiveLength);
        Assert.Equal(2040, located.Position);
        Assert.Equal(2040, located.MeasureBefore);
        Assert.Equal(source.Count - 2040, located.MeasureAfter);

        InstrumentedCountMeasure.Reset();
        Assert.True(located.TrySeekByMeasure(new CountAbove(2041), out var soughtAgain));
        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(2041, soughtAgain.Position);

        InstrumentedCountMeasure.Reset();
        var moved = located.MoveNext();
        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(2041, moved.Position);
        Assert.Equal(2041, moved.MeasureBefore);

        InstrumentedCountMeasure.Reset();
        moved = moved.MoveNext();
        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(2042, moved.Position);

        InstrumentedCountMeasure.Reset();
        var edited = located.Insert(-1);
        Assert.Equal(1, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(source.Count + 1, edited.Count);
        Assert.Equal(2041, edited.Position);
        Assert.Same(source, located.Snapshot());
        Assert.NotSame(source, edited.Snapshot());
    }

    /// <summary>Focus-local edits measure only bounded new content, while a pull never exceeds chunk plus focus.</summary>
    [Fact]
    public void LocalEditsAndBoundaryMovement_RespectMeasureCallbackCeilings()
    {
        var source = CreateSource(4098);

        InstrumentedCountMeasure.Reset();
        var cursor = source.GetCursor(2040);
        Assert.InRange(InstrumentedCountMeasure.MeasureCalls, 0, 2064);

        InstrumentedCountMeasure.Reset();
        var inserted = cursor.Insert(-1);
        Assert.InRange(InstrumentedCountMeasure.MeasureCalls, 1, 16);

        InstrumentedCountMeasure.Reset();
        var range = cursor.InsertRange(Enumerable.Range(-16, 16).ToArray());
        Assert.InRange(InstrumentedCountMeasure.MeasureCalls, 16, 16);

        InstrumentedCountMeasure.Reset();
        var replaced = cursor.ReplaceNext(-2);
        Assert.InRange(InstrumentedCountMeasure.MeasureCalls, 1, 16);

        Assert.Equal(source.Count + 1, inserted.Count);
        Assert.Equal(source.Count + 16, range.Count);
        Assert.Equal(source.Count, replaced.Count);

        InstrumentedCountMeasure.Reset();
        for (var cycle = 0; cycle < 4; cycle++)
        {
            for (var step = 0; step < 64; step++)
                cursor = cursor.MoveNext();
            for (var step = 0; step < 64; step++)
                cursor = cursor.MovePrevious();
        }

        Assert.Equal(2040, cursor.Position);
        Assert.InRange(InstrumentedCountMeasure.MeasureCalls, 0, 2064);
    }

    /// <summary>Dirty snapshot construction recombines prepared aggregates without measuring existing elements.</summary>
    [Fact]
    public void DirtyAndRepeatedSnapshots_InvokeNoElementMeasureCallbacks()
    {
        var source = CreateSource(5000);
        var edited = source.GetCursor(2048).Insert(-1).InsertRange([-2, -3, -4]);
        Assert.False(edited.GetDiagnostics().HasCachedSnapshot);

        InstrumentedCountMeasure.Reset();
        using var diagnostics = RopeCursorDiagnostics.BeginSession();
        var snapshot = edited.Snapshot();

        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(0, diagnostics.Snapshot.ElementMeasureCallbacks);
        Assert.True(edited.GetDiagnostics().HasCachedSnapshot);
        Assert.Same(snapshot, edited.Snapshot());
        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.Equal(source.Count + 4, snapshot.Count);
        Assert.Equal(source.Measure + 4, snapshot.Measure);
        snapshot.ValidateInvariants();
    }

    /// <summary>Concurrent first snapshots return one winner and never repeat element-measure callbacks.</summary>
    [Fact]
    public void Snapshot_FirstCallRaceReturnsOneWinnerWithZeroMeasureCallbacks()
    {
        var source = CreateSource(5000);
        var edited = source.GetCursor(2048).Insert(-1);
        var contexts = new[]
        {
            edited,
            edited.MovePrevious(),
            edited.MoveNext(),
            edited.MoveNext().MoveNext(),
        };
        var snapshots = new MeasuredRope<int, int, InstrumentedCountMeasure>[64];

        Assert.False(edited.GetDiagnostics().HasCachedSnapshot);
        InstrumentedCountMeasure.Reset();
        Parallel.For(0, snapshots.Length, index =>
            snapshots[index] = contexts[index % contexts.Length].Snapshot());

        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.All(snapshots, snapshot => Assert.Same(snapshots[0], snapshot));
        Assert.All(contexts, context =>
        {
            Assert.Same(edited.VersionIdentityForDiagnostics, context.VersionIdentityForDiagnostics);
            Assert.True(context.GetDiagnostics().HasCachedSnapshot);
            Assert.Same(snapshots[0], context.Snapshot());
        });
        Assert.Equal(source.Count + 1, snapshots[0].Count);
        Assert.Equal(source.Measure + 1, snapshots[0].Measure);
    }

    /// <summary>A failing element-measure callback exposes no edit version and the receiver can be retried.</summary>
    [Fact]
    public void Edit_MeasureFailureIsAtomicAndRetryable()
    {
        var source = CreateSource(513);
        var cursor = source.GetCursor(256);
        var version = cursor.VersionIdentityForDiagnostics;

        InstrumentedCountMeasure.Reset(throwOnMeasure: 1);
        Assert.Throws<InjectedMeasureException>(() => cursor.Insert(-1));
        AssertReceiverUnchanged(source, cursor, version);

        InstrumentedCountMeasure.Reset(throwOnMeasure: 2);
        Assert.Throws<InjectedMeasureException>(() => cursor.InsertRange([-1, -2, -3]));
        AssertReceiverUnchanged(source, cursor, version);

        InstrumentedCountMeasure.Reset(throwOnMeasure: 1);
        Assert.Throws<InjectedMeasureException>(() => cursor.ReplaceNext(-1));
        AssertReceiverUnchanged(source, cursor, version);

        InstrumentedCountMeasure.Reset();
        var retried = cursor.Insert(-1);
        Assert.Equal(source.Count + 1, retried.Count);
        Assert.Equal(source.Measure + 1, retried.MeasureBefore + retried.MeasureAfter);
        Assert.NotSame(version, retried.VersionIdentityForDiagnostics);
    }

    /// <summary>A failing combine callback exposes no edit version and the receiver can be retried.</summary>
    [Fact]
    public void Edit_CombineFailureIsAtomicAndRetryable()
    {
        var source = CreateSource(513);
        var cursor = source.GetCursor(256);
        var version = cursor.VersionIdentityForDiagnostics;

        InstrumentedCountMeasure.Reset(throwOnCombine: 1);
        Assert.Throws<InjectedCombineException>(() => cursor.Insert(-1));
        AssertReceiverUnchanged(source, cursor, version);

        InstrumentedCountMeasure.Reset(throwOnCombine: 1);
        Assert.Throws<InjectedCombineException>(() => cursor.InsertRange([-1, -2, -3]));
        AssertReceiverUnchanged(source, cursor, version);

        InstrumentedCountMeasure.Reset(throwOnCombine: 1);
        Assert.Throws<InjectedCombineException>(() => cursor.ReplaceNext(-1));
        AssertReceiverUnchanged(source, cursor, version);

        InstrumentedCountMeasure.Reset();
        var retried = cursor.ReplaceNext(-1);
        Assert.Equal(source.Count, retried.Count);
        Assert.Equal(source.Measure, retried.MeasureBefore + retried.MeasureAfter);
        Assert.NotSame(version, retried.VersionIdentityForDiagnostics);
    }

    /// <summary>A failed dirty snapshot publishes nothing, invokes no Measure, and succeeds on retry.</summary>
    [Fact]
    public void Snapshot_CombineFailurePublishesNothingAndRetryReturnsCanonicalSnapshot()
    {
        var source = CreateSource(5000);
        var edited = source.GetCursor(2048).Insert(-1);
        var version = edited.VersionIdentityForDiagnostics;
        Assert.False(edited.GetDiagnostics().HasCachedSnapshot);

        InstrumentedCountMeasure.Reset(throwOnCombine: 1);
        Assert.Throws<InjectedCombineException>(() => edited.Snapshot());

        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.False(edited.GetDiagnostics().HasCachedSnapshot);
        Assert.Same(version, edited.VersionIdentityForDiagnostics);

        InstrumentedCountMeasure.Reset();
        var retry = edited.Snapshot();
        Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        Assert.True(edited.GetDiagnostics().HasCachedSnapshot);
        Assert.Same(retry, edited.Snapshot());
        Assert.Equal(source.Count + 1, retry.Count);
        Assert.Equal(source.Measure + 1, retry.Measure);
        retry.ValidateInvariants();
    }

    /// <summary>A racing failed candidate cannot dislodge a successful winner, and the loser retries to it.</summary>
    [Fact]
    public async Task Snapshot_FailedCandidateRacingWinnerLeavesOnePublishedReference()
    {
        var source = CreateSource(5000);
        var edited = source.GetCursor(2048).Insert(-1);

        InstrumentedCountMeasure.Reset(throwOnCombine: 1);
        InstrumentedCountMeasure.BlockCombine(1);
        var failedCandidate = Task.Factory.StartNew(
            () => Record.Exception(() => edited.Snapshot()),
            CancellationToken.None,
            TaskCreationOptions.LongRunning,
            TaskScheduler.Default);
        try
        {
            InstrumentedCountMeasure.WaitForBlockedCombine();
            var successfulCandidate = Task.Factory.StartNew(
                () => edited.Snapshot(),
                CancellationToken.None,
                TaskCreationOptions.LongRunning,
                TaskScheduler.Default);
            Assert.Same(
                successfulCandidate,
                await Task.WhenAny(successfulCandidate, Task.Delay(TimeSpan.FromSeconds(10))));
            var winner = await successfulCandidate;

            InstrumentedCountMeasure.ReleaseBlockedCombine();
            Assert.Same(
                failedCandidate,
                await Task.WhenAny(failedCandidate, Task.Delay(TimeSpan.FromSeconds(10))));
            Assert.IsType<InjectedCombineException>(await failedCandidate);
            Assert.True(edited.GetDiagnostics().HasCachedSnapshot);
            Assert.Same(winner, edited.Snapshot());
            Assert.Equal(0, InstrumentedCountMeasure.MeasureCalls);
        }
        finally
        {
            InstrumentedCountMeasure.ReleaseBlockedCombine();
        }
    }

    private static MeasuredRope<int, int, InstrumentedCountMeasure> CreateSource(int count)
    {
        InstrumentedCountMeasure.Reset();
        return MeasuredRope<int, int, InstrumentedCountMeasure>.Create(
            Enumerable.Range(0, count).ToArray());
    }

    private static void AssertSeekCallbackCeiling(
        int expectedPosition,
        MeasuredRopeCursor<int, int, InstrumentedCountMeasure> cursor)
    {
        Assert.Equal(expectedPosition, cursor.Position);
        Assert.InRange(InstrumentedCountMeasure.MeasureCalls, 0, 2064);
        Assert.Equal(expectedPosition, cursor.MeasureBefore);
        Assert.Equal(cursor.Count - expectedPosition, cursor.MeasureAfter);
        cursor.Validate();
    }

    private static void AssertReceiverUnchanged(
        MeasuredRope<int, int, InstrumentedCountMeasure> source,
        MeasuredRopeCursor<int, int, InstrumentedCountMeasure> cursor,
        object version)
    {
        InstrumentedCountMeasure.DisableFailures();
        Assert.Same(version, cursor.VersionIdentityForDiagnostics);
        Assert.Equal(256, cursor.Position);
        Assert.Equal(256, cursor.MeasureBefore);
        Assert.Equal(source.Count - 256, cursor.MeasureAfter);
        Assert.Same(source, cursor.Snapshot());
        Assert.Equal(source.ToArray(), cursor.Snapshot().ToArray());
    }

    private readonly struct CountAbove(int count) : IMeasurePredicate<int>
    {
        public bool Invoke(int measure) => measure > count;
    }

    private readonly struct InstrumentedCountMeasure : IMeasure<int, int>
    {
        private static int s_measureCalls;
        private static int s_combineCalls;
        private static int s_throwOnMeasure;
        private static int s_throwOnCombine;
        private static int s_blockOnCombine;
        private static ManualResetEventSlim? s_blockedCombineEntered;
        private static ManualResetEventSlim? s_releaseBlockedCombine;

        internal static int MeasureCalls => Volatile.Read(ref s_measureCalls);

        internal static int CombineCalls => Volatile.Read(ref s_combineCalls);

        public static int Empty => 0;

        public static int Measure(int element)
        {
            var ordinal = Interlocked.Increment(ref s_measureCalls);
            if (ordinal == Volatile.Read(ref s_throwOnMeasure))
                throw new InjectedMeasureException();
            return 1;
        }

        public static int Combine(int left, int right)
        {
            var ordinal = Interlocked.Increment(ref s_combineCalls);
            if (ordinal == Volatile.Read(ref s_blockOnCombine))
            {
                Volatile.Read(ref s_blockedCombineEntered)?.Set();
                if (!(Volatile.Read(ref s_releaseBlockedCombine)?.Wait(TimeSpan.FromSeconds(10)) ?? false))
                    throw new TimeoutException("Timed out waiting to release the blocked Combine callback.");
            }
            if (ordinal == Volatile.Read(ref s_throwOnCombine))
                throw new InjectedCombineException();
            return checked(left + right);
        }

        internal static void Reset(int throwOnMeasure = 0, int throwOnCombine = 0)
        {
            Volatile.Write(ref s_measureCalls, 0);
            Volatile.Write(ref s_combineCalls, 0);
            Volatile.Write(ref s_throwOnMeasure, throwOnMeasure);
            Volatile.Write(ref s_throwOnCombine, throwOnCombine);
            Volatile.Write(ref s_blockOnCombine, 0);
            Volatile.Write(ref s_blockedCombineEntered, null);
            Volatile.Write(ref s_releaseBlockedCombine, null);
        }

        internal static void BlockCombine(int ordinal)
        {
            Volatile.Write(ref s_blockedCombineEntered, new ManualResetEventSlim());
            Volatile.Write(ref s_releaseBlockedCombine, new ManualResetEventSlim());
            Volatile.Write(ref s_blockOnCombine, ordinal);
        }

        internal static void WaitForBlockedCombine()
        {
            var entered = Volatile.Read(ref s_blockedCombineEntered);
            Assert.NotNull(entered);
            Assert.True(entered.Wait(TimeSpan.FromSeconds(10)));
        }

        internal static void ReleaseBlockedCombine() =>
            Volatile.Read(ref s_releaseBlockedCombine)?.Set();

        internal static void DisableFailures()
        {
            Volatile.Write(ref s_throwOnMeasure, 0);
            Volatile.Write(ref s_throwOnCombine, 0);
        }
    }

    private sealed class InjectedMeasureException : Exception;

    private sealed class InjectedCombineException : Exception;
}
