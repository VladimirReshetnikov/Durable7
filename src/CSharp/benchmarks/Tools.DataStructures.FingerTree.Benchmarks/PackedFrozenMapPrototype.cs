using System.Collections.Frozen;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

/// <summary>
/// F0-only fixed-layout frozen-map prototype. This is deliberately benchmark-local: it is evidence
/// for (or against) a later public design, not a provisional production implementation.
/// </summary>
/// <remarks>
/// Entries are packed in the exact enumeration order of the source <see cref="PersistentHashMap{TKey, TValue}"/>.
/// A separate integer table uses zero for an empty slot and entry-index-plus-one for an occupied slot.
/// The one fixed layout is linear probing at no more than 70% load. Lookups only read the two arrays
/// and invoke the exact comparer object captured from the source; the prototype performs no lookup-time
/// mutation or allocation of its own. Comparer callbacks remain allowed to allocate or throw.
/// </remarks>
internal sealed class PackedFrozenMapPrototype<TKey, TValue>
{
    private const int LoadNumerator = 7;
    private const int LoadDenominator = 10;

    private readonly PackedEntry[] _entries;
    private readonly int[] _slots;
    private readonly IEqualityComparer<TKey> _comparer;

    private PackedFrozenMapPrototype(PersistentHashMap<TKey, TValue> source)
    {
        _comparer = source.Comparer;
        _entries = source.Count == 0 ? [] : new PackedEntry[source.Count];
        _slots = source.Count == 0 ? [] : new int[GetSlotCount(source.Count)];

        var entryIndex = 0;
        foreach (var pair in source)
        {
            // Recompute each real hash through the retained comparer. F0 intentionally does not use
            // synthetic precomputed probe integers or reach into the CHAMP node representation.
            var hash = unchecked((uint)_comparer.GetHashCode(pair.Key!));
            _entries[entryIndex] = new PackedEntry(hash, pair.Key, pair.Value);
            InsertSlot(hash, entryIndex);
            entryIndex++;
        }

        if (entryIndex != _entries.Length)
            throw new InvalidOperationException("The source map count changed while it was enumerated.");
    }

    internal static PackedFrozenMapPrototype<TKey, TValue> Create(
        PersistentHashMap<TKey, TValue> source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return new PackedFrozenMapPrototype<TKey, TValue>(source);
    }

    internal int Count => _entries.Length;

    internal IEqualityComparer<TKey> Comparer => _comparer;

    internal PackedFrozenMapPrototypeDiagnostics Diagnostics
    {
        get
        {
            var entryBytes = EstimateArrayBytes<PackedEntry>(_entries.Length);
            var slotBytes = EstimateArrayBytes<int>(_slots.Length);
            return new PackedFrozenMapPrototypeDiagnostics(
                EntryCount: _entries.Length,
                SlotCount: _slots.Length,
                LoadFactor: _slots.Length == 0 ? 0 : (double)_entries.Length / _slots.Length,
                EstimatedEntryArrayBytes: entryBytes,
                EstimatedSlotArrayBytes: slotBytes,
                EstimatedRetainedArrayBytes: checked(entryBytes + slotBytes));
        }
    }

    internal bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        if (TryGetEntry(key, out _, out value))
            return true;

