using System.Collections.Frozen;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.CompilerServices;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.FingerTree.Benchmarks;

/// <summary>
/// F1-only packed-entry frozen-map prototype with a fixed Robin-Hood open-addressed index.
/// </summary>
/// <remarks>
/// The packed entry array always follows source-map enumeration order. Robin-Hood displacement
/// changes only the integer index, so it cannot change stored key representatives or enumeration.
/// The table uses the same maximum 70% load as the F0 linear-probe prototype.
/// </remarks>
internal sealed class RobinHoodFrozenMapPrototype<TKey, TValue>
{
    private const int LoadNumerator = 7;
    private const int LoadDenominator = 10;

    private readonly FrozenLayoutEntry<TKey, TValue>[] _entries;
    private readonly int[] _slots;
    private readonly IEqualityComparer<TKey> _comparer;

    private RobinHoodFrozenMapPrototype(PersistentHashMap<TKey, TValue> source)
    {
        _comparer = source.Comparer;
        _entries = source.Count == 0 ? [] : new FrozenLayoutEntry<TKey, TValue>[source.Count];
        _slots = source.Count == 0 ? [] : new int[GetSlotCount(source.Count)];

        var entryIndex = 0;
        foreach (var pair in source)
        {
            var hash = unchecked((uint)_comparer.GetHashCode(pair.Key!));
            _entries[entryIndex] = new FrozenLayoutEntry<TKey, TValue>(hash, pair.Key, pair.Value);
            InsertSlot(entryIndex, hash);
            entryIndex++;
        }

        if (entryIndex != _entries.Length)
            throw new InvalidOperationException("The source map count changed while it was enumerated.");
    }

    internal static RobinHoodFrozenMapPrototype<TKey, TValue> Create(
        PersistentHashMap<TKey, TValue> source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return new RobinHoodFrozenMapPrototype<TKey, TValue>(source);
    }

    internal int Count => _entries.Length;

    internal IEqualityComparer<TKey> Comparer => _comparer;

