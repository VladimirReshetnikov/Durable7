// Shared support for the axis2 benchmark policy benchmarks.

namespace Durable7.FingerTree.Benchmarks;

/// <summary>
/// The constants the Axis 2 benchmarks are gated on. Every value here was fixed before any result
/// was collected, so the gates cannot be tuned in response to the numbers they judge.
/// </summary>
internal static class Axis2BenchmarkPolicy
{
    internal const int Seed = 0x5EED_2026;
    internal const int FullCollisionHash = 0x51A7;
    internal const int ClusteredPrefixBits = 15;
    internal const int TransientCollisionBucketSize = 32;
    internal const int TransientDiagnosticEditLimit = 64;
    internal const double PracticalLatencyMargin = 0.10;
    internal const double PracticalAllocationMargin = 0.10;
    internal const double PracticalRetainedMemoryMargin = 0.10;

    // C2 is decided at the same representative edit point and sample cadence selected for C1.
    // These constants were locked before any measured-cursor result was collected; do not tune
    // them in response to benchmark output.
    internal const int MeasuredCursorGateDocumentSize = 65_536;
    internal const int MeasuredCursorGateEditCount = 256;
    internal const int MeasuredCursorGateLocalityWindow = 8;
    internal const int MeasuredCursorGateSnapshotCadence = 16;
    internal const double MeasuredCursorGateLatencyImprovement = 0.10;
    internal const double MeasuredCursorGateAllocationImprovement = 0.10;

    // Query guardrails are intentionally separate from the local-edit win. Every constant below
    // was fixed before collecting C2 results, including prepared and freshly dirty cursor lanes.
    internal const double MeasuredCursorMaximumPositionSeekRatio = 1.25;
    internal const double MeasuredCursorMaximumPositionSeekAllocationRatio = 1.10;
    internal const double MeasuredCursorMaximumSourceMeasureSeekRatio = 2.00;
    internal const long MeasuredCursorMaximumSourceMeasureSeekAllocationBytes = 16 * 1_024;
    internal const double MeasuredCursorMaximumPreparedMeasureSeekRatio = 2.00;
    internal const long MeasuredCursorMaximumPreparedMeasureSeekAllocationBytes = 16 * 1_024;
    internal const int MeasuredCursorMaximumLocateMeasureCallbacks = 2_048;
    internal const int MeasuredCursorMaximumFocusMeasureCallbacks = 16;
    internal const int MeasuredCursorMaximumMeasureSeekMeasureCallbacks =
        MeasuredCursorMaximumLocateMeasureCallbacks + MeasuredCursorMaximumFocusMeasureCallbacks;
    internal const double MeasuredCursorMaximumLineColumnRatio = 1.25;
    internal const long MeasuredCursorMaximumLineColumnAllocationBytes = 64;
    internal const double MeasuredCursorMaximumDirtyQueryRatio = 1.25;
    internal const double MeasuredCursorMaximumDirtyQueryAllocationRatio = 1.10;

    internal const int MeasuredCursorSparseNewlineInterval = 256;
    internal const int MeasuredCursorDenseNewlineInterval = 8;

    internal static readonly int[] HashCounts = [0, 1, 8, 32, 1_024, 100_000];
    internal static readonly int[] CursorDocumentSizes = [1_024, 65_536, 1_048_576];
    internal static readonly int[] CursorLocalityWindows = [1, 8, 256, int.MaxValue];
    internal static readonly int[] SnapshotCadences = [1, 16, 256];
    internal static readonly int[] FocusCapCandidates = [16, 32, 64, 128];
    internal static readonly int[] FlushSizeCandidates = [256, 512, 1_024, 2_048];
    internal static readonly int[] PublicationCadences = [1, 8, 64, int.MaxValue];
    internal static readonly int[] LookupHitPercentages = [0, 50, 100];
    internal static readonly Axis2TransientWorkload[] TransientWorkloads = CreateTransientWorkloads();

    internal static double RequiredLatencyImprovement(double measuredRelativeNoiseFloor) =>
        Math.Max(measuredRelativeNoiseFloor, PracticalLatencyMargin);

