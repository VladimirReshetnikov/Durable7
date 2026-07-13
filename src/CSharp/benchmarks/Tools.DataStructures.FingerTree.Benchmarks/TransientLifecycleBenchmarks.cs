using System.Globalization;
using BenchmarkDotNet.Attributes;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

/// <summary>
/// Qualifies the Axis 2 T0 owner-token opportunity without implementing or timing a transient.
/// Structural and callback counters run only in setup and are emitted from cleanup.
/// </summary>
[MemoryDiagnoser]
public class TransientLifecycleBenchmarks
{
    private PersistentHashMap<Axis2HashKey, int> _base = null!;
    private KeyValuePair<Axis2HashKey, int>[] _baseEntries = null!;
    private Axis2HashEdit[] _edits = null!;
    private int _collisionBucketSize;
    private Axis2TransientOpportunityCounters _opportunityCounters;

    [ParamsSource(nameof(WorkloadValues))]
    public Axis2TransientWorkload Workload { get; set; }

    public IEnumerable<Axis2TransientWorkload> WorkloadValues => Axis2BenchmarkPolicy.TransientWorkloads;

    [Params(
        Axis2EditHistory.RepeatedKey,
        Axis2EditHistory.ClusteredPrefix,
        Axis2EditHistory.DisjointInsert,
        Axis2EditHistory.FullCollision,
        Axis2EditHistory.Removal,
        Axis2EditHistory.Mixed)]
    public Axis2EditHistory History { get; set; }

    [Params(
        Axis2PublicationCadence.EveryEdit,
        Axis2PublicationCadence.Every8,
        Axis2PublicationCadence.Every64,
        Axis2PublicationCadence.End)]
    public Axis2PublicationCadence PublicationCadence { get; set; }

    /// <summary>Initializes the direct-persistent lane and collects its untimed opportunity counters.</summary>
    [GlobalSetup(Target = nameof(PersistentHistory))]
    public void SetupPersistentHistory()
    {
        SetupCore();
        _opportunityCounters = CollectPersistentOpportunityCounters();
    }

    /// <summary>Initializes the canonical-builder control without running graph/callback diagnostics.</summary>
    [GlobalSetup(Target = nameof(BulkBuilderHistory))]
    public void SetupBulkBuilderHistory() => SetupCore();

    /// <summary>Writes one machine-readable counter row to the raw BenchmarkDotNet log.</summary>
    [GlobalCleanup(Target = nameof(PersistentHistory))]
    public void EmitPersistentOpportunityCounters() =>
        Console.WriteLine(_opportunityCounters.ToCsv());

    /// <summary>Applies every edit as an ordinary persistent operation and observes requested publications.</summary>
    [Benchmark(Baseline = true)]
    [BenchmarkCategory("Axis2T0", "EditPublication", "Persistent")]
    public Axis2HistoryResult PersistentHistory()
    {
        var map = _base;
        var checksum = (long)Axis2BenchmarkPolicy.Seed;
        for (var index = 0; index < _edits.Length; index++)
        {
            map = Apply(map, _edits[index]);
            if (Axis2BenchmarkPolicy.IsPublicationBoundary(index + 1, _edits.Length, PublicationCadence))
            {
                checksum = Axis2BenchmarkPolicy.AddPublicationChecksum(
                    checksum,
                    map.Count,
                    index + 1);
            }
        }

        return new Axis2HistoryResult(map, checksum);
    }

    /// <summary>
    /// Stages each publication batch and rebuilds canonical CHAMP shape. This is the existing
    /// construction control; it is deliberately not described as an owner-token transient.
    /// </summary>
    [Benchmark]
    [BenchmarkCategory("Axis2T0", "EditPublication", "BulkBuilder")]
    public Axis2HistoryResult BulkBuilderHistory()
    {
        var map = _base;
        var checksum = (long)Axis2BenchmarkPolicy.Seed;
        var batchSize = Axis2BenchmarkPolicy.GetPublicationBatchSize(PublicationCadence, _edits.Length);
        for (var start = 0; start < _edits.Length; start += batchSize)
        {
            var end = Math.Min(start + batchSize, _edits.Length);
            map = RebuildWithBulkBuilder(map, _edits.AsSpan(start, end - start));
            checksum = Axis2BenchmarkPolicy.AddPublicationChecksum(checksum, map.Count, end);
        }

        return new Axis2HistoryResult(map, checksum);
    }

