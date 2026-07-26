// Tests for the DABA Lite lite adversarial.

using System.Reflection;
using System.Runtime.CompilerServices;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Adversarial state-machine, callback-failure, and storage-lifetime tests for DABA Lite.</summary>
public sealed class DabaLiteAdversarialTests
{
    private const int OffsetIdentity = 11;

    /// <summary>Exhaustively validates every short insert/evict history against a noncommutative model.</summary>
    [Fact]
    public void ExhaustiveShortHistories_MatchNoncommutativeModelAndValidate()
    {
        var left = Matrix.Create(2);
        var right = Matrix.Create(7);
        Assert.NotEqual(Matrix.Multiply(left, right), Matrix.Multiply(right, left));

        const int historyLength = 10;
        for (var mask = 0; mask < 1 << historyLength; mask++)
        {
            var daba = new DabaLite<Matrix, MatrixMonoid>();
            var model = new Queue<Matrix>();
            for (var step = 0; step < historyLength; step++)
            {
                if ((mask & (1 << step)) != 0)
                {
                    var value = Matrix.Create(mask * 17 + step + 1);
                    daba.Insert(value);
                    model.Enqueue(value);
                }
                else if (model.Count == 0)
                {
                    Assert.False(daba.TryEvict());
                }
                else
                {
                    Assert.True(daba.TryEvict());
                    model.Dequeue();
                }

                AssertMatrixState(daba, model);
            }
        }
    }

    /// <summary>Exercises both sides of every chunk boundary and long-running steady-window churn.</summary>
    [Fact]
    public void ChunkBoundariesAndChurn_RemainBoundedAndValid()
    {
        foreach (var size in new[] { 63, 64, 65, 127, 128, 129 })
        {
            var daba = new DabaLite<Matrix, MatrixMonoid>();
            var model = new Queue<Matrix>();
            for (var i = 0; i < size; i++)
            {
                var value = Matrix.Create(i + 1);
                daba.Insert(value);
                model.Enqueue(value);
            }

            var initial = daba.ValidateStructure();
            Assert.Equal(size / 64 + 1, initial.BlockCount);
            Assert.InRange(initial.SlackSlotCount, 1, 127);
            AssertMatrixState(daba, model);

            for (var i = 0; i < 512; i++)
            {
                daba.Evict();
                model.Dequeue();
                var value = Matrix.Create(10_000 + size * 1_000 + i);
                daba.Insert(value);
                model.Enqueue(value);
                var statistics = daba.ValidateStructure();
                Assert.InRange(statistics.SlackSlotCount, 1, 127);
                Assert.InRange(statistics.BlockCount, 1, size / 64 + 2);
                if ((i & 15) == 0)
                    Assert.Equal(FoldMatrices(model), daba.Aggregate);
            }

            while (model.Count != 0)
            {
                daba.Evict();
                model.Dequeue();
                AssertMatrixState(daba, model);
            }

            var empty = daba.ValidateStructure();
            Assert.Equal(1, empty.BlockCount);
            Assert.Equal(64, empty.AllocatedSlotCapacity);
        }
    }

    /// <summary>Proves every fixup phase with a non-default identity and observes the 3/2/1 combine maxima.</summary>
    [Fact]
    public void NonDefaultIdentity_CoversEveryFixupPhaseAndCombineCeiling()
    {
        CountingOffsetMonoid.Reset();
        var daba = new DabaLite<int, CountingOffsetMonoid>();
        var model = new Queue<int>();
        var phases = new HashSet<FixupPhase>();
        var maximumInsert = 0;
        var maximumEvict = 0;
        var maximumQuery = 0;

        for (var cycle = 0; cycle < 4; cycle++)
        {
            for (var i = 0; i < 130; i++)
            {
                phases.Add(ClassifyNextFixup(daba.ValidateStructure(), evicting: false));
                CountingOffsetMonoid.Reset();
                var value = cycle * 1_000 + i + 20;
                daba.Insert(value);
                model.Enqueue(value);
                Assert.InRange(CountingOffsetMonoid.CombineCount, 1, 3);
                maximumInsert = Math.Max(maximumInsert, CountingOffsetMonoid.CombineCount);
                AssertOffsetState(daba, model, ref maximumQuery);
            }

            while (model.Count != 0)
            {
                phases.Add(ClassifyNextFixup(daba.ValidateStructure(), evicting: true));
                CountingOffsetMonoid.Reset();
                daba.Evict();
                model.Dequeue();
                Assert.InRange(CountingOffsetMonoid.CombineCount, 0, 2);
                maximumEvict = Math.Max(maximumEvict, CountingOffsetMonoid.CombineCount);
                AssertOffsetState(daba, model, ref maximumQuery);
            }
        }

        Assert.Equal(new HashSet<FixupPhase>(Enum.GetValues<FixupPhase>()), phases);
        Assert.Equal(3, maximumInsert);
        Assert.Equal(2, maximumEvict);
        Assert.Equal(1, maximumQuery);
    }