    internal PackedFrozenMapPrototypeDiagnostics Diagnostics
    {
        get
        {
            var entryBytes = FrozenLayoutMemory.EstimateArrayBytes<FrozenLayoutEntry<TKey, TValue>>(_entries.Length);
            var slotBytes = FrozenLayoutMemory.EstimateArrayBytes<int>(_slots.Length);
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

    /// <summary>Rebuilds canonical CHAMP topology from the packed source-order entries.</summary>
    internal PersistentHashMap<TKey, TValue> ToPersistent()
    {
        var builder = PersistentHashMap<TKey, TValue>.CreateBulkBuilder(_comparer);
        foreach (ref readonly var entry in _entries.AsSpan())
            builder.SetItem(entry.Key, entry.Value);
        return builder.ToImmutable();
    }

    public FrozenLayoutEnumerator<TKey, TValue> GetEnumerator() => new(_entries);

    private bool TryGetEntry(
        TKey key,
        [MaybeNullWhen(false)] out TKey actualKey,
        [MaybeNullWhen(false)] out TValue value)
    {
        if (_slots.Length == 0)
        {
            actualKey = default;
            value = default;
            return false;
        }

        var hash = unchecked((uint)_comparer.GetHashCode(key!));
        var slot = GetHomeSlot(hash, _slots.Length);
        for (var distance = 0; distance < _slots.Length; distance++)
        {
            var encodedEntryIndex = _slots[slot];
            if (encodedEntryIndex == 0)
            {
                actualKey = default;
                value = default;
                return false;
            }

            ref readonly var entry = ref _entries[encodedEntryIndex - 1];
            var residentDistance = GetProbeDistance(
                GetHomeSlot(entry.Hash, _slots.Length),
                slot,
                _slots.Length);
            if (residentDistance < distance)
            {
                actualKey = default;
                value = default;
                return false;
            }

            if (entry.Hash == hash && _comparer.Equals(entry.Key, key))
            {
                actualKey = entry.Key;
                value = entry.Value;
                return true;
            }

            slot = NextSlot(slot, _slots.Length);
        }

        throw new InvalidOperationException("The Robin-Hood index contains no terminating empty slot.");
    }

    private void InsertSlot(int entryIndex, uint hash)
    {
        var candidateEntryIndex = entryIndex;
        var candidateHash = hash;
        var slot = GetHomeSlot(candidateHash, _slots.Length);
        var distance = 0;

        while (distance < _slots.Length)
        {
            var encodedResidentIndex = _slots[slot];
            if (encodedResidentIndex == 0)
            {
                _slots[slot] = checked(candidateEntryIndex + 1);
                return;
            }

            var residentIndex = encodedResidentIndex - 1;
            ref readonly var resident = ref _entries[residentIndex];
            var residentDistance = GetProbeDistance(
                GetHomeSlot(resident.Hash, _slots.Length),
                slot,
                _slots.Length);
            if (residentDistance < distance)
            {
                _slots[slot] = checked(candidateEntryIndex + 1);
                candidateEntryIndex = residentIndex;
                candidateHash = resident.Hash;
                distance = residentDistance;
            }

            slot = NextSlot(slot, _slots.Length);
            distance++;
        }

        throw new InvalidOperationException("The Robin-Hood index has no empty slot.");
    }

    private static int GetSlotCount(int entryCount)
    {
        var required = checked((((long)entryCount * LoadDenominator) + LoadNumerator - 1) / LoadNumerator);
        return checked((int)Math.Max(2, required));
    }

    private static int GetHomeSlot(uint hash, int slotCount) => (int)(hash % (uint)slotCount);

    private static int GetProbeDistance(int home, int slot, int slotCount) =>
        slot >= home ? slot - home : checked(slotCount - home + slot);

    private static int NextSlot(int slot, int slotCount) => slot + 1 == slotCount ? 0 : slot + 1;
}

/// <summary>
/// F1-only packed-entry frozen-map prototype with a fixed triangular quadratic-probe index.
/// </summary>
/// <remarks>
/// A power-of-two slot count and successive increments 1, 2, 3, ... make the triangular probe
/// sequence visit every slot exactly once. The entry array remains in source enumeration order;
/// only entry indexes are stored in the probe table.
/// </remarks>
internal sealed class QuadraticFrozenMapPrototype<TKey, TValue>
{
    private const int LoadNumerator = 7;
    private const int LoadDenominator = 10;

    private readonly FrozenLayoutEntry<TKey, TValue>[] _entries;
    private readonly int[] _slots;
    private readonly IEqualityComparer<TKey> _comparer;

    private QuadraticFrozenMapPrototype(PersistentHashMap<TKey, TValue> source)
    {
        _comparer = source.Comparer;
        _entries = source.Count == 0 ? [] : new FrozenLayoutEntry<TKey, TValue>[source.Count];
        _slots = source.Count == 0 ? [] : new int[GetSlotCount(source.Count)];

        var entryIndex = 0;
        foreach (var pair in source)
        {
            var hash = unchecked((uint)_comparer.GetHashCode(pair.Key!));
            _entries[entryIndex] = new FrozenLayoutEntry<TKey, TValue>(hash, pair.Key, pair.Value);
            InsertSlot(entryIndex, hash);
            entryIndex++;
        }

        if (entryIndex != _entries.Length)
            throw new InvalidOperationException("The source map count changed while it was enumerated.");
    }

    internal static QuadraticFrozenMapPrototype<TKey, TValue> Create(
        PersistentHashMap<TKey, TValue> source)
    {
        ArgumentNullException.ThrowIfNull(source);
        return new QuadraticFrozenMapPrototype<TKey, TValue>(source);
    }

    internal int Count => _entries.Length;

    internal IEqualityComparer<TKey> Comparer => _comparer;

    internal PackedFrozenMapPrototypeDiagnostics Diagnostics
    {
        get
        {
            var entryBytes = FrozenLayoutMemory.EstimateArrayBytes<FrozenLayoutEntry<TKey, TValue>>(_entries.Length);
            var slotBytes = FrozenLayoutMemory.EstimateArrayBytes<int>(_slots.Length);
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

    /// <summary>Rebuilds canonical CHAMP topology from the packed source-order entries.</summary>
    internal PersistentHashMap<TKey, TValue> ToPersistent()
    {
        var builder = PersistentHashMap<TKey, TValue>.CreateBulkBuilder(_comparer);
        foreach (ref readonly var entry in _entries.AsSpan())
            builder.SetItem(entry.Key, entry.Value);
        return builder.ToImmutable();
    }

    public FrozenLayoutEnumerator<TKey, TValue> GetEnumerator() => new(_entries);

    private bool TryGetEntry(
        TKey key,
        [MaybeNullWhen(false)] out TKey actualKey,
        [MaybeNullWhen(false)] out TValue value)
    {
        if (_slots.Length == 0)
        {
            actualKey = default;
            value = default;
            return false;
        }

        var hash = unchecked((uint)_comparer.GetHashCode(key!));
        var mask = _slots.Length - 1;
        var slot = (int)(hash & (uint)mask);
        for (var probe = 0; probe < _slots.Length; probe++)
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

            slot = (slot + probe + 1) & mask;
        }

        throw new InvalidOperationException("The quadratic-probe index contains no terminating empty slot.");
    }

    private void InsertSlot(int entryIndex, uint hash)
    {
        var mask = _slots.Length - 1;
        var slot = (int)(hash & (uint)mask);
        for (var probe = 0; probe < _slots.Length; probe++)
        {
            if (_slots[slot] == 0)
            {
                _slots[slot] = checked(entryIndex + 1);
                return;
            }

            slot = (slot + probe + 1) & mask;
        }

        throw new InvalidOperationException("The quadratic-probe index has no empty slot.");
    }

    private static int GetSlotCount(int entryCount)
    {
        var requiredLong = checked((((long)entryCount * LoadDenominator) + LoadNumerator - 1) / LoadNumerator);
        var required = checked((int)Math.Max(2, requiredLong));
        var slotCount = 2;
        while (slotCount < required)
        {
            if (slotCount > int.MaxValue / 2)
                throw new OverflowException("The quadratic-probe slot count exceeds the supported array size.");
            slotCount *= 2;
        }

        return slotCount;
    }
}

/// <summary>An entry shared by the F1 fixed-layout prototypes.</summary>
internal readonly struct FrozenLayoutEntry<TKey, TValue>(uint hash, TKey key, TValue value)
{
    internal readonly uint Hash = hash;
    internal readonly TKey Key = key;
    internal readonly TValue Value = value;
}

/// <summary>Allocation-free source-order enumerator shared by the F1 prototypes.</summary>
internal struct FrozenLayoutEnumerator<TKey, TValue>
{
    private readonly FrozenLayoutEntry<TKey, TValue>[] _entries;
    private int _index;

    internal FrozenLayoutEnumerator(FrozenLayoutEntry<TKey, TValue>[] entries)
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

/// <summary>Array-retention estimators used only outside timed benchmark methods.</summary>
internal static class FrozenLayoutMemory
{
    private static readonly MethodInfo SizeOfCoreMethod = typeof(FrozenLayoutMemory).GetMethod(
        nameof(SizeOfCore),
        BindingFlags.Static | BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("Unable to bind the frozen-layout size helper.");

    internal static long EstimateArrayBytes<TElement>(int length)
    {
        if (length == 0)
            return 0;

        return EstimateArrayBytes(length, Unsafe.SizeOf<TElement>());
    }

    /// <summary>
    /// Estimates the shallow arrays reachable through the current runtime's BCL Frozen
    /// implementation objects. Comparers and key/value payload object graphs are not traversed.
    /// </summary>
    internal static long EstimateBclFrozenArrayBytes<TKey, TValue>(
        FrozenDictionary<TKey, TValue> frozen)
        where TKey : notnull
    {
        ArgumentNullException.ThrowIfNull(frozen);
        var visited = new HashSet<object>(ReferenceEqualityComparer.Instance);
        return VisitFrozenImplementation(frozen, visited);
    }

    private static long VisitFrozenImplementation(object value, HashSet<object> visited)
    {
        if (!visited.Add(value))
            return 0;

        if (value is Array array)
        {
            var elementType = array.GetType().GetElementType()
                ?? throw new InvalidOperationException("A retained array has no element type.");
            return EstimateArrayBytes(array.LongLength, GetElementSize(elementType));
        }

        long bytes = 0;
        for (var type = value.GetType(); type is not null && type != typeof(object); type = type.BaseType)
        {
            foreach (var field in type.GetFields(
                BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly))
            {
                var retained = field.GetValue(value);
                if (retained is null)
                    continue;

                if (retained is Array || IsFrozenImplementationType(retained.GetType()))
                    bytes = checked(bytes + VisitFrozenImplementation(retained, visited));
            }
        }

        return bytes;
    }

    private static bool IsFrozenImplementationType(Type type) =>
        type.Namespace?.StartsWith("System.Collections.Frozen", StringComparison.Ordinal) == true;

    private static int GetElementSize(Type elementType) =>
        (int)(SizeOfCoreMethod.MakeGenericMethod(elementType).Invoke(null, null)
            ?? throw new InvalidOperationException("Unable to measure a retained array element."));

    private static int SizeOfCore<TElement>() => Unsafe.SizeOf<TElement>();

    private static long EstimateArrayBytes(long length, int elementSize)
    {
        // CoreCLR SZARRAY estimate: method-table pointer, sync block, and native-sized length/padding,
        // followed by inline element storage and rounded to object alignment.
        var unaligned = checked((3L * IntPtr.Size) + checked(length * elementSize));
        var alignment = IntPtr.Size;
        return checked((unaligned + alignment - 1) & ~(alignment - 1));
    }
}
