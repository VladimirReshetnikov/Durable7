// Shared support for the packed frozen map prototype benchmarks.

using System.Collections.Frozen;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Text;
using Durable7.Hamt;

namespace Durable7.FingerTree.Benchmarks;

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

    /// <summary>Creates an entry map using the supplied policies, which it retains.</summary>
    internal static PackedFrozenMapPrototype<TKey, TValue> Create(
        PersistentHashMap<TKey, TValue> source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return new PackedFrozenMapPrototype<TKey, TValue>(source);
    }

    /// <summary>Gets the number of entries in the map.</summary>
    internal int Count => _entries.Length;

    /// <summary>Gets the retained ordering policy.</summary>
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

    /// <summary>Reads the value stored for the key, reporting whether it was present.</summary>
    internal bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        if (TryGetEntry(key, out _, out value))
            return true;

        value = default;
        return false;
    }

    /// <summary>Reads the stored key representative, reporting whether the key was present.</summary>
    internal bool TryGetKey(TKey equalKey, [MaybeNullWhen(false)] out TKey actualKey)
    {
        if (TryGetEntry(equalKey, out actualKey, out _))
            return true;

        actualKey = equalKey;
        return false;
    }

    /// <summary>
    /// Rebuilds canonical CHAMP topology from packed source-order entries. This benchmark-local
    /// conversion models the layout-independent F2 contract without exposing a public frozen type.
    /// </summary>
    internal PersistentHashMap<TKey, TValue> ToPersistent()
    {
        var builder = PersistentHashMap<TKey, TValue>.CreateBulkBuilder(_comparer);
        foreach (ref readonly var entry in _entries.AsSpan())
            builder.SetItem(entry.Key, entry.Value);
        return builder.ToImmutable();
    }

    /// <summary>Returns an enumerator over the entries, in the map's own order.</summary>
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

    /// <summary>One entry in the packed layout, laid out for probing rather than for sharing.</summary>
    internal readonly struct PackedEntry(uint hash, TKey key, TValue value)
    {
        /// <summary>Returns the hash of the value.</summary>
        internal readonly uint Hash = hash;
        /// <summary>Gets the stored key.</summary>
        internal readonly TKey Key = key;
        /// <summary>Gets the stored value.</summary>
        internal readonly TValue Value = value;
    }

    /// <summary>Gets the struct enumerator.</summary>
    public struct Enumerator
    {
        private readonly PackedEntry[] _entries;
        private int _index;

        /// <summary>Creates a new enumerator.</summary>
        internal Enumerator(PackedEntry[] entries)
        {
            _entries = entries;
            _index = -1;
            Current = default;
        }

        /// <summary>Gets the value at the current position.</summary>
        public KeyValuePair<TKey, TValue> Current { get; private set; }

        /// <summary>Advances to the next element, reporting whether there was one.</summary>
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

/// <summary>
/// Shape and probe counts from the packed frozen map prototype, for comparing it against the
/// persistent map.
/// </summary>
internal readonly record struct PackedFrozenMapPrototypeDiagnostics(
    int EntryCount,
    int SlotCount,
    double LoadFactor,
    long EstimatedEntryArrayBytes,
    long EstimatedSlotArrayBytes,
    long EstimatedRetainedArrayBytes)
{
    /// <summary>Gets the estimated retained array bytes per entry.</summary>
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

    /// <summary>Creates a new frozen f 0 axis fixture.</summary>
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

    /// <summary>Gets the entries.</summary>
    internal KeyValuePair<Axis2HashKey, int>[] Entries { get; }

    /// <summary>Gets how many probes the operation needed.</summary>
    internal Axis2HashKey[] Probes { get; }

    /// <summary>Gets the persistent representation under comparison.</summary>
    internal PersistentHashMap<Axis2HashKey, int> Persistent { get; }

    /// <summary>Gets the packed representation under comparison.</summary>
    internal PackedFrozenMapPrototype<Axis2HashKey, int> Packed { get; }

    /// <summary>Gets the Robin Hood hashed representation under comparison.</summary>
    internal RobinHoodFrozenMapPrototype<Axis2HashKey, int> RobinHood { get; }

    /// <summary>Gets the quadratically probed representation under comparison.</summary>
    internal QuadraticFrozenMapPrototype<Axis2HashKey, int> Quadratic { get; }

    /// <summary>Gets the dictionary.</summary>
    internal Dictionary<Axis2HashKey, int> Dictionary { get; }

    /// <summary>Gets the immutable.</summary>
    internal System.Collections.Immutable.ImmutableDictionary<Axis2HashKey, int> Immutable { get; }

    /// <summary>Gets the bcl frozen.</summary>
    internal System.Collections.Frozen.FrozenDictionary<Axis2HashKey, int> BclFrozen { get; }

    /// <summary>Builds the persistent representation, measuring construction rather than lookup.</summary>
    internal PersistentHashMap<Axis2HashKey, int> ConstructPersistent() =>
        PersistentHashMap<Axis2HashKey, int>.CreateRange(Persistent, Axis2HashKeyComparer.Instance);

    /// <summary>Builds the packed representation.</summary>
    internal PackedFrozenMapPrototype<Axis2HashKey, int> ConstructPacked() =>
        PackedFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);

    /// <summary>Builds the Robin Hood hashed representation.</summary>
    internal RobinHoodFrozenMapPrototype<Axis2HashKey, int> ConstructRobinHood() =>
        RobinHoodFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);

    /// <summary>Builds the quadratically probed representation.</summary>
    internal QuadraticFrozenMapPrototype<Axis2HashKey, int> ConstructQuadratic() =>
        QuadraticFrozenMapPrototype<Axis2HashKey, int>.Create(Persistent);

    /// <summary>
    /// Converts the packed representation into the persistent one, measuring what adopting persistence costs.
    /// </summary>
    internal PersistentHashMap<Axis2HashKey, int> ConvertPackedToPersistent() =>
        Packed.ToPersistent();

    /// <summary>Converts the Robin Hood representation into the persistent one.</summary>
    internal PersistentHashMap<Axis2HashKey, int> ConvertRobinHoodToPersistent() =>
        RobinHood.ToPersistent();

    /// <summary>Converts the quadratic representation into the persistent one.</summary>
    internal PersistentHashMap<Axis2HashKey, int> ConvertQuadraticToPersistent() =>
        Quadratic.ToPersistent();

    /// <summary>Builds the framework's dictionary, as a baseline.</summary>
    internal Dictionary<Axis2HashKey, int> ConstructDictionary() =>
        CreateDictionary(Persistent, Axis2HashKeyComparer.Instance);

    /// <summary>Builds the framework's immutable dictionary, as a baseline.</summary>
    internal System.Collections.Immutable.ImmutableDictionary<Axis2HashKey, int> ConstructImmutable() =>
        CreateImmutable(Persistent, Axis2HashKeyComparer.Instance);

    /// <summary>Builds the framework's frozen dictionary, as a baseline.</summary>
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

    /// <summary>
    /// Reports how many arrays each representation retains, which is what the memory comparison is made on.
    /// </summary>
    internal static void ReportRetainedArrays(
        string lane,
        long persistentEstimatedRetainedBytes,
        PackedFrozenMapPrototypeDiagnostics linear,
        PackedFrozenMapPrototypeDiagnostics robinHood,
        PackedFrozenMapPrototypeDiagnostics quadratic,
        long? bclFrozenEstimatedArrayBytes)
    {
        Console.WriteLine(FormatRetainedArrays(
            lane,
            persistentEstimatedRetainedBytes,
            linear,
            robinHood,
            quadratic,
            bclFrozenEstimatedArrayBytes));
    }

    /// <summary>Formats the retained arrays.</summary>
    internal static string FormatRetainedArrays(
        string lane,
        long persistentEstimatedRetainedBytes,
        PackedFrozenMapPrototypeDiagnostics linear,
        PackedFrozenMapPrototypeDiagnostics robinHood,
        PackedFrozenMapPrototypeDiagnostics quadratic,
        long? bclFrozenEstimatedArrayBytes)
    {
        var bclFrozenBytes = bclFrozenEstimatedArrayBytes?.ToString(CultureInfo.InvariantCulture)
            ?? "omitted-null-semantics";
        return new StringBuilder(512)
            .Append("AXIS2_F1_RETAINED_V1,lane=").Append(lane)
            .Append(",entries=").Append(linear.EntryCount.ToString(CultureInfo.InvariantCulture))
            .Append(",persistent-estimated-graph=")
            .Append(persistentEstimatedRetainedBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",linear-slots=").Append(linear.SlotCount.ToString(CultureInfo.InvariantCulture))
            .Append(",linear-load=").Append(linear.LoadFactor.ToString("F4", CultureInfo.InvariantCulture))
            .Append(",linear-entry-array=")
            .Append(linear.EstimatedEntryArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",linear-slot-array=")
            .Append(linear.EstimatedSlotArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",linear-retained-arrays=")
            .Append(linear.EstimatedRetainedArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",robin-hood-slots=")
            .Append(robinHood.SlotCount.ToString(CultureInfo.InvariantCulture))
            .Append(",robin-hood-load=")
            .Append(robinHood.LoadFactor.ToString("F4", CultureInfo.InvariantCulture))
            .Append(",robin-hood-entry-array=")
            .Append(robinHood.EstimatedEntryArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",robin-hood-slot-array=")
            .Append(robinHood.EstimatedSlotArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",robin-hood-retained-arrays=")
            .Append(robinHood.EstimatedRetainedArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",quadratic-slots=")
            .Append(quadratic.SlotCount.ToString(CultureInfo.InvariantCulture))
            .Append(",quadratic-load=")
            .Append(quadratic.LoadFactor.ToString("F4", CultureInfo.InvariantCulture))
            .Append(",quadratic-entry-array=")
            .Append(quadratic.EstimatedEntryArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",quadratic-slot-array=")
            .Append(quadratic.EstimatedSlotArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",quadratic-retained-arrays=")
            .Append(quadratic.EstimatedRetainedArrayBytes.ToString(CultureInfo.InvariantCulture))
            .Append(",bcl-frozen-retained-arrays=").Append(bclFrozenBytes)
            .ToString();
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

    /// <summary>Creates a new frozen f 0 null collision fixture.</summary>
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

    /// <summary>Gets how many probes the operation needed.</summary>
    internal string?[] Probes { get; }

    /// <summary>Gets the persistent representation under comparison.</summary>
    internal PersistentHashMap<string?, int> Persistent { get; }

    /// <summary>Gets the packed representation under comparison.</summary>
    internal PackedFrozenMapPrototype<string?, int> Packed { get; }

    /// <summary>Gets the Robin Hood hashed representation under comparison.</summary>
    internal RobinHoodFrozenMapPrototype<string?, int> RobinHood { get; }

    /// <summary>Gets the quadratically probed representation under comparison.</summary>
    internal QuadraticFrozenMapPrototype<string?, int> Quadratic { get; }

    /// <summary>Builds the persistent representation, measuring construction rather than lookup.</summary>
    internal PersistentHashMap<string?, int> ConstructPersistent() =>
        PersistentHashMap<string?, int>.CreateRange(Persistent, _comparer);

    /// <summary>Builds the packed representation.</summary>
    internal PackedFrozenMapPrototype<string?, int> ConstructPacked() =>
        PackedFrozenMapPrototype<string?, int>.Create(Persistent);

    /// <summary>Builds the Robin Hood hashed representation.</summary>
    internal RobinHoodFrozenMapPrototype<string?, int> ConstructRobinHood() =>
        RobinHoodFrozenMapPrototype<string?, int>.Create(Persistent);

    /// <summary>Builds the quadratically probed representation.</summary>
    internal QuadraticFrozenMapPrototype<string?, int> ConstructQuadratic() =>
        QuadraticFrozenMapPrototype<string?, int>.Create(Persistent);

    /// <summary>
    /// Converts the packed representation into the persistent one, measuring what adopting persistence costs.
    /// </summary>
    internal PersistentHashMap<string?, int> ConvertPackedToPersistent() =>
        Packed.ToPersistent();

    /// <summary>Converts the Robin Hood representation into the persistent one.</summary>
    internal PersistentHashMap<string?, int> ConvertRobinHoodToPersistent() =>
        RobinHood.ToPersistent();

    /// <summary>Converts the quadratic representation into the persistent one.</summary>
    internal PersistentHashMap<string?, int> ConvertQuadraticToPersistent() =>
        Quadratic.ToPersistent();

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
        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(string? value) => 0x51a7;
    }
}