    /// <summary>Checks the strong exception guarantee at every insert and eviction combine callback ordinal.</summary>
    /// <param name="inserting">Whether the failing operation is insertion rather than eviction.</param>
    /// <param name="callbackOrdinal">The one-based combine callback that throws.</param>
    [Theory]
    [InlineData(true, 1)]
    [InlineData(true, 2)]
    [InlineData(true, 3)]
    [InlineData(false, 1)]
    [InlineData(false, 2)]
    public void CombineExceptions_LeaveExactStateUnchangedAndUsable(bool inserting, int callbackOrdinal)
    {
        var history = FindCombineHistory(inserting, callbackOrdinal);
        ThrowingCombineMonoid.Reset();
        Assert.True(TryReplay<ThrowingCombineMonoid>(history, out var daba, out var model));
        var beforeStatistics = daba.ValidateStructure();
        var beforeAggregate = FoldOffset(model);

        ThrowingCombineMonoid.ThrowOn(callbackOrdinal);
        Assert.Throws<CallbackException>(() => ApplyCandidate(daba, inserting));
        Assert.Equal(callbackOrdinal, ThrowingCombineMonoid.CombineCount);

        ThrowingCombineMonoid.Reset();
        Assert.Equal(beforeStatistics, daba.ValidateStructure());
        Assert.Equal(beforeAggregate, daba.Aggregate);
        RetryCandidateAndContinue(daba, model, inserting);
    }

    /// <summary>Checks rollback of the provisional successor chunk allocated at the 64-slot boundary.</summary>
    [Fact]
    public void InsertFailureAtChunkBoundary_UnlinksProvisionalSuccessorBeforeRetry()
    {
        ThrowingCombineMonoid.Reset();
        var daba = new DabaLite<int, ThrowingCombineMonoid>();
        var model = new Queue<int>();
        for (var i = 0; i < 63; i++)
        {
            var value = 20 + i;
            daba.Insert(value);
            model.Enqueue(value);
        }

        var beforeStatistics = daba.ValidateStructure();
        var beforeAggregate = FoldOffset(model);
        Assert.Equal(1, beforeStatistics.BlockCount);
        Assert.Equal(64, beforeStatistics.AllocatedSlotCapacity);
        Assert.Equal(1, beforeStatistics.SlackSlotCount);

        ThrowingCombineMonoid.ThrowOn(2);
        Assert.Throws<CallbackException>(() => daba.Insert(101));
        Assert.Equal(2, ThrowingCombineMonoid.CombineCount);

        ThrowingCombineMonoid.Reset();
        Assert.Equal(beforeStatistics, daba.ValidateStructure());
        Assert.Equal(beforeAggregate, daba.Aggregate);
        Assert.Equal(63, daba.Count);

        daba.Insert(101);
        model.Enqueue(101);
        var afterRetry = daba.ValidateStructure();
        Assert.Equal(64, afterRetry.Count);
        Assert.Equal(2, afterRetry.BlockCount);
        Assert.Equal(128, afterRetry.AllocatedSlotCapacity);
        Assert.Equal(FoldOffset(model), daba.Aggregate);
    }

    /// <summary>Checks every reachable identity callback failure, including the O(1) clear path.</summary>
    [Fact]
    public void EmptyExceptions_LeaveExactStateUnchangedAndUsable()
    {
        foreach (var inserting in new[] { true, false })
        {
            var maximum = FindMaximumEmptyCallbacks(inserting);
            Assert.InRange(maximum, 1, 2);
            for (var ordinal = 1; ordinal <= maximum; ordinal++)
            {
                var history = FindEmptyHistory(inserting, ordinal);
                ThrowingEmptyMonoid.Reset();
                Assert.True(TryReplay<ThrowingEmptyMonoid>(history, out var daba, out var model));
                var beforeStatistics = daba.ValidateStructure();
                var beforeAggregate = FoldOffset(model);

                ThrowingEmptyMonoid.ThrowOn(ordinal);
                Assert.Throws<CallbackException>(() => ApplyCandidate(daba, inserting));
                Assert.Equal(ordinal, ThrowingEmptyMonoid.EmptyCount);

                ThrowingEmptyMonoid.Reset();
                Assert.Equal(beforeStatistics, daba.ValidateStructure());
                Assert.Equal(beforeAggregate, daba.Aggregate);
                RetryCandidateAndContinue(daba, model, inserting);
            }
        }

        ThrowingEmptyMonoid.Reset();
        var clearTarget = new DabaLite<int, ThrowingEmptyMonoid>();
        clearTarget.Insert(23);
        var clearStatistics = clearTarget.ValidateStructure();
        var clearAggregate = clearTarget.Aggregate;
        ThrowingEmptyMonoid.ThrowOn(1);
        Assert.Throws<CallbackException>(clearTarget.Clear);
        ThrowingEmptyMonoid.Reset();
        Assert.Equal(clearStatistics, clearTarget.ValidateStructure());
        Assert.Equal(clearAggregate, clearTarget.Aggregate);
        clearTarget.Clear();
        Assert.True(clearTarget.IsEmpty);
    }

