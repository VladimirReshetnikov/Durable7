using Durable7.Hamt;

namespace Durable7.FingerTree.Benchmarks;

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

        var matrixCaseCount = 0;
        matrixCaseCount += VerifyAxisCases([1, 8, 32, 1_024, 100_000], Axis2HashShape.Uniform);
        matrixCaseCount += VerifyAxisCases([8, 32, 1_024], Axis2HashShape.ClusteredPrefix);
        matrixCaseCount += VerifyAxisCases([8, 32, 1_024], Axis2HashShape.FullCollision);
        matrixCaseCount += VerifyNullCollisionCases();

        VerifyReferenceRepresentativeCase();
        VerifyComparerCallbackCounts();
        VerifyRetainedRecordFormatting();
        VerifyBreakEvenArithmetic();
        VerifyComparerFailureContracts();

        Console.WriteLine(
            $"AXIS2_FROZEN_VERIFY_V2,matrix-cases={matrixCaseCount},representative-cases=1," +
            "to-persistent=passed,layout-diagnostics=passed," +
            "callback-counts=passed,retained-record=passed," +
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
                var fixture = new FrozenF0AxisFixture(
                    count,
                    hitPercentage,
                    shape,
                    lane: $"verify-{shape}",
                    emitRetainedDiagnostics: false);
                VerifyAxisFixture(fixture);
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
                var fixture = new FrozenF0NullCollisionFixture(
                    count,
                    hitPercentage,
                    emitRetainedDiagnostics: false);
                VerifyNullCollisionFixture(fixture);
                caseCount++;
            }
        }

        return caseCount;
    }

    private static void VerifyAxisFixture(FrozenF0AxisFixture fixture)
    {
        VerifyLinearSlotParity(fixture.Packed.Diagnostics, fixture.RobinHood.Diagnostics);
        VerifyLayoutDiagnostics(fixture.Packed.Diagnostics, fixture.Persistent.Count, powerOfTwoSlots: false);
        VerifyLayoutDiagnostics(fixture.RobinHood.Diagnostics, fixture.Persistent.Count, powerOfTwoSlots: false);
        VerifyLayoutDiagnostics(fixture.Quadratic.Diagnostics, fixture.Persistent.Count, powerOfTwoSlots: true);

        VerifyPrototypeRoundTrip(
            fixture.Persistent,
            fixture.ConvertPackedToPersistent(),
            map => PackedFrozenMapPrototype<Axis2HashKey, int>.Create(map).ToPersistent(),
            "linear");
        VerifyPrototypeRoundTrip(
            fixture.Persistent,
            fixture.ConvertRobinHoodToPersistent(),
            map => RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(map).ToPersistent(),
            "Robin-Hood");
        VerifyPrototypeRoundTrip(
            fixture.Persistent,
            fixture.ConvertQuadraticToPersistent(),
            map => QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(map).ToPersistent(),
            "quadratic");
    }

    private static void VerifyNullCollisionFixture(FrozenF0NullCollisionFixture fixture)
    {
        VerifyLinearSlotParity(fixture.Packed.Diagnostics, fixture.RobinHood.Diagnostics);
        VerifyLayoutDiagnostics(fixture.Packed.Diagnostics, fixture.Persistent.Count, powerOfTwoSlots: false);
        VerifyLayoutDiagnostics(fixture.RobinHood.Diagnostics, fixture.Persistent.Count, powerOfTwoSlots: false);
        VerifyLayoutDiagnostics(fixture.Quadratic.Diagnostics, fixture.Persistent.Count, powerOfTwoSlots: true);

        VerifyPrototypeRoundTrip(
            fixture.Persistent,
            fixture.ConvertPackedToPersistent(),
            map => PackedFrozenMapPrototype<string?, int>.Create(map).ToPersistent(),
            "linear null/collision");
        VerifyPrototypeRoundTrip(
            fixture.Persistent,
            fixture.ConvertRobinHoodToPersistent(),
            map => RobinHoodFrozenMapPrototype<string?, int>.Create(map).ToPersistent(),
            "Robin-Hood null/collision");
        VerifyPrototypeRoundTrip(
            fixture.Persistent,
            fixture.ConvertQuadraticToPersistent(),
            map => QuadraticFrozenMapPrototype<string?, int>.Create(map).ToPersistent(),
            "quadratic null/collision");
    }

    private static void VerifyLinearSlotParity(
        PackedFrozenMapPrototypeDiagnostics linear,
        PackedFrozenMapPrototypeDiagnostics robinHood)
    {
        if (linear.SlotCount != robinHood.SlotCount)
        {
            throw new InvalidOperationException(
                "The linear and Robin-Hood candidates must use the same fixed slot-count rule.");
        }
    }

    private static void VerifyLayoutDiagnostics(
        PackedFrozenMapPrototypeDiagnostics diagnostics,
        int expectedCount,
        bool powerOfTwoSlots)
    {
        if (diagnostics.EntryCount != expectedCount
            || diagnostics.EstimatedRetainedArrayBytes != checked(
                diagnostics.EstimatedEntryArrayBytes + diagnostics.EstimatedSlotArrayBytes))
        {
            throw new InvalidOperationException("A frozen prototype reported inconsistent retained-layout diagnostics.");
        }

        if (expectedCount == 0)
        {
            if (diagnostics.SlotCount != 0
                || diagnostics.LoadFactor != 0
                || diagnostics.EstimatedRetainedArrayBytes != 0)
            {
                throw new InvalidOperationException("An empty frozen prototype retained a nonempty array layout.");
            }

            return;
        }

        if (diagnostics.SlotCount <= expectedCount
            || diagnostics.LoadFactor <= 0
            || diagnostics.LoadFactor > 0.7 + 1e-12
            || diagnostics.EstimatedEntryArrayBytes <= 0
            || diagnostics.EstimatedSlotArrayBytes <= 0)
        {
            throw new InvalidOperationException("A frozen prototype violated its fixed load or retained-array contract.");
        }

        if (powerOfTwoSlots && (diagnostics.SlotCount & (diagnostics.SlotCount - 1)) != 0)
            throw new InvalidOperationException("The quadratic frozen prototype requires a power-of-two slot count.");
    }

    private static void VerifyPrototypeRoundTrip<TKey, TValue>(
        PersistentHashMap<TKey, TValue> source,
        PersistentHashMap<TKey, TValue> converted,
        Func<PersistentHashMap<TKey, TValue>, PersistentHashMap<TKey, TValue>> refreezeAndConvert,
        string layout)
    {
        VerifyPersistentSequence(source, converted, $"{layout} ToPersistent");
        VerifyPersistentSequence(
            converted,
            refreezeAndConvert(converted),
            $"{layout} freeze/persistent/freeze round trip");
    }

    private static void VerifyPersistentSequence<TKey, TValue>(
        PersistentHashMap<TKey, TValue> expected,
        PersistentHashMap<TKey, TValue> actual,
        string operation)
    {
        if (!ReferenceEquals(expected.Comparer, actual.Comparer) || expected.Count != actual.Count)
            throw new InvalidOperationException($"The {operation} changed comparer identity or count.");

        var expectedEnumerator = expected.GetEnumerator();
        var actualEnumerator = actual.GetEnumerator();
        while (expectedEnumerator.MoveNext())
        {
            if (!actualEnumerator.MoveNext()
                || !SameRepresentative(expectedEnumerator.Current.Key, actualEnumerator.Current.Key)
                || !SameRepresentative(expectedEnumerator.Current.Value, actualEnumerator.Current.Value))
            {
                throw new InvalidOperationException(
                    $"The {operation} changed source order or a stored key/value representative.");
            }
        }

        if (actualEnumerator.MoveNext())
            throw new InvalidOperationException($"The {operation} produced too many entries.");
    }

    private static bool SameRepresentative<T>(T expected, T actual) =>
        typeof(T).IsValueType
            ? EqualityComparer<T>.Default.Equals(expected, actual)
            : ReferenceEquals(expected, actual);

    private static void VerifyReferenceRepresentativeCase()
    {
        var comparer = new RepresentativeKeyComparer();
        var firstKey = new RepresentativeKey("alpha");
        var equivalentKey = new RepresentativeKey("ALPHA");
        var finalEquivalentKey = new RepresentativeKey("Alpha");
        var otherKey = new RepresentativeKey("beta");
        var initialValue = new RepresentativeValue("initial");
        var winningValue = new RepresentativeValue("winning");
        var equalWinningValue = new RepresentativeValue("winning");
        var otherValue = new RepresentativeValue("other");
        var source = PersistentHashMap<RepresentativeKey, RepresentativeValue>.CreateRange(
        [
            KeyValuePair.Create(firstKey, initialValue),
            KeyValuePair.Create(otherKey, otherValue),
            KeyValuePair.Create(equivalentKey, winningValue),
            KeyValuePair.Create(finalEquivalentKey, equalWinningValue),
        ],
        comparer);
        var query = new RepresentativeKey("aLpHa");

        if (!source.TryGetKey(query, out var sourceKey)
            || !source.TryGetValue(query, out var sourceValue)
            || !ReferenceEquals(sourceKey, firstKey)
            || !ReferenceEquals(sourceValue, winningValue))
        {
            throw new InvalidOperationException(
                "The reference-representative source fixture does not satisfy the CHAMP oracle.");
        }

        var linear = PackedFrozenMapPrototype<RepresentativeKey, RepresentativeValue>.Create(source);
        var robinHood = RobinHoodFrozenMapPrototype<RepresentativeKey, RepresentativeValue>.Create(source);
        var quadratic = QuadraticFrozenMapPrototype<RepresentativeKey, RepresentativeValue>.Create(source);
        if (!linear.TryGetKey(query, out var linearKey)
            || !linear.TryGetValue(query, out var linearValue)
            || !robinHood.TryGetKey(query, out var robinHoodKey)
            || !robinHood.TryGetValue(query, out var robinHoodValue)
            || !quadratic.TryGetKey(query, out var quadraticKey)
            || !quadratic.TryGetValue(query, out var quadraticValue)
            || !ReferenceEquals(linearKey, firstKey)
            || !ReferenceEquals(robinHoodKey, firstKey)
            || !ReferenceEquals(quadraticKey, firstKey)
            || !ReferenceEquals(linearValue, winningValue)
            || !ReferenceEquals(robinHoodValue, winningValue)
            || !ReferenceEquals(quadraticValue, winningValue))
        {
            throw new InvalidOperationException(
                "A frozen prototype changed the first key or last distinct value representative.");
        }

        var missing = new RepresentativeKey("missing");
        if (linear.TryGetKey(missing, out var linearMissing)
            || robinHood.TryGetKey(missing, out var robinHoodMissing)
            || quadratic.TryGetKey(missing, out var quadraticMissing)
            || !ReferenceEquals(linearMissing, missing)
            || !ReferenceEquals(robinHoodMissing, missing)
            || !ReferenceEquals(quadraticMissing, missing))
        {
            throw new InvalidOperationException(
                "A frozen prototype changed the failed TryGetKey query representative.");
        }

        VerifyPrototypeRoundTrip(
            source,
            linear.ToPersistent(),
            map => PackedFrozenMapPrototype<RepresentativeKey, RepresentativeValue>.Create(map).ToPersistent(),
            "linear reference-representative");
        VerifyPrototypeRoundTrip(
            source,
            robinHood.ToPersistent(),
            map => RobinHoodFrozenMapPrototype<RepresentativeKey, RepresentativeValue>.Create(map).ToPersistent(),
            "Robin-Hood reference-representative");
        VerifyPrototypeRoundTrip(
            source,
            quadratic.ToPersistent(),
            map => QuadraticFrozenMapPrototype<RepresentativeKey, RepresentativeValue>.Create(map).ToPersistent(),
            "quadratic reference-representative");
    }

    private static void VerifyComparerCallbackCounts()
    {
        var comparer = new Axis2CountingHashKeyComparer();
        var entries = Enumerable.Range(0, 8)
            .Select(index => KeyValuePair.Create(new Axis2HashKey(index, 17 + (index * 37)), index))
            .ToArray();
        var source = PersistentHashMap<Axis2HashKey, int>.CreateRange(entries, comparer);
        var hit = entries[3].Key;
        var miss = new Axis2HashKey(-1, 10_000);

        VerifyCandidateCallbackCounts(
            "linear",
            comparer,
            entries.Length,
            hit,
            miss,
            () => PackedFrozenMapPrototype<Axis2HashKey, int>.Create(source),
            (candidate, key) => candidate.TryGetValue(key, out _),
            (candidate, key) => (candidate.TryGetKey(key, out var actual), actual),
            candidate => candidate.ToPersistent());
        VerifyCandidateCallbackCounts(
            "Robin-Hood",
            comparer,
            entries.Length,
            hit,
            miss,
            () => RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(source),
            (candidate, key) => candidate.TryGetValue(key, out _),
            (candidate, key) => (candidate.TryGetKey(key, out var actual), actual),
            candidate => candidate.ToPersistent());
        VerifyCandidateCallbackCounts(
            "quadratic",
            comparer,
            entries.Length,
            hit,
            miss,
            () => QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(source),
            (candidate, key) => candidate.TryGetValue(key, out _),
            (candidate, key) => (candidate.TryGetKey(key, out var actual), actual),
            candidate => candidate.ToPersistent());
    }

    private static void VerifyCandidateCallbackCounts<TCandidate>(
        string layout,
        Axis2CountingHashKeyComparer comparer,
        int entryCount,
        Axis2HashKey hit,
        Axis2HashKey miss,
        Func<TCandidate> create,
        Func<TCandidate, Axis2HashKey, bool> tryGetValue,
        Func<TCandidate, Axis2HashKey, (bool Found, Axis2HashKey Actual)> tryGetKey,
        Func<TCandidate, PersistentHashMap<Axis2HashKey, int>> toPersistent)
    {
        comparer.Reset();
        var candidate = create();
        VerifyCallbackCounts(comparer, entryCount, equalityCount: 0, $"{layout} construction");

        comparer.Reset();
        if (!tryGetValue(candidate, hit))
            throw new InvalidOperationException($"The {layout} callback-count hit was not found.");
        VerifyCallbackCounts(comparer, hashCount: 1, equalityCount: 1, $"{layout} hit lookup");

        comparer.Reset();
        var missingResult = tryGetKey(candidate, miss);
        if (missingResult.Found || missingResult.Actual != miss)
            throw new InvalidOperationException($"The {layout} failed TryGetKey contract changed.");
        VerifyCallbackCounts(comparer, hashCount: 1, equalityCount: 0, $"{layout} miss lookup");

        comparer.Reset();
        _ = toPersistent(candidate);
        VerifyCallbackCounts(comparer, entryCount, equalityCount: 0, $"{layout} ToPersistent");
    }

    private static void VerifyCallbackCounts(
        Axis2CountingHashKeyComparer comparer,
        long hashCount,
        long equalityCount,
        string operation)
    {
        if (comparer.HashCallbackCount != hashCount || comparer.EqualityCallbackCount != equalityCount)
        {
            throw new InvalidOperationException(
                $"The {operation} callback counts were hash={comparer.HashCallbackCount}, " +
                $"equality={comparer.EqualityCallbackCount}; expected {hashCount}/{equalityCount}.");
        }
    }

    private static void VerifyRetainedRecordFormatting()
    {
        var diagnostics = new PackedFrozenMapPrototypeDiagnostics(
            EntryCount: 2,
            SlotCount: 4,
            LoadFactor: 0.5,
            EstimatedEntryArrayBytes: 48,
            EstimatedSlotArrayBytes: 32,
            EstimatedRetainedArrayBytes: 80);
        var actual = FrozenF0AxisFixture.FormatRetainedArrays(
            "culture-check",
            persistentEstimatedRetainedBytes: 123,
            diagnostics,
            diagnostics,
            diagnostics,
            bclFrozenEstimatedArrayBytes: 456);
        const string expected =
            "AXIS2_F1_RETAINED_V1,lane=culture-check,entries=2,persistent-estimated-graph=123," +
            "linear-slots=4,linear-load=0.5000,linear-entry-array=48,linear-slot-array=32," +
            "linear-retained-arrays=80,robin-hood-slots=4,robin-hood-load=0.5000," +
            "robin-hood-entry-array=48,robin-hood-slot-array=32,robin-hood-retained-arrays=80," +
            "quadratic-slots=4,quadratic-load=0.5000,quadratic-entry-array=48," +
            "quadratic-slot-array=32,quadratic-retained-arrays=80,bcl-frozen-retained-arrays=456";
        if (!string.Equals(actual, expected, StringComparison.Ordinal))
            throw new InvalidOperationException("The retained-layout record is not invariant or machine-parseable.");
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
        ExpectVerifierCallback(
            () => _ = linear.ToPersistent(),
            "linear conversion hash callback");
        ExpectVerifierCallback(
            () => _ = robinHood.ToPersistent(),
            "Robin-Hood conversion hash callback");
        ExpectVerifierCallback(
            () => _ = quadratic.ToPersistent(),
            "quadratic conversion hash callback");
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
        ExpectVerifierCallback(
            () => _ = linear.ToPersistent(),
            "linear conversion equality callback");
        ExpectVerifierCallback(
            () => _ = robinHood.ToPersistent(),
            "Robin-Hood conversion equality callback");
        ExpectVerifierCallback(
            () => _ = quadratic.ToPersistent(),
            "quadratic conversion equality callback");
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

        VerifyLayoutDiagnostics(linear.Diagnostics, expectedCount: 0, powerOfTwoSlots: false);
        VerifyLayoutDiagnostics(robinHood.Diagnostics, expectedCount: 0, powerOfTwoSlots: false);
        VerifyLayoutDiagnostics(quadratic.Diagnostics, expectedCount: 0, powerOfTwoSlots: true);
        VerifyLinearSlotParity(linear.Diagnostics, robinHood.Diagnostics);
        VerifyPersistentSequence(source, linear.ToPersistent(), "empty linear ToPersistent");
        VerifyPersistentSequence(source, robinHood.ToPersistent(), "empty Robin-Hood ToPersistent");
        VerifyPersistentSequence(source, quadratic.ToPersistent(), "empty quadratic ToPersistent");
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

    private sealed class RepresentativeKey(string text)
    {
        internal string Text { get; } = text;
    }

    private sealed record RepresentativeValue(string Text);

    private sealed class RepresentativeKeyComparer : IEqualityComparer<RepresentativeKey>
    {
        public bool Equals(RepresentativeKey? left, RepresentativeKey? right) =>
            ReferenceEquals(left, right)
            || (left is not null
                && right is not null
                && StringComparer.OrdinalIgnoreCase.Equals(left.Text, right.Text));

        public int GetHashCode(RepresentativeKey value) => Axis2BenchmarkPolicy.FullCollisionHash;
    }
}