    private void SetupCore()
    {
        var dataset = Axis2BenchmarkPolicy.CreateTransientDataset(Workload, History);
        _baseEntries = dataset.BaseEntries;
        _edits = dataset.Edits;
        _collisionBucketSize = dataset.CollisionBucketSize;
        _base = PersistentHashMap<Axis2HashKey, int>.CreateRange(
            _baseEntries,
            Axis2HashKeyComparer.Instance);
    }

    private Axis2TransientOpportunityCounters CollectPersistentOpportunityCounters()
    {
        var map = _base;
        var diagnosticEditCount = Math.Min(_edits.Length, Axis2BenchmarkPolicy.TransientDiagnosticEditLimit);
        long changedOperationCount = 0;
        long wrapperAllocationCount = 0;
        long nonPublicationWrapperCount = 0;
        long nodeVisitCount = 0;
        long copiedNodeCount = 0;
        long copiedArrayCount = 0;
        long nonPublicationCopiedNodeCount = 0;
        long nonPublicationCopiedArrayCount = 0;

        for (var index = 0; index < _edits.Length; index++)
        {
            var previous = map;
            var edit = _edits[index];
            map = Apply(previous, edit);
            var changed = !ReferenceEquals(previous, map);
            if (changed)
            {
                changedOperationCount++;
                wrapperAllocationCount++;
                if (!Axis2BenchmarkPolicy.IsPublicationBoundary(index + 1, _edits.Length, PublicationCadence))
                    nonPublicationWrapperCount++;
            }

            if (index >= diagnosticEditCount)
                continue;

            // GetMutationDiagnostics re-hashes the key to report node visits. This replay uses the
            // ordinary comparer; the separate callback-count replay below never invokes diagnostics.
            var diagnostics = map.GetMutationDiagnostics(previous, edit.Key);
            nodeVisitCount += diagnostics.NodeVisits;
            copiedNodeCount += diagnostics.CopiedNodeCount;
            copiedArrayCount += diagnostics.CopiedArrayCount;
            if (!Axis2BenchmarkPolicy.IsPublicationBoundary(index + 1, _edits.Length, PublicationCadence))
            {
                nonPublicationCopiedNodeCount += diagnostics.CopiedNodeCount;
                nonPublicationCopiedArrayCount += diagnostics.CopiedArrayCount;
            }
        }

        var countingComparer = new Axis2CountingHashKeyComparer();
        var callbackMap = PersistentHashMap<Axis2HashKey, int>.CreateRange(_baseEntries, countingComparer);
        countingComparer.Reset();
        foreach (var edit in _edits)
            callbackMap = Apply(callbackMap, edit);

        if (callbackMap.Count != map.Count || ComputeSemanticChecksum(callbackMap) != ComputeSemanticChecksum(map))
            throw new InvalidOperationException("The callback-count replay diverged from the diagnostic replay.");

        return new Axis2TransientOpportunityCounters(
            Workload.BaseCount,
            Workload.EditCount,
            diagnosticEditCount,
            Axis2BenchmarkPolicy.GetPublicationCount(_edits.Length, PublicationCadence),
            History,
            PublicationCadence,
            _collisionBucketSize,
            changedOperationCount,
            wrapperAllocationCount,
            nonPublicationWrapperCount,
            nodeVisitCount,
            copiedNodeCount,
            copiedArrayCount,
            nonPublicationCopiedNodeCount,
            nonPublicationCopiedArrayCount,
            countingComparer.HashCallbackCount,
            countingComparer.EqualityCallbackCount);
    }