    /// <summary>Proves retired chunks and an evicted reference are collectible while the DABA remains alive.</summary>
    [Fact]
    public void RetiredStorage_DoesNotRemainRootedByTheLiveAggregator()
    {
        var daba = new DabaLite<int, OffsetMonoid>();
        var retiredBlock = CaptureFirstBlock(daba);
        for (var i = 0; i < 129; i++)
            daba.Insert(i + 20);
        for (var i = 0; i < 64; i++)
            daba.Evict();
        Assert.Equal(2, daba.ValidateStructure().BlockCount);
        CollectUntilDead(retiredBlock);
        Assert.False(retiredBlock.IsAlive);
        GC.KeepAlive(daba);

        var (referenceDaba, evictedToken) = CreatePromptReleaseScenario();
        Assert.False(referenceDaba.Aggregate.IsIdentity);
        CollectUntilDead(evictedToken);
        Assert.False(evictedToken.IsAlive);
        GC.KeepAlive(referenceDaba);
    }

    /// <summary>Checks that clear performs no combine, releases all chunks, and leaves a reusable one-block state.</summary>
    [Fact]
    public void Clear_UsesNoCombineAndResetsToOneReusableBlock()
    {
        CountingOffsetMonoid.Reset();
        var daba = new DabaLite<int, CountingOffsetMonoid>();
        for (var i = 0; i < 257; i++)
            daba.Insert(i + 20);

        CountingOffsetMonoid.Reset();
        Assert.Equal(257, daba.ValidateStructure().Count);
        Assert.Equal(0, CountingOffsetMonoid.CombineCount);
        Assert.Equal(0, CountingOffsetMonoid.EmptyCount);

        daba.Clear();
        Assert.Equal(0, CountingOffsetMonoid.CombineCount);
        Assert.Equal(1, CountingOffsetMonoid.EmptyCount);
        var statistics = daba.ValidateStructure();
        Assert.Equal(0, statistics.Count);
        Assert.Equal(1, statistics.BlockCount);
        Assert.Equal(64, statistics.AllocatedSlotCapacity);
        Assert.Equal(64, statistics.SlackSlotCount);

        CountingOffsetMonoid.Reset();
        daba.Clear();
        Assert.Equal(0, CountingOffsetMonoid.CombineCount);
        Assert.Equal(0, CountingOffsetMonoid.EmptyCount);

        daba.Insert(20);
        daba.Insert(30);
        Assert.Equal(39, daba.Aggregate);
        Assert.Equal(2, daba.ValidateStructure().Count);
    }

    private static void AssertMatrixState(DabaLite<Matrix, MatrixMonoid> daba, Queue<Matrix> model)
    {
        var statistics = daba.ValidateStructure();
        Assert.Equal(model.Count, statistics.Count);
        Assert.Equal(FoldMatrices(model), daba.Aggregate);
    }

    private static Matrix FoldMatrices(IEnumerable<Matrix> values)
    {
        var result = Matrix.Identity;
        foreach (var value in values)
            result = Matrix.Multiply(result, value);
        return result;
    }

    private static void AssertOffsetState(
        DabaLite<int, CountingOffsetMonoid> daba,
        Queue<int> model,
        ref int maximumQuery)
    {
        Assert.Equal(model.Count, daba.ValidateStructure().Count);
        CountingOffsetMonoid.Reset();
        Assert.Equal(FoldOffset(model), daba.Aggregate);
        Assert.InRange(CountingOffsetMonoid.CombineCount, 0, 1);
        maximumQuery = Math.Max(maximumQuery, CountingOffsetMonoid.CombineCount);
    }

    private static int FoldOffset(IEnumerable<int> values)
    {
        var result = OffsetIdentity;
        foreach (var value in values)
            result = result + value - OffsetIdentity;
        return result;
    }