        value = default;
        return false;
    }

    internal bool TryGetKey(TKey equalKey, [MaybeNullWhen(false)] out TKey actualKey)
    {
        if (TryGetEntry(equalKey, out actualKey, out _))
            return true;

        actualKey = equalKey;
        return false;
    }

    public Enumerator GetEnumerator() => new(_entries);

    private bool TryGetEntry(
        TKey key,
        [MaybeNullWhen(false)] out TKey actualKey,
        [MaybeNullWhen(false)] out TValue value)
    {
        // Match PersistentHashMap's empty-map behavior: no comparer callback is made when there is
        // no root/slot table to search.
        if (_slots.Length == 0)
        {
            actualKey = default;
            value = default;
            return false;
        }

        var hash = unchecked((uint)_comparer.GetHashCode(key!));
        var slot = (int)(hash % (uint)_slots.Length);
        while (true)
        {
            var encodedEntryIndex = _slots[slot];
            if (encodedEntryIndex == 0)
            {
                actualKey = default;
                value = default;
                return false;
            }

            ref readonly var entry = ref _entries[encodedEntryIndex - 1];
            if (entry.Hash == hash && _comparer.Equals(entry.Key, key))
            {
                actualKey = entry.Key;
                value = entry.Value;
                return true;
            }

            slot++;
            if (slot == _slots.Length)
                slot = 0;
        }
    }

    private void InsertSlot(uint hash, int entryIndex)
    {
        var slot = (int)(hash % (uint)_slots.Length);
        while (_slots[slot] != 0)
        {
            slot++;
            if (slot == _slots.Length)
                slot = 0;
        }

        _slots[slot] = checked(entryIndex + 1);
    }

    private static int GetSlotCount(int entryCount)
    {
        var required = checked((((long)entryCount * LoadDenominator) + LoadNumerator - 1) / LoadNumerator);
        return checked((int)Math.Max(2, required));
    }

    private static long EstimateArrayBytes<TElement>(int length)
    {
        if (length == 0)
            return 0;

        // CoreCLR SZARRAY estimate: method-table pointer, sync block, and native-sized length/padding,
        // followed by inline element storage, rounded to object alignment. Payload object graphs and
        // the prototype wrapper/comparer are intentionally excluded from this array-only F0 report.
        var unaligned = checked((3L * IntPtr.Size) + ((long)length * Unsafe.SizeOf<TElement>()));
        return Align(unaligned);
    }

    private static long Align(long bytes)
    {
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }

    internal readonly struct PackedEntry(uint hash, TKey key, TValue value)
    {
        internal readonly uint Hash = hash;
        internal readonly TKey Key = key;
        internal readonly TValue Value = value;
    }

    public struct Enumerator
    {
        private readonly PackedEntry[] _entries;
        private int _index;

        internal Enumerator(PackedEntry[] entries)
        {
            _entries = entries;
            _index = -1;
            Current = default;
        }

        public KeyValuePair<TKey, TValue> Current { get; private set; }

        public bool MoveNext()
        {
            var next = _index + 1;
            if ((uint)next >= (uint)_entries.Length)
            {
                _index = _entries.Length;
                Current = default;
                return false;
            }

            _index = next;
            ref readonly var entry = ref _entries[next];
            Current = KeyValuePair.Create(entry.Key, entry.Value);
            return true;
        }
    }
}

internal readonly record struct PackedFrozenMapPrototypeDiagnostics(
    int EntryCount,
    int SlotCount,
    double LoadFactor,
    long EstimatedEntryArrayBytes,
    long EstimatedSlotArrayBytes,
    long EstimatedRetainedArrayBytes)
{
    internal double EstimatedRetainedArrayBytesPerEntry =>
        EntryCount == 0 ? 0 : (double)EstimatedRetainedArrayBytes / EntryCount;
}

/// <summary>Converts raw F0 benchmark means into the read count that amortizes freezing a CHAMP map.</summary>
internal static class FrozenF0BreakEven
{
    /// <summary>
    /// Calculates the first whole read at which the one-time packed conversion cost is recovered.
    /// The persistent baseline has no conversion cost because the source map already exists.
    /// </summary>
    /// <returns>
    /// A whole-number read count, or positive infinity when the packed lookup is not faster than
    /// the persistent lookup. Batch means are divided by <paramref name="readsPerBatch"/> before
    /// the comparison, so the helper accepts the existing 1,024-probe benchmark lanes directly.
    /// </returns>
    internal static double CalculateReads(
        double packedConstructionNanoseconds,
        double persistentLookupBatchNanoseconds,
        double packedLookupBatchNanoseconds,
        int readsPerBatch)
    {
        ValidateNonNegativeFinite(packedConstructionNanoseconds, nameof(packedConstructionNanoseconds));
        ValidateNonNegativeFinite(persistentLookupBatchNanoseconds, nameof(persistentLookupBatchNanoseconds));
        ValidateNonNegativeFinite(packedLookupBatchNanoseconds, nameof(packedLookupBatchNanoseconds));
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(readsPerBatch);

        var savedNanosecondsPerRead =
            (persistentLookupBatchNanoseconds - packedLookupBatchNanoseconds) / readsPerBatch;
        return savedNanosecondsPerRead <= 0
            ? double.PositiveInfinity
            : Math.Ceiling(packedConstructionNanoseconds / savedNanosecondsPerRead);
    }

    private static void ValidateNonNegativeFinite(double value, string parameterName)
    {
        if (!double.IsFinite(value) || value < 0)
            throw new ArgumentOutOfRangeException(parameterName);
    }
}

