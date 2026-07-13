namespace Tools.DataStructures.FingerTree.Benchmarks;

internal static class Axis2BenchmarkPolicy
{
    internal const int Seed = 0x5EED_2026;
    internal const double PracticalLatencyMargin = 0.10;
    internal const double PracticalAllocationMargin = 0.10;
    internal const double PracticalRetainedMemoryMargin = 0.10;

    internal static readonly int[] HashCounts = [0, 1, 8, 32, 1_024, 100_000];
    internal static readonly int[] CursorDocumentSizes = [1_024, 65_536, 1_048_576];
    internal static readonly int[] CursorLocalityWindows = [1, 8, 256, int.MaxValue];
    internal static readonly int[] SnapshotCadences = [1, 16, 256];
    internal static readonly int[] FocusCapCandidates = [16, 32, 64, 128];
    internal static readonly int[] FlushSizeCandidates = [256, 512, 1_024, 2_048];
    internal static readonly int[] PublicationCadences = [1, 8, 64, int.MaxValue];
    internal static readonly int[] LookupHitPercentages = [0, 50, 100];

    internal static double RequiredLatencyImprovement(double measuredRelativeNoiseFloor) =>
        Math.Max(measuredRelativeNoiseFloor, PracticalLatencyMargin);

    internal static double RequiredAllocationImprovement(double measuredRelativeNoiseFloor) =>
        Math.Max(measuredRelativeNoiseFloor, PracticalAllocationMargin);

    internal static KeyValuePair<Axis2HashKey, int>[] CreateHashEntries(int count, Axis2HashShape shape)
    {
        var entries = new KeyValuePair<Axis2HashKey, int>[count];
        for (var index = 0; index < count; index++)
        {
            var hash = shape switch
            {
                Axis2HashShape.Uniform => Mix(index),
                Axis2HashShape.ClusteredPrefix => Mix(index) & unchecked((int)0xffffffe0),
                Axis2HashShape.FullCollision => 0x51a7,
                _ => throw new ArgumentOutOfRangeException(nameof(shape)),
            };
            entries[index] = KeyValuePair.Create(new Axis2HashKey(index, hash), index * 17);
        }

        return entries;
    }

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
            var hash = shape switch
            {
                Axis2HashShape.Uniform => Mix(value),
                Axis2HashShape.ClusteredPrefix => Mix(value) & unchecked((int)0xffffffe0),
                Axis2HashShape.FullCollision => 0x51a7,
                _ => throw new ArgumentOutOfRangeException(nameof(shape)),
            };
            probes[index] = new Axis2HashKey(value, hash);
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

internal enum Axis2HashShape
{
    Uniform,
    ClusteredPrefix,
    FullCollision,
}

internal readonly record struct Axis2HashKey(int Value, int Hash);

internal sealed class Axis2HashKeyComparer : IEqualityComparer<Axis2HashKey>
{
    internal static Axis2HashKeyComparer Instance { get; } = new();

    private Axis2HashKeyComparer()
    {
    }

    public bool Equals(Axis2HashKey left, Axis2HashKey right) => left.Value == right.Value;

    public int GetHashCode(Axis2HashKey value) => value.Hash;
}
