using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

/// <summary>
/// Runs the frozen F0/F1 semantic matrix without BenchmarkDotNet, timing, or retained-size output.
/// </summary>
internal static class Axis2FrozenCorrectnessVerifier
{
    private const string Command = "--verify-axis2-frozen-layouts";

    internal static bool TryRun(string[] args)
    {
        if (args.Length == 0 || !string.Equals(args[0], Command, StringComparison.Ordinal))
            return false;

        if (args.Length != 1)
            throw new ArgumentException($"Usage: {Command}.");

        var caseCount = 0;
        caseCount += VerifyAxisCases([1, 8, 32, 1_024, 100_000], Axis2HashShape.Uniform);
        caseCount += VerifyAxisCases([8, 32, 1_024], Axis2HashShape.ClusteredPrefix);
        caseCount += VerifyAxisCases([8, 32, 1_024], Axis2HashShape.FullCollision);
        caseCount += VerifyNullCollisionCases();

        VerifyBreakEvenArithmetic();
        VerifyComparerFailureContracts();

        Console.WriteLine(
            $"AXIS2_FROZEN_VERIFY_V1,cases={caseCount}," +
            "break-even-arithmetic=passed,comparer-failures=passed.");
        return true;
    }

    private static int VerifyAxisCases(int[] counts, Axis2HashShape shape)
    {
        var caseCount = 0;
        foreach (var count in counts)
        {
            foreach (var hitPercentage in Axis2BenchmarkPolicy.LookupHitPercentages)
            {
                _ = new FrozenF0AxisFixture(
                    count,
                    hitPercentage,
                    shape,
                    lane: $"verify-{shape}",
                    emitRetainedDiagnostics: false);
                caseCount++;
            }
        }

        return caseCount;
    }

    private static int VerifyNullCollisionCases()
    {
        var caseCount = 0;
        foreach (var count in new[] { 8, 32 })
        {
            foreach (var hitPercentage in Axis2BenchmarkPolicy.LookupHitPercentages)
            {
                _ = new FrozenF0NullCollisionFixture(
                    count,
                    hitPercentage,
                    emitRetainedDiagnostics: false);
                caseCount++;
            }
        }

        return caseCount;
    }

    private static void VerifyBreakEvenArithmetic()
    {
        var reads = FrozenF0BreakEven.CalculateReads(
            packedConstructionNanoseconds: 100,
            persistentLookupBatchNanoseconds: 200,
            packedLookupBatchNanoseconds: 100,
            readsPerBatch: 10);
        if (reads != 10)
            throw new InvalidOperationException("The F0 break-even helper returned the wrong whole-read boundary.");

        if (!double.IsPositiveInfinity(FrozenF0BreakEven.CalculateReads(100, 100, 100, 10))
            || !double.IsPositiveInfinity(FrozenF0BreakEven.CalculateReads(100, 90, 100, 10)))
        {
            throw new InvalidOperationException(
                "The F0 break-even helper must reject equal or slower frozen lookup with infinity.");
        }

        ExpectArgumentOutOfRange(
            () => _ = FrozenF0BreakEven.CalculateReads(double.NaN, 100, 50, 10),
            "non-finite construction mean");
        ExpectArgumentOutOfRange(
            () => _ = FrozenF0BreakEven.CalculateReads(100, 100, 50, 0),
            "non-positive batch size");
    }

    private static void VerifyComparerFailureContracts()
    {
        VerifyConstructionHashFailure();
        VerifyLookupHashFailure();
        VerifyLookupEqualityFailure();
        VerifyEmptyLookupSkipsComparer();
    }

    private static void VerifyConstructionHashFailure()
    {
        var comparer = new ArmedComparer();
        var source = CreateFailureSource(comparer);
        comparer.ThrowOnHash = true;

        ExpectVerifierCallback(
            () => _ = PackedFrozenMapPrototype<Axis2HashKey, int>.Create(source),
            "linear construction hash callback");
        ExpectVerifierCallback(
            () => _ = RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(source),
            "Robin-Hood construction hash callback");
        ExpectVerifierCallback(
            () => _ = QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(source),
            "quadratic construction hash callback");
    }