/// <summary>Shared, exact-semantics fixture for the non-null F0 control matrix.</summary>
internal sealed class FrozenF0AxisFixture
{
    private const int ProbeCount = 1_024;

    internal FrozenF0AxisFixture(
        int count,
        int hitPercentage,
        Axis2HashShape shape,
        string lane,
        bool emitRetainedDiagnostics = true)
    {
        var comparer = Axis2HashKeyComparer.Instance;
        Entries = Axis2BenchmarkPolicy.CreateHashEntries(count, shape);
        Probes = CreateProbes(Entries, ProbeCount, hitPercentage, shape);
        Persistent = PersistentHashMap<Axis2HashKey, int>.CreateRange(Entries, comparer);
        Packed = PackedFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);
        RobinHood = RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);
        Quadratic = QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);
        Dictionary = CreateDictionary(Persistent, comparer);
        Immutable = CreateImmutable(Persistent, comparer);
        BclFrozen = Persistent.ToFrozenDictionary(comparer);

        if (!ReferenceEquals(Persistent.Comparer, comparer)
            || !ReferenceEquals(Packed.Comparer, comparer)
            || !ReferenceEquals(RobinHood.Comparer, comparer)
            || !ReferenceEquals(Quadratic.Comparer, comparer)
            || !ReferenceEquals(Dictionary.Comparer, comparer)
            || !ReferenceEquals(Immutable.KeyComparer, comparer)
            || !ReferenceEquals(BclFrozen.Comparer, comparer))
        {
            throw new InvalidOperationException("Every F0/F1 control must retain the exact comparer object.");
        }

        ValidateSemanticParity();
        ValidatePackedEnumeration(Persistent, Packed);
        ValidateRobinHoodEnumeration(Persistent, RobinHood);
        ValidateQuadraticEnumeration(Persistent, Quadratic);
        if (emitRetainedDiagnostics)
        {
            ReportRetainedArrays(
                lane,
                Persistent.GetStructureDiagnostics().EstimatedRetainedBytes,
                Packed.Diagnostics,
                RobinHood.Diagnostics,
                Quadratic.Diagnostics,
                FrozenLayoutMemory.EstimateBclFrozenArrayBytes(BclFrozen));
        }
    }

    internal KeyValuePair<Axis2HashKey, int>[] Entries { get; }

    internal Axis2HashKey[] Probes { get; }

    internal PersistentHashMap<Axis2HashKey, int> Persistent { get; }

    internal PackedFrozenMapPrototype<Axis2HashKey, int> Packed { get; }

    internal RobinHoodFrozenMapPrototype<Axis2HashKey, int> RobinHood { get; }

    internal QuadraticFrozenMapPrototype<Axis2HashKey, int> Quadratic { get; }

    internal Dictionary<Axis2HashKey, int> Dictionary { get; }

    internal System.Collections.Immutable.ImmutableDictionary<Axis2HashKey, int> Immutable { get; }

    internal System.Collections.Frozen.FrozenDictionary<Axis2HashKey, int> BclFrozen { get; }

    internal PersistentHashMap<Axis2HashKey, int> ConstructPersistent() =>
        PersistentHashMap<Axis2HashKey, int>.CreateRange(Persistent, Axis2HashKeyComparer.Instance);

    internal PackedFrozenMapPrototype<Axis2HashKey, int> ConstructPacked() =>
        PackedFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);

    internal RobinHoodFrozenMapPrototype<Axis2HashKey, int> ConstructRobinHood() =>
        RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);

    internal QuadraticFrozenMapPrototype<Axis2HashKey, int> ConstructQuadratic() =>
        QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);

    internal Dictionary<Axis2HashKey, int> ConstructDictionary() =>
        CreateDictionary(Persistent, Axis2HashKeyComparer.Instance);

    internal System.Collections.Immutable.ImmutableDictionary<Axis2HashKey, int> ConstructImmutable() =>
        CreateImmutable(Persistent, Axis2HashKeyComparer.Instance);

    internal System.Collections.Frozen.FrozenDictionary<Axis2HashKey, int> ConstructBclFrozen() =>
        Persistent.ToFrozenDictionary(Axis2HashKeyComparer.Instance);

    private static Dictionary<Axis2HashKey, int> CreateDictionary(
        PersistentHashMap<Axis2HashKey, int> source,
        IEqualityComparer<Axis2HashKey> comparer)
    {
        var result = new Dictionary<Axis2HashKey, int>(source.Count, comparer);
        foreach (var pair in source)
            result.Add(pair.Key, pair.Value);
        return result;
    }

    private static System.Collections.Immutable.ImmutableDictionary<Axis2HashKey, int> CreateImmutable(
        PersistentHashMap<Axis2HashKey, int> source,
        IEqualityComparer<Axis2HashKey> comparer)
    {
        var builder = System.Collections.Immutable.ImmutableDictionary.CreateBuilder<Axis2HashKey, int>(comparer);
        foreach (var pair in source)
            builder.Add(pair.Key, pair.Value);
        return builder.ToImmutable();
    }

    private static Axis2HashKey[] CreateProbes(
        KeyValuePair<Axis2HashKey, int>[] entries,
        int probeCount,
        int hitPercentage,
        Axis2HashShape shape)
    {
        if ((uint)hitPercentage > 100)
            throw new ArgumentOutOfRangeException(nameof(hitPercentage));

        var probes = new Axis2HashKey[probeCount];
        var hitCount = entries.Length == 0 ? 0 : probeCount * hitPercentage / 100;
        for (var index = 0; index < probes.Length; index++)
        {
            if (index < hitCount)
            {
                var stored = entries[index % entries.Length].Key;
                probes[index] = new Axis2HashKey(stored.Value, stored.Hash);
                continue;
            }

            var value = checked(entries.Length + index + 1);
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

    private static int Mix(int value)
    {
        var bits = unchecked((uint)(value + Axis2BenchmarkPolicy.Seed));
        bits ^= bits >> 16;
        bits *= 0x7feb352d;
        bits ^= bits >> 15;
        bits *= 0x846ca68b;
        bits ^= bits >> 16;
        return unchecked((int)bits);
    }

    private static void Shuffle<T>(T[] values)
    {
        var random = new Random(Axis2BenchmarkPolicy.Seed);
        for (var index = values.Length - 1; index > 0; index--)
        {
            var selected = random.Next(index + 1);
            (values[index], values[selected]) = (values[selected], values[index]);
        }
    }

    private void ValidateSemanticParity()
    {
        if (Packed.Count != Persistent.Count
            || RobinHood.Count != Persistent.Count
            || Quadratic.Count != Persistent.Count
            || BclFrozen.Count != Persistent.Count)
        {
            throw new InvalidOperationException("Every frozen layout must preserve the source-map count.");
        }

        foreach (var probe in Probes)
        {
            var expectedFound = Persistent.TryGetValue(probe, out var expectedValue);
            ValidateLookup(
                "linear",
                expectedFound,
                expectedValue,
                Packed.TryGetValue(probe, out var linearValue),
                linearValue);
            ValidateLookup(
                "Robin-Hood",
                expectedFound,
                expectedValue,
                RobinHood.TryGetValue(probe, out var robinHoodValue),
                robinHoodValue);
            ValidateLookup(
                "quadratic",
                expectedFound,
                expectedValue,
                Quadratic.TryGetValue(probe, out var quadraticValue),
                quadraticValue);
            ValidateLookup(
                "BCL FrozenDictionary",
                expectedFound,
                expectedValue,
                BclFrozen.TryGetValue(probe, out var bclValue),
                bclValue);
        }

        foreach (var pair in Persistent)
        {
            if (!Packed.TryGetKey(pair.Key, out var linearKey)
                || !RobinHood.TryGetKey(pair.Key, out var robinHoodKey)
                || !Quadratic.TryGetKey(pair.Key, out var quadraticKey)
                || !pair.Key.Equals(linearKey)
                || !pair.Key.Equals(robinHoodKey)
                || !pair.Key.Equals(quadraticKey))
            {
                throw new InvalidOperationException(
                    "Every repository frozen layout must retain the source map's stored key representative.");
            }
        }

        var bclEntryCount = 0;
        foreach (var pair in BclFrozen)
        {
            if (!Persistent.TryGetValue(pair.Key, out var expectedValue) || expectedValue != pair.Value)
                throw new InvalidOperationException("The BCL FrozenDictionary control changed source content.");
            bclEntryCount++;
        }

        if (bclEntryCount != Persistent.Count)
            throw new InvalidOperationException("The BCL FrozenDictionary control enumerated the wrong count.");
    }

    private static void ValidateLookup(
        string layout,
        bool expectedFound,
        int expectedValue,
        bool actualFound,
        int actualValue)
    {
        if (actualFound != expectedFound || (actualFound && actualValue != expectedValue))
            throw new InvalidOperationException($"The {layout} lookup result differs from the source map.");
    }

    private static void ValidatePackedEnumeration(
        PersistentHashMap<Axis2HashKey, int> source,
        PackedFrozenMapPrototype<Axis2HashKey, int> packed)
    {
        var sourceEnumerator = source.GetEnumerator();
        var packedEnumerator = packed.GetEnumerator();
        while (sourceEnumerator.MoveNext())
        {
            if (!packedEnumerator.MoveNext() || !sourceEnumerator.Current.Equals(packedEnumerator.Current))
                throw new InvalidOperationException("The packed prototype must preserve source-map enumeration exactly.");
        }

        if (packedEnumerator.MoveNext())
            throw new InvalidOperationException("The packed prototype enumerated more entries than its source map.");
    }

    private static void ValidateRobinHoodEnumeration(
        PersistentHashMap<Axis2HashKey, int> source,
        RobinHoodFrozenMapPrototype<Axis2HashKey, int> robinHood)
    {
        var sourceEnumerator = source.GetEnumerator();
        var frozenEnumerator = robinHood.GetEnumerator();
        while (sourceEnumerator.MoveNext())
        {
            if (!frozenEnumerator.MoveNext() || !sourceEnumerator.Current.Equals(frozenEnumerator.Current))
            {
                throw new InvalidOperationException(
                    "The Robin-Hood prototype must preserve source-map enumeration exactly.");
            }
        }

        if (frozenEnumerator.MoveNext())
            throw new InvalidOperationException("The Robin-Hood prototype enumerated too many entries.");
    }

    private static void ValidateQuadraticEnumeration(
        PersistentHashMap<Axis2HashKey, int> source,
        QuadraticFrozenMapPrototype<Axis2HashKey, int> quadratic)
    {
        var sourceEnumerator = source.GetEnumerator();
        var frozenEnumerator = quadratic.GetEnumerator();
        while (sourceEnumerator.MoveNext())
        {
            if (!frozenEnumerator.MoveNext() || !sourceEnumerator.Current.Equals(frozenEnumerator.Current))
            {
                throw new InvalidOperationException(
                    "The quadratic prototype must preserve source-map enumeration exactly.");
            }
        }

        if (frozenEnumerator.MoveNext())
            throw new InvalidOperationException("The quadratic prototype enumerated too many entries.");
    }

    internal static void ReportRetainedArrays(
        string lane,
        long persistentEstimatedRetainedBytes,
        PackedFrozenMapPrototypeDiagnostics linear,
        PackedFrozenMapPrototypeDiagnostics robinHood,
        PackedFrozenMapPrototypeDiagnostics quadratic,
        long? bclFrozenEstimatedArrayBytes)
    {
        Console.WriteLine(
            $"AXIS2_F1_RETAINED_V1,lane={lane},entries={linear.EntryCount}," +
            $"persistent-estimated-graph={persistentEstimatedRetainedBytes}," +
            $"linear-slots={linear.SlotCount},linear-load={linear.LoadFactor:F4}," +
            $"linear-entry-array={linear.EstimatedEntryArrayBytes}," +
            $"linear-slot-array={linear.EstimatedSlotArrayBytes}," +
            $"linear-retained-arrays={linear.EstimatedRetainedArrayBytes}," +
            $"robin-hood-slots={robinHood.SlotCount},robin-hood-load={robinHood.LoadFactor:F4}," +
            $"robin-hood-entry-array={robinHood.EstimatedEntryArrayBytes}," +
            $"robin-hood-slot-array={robinHood.EstimatedSlotArrayBytes}," +
            $"robin-hood-retained-arrays={robinHood.EstimatedRetainedArrayBytes}," +
            $"quadratic-slots={quadratic.SlotCount},quadratic-load={quadratic.LoadFactor:F4}," +
            $"quadratic-entry-array={quadratic.EstimatedEntryArrayBytes}," +
            $"quadratic-slot-array={quadratic.EstimatedSlotArrayBytes}," +
            $"quadratic-retained-arrays={quadratic.EstimatedRetainedArrayBytes}," +
            $"bcl-frozen-retained-arrays={bclFrozenEstimatedArrayBytes?.ToString() ?? "omitted-null-semantics"}. " +
            "Array estimates exclude wrappers, comparers, and key/value payload object graphs; " +
            "the BCL value reflects arrays reachable through this runtime's Frozen implementation objects.");
    }
}

/// <summary>
/// Null-key and stored-representative F0 fixture. Dictionary, ImmutableDictionary, and BCL
/// FrozenDictionary controls are deliberately absent because they reject null keys; substituting a
/// sentinel would change the semantics of the lane.
/// </summary>
internal sealed class FrozenF0NullCollisionFixture
{
    private const int ProbeCount = 1_024;
    private readonly NullCollisionComparer _comparer = new();

    internal FrozenF0NullCollisionFixture(
        int count,
        int hitPercentage,
        bool emitRetainedDiagnostics = true)
    {
        if (count < 2)
            throw new ArgumentOutOfRangeException(nameof(count));

        var originalRepresentatives = new string?[count];
        var input = new List<KeyValuePair<string?, int>>(count * 2);
        input.Add(KeyValuePair.Create<string?, int>(null, -1));
        for (var index = 1; index < count; index++)
        {
            var representative = new string($"KEY-{index:D5}".ToCharArray());
            originalRepresentatives[index] = representative;
            input.Add(KeyValuePair.Create<string?, int>(representative, index));
        }

        // Later equivalent spellings replace values while PersistentHashMap keeps the first stored
        // key objects. Null is also replaced. Every key deliberately has the same full hash.
        input.Add(KeyValuePair.Create<string?, int>(null, -17));
        for (var index = 1; index < count; index++)
            input.Add(KeyValuePair.Create<string?, int>($"key-{index:D5}", index * 17));

        Persistent = PersistentHashMap<string?, int>.CreateRange(input, _comparer);
        Packed = PackedFrozenMapPrototype<string?, int>.Create(Persistent);
        RobinHood = RobinHoodFrozenMapPrototype<string?, int>.Create(Persistent);
        Quadratic = QuadraticFrozenMapPrototype<string?, int>.Create(Persistent);
        Probes = CreateProbes(count, hitPercentage);

        if (!ReferenceEquals(Persistent.Comparer, _comparer)
            || !ReferenceEquals(Packed.Comparer, _comparer)
            || !ReferenceEquals(RobinHood.Comparer, _comparer)
            || !ReferenceEquals(Quadratic.Comparer, _comparer))
        {
            throw new InvalidOperationException("The null/collision lane must retain the exact comparer object.");
        }

        ValidateSemanticParity(originalRepresentatives);
        ValidateRepositoryEnumeration();

        if (emitRetainedDiagnostics)
        {
            FrozenF0AxisFixture.ReportRetainedArrays(
                "null-full-collision",
                Persistent.GetStructureDiagnostics().EstimatedRetainedBytes,
                Packed.Diagnostics,
                RobinHood.Diagnostics,
                Quadratic.Diagnostics,
                bclFrozenEstimatedArrayBytes: null);
        }
    }

    internal string?[] Probes { get; }

    internal PersistentHashMap<string?, int> Persistent { get; }

    internal PackedFrozenMapPrototype<string?, int> Packed { get; }

    internal RobinHoodFrozenMapPrototype<string?, int> RobinHood { get; }

    internal QuadraticFrozenMapPrototype<string?, int> Quadratic { get; }

    internal PersistentHashMap<string?, int> ConstructPersistent() =>
        PersistentHashMap<string?, int>.CreateRange(Persistent, _comparer);

    internal PackedFrozenMapPrototype<string?, int> ConstructPacked() =>
        PackedFrozenMapPrototype<string?, int>.Create(Persistent);

    internal RobinHoodFrozenMapPrototype<string?, int> ConstructRobinHood() =>
        RobinHoodFrozenMapPrototype<string?, int>.Create(Persistent);

    internal QuadraticFrozenMapPrototype<string?, int> ConstructQuadratic() =>
        QuadraticFrozenMapPrototype<string?, int>.Create(Persistent);

    private void ValidateSemanticParity(string?[] originalRepresentatives)
    {
        if (Packed.Count != Persistent.Count
            || RobinHood.Count != Persistent.Count
            || Quadratic.Count != Persistent.Count)
        {
            throw new InvalidOperationException("Every repository frozen layout must preserve the source-map count.");
        }

        foreach (var probe in Probes)
        {
            var expectedFound = Persistent.TryGetValue(probe, out var expectedValue);
            ValidateLookup(
                "linear",
                expectedFound,
                expectedValue,
                Packed.TryGetValue(probe, out var linearValue),
                linearValue);
            ValidateLookup(
                "Robin-Hood",
                expectedFound,
                expectedValue,
                RobinHood.TryGetValue(probe, out var robinHoodValue),
                robinHoodValue);
            ValidateLookup(
                "quadratic",
                expectedFound,
                expectedValue,
                Quadratic.TryGetValue(probe, out var quadraticValue),
                quadraticValue);
        }

        for (var index = 0; index < originalRepresentatives.Length; index++)
        {
            var query = index == 0 ? null : $"key-{index:D5}";
            if (!Persistent.TryGetKey(query, out var persistentRepresentative)
                || !Packed.TryGetKey(query, out var linearRepresentative)
                || !RobinHood.TryGetKey(query, out var robinHoodRepresentative)
                || !Quadratic.TryGetKey(query, out var quadraticRepresentative)
                || !ReferenceEquals(persistentRepresentative, originalRepresentatives[index])
                || !ReferenceEquals(linearRepresentative, originalRepresentatives[index])
                || !ReferenceEquals(robinHoodRepresentative, originalRepresentatives[index])
                || !ReferenceEquals(quadraticRepresentative, originalRepresentatives[index]))
            {
                throw new InvalidOperationException(
                    "Freezing must retain every first stored key representative, including null.");
            }
        }
    }

    private static void ValidateLookup(
        string layout,
        bool expectedFound,
        int expectedValue,
        bool actualFound,
        int actualValue)
    {
        if (actualFound != expectedFound || (actualFound && actualValue != expectedValue))
            throw new InvalidOperationException($"The {layout} null/collision lookup differs from the source map.");
    }

    private void ValidateRepositoryEnumeration()
    {
        var sourceEnumerator = Persistent.GetEnumerator();
        var packedEnumerator = Packed.GetEnumerator();
        var robinHoodEnumerator = RobinHood.GetEnumerator();
        var quadraticEnumerator = Quadratic.GetEnumerator();
        while (sourceEnumerator.MoveNext())
        {
            if (!packedEnumerator.MoveNext()
                || !robinHoodEnumerator.MoveNext()
                || !quadraticEnumerator.MoveNext()
                || !ReferenceEquals(sourceEnumerator.Current.Key, packedEnumerator.Current.Key)
                || !ReferenceEquals(sourceEnumerator.Current.Key, robinHoodEnumerator.Current.Key)
                || !ReferenceEquals(sourceEnumerator.Current.Key, quadraticEnumerator.Current.Key)
                || sourceEnumerator.Current.Value != packedEnumerator.Current.Value
                || sourceEnumerator.Current.Value != robinHoodEnumerator.Current.Value
                || sourceEnumerator.Current.Value != quadraticEnumerator.Current.Value)
            {
                throw new InvalidOperationException(
                    "Every repository frozen prototype must retain source-map order and key representatives exactly.");
            }
        }

        if (packedEnumerator.MoveNext() || robinHoodEnumerator.MoveNext() || quadraticEnumerator.MoveNext())
            throw new InvalidOperationException("A repository frozen prototype enumerated too many entries.");
    }

    private static string?[] CreateProbes(int count, int hitPercentage)
    {
        if ((uint)hitPercentage > 100)
            throw new ArgumentOutOfRangeException(nameof(hitPercentage));

        var probes = new string?[ProbeCount];
        var hitCount = ProbeCount * hitPercentage / 100;
        for (var index = 0; index < probes.Length; index++)
        {
            probes[index] = index < hitCount
                ? index % count == 0 ? null : $"key-{index % count:D5}"
                : $"MISSING-{index:D5}";
        }

        var random = new Random(Axis2BenchmarkPolicy.Seed);
        for (var index = probes.Length - 1; index > 0; index--)
        {
            var selected = random.Next(index + 1);
            (probes[index], probes[selected]) = (probes[selected], probes[index]);
        }

        return probes;
    }

    private sealed class NullCollisionComparer : IEqualityComparer<string?>
    {
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string? value) => 0x51a7;
    }
}