    private static FixupPhase ClassifyNextFixup(DabaLiteStatistics statistics, bool evicting)
    {
        if ((!evicting && statistics.Count == 0) || (evicting && statistics.FrontLength == 1))
            return FixupPhase.Singleton;
        if (statistics.LeftLength + statistics.RightLength + statistics.AccumulatorLength == 0)
            return FixupPhase.FlipAndShrink;
        return statistics.LeftLength == 0 ? FixupPhase.Shift : FixupPhase.Shrink;
    }

    private static List<bool> FindCombineHistory(bool inserting, int minimumCallbacks) =>
        FindHistory<ThrowingCombineMonoid>(
            inserting,
            minimumCallbacks,
            ThrowingCombineMonoid.Reset,
            () => ThrowingCombineMonoid.CombineCount);

    private static List<bool> FindEmptyHistory(bool inserting, int minimumCallbacks) =>
        FindHistory<ThrowingEmptyMonoid>(
            inserting,
            minimumCallbacks,
            ThrowingEmptyMonoid.Reset,
            () => ThrowingEmptyMonoid.EmptyCount);

    private static int FindMaximumEmptyCallbacks(bool inserting)
    {
        var maximum = 0;
        for (var ordinal = 1; ordinal <= 3; ordinal++)
        {
            try
            {
                _ = FindEmptyHistory(inserting, ordinal);
                maximum = ordinal;
            }
            catch (InvalidOperationException)
            {
                break;
            }
        }
        return maximum;
    }

    private static List<bool> FindHistory<TMonoid>(
        bool inserting,
        int minimumCallbacks,
        Action reset,
        Func<int> callbackCount)
        where TMonoid : IMonoid<int>
    {
        for (var length = 0; length <= 10; length++)
        {
            for (var mask = 0; mask < 1 << length; mask++)
            {
                var history = DecodeHistory(mask, length);
                reset();
                if (!TryReplay<TMonoid>(history, out var daba, out var model) || (!inserting && model.Count == 0))
                    continue;
                reset();
                ApplyCandidate(daba, inserting);
                if (callbackCount() >= minimumCallbacks)
                {
                    reset();
                    return history;
                }
            }
        }
        throw new InvalidOperationException("No short DABA history reaches the requested callback ordinal.");
    }

    private static List<bool> DecodeHistory(int mask, int length)
    {
        var result = new List<bool>(length);
        for (var i = 0; i < length; i++)
            result.Add((mask & (1 << i)) != 0);
        return result;
    }

    private static bool TryReplay<TMonoid>(
        IReadOnlyList<bool> history,
        out DabaLite<int, TMonoid> daba,
        out Queue<int> model)
        where TMonoid : IMonoid<int>
    {
        daba = new DabaLite<int, TMonoid>();
        model = new Queue<int>();
        var next = 20;
        foreach (var inserting in history)
        {
            if (inserting)
            {
                daba.Insert(next);
                model.Enqueue(next++);
            }
            else
            {
                if (model.Count == 0)
                    return false;
                daba.Evict();
                model.Dequeue();
            }
        }
        return true;
    }

    private static void ApplyCandidate<TMonoid>(DabaLite<int, TMonoid> daba, bool inserting)
        where TMonoid : IMonoid<int>
    {
        if (inserting)
            daba.Insert(101);
        else
            daba.Evict();
    }