    internal static double RequiredAllocationImprovement(double measuredRelativeNoiseFloor) =>
        Math.Max(measuredRelativeNoiseFloor, PracticalAllocationMargin);

    internal static double RequiredMeasuredCursorLatencyImprovement(double measuredRelativeNoiseFloor) =>
        Math.Max(measuredRelativeNoiseFloor, MeasuredCursorGateLatencyImprovement);

    internal static double RequiredMeasuredCursorAllocationImprovement(double measuredRelativeNoiseFloor) =>
        Math.Max(measuredRelativeNoiseFloor, MeasuredCursorGateAllocationImprovement);

    internal static KeyValuePair<Axis2HashKey, int>[] CreateHashEntries(int count, Axis2HashShape shape)
    {
        var entries = new KeyValuePair<Axis2HashKey, int>[count];
        for (var index = 0; index < count; index++)
            entries[index] = KeyValuePair.Create(CreateHashKey(index, shape), index * 17);

        return entries;
    }

    internal static Axis2TransientDataset CreateTransientDataset(
        Axis2TransientWorkload workload,
        Axis2EditHistory history)
    {
        var entries = CreateTransientBaseEntries(workload.BaseCount, history, out var collisionBucketSize);
        var edits = new Axis2HashEdit[workload.EditCount];
        for (var index = 0; index < edits.Length; index++)
            edits[index] = CreateTransientEdit(entries, collisionBucketSize, history, index);

        return new Axis2TransientDataset(entries, edits, collisionBucketSize);
    }

    internal static int GetPublicationBatchSize(Axis2PublicationCadence cadence, int editCount) =>
        cadence == Axis2PublicationCadence.End
            ? Math.Max(editCount, 1)
            : (int)cadence;

    internal static bool IsPublicationBoundary(
        int completedEditCount,
        int totalEditCount,
        Axis2PublicationCadence cadence) =>
        completedEditCount == totalEditCount ||
        (cadence != Axis2PublicationCadence.End && completedEditCount % (int)cadence == 0);

    internal static int GetPublicationCount(int editCount, Axis2PublicationCadence cadence)
    {
        var batchSize = GetPublicationBatchSize(cadence, editCount);
        return checked((editCount + batchSize - 1) / batchSize);
    }

    internal static long AddPublicationChecksum(long checksum, int count, int completedEditCount) =>
        unchecked(((checksum * 1_099_511_628_211L) ^ count) + completedEditCount);

    internal static Axis2HashKey[] CreateLookupProbes(int count, int hitPercentage, Axis2HashShape shape)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if ((uint)hitPercentage > 100)
            throw new ArgumentOutOfRangeException(nameof(hitPercentage));

        var probes = new Axis2HashKey[Math.Max(count, 1)];
        var hitCount = probes.Length * hitPercentage / 100;
        for (var index = 0; index < probes.Length; index++)
        {
            var value = index < hitCount && count > 0 ? index % count : checked(count + index + 1);
            probes[index] = CreateHashKey(value, shape);
        }