    private static void VerifyLookupHashFailure()
    {
        var comparer = new ArmedComparer();
        var source = CreateFailureSource(comparer);
        var linear = PackedFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var robinHood = RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var quadratic = QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        comparer.ThrowOnHash = true;
        var query = new Axis2HashKey(1, Axis2BenchmarkPolicy.FullCollisionHash);

        ExpectVerifierCallback(
            () => _ = linear.TryGetValue(query, out _),
            "linear lookup hash callback");
        ExpectVerifierCallback(
            () => _ = robinHood.TryGetValue(query, out _),
            "Robin-Hood lookup hash callback");
        ExpectVerifierCallback(
            () => _ = quadratic.TryGetValue(query, out _),
            "quadratic lookup hash callback");
    }

    private static void VerifyLookupEqualityFailure()
    {
        var comparer = new ArmedComparer();
        var source = CreateFailureSource(comparer);
        var linear = PackedFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var robinHood = RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var quadratic = QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        comparer.ThrowOnEquality = true;
        var missing = new Axis2HashKey(10_000, Axis2BenchmarkPolicy.FullCollisionHash);

        ExpectVerifierCallback(
            () => _ = linear.TryGetValue(missing, out _),
            "linear lookup equality callback");
        ExpectVerifierCallback(
            () => _ = robinHood.TryGetValue(missing, out _),
            "Robin-Hood lookup equality callback");
        ExpectVerifierCallback(
            () => _ = quadratic.TryGetValue(missing, out _),
            "quadratic lookup equality callback");
    }

    private static void VerifyEmptyLookupSkipsComparer()
    {
        var comparer = new ArmedComparer();
        var source = PersistentHashMap<Axis2HashKey, int>.CreateRange(
            Array.Empty<KeyValuePair<Axis2HashKey, int>>(),
            comparer);
        comparer.ThrowOnHash = true;
        comparer.ThrowOnEquality = true;

        var linear = PackedFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var robinHood = RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var quadratic = QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(source);
        var query = new Axis2HashKey(0, Axis2BenchmarkPolicy.FullCollisionHash);

        if (linear.TryGetValue(query, out _)
            || robinHood.TryGetValue(query, out _)
            || quadratic.TryGetValue(query, out _))
        {
            throw new InvalidOperationException("An empty frozen prototype reported a present key.");
        }

        if (!ReferenceEquals(linear.Comparer, comparer)
            || !ReferenceEquals(robinHood.Comparer, comparer)
            || !ReferenceEquals(quadratic.Comparer, comparer))
        {
            throw new InvalidOperationException("An empty frozen prototype changed comparer identity.");
        }
    }

    private static PersistentHashMap<Axis2HashKey, int> CreateFailureSource(ArmedComparer comparer) =>
        PersistentHashMap<Axis2HashKey, int>.CreateRange(
        [
            KeyValuePair.Create(new Axis2HashKey(1, Axis2BenchmarkPolicy.FullCollisionHash), 17),
            KeyValuePair.Create(new Axis2HashKey(2, Axis2BenchmarkPolicy.FullCollisionHash), 34),
        ],
        comparer);

    private static void ExpectArgumentOutOfRange(Action action, string operation)
    {
        try
        {
            action();
        }
        catch (ArgumentOutOfRangeException)
        {
            return;
        }

        throw new InvalidOperationException($"The verifier expected {operation} to throw ArgumentOutOfRangeException.");
    }

    private static void ExpectVerifierCallback(Action action, string operation)
    {
        try
        {
            action();
        }
        catch (VerifierCallbackException)
        {
            return;
        }

        throw new InvalidOperationException($"The verifier expected {operation} to propagate the comparer exception.");
    }

    private sealed class ArmedComparer : IEqualityComparer<Axis2HashKey>
    {
        internal bool ThrowOnHash { get; set; }

        internal bool ThrowOnEquality { get; set; }

        public bool Equals(Axis2HashKey left, Axis2HashKey right)
        {
            if (ThrowOnEquality)
                throw new VerifierCallbackException();

            return left.Value == right.Value;
        }

        public int GetHashCode(Axis2HashKey value)
        {
            if (ThrowOnHash)
                throw new VerifierCallbackException();

            return value.Hash;
        }
    }

    private sealed class VerifierCallbackException : Exception
    {
    }
}