    private static PersistentHashMap<Axis2HashKey, int> RebuildWithBulkBuilder(
        PersistentHashMap<Axis2HashKey, int> source,
        ReadOnlySpan<Axis2HashEdit> edits)
    {
        // The final edit for each key is sufficient within a publication batch. Dictionary preserves
        // the first stored key representative while replacing only its edit payload.
        var finalEdits = new Dictionary<Axis2HashKey, Axis2HashEdit>(
            edits.Length,
            Axis2HashKeyComparer.Instance);
        foreach (var edit in edits)
            finalEdits[edit.Key] = edit;

        var builder = PersistentHashMap<Axis2HashKey, int>.CreateBulkBuilder(Axis2HashKeyComparer.Instance);
        foreach (var entry in source)
        {
            if (!finalEdits.ContainsKey(entry.Key))
                builder.SetItem(entry.Key, entry.Value);
        }

        foreach (var edit in finalEdits.Values)
        {
            if (edit.Kind == Axis2HashEditKind.Set)
                builder.SetItem(edit.Key, edit.Value);
        }

        return builder.ToImmutable();
    }

    private static PersistentHashMap<Axis2HashKey, int> Apply(
        PersistentHashMap<Axis2HashKey, int> map,
        Axis2HashEdit edit) =>
        edit.Kind == Axis2HashEditKind.Set
            ? map.SetItem(edit.Key, edit.Value)
            : map.Remove(edit.Key);

    private static long ComputeSemanticChecksum(PersistentHashMap<Axis2HashKey, int> map)
    {
        var checksum = 17L;
        foreach (var entry in map)
        {
            checksum = unchecked((checksum * 31) + entry.Key.Value);
            checksum = unchecked((checksum * 31) + entry.Value);
        }

        return checksum;
    }
}

/// <summary>A comparable escaping result for both T0 timed lanes.</summary>
public readonly record struct Axis2HistoryResult(
    PersistentHashMap<Axis2HashKey, int> Map,
    long PublicationChecksum);

internal readonly record struct Axis2TransientOpportunityCounters(
    int BaseCount,
    int RequestedEditCount,
    int DiagnosticEditCount,
    int PublicationCount,
    Axis2EditHistory History,
    Axis2PublicationCadence PublicationCadence,
    int CollisionBucketSize,
    long ChangedOperationCount,
    long WrapperAllocationCount,
    long NonPublicationWrapperCount,
    long NodeVisitCount,
    long CopiedNodeCount,
    long CopiedArrayCount,
    long PotentiallyReusableCopiedNodeUpperBound,
    long PotentiallyReusableCopiedArrayUpperBound,
    long HashCallbackCount,
    long EqualityCallbackCount)
{
    internal string ToCsv() => string.Join(
        ',',
        "AXIS2_T0_COUNTER_V1",
        $"base_count={BaseCount.ToString(CultureInfo.InvariantCulture)}",
        $"requested_edits={RequestedEditCount.ToString(CultureInfo.InvariantCulture)}",
        $"diagnostic_edits={DiagnosticEditCount.ToString(CultureInfo.InvariantCulture)}",
        $"publications={PublicationCount.ToString(CultureInfo.InvariantCulture)}",
        $"history={History}",
        $"cadence={PublicationCadence}",
        $"collision_bucket_size={CollisionBucketSize.ToString(CultureInfo.InvariantCulture)}",
        $"changed_operations={ChangedOperationCount.ToString(CultureInfo.InvariantCulture)}",
        $"wrapper_allocations={WrapperAllocationCount.ToString(CultureInfo.InvariantCulture)}",
        $"nonpublication_wrappers={NonPublicationWrapperCount.ToString(CultureInfo.InvariantCulture)}",
        $"node_visits={NodeVisitCount.ToString(CultureInfo.InvariantCulture)}",
        $"copied_nodes={CopiedNodeCount.ToString(CultureInfo.InvariantCulture)}",
        $"copied_arrays={CopiedArrayCount.ToString(CultureInfo.InvariantCulture)}",
        $"potentially_reusable_nodes_upper={PotentiallyReusableCopiedNodeUpperBound.ToString(CultureInfo.InvariantCulture)}",
        $"potentially_reusable_arrays_upper={PotentiallyReusableCopiedArrayUpperBound.ToString(CultureInfo.InvariantCulture)}",
        $"hash_callbacks={HashCallbackCount.ToString(CultureInfo.InvariantCulture)}",
        $"equality_callbacks={EqualityCallbackCount.ToString(CultureInfo.InvariantCulture)}");
}