        Shuffle(probes);
        return probes;
    }

    internal static char[] CreateText(int length, int newlineInterval = 80)
    {
        var text = new char[length];
        for (var index = 0; index < text.Length; index++)
            text[index] = newlineInterval > 0 && (index + 1) % newlineInterval == 0
                ? '\n'
                : (char)('a' + (Mix(index) & 15));
        return text;
    }

    private static Axis2TransientWorkload[] CreateTransientWorkloads()
    {
        var workloads = new List<Axis2TransientWorkload>();
        foreach (var count in HashCounts)
        {
            var fullCount = Math.Max(count, 1);
            var editCount = 1;
            while (editCount < fullCount)
            {
                workloads.Add(new Axis2TransientWorkload(count, editCount));
                editCount = checked(editCount * 8);
            }

            workloads.Add(new Axis2TransientWorkload(count, fullCount));
        }

        return [.. workloads];
    }

    private static KeyValuePair<Axis2HashKey, int>[] CreateTransientBaseEntries(
        int count,
        Axis2EditHistory history,
        out int collisionBucketSize)
    {
        var shape = history == Axis2EditHistory.ClusteredPrefix
            ? Axis2HashShape.ClusteredPrefix
            : Axis2HashShape.Uniform;
        var entries = CreateHashEntries(count, shape);
        collisionBucketSize = 0;
        if (history != Axis2EditHistory.FullCollision)
            return entries;

        collisionBucketSize = Math.Min(count, TransientCollisionBucketSize);
        for (var index = 0; index < collisionBucketSize; index++)
        {
            var entry = entries[index];
            entries[index] = KeyValuePair.Create(
                new Axis2HashKey(entry.Key.Value, FullCollisionHash),
                entry.Value);
        }

        // Do not accidentally extend the collision bucket with a uniformly generated hash.
        for (var index = collisionBucketSize; index < entries.Length; index++)
        {
            var entry = entries[index];
            if (entry.Key.Hash == FullCollisionHash)
            {
                entries[index] = KeyValuePair.Create(
                    new Axis2HashKey(entry.Key.Value, entry.Key.Hash ^ int.MinValue),
                    entry.Value);
            }
        }

        return entries;
    }

    private static Axis2HashEdit CreateTransientEdit(
        KeyValuePair<Axis2HashKey, int>[] entries,
        int collisionBucketSize,
        Axis2EditHistory history,
        int index)
    {
        var count = entries.Length;
        var replacementValue = unchecked(-1 - (index * 31));
        return history switch
        {
            Axis2EditHistory.RepeatedKey => Axis2HashEdit.Set(
                count == 0 ? CreateHashKey(0, Axis2HashShape.Uniform) : entries[0].Key,
                replacementValue),
            Axis2EditHistory.ClusteredPrefix => Axis2HashEdit.Set(
                count == 0
                    ? CreateHashKey(index, Axis2HashShape.ClusteredPrefix)
                    : entries[index % count].Key,
                replacementValue),
            Axis2EditHistory.DisjointInsert => Axis2HashEdit.Set(
                CreateHashKey(checked(count + index + 1), Axis2HashShape.Uniform),
                replacementValue),
            Axis2EditHistory.FullCollision => Axis2HashEdit.Set(
                collisionBucketSize == 0
                    ? CreateHashKey(index, Axis2HashShape.FullCollision)
                    : entries[index % collisionBucketSize].Key,
                replacementValue),
            Axis2EditHistory.Removal => Axis2HashEdit.Remove(
                count == 0
                    ? CreateHashKey(index, Axis2HashShape.Uniform)
                    : entries[index % count].Key),
            Axis2EditHistory.Mixed => CreateMixedTransientEdit(entries, index, replacementValue),
            _ => throw new ArgumentOutOfRangeException(nameof(history)),
        };
    }

    private static Axis2HashEdit CreateMixedTransientEdit(
        KeyValuePair<Axis2HashKey, int>[] entries,
        int index,
        int replacementValue)
    {
        var count = entries.Length;
        if (count == 0)
        {
            var key = CreateHashKey(index, Axis2HashShape.Uniform);
            return index % 4 == 1 ? Axis2HashEdit.Remove(key) : Axis2HashEdit.Set(key, replacementValue);
        }

        return (index % 4) switch
        {
            0 => Axis2HashEdit.Set(entries[index % count].Key, replacementValue),
            1 => Axis2HashEdit.Remove(entries[index % count].Key),
            2 => Axis2HashEdit.Set(
                CreateHashKey(checked(count + index + 1), Axis2HashShape.Uniform),
                replacementValue),
            _ => Axis2HashEdit.Set(entries[index % count].Key, replacementValue),
        };
    }

    private static Axis2HashKey CreateHashKey(int value, Axis2HashShape shape)
    {
        var hash = shape switch
        {
            Axis2HashShape.Uniform => Mix(value),
            // CHAMP consumes hashes from the low bits, so fifteen zero low bits force three
            // shared 5-bit levels before any two clustered keys may diverge.
            Axis2HashShape.ClusteredPrefix => unchecked(value << ClusteredPrefixBits),
            Axis2HashShape.FullCollision => FullCollisionHash,
            _ => throw new ArgumentOutOfRangeException(nameof(shape)),
        };
        return new Axis2HashKey(value, hash);
    }

    private static int Mix(int value)
    {
        var bits = unchecked((uint)(value + Seed));
        bits ^= bits >> 16;
        bits *= 0x7feb352d;
        bits ^= bits >> 15;
        bits *= 0x846ca68b;
        bits ^= bits >> 16;
        return unchecked((int)bits);
    }

    private static void Shuffle<T>(T[] values)
    {
        var random = new Random(Seed);
        for (var index = values.Length - 1; index > 0; index--)
        {
            var selected = random.Next(index + 1);
            (values[index], values[selected]) = (values[selected], values[index]);
        }
    }
}