    private static void RetryCandidateAndContinue<TMonoid>(
        DabaLite<int, TMonoid> daba,
        Queue<int> model,
        bool inserting)
        where TMonoid : IMonoid<int>
    {
        ApplyCandidate(daba, inserting);
        if (inserting)
            model.Enqueue(101);
        else
            model.Dequeue();

        for (var i = 0; i < 24; i++)
        {
            daba.Insert(200 + i);
            model.Enqueue(200 + i);
            if ((i & 1) == 0)
            {
                daba.Evict();
                model.Dequeue();
            }
            Assert.Equal(model.Count, daba.ValidateStructure().Count);
            Assert.Equal(FoldOffset(model), daba.Aggregate);
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference CaptureFirstBlock<T, TMonoid>(DabaLite<T, TMonoid> daba)
        where TMonoid : IMonoid<T>
    {
        var queue = typeof(DabaLite<T, TMonoid>)
            .GetField("_queue", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(daba)!;
        var block = queue.GetType()
            .GetField("_first", BindingFlags.Instance | BindingFlags.NonPublic)!
            .GetValue(queue)!;
        return new WeakReference(block);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (DabaLite<ReferenceAggregate, FirstReferenceMonoid> Daba, WeakReference Token)
        CreatePromptReleaseScenario()
    {
        var daba = new DabaLite<ReferenceAggregate, FirstReferenceMonoid>();
        var victim = new object();
        var weak = new WeakReference(victim);
        daba.Insert(new ReferenceAggregate(victim));
        daba.Insert(new ReferenceAggregate(new object()));
        daba.Evict();
        return (daba, weak);
    }

    private static void CollectUntilDead(WeakReference reference)
    {
        for (var i = 0; i < 5 && reference.IsAlive; i++)
        {
            GC.Collect(2, GCCollectionMode.Forced, blocking: true, compacting: true);
            GC.WaitForPendingFinalizers();
        }
    }

    private enum FixupPhase
    {
        Singleton,
        FlipAndShrink,
        Shift,
        Shrink,
    }

    private readonly record struct Matrix(long M00, long M01, long M10, long M11)
    {
        private const long Modulus = 1_000_003;

        internal static Matrix Identity { get; } = new(1, 0, 0, 1);

        internal static Matrix Create(int seed) => new(
            Math.Abs((long)seed * 17 + 3) % Modulus,
            Math.Abs((long)seed * 29 + 5) % Modulus,
            Math.Abs((long)seed * 43 + 7) % Modulus,
            Math.Abs((long)seed * 61 + 11) % Modulus);

        internal static Matrix Multiply(Matrix left, Matrix right) => new(
            (left.M00 * right.M00 + left.M01 * right.M10) % Modulus,
            (left.M00 * right.M01 + left.M01 * right.M11) % Modulus,
            (left.M10 * right.M00 + left.M11 * right.M10) % Modulus,
            (left.M10 * right.M01 + left.M11 * right.M11) % Modulus);
    }

    private readonly struct MatrixMonoid : IMonoid<Matrix>
    {
        public static Matrix Empty => Matrix.Identity;

        public static Matrix Combine(Matrix left, Matrix right) => Matrix.Multiply(left, right);
    }

    private readonly struct OffsetMonoid : IMonoid<int>
    {
        public static int Empty => OffsetIdentity;

        public static int Combine(int left, int right) => left + right - OffsetIdentity;
    }

    private readonly struct CountingOffsetMonoid : IMonoid<int>
    {
        public static int CombineCount { get; private set; }

        public static int EmptyCount { get; private set; }

        public static int Empty
        {
            get
            {
                EmptyCount++;
                return OffsetIdentity;
            }
        }

        public static int Combine(int left, int right)
        {
            CombineCount++;
            return left + right - OffsetIdentity;
        }

        public static void Reset()
        {
            CombineCount = 0;
            EmptyCount = 0;
        }
    }

    private readonly struct ThrowingCombineMonoid : IMonoid<int>
    {
        private static int _throwOn;

        public static int CombineCount { get; private set; }

        public static int Empty => OffsetIdentity;

        public static int Combine(int left, int right)
        {
            CombineCount++;
            if (CombineCount == _throwOn)
                throw new CallbackException();
            return left + right - OffsetIdentity;
        }

        public static void Reset()
        {
            CombineCount = 0;
            _throwOn = 0;
        }

        public static void ThrowOn(int ordinal)
        {
            CombineCount = 0;
            _throwOn = ordinal;
        }
    }

    private readonly struct ThrowingEmptyMonoid : IMonoid<int>
    {
        private static int _throwOn;

        public static int EmptyCount { get; private set; }

        public static int Empty
        {
            get
            {
                EmptyCount++;
                if (EmptyCount == _throwOn)
                    throw new CallbackException();
                return OffsetIdentity;
            }
        }

        public static int Combine(int left, int right) => left + right - OffsetIdentity;

        public static void Reset()
        {
            EmptyCount = 0;
            _throwOn = 0;
        }

        public static void ThrowOn(int ordinal)
        {
            EmptyCount = 0;
            _throwOn = ordinal;
        }
    }

    private sealed class ReferenceAggregate(object? token, bool isIdentity = false)
    {
        internal static ReferenceAggregate Identity { get; } = new(null, isIdentity: true);

        internal object? Token { get; } = token;

        internal bool IsIdentity { get; } = isIdentity;
    }

    private readonly struct FirstReferenceMonoid : IMonoid<ReferenceAggregate>
    {
        public static ReferenceAggregate Empty => ReferenceAggregate.Identity;

        public static ReferenceAggregate Combine(ReferenceAggregate left, ReferenceAggregate right) =>
            left.IsIdentity ? right : left;
    }

    private sealed class CallbackException : Exception;
}