/// <summary>
/// Which hash distribution a generated key set uses, so a benchmark can separate well-spread keys
/// from clustered and fully colliding ones.
/// </summary>
internal enum Axis2HashShape
{
    Uniform,
    ClusteredPrefix,
    FullCollision,
}

/// <summary>
/// A generated benchmark key whose hash is dictated by the chosen shape rather than by its value.
/// </summary>
public readonly record struct Axis2HashKey(int Value, int Hash);

/// <summary>
/// Equality and hashing for the generated keys, honouring the shape's dictated hash.
/// </summary>
internal sealed class Axis2HashKeyComparer : IEqualityComparer<Axis2HashKey>
{
    internal static Axis2HashKeyComparer Instance { get; } = new();

    private Axis2HashKeyComparer()
    {
    }

    public bool Equals(Axis2HashKey left, Axis2HashKey right) => left.Value == right.Value;

    public int GetHashCode(Axis2HashKey value) => value.Hash;
}

/// <summary>
/// The key comparer with call counting, so a benchmark can report how often the collection
/// consulted the policy.
/// </summary>
internal sealed class Axis2CountingHashKeyComparer : IEqualityComparer<Axis2HashKey>
{
    internal long HashCallbackCount { get; private set; }

    internal long EqualityCallbackCount { get; private set; }

    public bool Equals(Axis2HashKey left, Axis2HashKey right)
    {
        EqualityCallbackCount++;
        return left.Value == right.Value;
    }

    public int GetHashCode(Axis2HashKey value)
    {
        HashCallbackCount++;
        return value.Hash;
    }

    internal void Reset()
    {
        HashCallbackCount = 0;
        EqualityCallbackCount = 0;
    }
}

/// <summary>One generated key set together with the edits to replay against it.</summary>
internal sealed record Axis2TransientDataset(
    KeyValuePair<Axis2HashKey, int>[] BaseEntries,
    Axis2HashEdit[] Edits,
    int CollisionBucketSize);

/// <summary>Which kind of edit a generated workload step performs.</summary>
internal enum Axis2HashEditKind
{
    Set,
    Remove,
}

/// <summary>One generated edit: what to do and which key to do it to.</summary>
internal readonly record struct Axis2HashEdit(
    Axis2HashEditKind Kind,
    Axis2HashKey Key,
    int Value)
{
    internal static Axis2HashEdit Set(Axis2HashKey key, int value) =>
        new(Axis2HashEditKind.Set, key, value);

    internal static Axis2HashEdit Remove(Axis2HashKey key) =>
        new(Axis2HashEditKind.Remove, key, default);
}

/// <summary>
/// A generated edit sequence with the history and publication cadence it is replayed under.
/// </summary>
public readonly record struct Axis2TransientWorkload(int BaseCount, int EditCount)
{
    public override string ToString() => $"N{BaseCount}_E{EditCount}";
}

/// <summary>
/// How the versions a workload edits were themselves built, which decides how much structure is
/// already shared.
/// </summary>
public enum Axis2EditHistory
{
    RepeatedKey,
    ClusteredPrefix,
    DisjointInsert,
    FullCollision,
    Removal,
    Mixed,
}

/// <summary>
/// How often a transient session publishes a version, which is what the transient's benefit is
/// measured against.
/// </summary>
public enum Axis2PublicationCadence
{
    EveryEdit = 1,
    Every8 = 8,
    Every64 = 64,
    End = int.MaxValue,
}
