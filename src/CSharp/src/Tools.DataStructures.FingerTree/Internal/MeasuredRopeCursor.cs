using System.Collections.Concurrent;
using System.Runtime.CompilerServices;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An immutable cursor-local buffer. Besides the values it retains every element measure and ordered prefix and
/// suffix aggregates, so a non-invertible, noncommutative monoid can move either way without remeasuring values.
/// </summary>
internal sealed class MeasuredCursorBuffer<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private readonly ReadOnlyMemory<T> _values;
    private readonly TMeasure[] _elementMeasures;
    private readonly TMeasure[] _prefixMeasures;
    private readonly TMeasure[] _suffixMeasures;

    private MeasuredCursorBuffer(
        ReadOnlyMemory<T> values,
        TMeasure[] elementMeasures,
        TMeasure[] prefixMeasures,
        TMeasure[] suffixMeasures)
    {
        _values = values;
        _elementMeasures = elementMeasures;
        _prefixMeasures = prefixMeasures;
        _suffixMeasures = suffixMeasures;
    }

    internal int Length => _values.Length;

    internal ReadOnlyMemory<T> Values => _values;

    internal TMeasure Measure => _prefixMeasures[^1];

    internal T this[int index] => _values.Span[index];

    internal TMeasure ElementMeasure(int index) => _elementMeasures[index];

    internal TMeasure PrefixMeasure(int count) => _prefixMeasures[count];

    internal TMeasure SuffixMeasure(int start) => _suffixMeasures[start];

    internal static MeasuredCursorBuffer<T, TMeasure, TMeasureOps> Empty(TMeasure empty) =>
        new(ReadOnlyMemory<T>.Empty, [], [empty], [empty]);

    internal static MeasuredCursorBuffer<T, TMeasure, TMeasureOps> MeasureSpan(ReadOnlySpan<T> values)
    {
        if (values.IsEmpty)
            return Empty(InvokeEmpty());

        var ownedValues = values.ToArray();
        var measures = new TMeasure[ownedValues.Length];
        for (var i = 0; i < ownedValues.Length; i++)
            measures[i] = InvokeMeasure(ownedValues[i]);
        return FromPrepared(ownedValues, measures);
    }

    internal static MeasuredCursorBuffer<T, TMeasure, TMeasureOps> FromPrepared(
        ReadOnlyMemory<T> values,
        TMeasure[] elementMeasures)
    {
        if (values.Length != elementMeasures.Length)
            throw new ArgumentException("The value and element-measure buffers must have the same length.");

        var prefix = new TMeasure[values.Length + 1];
        prefix[0] = InvokeEmpty();
        for (var i = 0; i < elementMeasures.Length; i++)
            prefix[i + 1] = InvokeCombine(prefix[i], elementMeasures[i]);

        var suffix = new TMeasure[values.Length + 1];
        suffix[^1] = InvokeEmpty();
        for (var i = elementMeasures.Length - 1; i >= 0; i--)
            suffix[i] = InvokeCombine(elementMeasures[i], suffix[i + 1]);

        return new MeasuredCursorBuffer<T, TMeasure, TMeasureOps>(values, elementMeasures, prefix, suffix);
    }

    internal static MeasuredCursorBuffer<T, TMeasure, TMeasureOps> FromPrepared(
        ReadOnlyMemory<T> values,
        ReadOnlyMemory<TMeasure> elementMeasures) =>
        FromPrepared(values, elementMeasures.ToArray());

    internal MeasuredCursorBuffer<T, TMeasure, TMeasureOps> CopySlice(int start, int length)
    {
        if (length == 0)
            return Empty(_prefixMeasures[0]);

        var values = _values.Span.Slice(start, length).ToArray();
        var measures = new TMeasure[length];
        _elementMeasures.AsSpan(start, length).CopyTo(measures);
        return FromPrepared(values, measures);
    }

    internal MeasuredChunk<T, TMeasure, TMeasureOps> ToMeasuredChunk(int start, int length)
    {
        if (length <= 0)
            throw new ArgumentOutOfRangeException(nameof(length));

        var aggregate = start == 0
            ? PrefixMeasure(length)
            : start + length == Length
                ? SuffixMeasure(start)
                : AggregateRange(start, length);
        return new MeasuredChunk<T, TMeasure, TMeasureOps>(
            new Chunk<T>(_values.Slice(start, length)),
            aggregate);
    }

    internal MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> PrepareFragment(
        int start,
        int length)
    {
        if (length <= 0)
            throw new ArgumentOutOfRangeException(nameof(length));

        var aggregate = start == 0
            ? PrefixMeasure(length)
            : start + length == Length
                ? SuffixMeasure(start)
                : AggregateRange(start, length);
        return new MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>(
            _values.Slice(start, length),
            _elementMeasures.AsMemory(start, length),
            aggregate);
    }

    internal MeasuredCursorBuffer<T, TMeasure, TMeasureOps> Insert(int index, T value, TMeasure measure)
    {
        var values = new T[Length + 1];
        _values.Span[..index].CopyTo(values);
        values[index] = value;
        _values.Span[index..].CopyTo(values.AsSpan(index + 1));

        var measures = new TMeasure[Length + 1];
        _elementMeasures.AsSpan(0, index).CopyTo(measures);
        measures[index] = measure;
        _elementMeasures.AsSpan(index).CopyTo(measures.AsSpan(index + 1));
        return FromPrepared(values, measures);
    }

    internal MeasuredCursorBuffer<T, TMeasure, TMeasureOps> InsertRange(
        int index,
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps> inserted)
    {
        if (inserted.Length == 0)
            return this;

        var values = new T[checked(Length + inserted.Length)];
        _values.Span[..index].CopyTo(values);
        inserted._values.Span.CopyTo(values.AsSpan(index));
        _values.Span[index..].CopyTo(values.AsSpan(index + inserted.Length));

        var measures = new TMeasure[values.Length];
        _elementMeasures.AsSpan(0, index).CopyTo(measures);
        inserted._elementMeasures.CopyTo(measures, index);
        _elementMeasures.AsSpan(index).CopyTo(measures.AsSpan(index + inserted.Length));
        return FromPrepared(values, measures);
    }

    internal MeasuredCursorBuffer<T, TMeasure, TMeasureOps> RemoveAt(int index)
    {
        if (Length == 1)
            return Empty(_prefixMeasures[0]);

        var values = new T[Length - 1];
        _values.Span[..index].CopyTo(values);
        _values.Span[(index + 1)..].CopyTo(values.AsSpan(index));

        var measures = new TMeasure[Length - 1];
        _elementMeasures.AsSpan(0, index).CopyTo(measures);
        _elementMeasures.AsSpan(index + 1).CopyTo(measures.AsSpan(index));
        return FromPrepared(values, measures);
    }

    internal MeasuredCursorBuffer<T, TMeasure, TMeasureOps> ReplaceAt(int index, T value, TMeasure measure)
    {
        var values = _values.ToArray();
        values[index] = value;
        var measures = (TMeasure[])_elementMeasures.Clone();
        measures[index] = measure;
        return FromPrepared(values, measures);
    }

    internal static MeasuredCursorBuffer<T, TMeasure, TMeasureOps> Concat(
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps> left,
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps> right)
    {
        if (left.Length == 0)
            return right;
        if (right.Length == 0)
            return left;

        var values = new T[checked(left.Length + right.Length)];
        left._values.Span.CopyTo(values);
        right._values.Span.CopyTo(values.AsSpan(left.Length));
        var measures = new TMeasure[values.Length];
        left._elementMeasures.CopyTo(measures, 0);
        right._elementMeasures.CopyTo(measures, left.Length);
        return FromPrepared(values, measures);
    }

    internal static MeasuredCursorBuffer<T, TMeasure, TMeasureOps> Concat(
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps> first,
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps> second,
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps> third) =>
        Concat(Concat(first, second), third);

    internal long EstimateRetainedBytes()
    {
        long result = EstimateArrayBytes<T>(_values.Length);
        result = checked(result + EstimateArrayBytes<TMeasure>(_elementMeasures.Length));
        result = checked(result + EstimateArrayBytes<TMeasure>(_prefixMeasures.Length));
        return checked(result + EstimateArrayBytes<TMeasure>(_suffixMeasures.Length));
    }

    private TMeasure AggregateRange(int start, int length)
    {
        var aggregate = InvokeEmpty();
        var end = start + length;
        for (var i = start; i < end; i++)
            aggregate = InvokeCombine(aggregate, _elementMeasures[i]);
        return aggregate;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static TMeasure InvokeEmpty() => TMeasureOps.Empty;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static TMeasure InvokeMeasure(T value)
    {
        RopeCursorDiagnostics.RecordElementMeasureCallback();
        return TMeasureOps.Measure(value);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal static TMeasure InvokeCombine(TMeasure left, TMeasure right)
    {
        RopeCursorDiagnostics.RecordMeasureCombineCallback();
        return TMeasureOps.Combine(left, right);
    }

    private static long EstimateArrayBytes<TItem>(int length)
    {
        if (length == 0)
            return 0;
        var bytes = checked((3L * IntPtr.Size) + ((long)length * Unsafe.SizeOf<TItem>()));
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }
}

/// <summary>
/// A prepared ordinary-chunk fragment. Element measures are retained once; directional prefix and
/// suffix tables are published lazily only when navigation consumes that side of the fragment.
/// This keeps cursor creation proportional to one element-measure array while preserving O(1)
/// aggregate updates after the first boundary pull.
/// </summary>
internal sealed class MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private readonly ReadOnlyMemory<T> _values;
    private readonly ReadOnlyMemory<TMeasure> _elementMeasures;
    private TMeasure[]? _prefixMeasures;
    private readonly int _prefixOffset;
    private TMeasure[]? _suffixMeasures;
    private readonly int _suffixOffset;

    internal MeasuredCursorPreparedFragment(
        ReadOnlyMemory<T> values,
        ReadOnlyMemory<TMeasure> elementMeasures,
        TMeasure measure,
        TMeasure[]? prefixMeasures = null,
        int prefixOffset = 0,
        TMeasure[]? suffixMeasures = null,
        int suffixOffset = 0)
    {
        if (values.Length != elementMeasures.Length)
            throw new ArgumentException("The value and element-measure fragments must have the same length.");
        _values = values;
        _elementMeasures = elementMeasures;
        _prefixMeasures = prefixMeasures;
        _prefixOffset = prefixOffset;
        _suffixMeasures = suffixMeasures;
        _suffixOffset = suffixOffset;
        Measure = measure;
    }

    internal int Length => _values.Length;

    internal TMeasure Measure { get; }

    internal TMeasure ElementMeasure(int index) => _elementMeasures.Span[index];

    internal static MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> MeasureChunk(
        MeasuredChunk<T, TMeasure, TMeasureOps> chunk)
    {
        var measures = new TMeasure[chunk.Length];
        var span = chunk.Chunk.Data.Span;
        for (var index = 0; index < span.Length; index++)
            measures[index] = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeMeasure(span[index]);
        return new MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>(
            chunk.Chunk.Data,
            measures,
            chunk.Measure);
    }

    internal TMeasure AggregateRange(int start, int length)
    {
        if (length == 0)
            return MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
        if (start == 0 && length == Length)
            return Measure;

        var aggregate = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
        var end = checked(start + length);
        for (var index = start; index < end; index++)
        {
            aggregate = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeCombine(
                aggregate,
                _elementMeasures.Span[index]);
        }
        return aggregate;
    }

    internal TMeasure PrefixMeasure(int count)
    {
        if (count == Length)
            return Measure;
        var prefix = Volatile.Read(ref _prefixMeasures);
        if (prefix is null)
        {
            var candidate = new TMeasure[Length + 1];
            candidate[0] = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
            for (var index = 0; index < Length; index++)
            {
                candidate[index + 1] = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeCombine(
                    candidate[index],
                    _elementMeasures.Span[index]);
            }
            prefix = Interlocked.CompareExchange(ref _prefixMeasures, candidate, null) ?? candidate;
        }
        return prefix[_prefixOffset + count];
    }

    internal TMeasure SuffixMeasure(int start)
    {
        if (start == 0)
            return Measure;
        var suffix = Volatile.Read(ref _suffixMeasures);
        if (suffix is null)
        {
            var candidate = new TMeasure[Length + 1];
            candidate[^1] = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
            for (var index = Length - 1; index >= 0; index--)
            {
                candidate[index] = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeCombine(
                    _elementMeasures.Span[index],
                    candidate[index + 1]);
            }
            suffix = Interlocked.CompareExchange(ref _suffixMeasures, candidate, null) ?? candidate;
        }
        return suffix[_suffixOffset + start];
    }

    internal MeasuredCursorBuffer<T, TMeasure, TMeasureOps> ToBuffer(int start, int length) =>
        length == 0
            ? MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Empty(
                MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty())
            : MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.FromPrepared(
                _values.Slice(start, length),
                _elementMeasures.Slice(start, length));

    internal MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> Slice(
        int start,
        int length,
        TMeasure measure)
    {
        var prefix = start == 0 ? Volatile.Read(ref _prefixMeasures) : null;
        var suffix = start + length == Length ? Volatile.Read(ref _suffixMeasures) : null;
        return new MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>(
            _values.Slice(start, length),
            _elementMeasures.Slice(start, length),
            measure,
            prefix,
            prefix is null ? 0 : _prefixOffset,
            suffix,
            suffix is null ? 0 : _suffixOffset + start);
    }

    internal MeasuredChunk<T, TMeasure, TMeasureOps> ToMeasuredChunk() =>
        new(new Chunk<T>(_values), Measure);
}

/// <summary>
/// A version-lineage cache for ordinary chunks that have already paid the element-measure scan.
/// Entries are keyed by immutable backing-memory slices, so pulling, spilling, and later crossing
/// the same seam can reuse retained element measures and lazily published directional aggregates. Failed preparation is never
/// installed, and racing preparation may duplicate work but can only publish a successful buffer.
/// </summary>
internal sealed class MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private readonly ConcurrentDictionary<ReadOnlyMemory<T>, MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>> _fragments = [];

    internal MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> GetOrMeasure(
        MeasuredChunk<T, TMeasure, TMeasureOps> chunk)
    {
        var key = chunk.Chunk.Data;
        if (_fragments.TryGetValue(key, out var cached))
            return cached;

        var candidate = MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>.MeasureChunk(chunk);
        return _fragments.GetOrAdd(key, candidate);
    }

    internal bool TryGet(
        MeasuredChunk<T, TMeasure, TMeasureOps> chunk,
        out MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> prepared) =>
        _fragments.TryGetValue(chunk.Chunk.Data, out prepared!);

    internal void Register(
        MeasuredChunk<T, TMeasure, TMeasureOps> chunk,
        MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> prepared) =>
        _fragments.TryAdd(chunk.Chunk.Data, prepared);
}

internal readonly record struct MeasuredRopeZipperContext<T, TMeasure, TMeasureOps>(
    FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> LeftTree,
    MeasuredCursorBuffer<T, TMeasure, TMeasureOps> LeftCarry,
    MeasuredCursorBuffer<T, TMeasure, TMeasureOps> Active,
    int Gap,
    MeasuredCursorBuffer<T, TMeasure, TMeasureOps> RightCarry,
    FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> RightTree,
    int Position,
    RopeCursorPrototypeConfiguration Configuration,
    MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps>? FragmentCache,
    TMeasure MeasureBefore,
    TMeasure MeasureAfter,
    MeasuredRope<T, TMeasure, TMeasureOps>? DeferredSource,
    MeasuredChunk<T, TMeasure, TMeasureOps>? DeferredChunk,
    int DeferredChunkStart,
    int DeferredGap)
    where TMeasureOps : IMeasure<T, TMeasure>;

internal sealed class MeasuredCursorVersionState<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private readonly MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> _snapshotSeed;
    private MeasuredRope<T, TMeasure, TMeasureOps>? _snapshot;

    internal MeasuredCursorVersionState(
        int count,
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> snapshotSeed,
        MeasuredRope<T, TMeasure, TMeasureOps>? snapshot)
        : this(
            count,
            MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeCombine(
                snapshotSeed.MeasureBefore,
                snapshotSeed.MeasureAfter),
            snapshotSeed,
            snapshot)
    {
    }

    internal MeasuredCursorVersionState(
        int count,
        TMeasure measure,
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> snapshotSeed,
        MeasuredRope<T, TMeasure, TMeasureOps>? snapshot)
    {
        Count = count;
        Measure = measure;
        _snapshotSeed = snapshotSeed;
        _snapshot = snapshot;
    }

    internal int Count { get; }

    internal TMeasure Measure { get; }

    internal bool HasCachedSnapshot => Volatile.Read(ref _snapshot) is not null;

    internal MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> SnapshotSeedForDiagnostics => _snapshotSeed;

    internal MeasuredRope<T, TMeasure, TMeasureOps> Snapshot()
    {
        var cached = Volatile.Read(ref _snapshot);
        if (cached is not null)
            return cached;

        var candidate = MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.BuildSnapshot(_snapshotSeed, Count);
        var winner = Interlocked.CompareExchange(ref _snapshot, candidate, null);
        return winner ?? candidate;
    }
}

internal readonly record struct MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps>(
    MeasuredCursorVersionState<T, TMeasure, TMeasureOps> Version,
    MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Context)
    where TMeasureOps : IMeasure<T, TMeasure>;

internal readonly record struct MeasuredRopeCursorStateDiagnostics<TMeasure>(
    int Count,
    int Position,
    int ActiveLength,
    int LeftCarryLength,
    int RightCarryLength,
    int LeftOrdinaryChunkCount,
    int RightOrdinaryChunkCount,
    bool HasCachedSnapshot,
    int FocusCapacity,
    int FlushChunkSize,
    long EstimatedRetainedBufferBytes,
    TMeasure MeasureBefore,
    TMeasure MeasureAfter);

internal static class MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private static readonly FingerTree<
        MeasuredChunk<T, TMeasure, TMeasureOps>,
        MeasurePair<int, TMeasure>,
        MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> EmptyTree = FingerTree<
            MeasuredChunk<T, TMeasure, TMeasureOps>,
            MeasurePair<int, TMeasure>,
            MeasuredChunkMeasure<T, TMeasure, TMeasureOps>>.Empty;

    internal static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> Create(
        MeasuredRope<T, TMeasure, TMeasureOps> source,
        int position,
        RopeCursorPrototypeConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(source);
        if ((uint)position > (uint)source.Count)
            throw new ArgumentOutOfRangeException(nameof(position));

        var context = CreateContext(source, position, configuration);
        return new MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps>(
            new MeasuredCursorVersionState<T, TMeasure, TMeasureOps>(
                source.Count,
                source.Measure,
                context,
                source),
            context);
    }

    internal static (bool Found, MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> Created)
        CreateByMeasure<TPredicate>(
            MeasuredRope<T, TMeasure, TMeasureOps> source,
            TPredicate predicate,
            RopeCursorPrototypeConfiguration configuration)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        ArgumentNullException.ThrowIfNull(source);
        var fragmentCache = new MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps>();
        var (found, context) = TryLocateContext(
            source,
            predicate,
            configuration,
            fragmentCache,
            cacheMisses: false);
        return (
            found,
            new MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps>(
                new MeasuredCursorVersionState<T, TMeasure, TMeasureOps>(
                    source.Count,
                    source.Measure,
                    context,
                    source),
                context));
    }

    internal static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> CreateContext(
        MeasuredRope<T, TMeasure, TMeasureOps> source,
        int position,
        RopeCursorPrototypeConfiguration configuration,
        MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps>? fragmentCache = null)
    {
        if ((uint)position > (uint)source.Count)
            throw new ArgumentOutOfRangeException(nameof(position));

        if (source.IsEmpty)
        {
            var emptyMeasure = source.Measure;
            var empty = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Empty(emptyMeasure);
            fragmentCache ??= new MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps>();
            return new MeasuredRopeZipperContext<T, TMeasure, TMeasureOps>(
                EmptyTree,
                empty,
                empty,
                0,
                empty,
                EmptyTree,
                0,
                configuration,
                fragmentCache,
                emptyMeasure,
                emptyMeasure,
                DeferredSource: null,
                DeferredChunk: null,
                DeferredChunkStart: 0,
                DeferredGap: 0);
        }

        FingerTree<
            MeasuredChunk<T, TMeasure, TMeasureOps>,
            MeasurePair<int, TMeasure>,
            MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> left;
        FingerTree<
            MeasuredChunk<T, TMeasure, TMeasureOps>,
            MeasurePair<int, TMeasure>,
            MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> right;
        MeasuredChunk<T, TMeasure, TMeasureOps> hit;
        int chunkStart;
        int gapInChunk;

        if (position == source.Count)
        {
            source.TreeForMeasuredCursor.TryViewRight(out hit, out left);
            right = EmptyTree;
            chunkStart = left.Measure.First;
            gapInChunk = hit.Length;
        }
        else
        {
            source.TreeForMeasuredCursor.TrySplitFind(
                new PairCountAbovePredicate<TMeasure>(position),
                out left,
                out hit,
                out right);
            chunkStart = left.Measure.First;
            gapInChunk = position - chunkStart;
        }

        fragmentCache ??= new MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps>();
        var prepared = fragmentCache.GetOrMeasure(hit);
        var activeLength = Math.Min(configuration.FocusCapacity, prepared.Length);
        var activeStart = Math.Clamp(
            gapInChunk - (activeLength / 2),
            0,
            prepared.Length - activeLength);
        var leftFragmentMeasure = prepared.AggregateRange(0, activeStart);
        var suffixStart = activeStart + activeLength;
        var rightFragmentMeasure = prepared.AggregateRange(
            suffixStart,
            prepared.Length - suffixStart);
        return CreateContextFromLocatedChunk(
            left,
            prepared,
            right,
            chunkStart,
            gapInChunk,
            configuration,
            fragmentCache,
            leftFragmentMeasure,
            rightFragmentMeasure);
    }

    internal static (bool Found, MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Context)
        TryLocateContext<TPredicate>(
            MeasuredRope<T, TMeasure, TMeasureOps> source,
            TPredicate predicate,
            RopeCursorPrototypeConfiguration configuration,
            MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps> fragmentCache,
            bool cacheMisses)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        if (!source.TreeForMeasuredCursor.TryLocateWithSuffix(
                new SecondComponentPredicate<TPredicate, int, TMeasure>(predicate),
                out var before,
                out var hit,
                out var after))
        {
            return (false, CreateDeferredEndContext(source, configuration, fragmentCache));
        }

        (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter) located;
        if (fragmentCache.TryGet(hit, out var prepared))
        {
            located = RopeCursorDiagnostics.IsSessionActive
                ? LocateInPreparedChunkWithDiagnostics(prepared, before.Second, after.Second, predicate)
                : LocateInPreparedChunk(prepared, before.Second, after.Second, predicate);
        }
        else if (cacheMisses)
        {
            var measured = RopeCursorDiagnostics.IsSessionActive
                ? LocateAndPrepareChunkWithDiagnostics(hit, before.Second, after.Second, predicate)
                : LocateAndPrepareChunk(hit, before.Second, after.Second, predicate);
            fragmentCache.Register(
                hit,
                new MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps>(
                    hit.Chunk.Data,
                    measured.ElementMeasures,
                    hit.Measure));
            located = (measured.Found, measured.Gap, measured.MeasureBefore, measured.MeasureAfter);
        }
        else
        {
            located = RopeCursorDiagnostics.IsSessionActive
                ? LocateInUncachedChunkWithDiagnostics(hit, before.Second, after.Second, predicate)
                : LocateInUncachedChunk(hit, before.Second, after.Second, predicate);
        }

        // A lawful monotone predicate that selected this chunk must flip within it. Treat a stateful or otherwise
        // inconsistent predicate as a miss instead of manufacturing a boundary from the final element.
        if (!located.Found)
            return (false, CreateDeferredEndContext(source, configuration, fragmentCache));

        // The scan deliberately continues after the boundary. For an arbitrary, non-invertible and
        // noncommutative monoid this is the only way to cache the exact suffix without either a second
        // element-measure pass or cursor-local arrays. The selected ordinary chunk is retained as an
        // immutable deferred focus and is prepared only if navigation or editing actually needs it.
        return (
            true,
            CreateDeferredContext(
                source,
                hit,
                before.First,
                located.Gap,
                configuration,
                fragmentCache,
                located.MeasureBefore,
                located.MeasureAfter));
    }

    private static (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter)
        LocateInUncachedChunk<TPredicate>(
            MeasuredChunk<T, TMeasure, TMeasureOps> hit,
            TMeasure measureBeforeChunk,
            TMeasure measureAfterChunk,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var accumulated = measureBeforeChunk;
        var measureAfter = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
        var gap = -1;
        var measureBefore = default(TMeasure)!;
        for (var offset = 0; offset < hit.Length; offset++)
        {
            var elementMeasure = TMeasureOps.Measure(hit[offset]);
            if (gap < 0)
            {
                var inclusive = TMeasureOps.Combine(accumulated, elementMeasure);
                if (predicate.Invoke(inclusive))
                {
                    gap = offset;
                    measureBefore = accumulated;
                    measureAfter = elementMeasure;
                }
                else
                {
                    accumulated = inclusive;
                }
            }
            else
            {
                measureAfter = TMeasureOps.Combine(measureAfter, elementMeasure);
            }
        }

        return gap < 0
            ? (false, 0, default!, default!)
            : (true, gap, measureBefore, TMeasureOps.Combine(measureAfter, measureAfterChunk));
    }

    private static (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter)
        LocateInUncachedChunkWithDiagnostics<TPredicate>(
            MeasuredChunk<T, TMeasure, TMeasureOps> hit,
            TMeasure measureBeforeChunk,
            TMeasure measureAfterChunk,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var measureCallbackCount = 0;
        var combineCallbackCount = 0;
        try
        {
            var accumulated = measureBeforeChunk;
            var measureAfter = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
            var gap = -1;
            var measureBefore = default(TMeasure)!;
            for (var offset = 0; offset < hit.Length; offset++)
            {
                measureCallbackCount++;
                var elementMeasure = TMeasureOps.Measure(hit[offset]);
                if (gap < 0)
                {
                    combineCallbackCount++;
                    var inclusive = TMeasureOps.Combine(accumulated, elementMeasure);
                    if (predicate.Invoke(inclusive))
                    {
                        gap = offset;
                        measureBefore = accumulated;
                        measureAfter = elementMeasure;
                    }
                    else
                    {
                        accumulated = inclusive;
                    }
                }
                else
                {
                    combineCallbackCount++;
                    measureAfter = TMeasureOps.Combine(measureAfter, elementMeasure);
                }
            }

            if (gap < 0)
                return (false, 0, default!, default!);

            combineCallbackCount++;
            return (true, gap, measureBefore, TMeasureOps.Combine(measureAfter, measureAfterChunk));
        }
        finally
        {
            RopeCursorDiagnostics.RecordElementMeasureCallback(measureCallbackCount);
            RopeCursorDiagnostics.RecordMeasureCombineCallback(combineCallbackCount);
        }
    }

    private static (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter, TMeasure[] ElementMeasures)
        LocateAndPrepareChunk<TPredicate>(
            MeasuredChunk<T, TMeasure, TMeasureOps> hit,
            TMeasure measureBeforeChunk,
            TMeasure measureAfterChunk,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var elementMeasures = new TMeasure[hit.Length];
        var accumulated = measureBeforeChunk;
        var measureAfter = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
        var gap = -1;
        var measureBefore = default(TMeasure)!;
        for (var offset = 0; offset < hit.Length; offset++)
        {
            var elementMeasure = TMeasureOps.Measure(hit[offset]);
            elementMeasures[offset] = elementMeasure;
            if (gap < 0)
            {
                var inclusive = TMeasureOps.Combine(accumulated, elementMeasure);
                if (predicate.Invoke(inclusive))
                {
                    gap = offset;
                    measureBefore = accumulated;
                    measureAfter = elementMeasure;
                }
                else
                {
                    accumulated = inclusive;
                }
            }
            else
            {
                measureAfter = TMeasureOps.Combine(measureAfter, elementMeasure);
            }
        }

        return gap < 0
            ? (false, 0, default!, default!, elementMeasures)
            : (true, gap, measureBefore, TMeasureOps.Combine(measureAfter, measureAfterChunk), elementMeasures);
    }

    private static (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter, TMeasure[] ElementMeasures)
        LocateAndPrepareChunkWithDiagnostics<TPredicate>(
            MeasuredChunk<T, TMeasure, TMeasureOps> hit,
            TMeasure measureBeforeChunk,
            TMeasure measureAfterChunk,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var measureCallbackCount = 0;
        var combineCallbackCount = 0;
        try
        {
            var elementMeasures = new TMeasure[hit.Length];
            var accumulated = measureBeforeChunk;
            var measureAfter = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty();
            var gap = -1;
            var measureBefore = default(TMeasure)!;
            for (var offset = 0; offset < hit.Length; offset++)
            {
                measureCallbackCount++;
                var elementMeasure = TMeasureOps.Measure(hit[offset]);
                elementMeasures[offset] = elementMeasure;
                if (gap < 0)
                {
                    combineCallbackCount++;
                    var inclusive = TMeasureOps.Combine(accumulated, elementMeasure);
                    if (predicate.Invoke(inclusive))
                    {
                        gap = offset;
                        measureBefore = accumulated;
                        measureAfter = elementMeasure;
                    }
                    else
                    {
                        accumulated = inclusive;
                    }
                }
                else
                {
                    combineCallbackCount++;
                    measureAfter = TMeasureOps.Combine(measureAfter, elementMeasure);
                }
            }

            if (gap < 0)
                return (false, 0, default!, default!, elementMeasures);

            combineCallbackCount++;
            return (
                true,
                gap,
                measureBefore,
                TMeasureOps.Combine(measureAfter, measureAfterChunk),
                elementMeasures);
        }
        finally
        {
            RopeCursorDiagnostics.RecordElementMeasureCallback(measureCallbackCount);
            RopeCursorDiagnostics.RecordMeasureCombineCallback(combineCallbackCount);
        }
    }

    private static (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter)
        LocateInPreparedChunk<TPredicate>(
            MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> prepared,
            TMeasure measureBeforeChunk,
            TMeasure measureAfterChunk,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var accumulated = measureBeforeChunk;
        for (var offset = 0; offset < prepared.Length; offset++)
        {
            var elementMeasure = prepared.ElementMeasure(offset);
            var inclusive = TMeasureOps.Combine(accumulated, elementMeasure);
            if (predicate.Invoke(inclusive))
            {
                return (
                    true,
                    offset,
                    accumulated,
                    TMeasureOps.Combine(prepared.SuffixMeasure(offset), measureAfterChunk));
            }
            accumulated = inclusive;
        }

        return (false, 0, default!, default!);
    }

    private static (bool Found, int Gap, TMeasure MeasureBefore, TMeasure MeasureAfter)
        LocateInPreparedChunkWithDiagnostics<TPredicate>(
            MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> prepared,
            TMeasure measureBeforeChunk,
            TMeasure measureAfterChunk,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var combineCallbackCount = 0;
        try
        {
            var accumulated = measureBeforeChunk;
            for (var offset = 0; offset < prepared.Length; offset++)
            {
                var elementMeasure = prepared.ElementMeasure(offset);
                combineCallbackCount++;
                var inclusive = TMeasureOps.Combine(accumulated, elementMeasure);
                if (predicate.Invoke(inclusive))
                {
                    combineCallbackCount++;
                    return (
                        true,
                        offset,
                        accumulated,
                        TMeasureOps.Combine(prepared.SuffixMeasure(offset), measureAfterChunk));
                }
                accumulated = inclusive;
            }

            return (false, 0, default!, default!);
        }
        finally
        {
            RopeCursorDiagnostics.RecordMeasureCombineCallback(combineCallbackCount);
        }
    }

    internal static bool TryPeekPrevious(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        out T value)
    {
        if (context.Position == 0)
        {
            value = default!;
            return false;
        }

        if (context.DeferredSource is { } deferredSource)
        {
            if (context.DeferredChunk is { } deferred && context.DeferredGap > 0)
            {
                value = deferred[context.DeferredGap - 1];
                return true;
            }

            RopeCursorDiagnostics.RecordNodeVisits();
            value = deferredSource[context.Position - 1];
            return true;
        }

        if (context.Gap > 0)
        {
            value = context.Active[context.Gap - 1];
            return true;
        }

        if (context.LeftCarry.Length > 0)
        {
            value = context.LeftCarry[context.LeftCarry.Length - 1];
            return true;
        }

        RopeCursorDiagnostics.RecordNodeVisits();
        var chunk = context.LeftTree.Last;
        value = chunk[chunk.Length - 1];
        return true;
    }

    internal static bool TryPeekNext(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int count,
        out T value)
    {
        if (context.Position == count)
        {
            value = default!;
            return false;
        }

        if (context.DeferredSource is { } deferredSource)
        {
            if (context.DeferredChunk is { } deferred && context.DeferredGap < deferred.Length)
            {
                value = deferred[context.DeferredGap];
                return true;
            }

            RopeCursorDiagnostics.RecordNodeVisits();
            value = deferredSource[context.Position];
            return true;
        }

        if (context.Gap < context.Active.Length)
        {
            value = context.Active[context.Gap];
            return true;
        }

        if (context.RightCarry.Length > 0)
        {
            value = context.RightCarry[0];
            return true;
        }

        RopeCursorDiagnostics.RecordNodeVisits();
        var chunk = context.RightTree.First;
        value = chunk[0];
        return true;
    }

    internal static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> MovePrevious(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
        int count)
    {
        if (source.Position == 0)
            throw new InvalidOperationException("The cursor is already at the start of the rope.");

        var context = Materialize(source);
        if (context.Gap == 0)
        {
            context = PullFromLeft(context, Math.Max(1, context.Configuration.FocusCapacity / 2));
            context = Balance(context, preferLeft: false);
        }

        context = context with { Gap = context.Gap - 1, Position = context.Position - 1 };
        context = WithMeasures(context);
        Validate(context, count);
        return context;
    }

    internal static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> MoveNext(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
        int count)
    {
        if (source.Position == count)
            throw new InvalidOperationException("The cursor is already at the end of the rope.");

        var context = Materialize(source);
        if (context.Gap == context.Active.Length)
        {
            context = PullFromRight(context, Math.Max(1, context.Configuration.FocusCapacity / 2));
            context = Balance(context, preferLeft: true);
        }

        context = context with { Gap = context.Gap + 1, Position = context.Position + 1 };
        context = WithMeasures(context);
        Validate(context, count);
        return context;
    }

    internal static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Seek(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
        int position)
    {
        if ((uint)position > (uint)version.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        if (position == source.Position)
            return source;

        RopeCursorDiagnostics.RecordNodeVisits();
        return CreateContext(version.Snapshot(), position, source.Configuration);
    }

    internal static (bool Found, MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Context)
        SeekByMeasure<TPredicate>(
            MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
            in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
            TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure> =>
        PreserveSamePosition(
            TryLocateContext(
                version.Snapshot(),
                predicate,
                source.Configuration,
                source.FragmentCache!,
                cacheMisses: true),
            source);

    internal static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> Insert(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
        T value)
    {
        if (version.Count == int.MaxValue)
            throw new OverflowException("The operation would create a rope with more than Int32.MaxValue elements.");

        var valueMeasure = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeMeasure(value);
        var context = Materialize(source);
        var active = context.Active.Insert(context.Gap, value, valueMeasure);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with
        {
            Active = active,
            Gap = context.Gap + 1,
            Position = context.Position + 1,
        };
        context = Balance(context, preferLeft: true);
        context = WithMeasures(context);
        return CreateEditedVersion(context, version.Count + 1);
    }

    internal static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> InsertRange(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
        ReadOnlySpan<T> values)
    {
        if (values.IsEmpty)
            return new MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps>(version, source);
        if (values.Length > int.MaxValue - version.Count)
            throw new OverflowException("The operation would create a rope with more than Int32.MaxValue elements.");

        var inserted = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.MeasureSpan(values);
        var context = Materialize(source);
        var active = context.Active.InsertRange(context.Gap, inserted);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with
        {
            Active = active,
            Gap = context.Gap + values.Length,
            Position = context.Position + values.Length,
        };
        context = Balance(context, preferLeft: true);
        context = WithMeasures(context);
        return CreateEditedVersion(context, checked(version.Count + values.Length));
    }

    internal static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> DeletePrevious(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source)
    {
        if (source.Position == 0)
            throw new InvalidOperationException("The cursor has no previous element to delete.");

        var context = Materialize(source);
        if (context.Gap == 0)
            context = PullFromLeft(context, Math.Max(1, context.Configuration.FocusCapacity / 2));

        var active = context.Active.RemoveAt(context.Gap - 1);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with
        {
            Active = active,
            Gap = context.Gap - 1,
            Position = context.Position - 1,
        };
        context = Balance(context, preferLeft: false);
        context = Refill(context);
        context = WithMeasures(context);
        return CreateEditedVersion(context, version.Count - 1);
    }

    internal static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> DeleteNext(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source)
    {
        if (source.Position == version.Count)
            throw new InvalidOperationException("The cursor has no next element to delete.");

        var context = Materialize(source);
        if (context.Gap == context.Active.Length)
            context = PullFromRight(context, Math.Max(1, context.Configuration.FocusCapacity / 2));

        var active = context.Active.RemoveAt(context.Gap);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with { Active = active };
        context = Balance(context, preferLeft: true);
        context = Refill(context);
        context = WithMeasures(context);
        return CreateEditedVersion(context, version.Count - 1);
    }

    internal static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> ReplaceNext(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source,
        T value)
    {
        if (source.Position == version.Count)
            throw new InvalidOperationException("The cursor has no next element to replace.");

        var valueMeasure = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeMeasure(value);
        var context = Materialize(source);
        if (context.Gap == context.Active.Length)
        {
            context = PullFromRight(context, Math.Max(1, context.Configuration.FocusCapacity / 2));
            context = Balance(context, preferLeft: true);
        }

        var active = context.Active.ReplaceAt(context.Gap, value, valueMeasure);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with { Active = active };
        context = WithMeasures(context);
        return CreateEditedVersion(context, version.Count);
    }

    internal static MeasuredRope<T, TMeasure, TMeasureOps> BuildSnapshot(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int count)
    {
        var materialized = Materialize(context);
        var middle = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(
            materialized.LeftCarry,
            materialized.Active,
            materialized.RightCarry);
        var chunks = new List<MeasuredChunk<T, TMeasure, TMeasureOps>>(
            (middle.Length + RopeChunking.MaxChunkSize - 1) / RopeChunking.MaxChunkSize);
        for (var offset = 0; offset < middle.Length; offset += RopeChunking.MaxChunkSize)
        {
            var length = Math.Min(RopeChunking.MaxChunkSize, middle.Length - offset);
            chunks.Add(middle.ToMeasuredChunk(offset, length));
        }

        RopeCursorDiagnostics.RecordSnapshotNormalization();
        RopeCursorDiagnostics.RecordSpineAllocation(2);
        if (materialized.LeftCarry.Length > 0)
            RopeCursorDiagnostics.RecordCarryCopy(materialized.LeftCarry.Length);
        if (materialized.RightCarry.Length > 0)
            RopeCursorDiagnostics.RecordCarryCopy(materialized.RightCarry.Length);
        RopeCursorDiagnostics.RecordFocusCopy(materialized.Active.Length);

        var snapshot = MeasuredRope<T, TMeasure, TMeasureOps>.FromMeasuredCursorParts(
            materialized.LeftTree,
            [.. chunks],
            materialized.RightTree);
        if (snapshot.Count != count)
            throw new InvalidOperationException("The measured cursor snapshot does not match its logical version count.");
        return snapshot;
    }

    internal static MeasuredRopeCursorStateDiagnostics<TMeasure> GetDiagnostics(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context)
    {
        var deferred = context.DeferredSource is not null;
        return new(
            Count: version.Count,
            Position: context.Position,
            ActiveLength: deferred ? 0 : context.Active.Length,
            LeftCarryLength: deferred ? 0 : context.LeftCarry.Length,
            RightCarryLength: deferred ? 0 : context.RightCarry.Length,
            LeftOrdinaryChunkCount: context.LeftTree.Count(),
            RightOrdinaryChunkCount: context.RightTree.Count(),
            HasCachedSnapshot: version.HasCachedSnapshot,
            FocusCapacity: context.Configuration.FocusCapacity,
            FlushChunkSize: context.Configuration.FlushChunkSize,
            EstimatedRetainedBufferBytes: EstimateRetainedBufferBytes(
                version.SnapshotSeedForDiagnostics,
                context),
            MeasureBefore: context.MeasureBefore,
            MeasureAfter: context.MeasureAfter);
    }

    internal static void Validate(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int count)
    {
        if (context.DeferredSource is { } deferredSource)
        {
            if (context.FragmentCache is null || context.LeftCarry is not null ||
                context.Active is not null || context.Gap != 0 || context.RightCarry is not null ||
                !context.LeftTree.IsEmpty || !context.RightTree.IsEmpty)
            {
                throw new InvalidOperationException("A deferred measured cursor contains materialized focus state.");
            }
            if (deferredSource.Count != count || (uint)context.Position > (uint)count)
            {
                throw new InvalidOperationException(
                    "The deferred measured cursor decomposition does not match its logical position or count.");
            }
            if (context.DeferredChunk is { } deferred)
            {
                if ((uint)context.DeferredGap > (uint)deferred.Length ||
                    checked(context.DeferredChunkStart + context.DeferredGap) != context.Position)
                {
                    throw new InvalidOperationException(
                        "The deferred measured cursor gap lies outside its located ordinary chunk.");
                }
            }
            else if (context.Position != count || context.DeferredChunkStart != count || context.DeferredGap != 0)
            {
                throw new InvalidOperationException("A deferred miss is not the logical end cursor.");
            }
            return;
        }

        if (context.FragmentCache is null)
            throw new InvalidOperationException("A materialized measured cursor has no fragment cache.");
        if ((uint)context.Gap > (uint)context.Active.Length)
            throw new InvalidOperationException("The measured cursor gap lies outside its active window.");
        if (context.Active.Length > context.Configuration.FocusCapacity)
            throw new InvalidOperationException("The measured cursor active window exceeds its configured capacity.");
        if (context.LeftCarry.Length >= context.Configuration.FlushChunkSize ||
            context.RightCarry.Length >= context.Configuration.FlushChunkSize)
        {
            throw new InvalidOperationException("A measured cursor carry reached its flush threshold.");
        }

        var computedPosition = checked(context.LeftTree.Measure.First + context.LeftCarry.Length + context.Gap);
        var computedCount = checked(
            context.LeftTree.Measure.First +
            context.LeftCarry.Length +
            context.Active.Length +
            context.RightCarry.Length +
            context.RightTree.Measure.First);
        if (computedPosition != context.Position || computedCount != count)
            throw new InvalidOperationException("The measured cursor decomposition does not match its logical position or count.");
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> CreateDeferredEndContext(
        MeasuredRope<T, TMeasure, TMeasureOps> source,
        RopeCursorPrototypeConfiguration configuration,
        MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps> fragmentCache)
    {
        if (source.IsEmpty)
            return CreateContext(source, 0, configuration, fragmentCache);

        return CreateDeferredContext(
            source,
            chunk: null,
            source.Count,
            gapInChunk: 0,
            configuration,
            fragmentCache,
            source.Measure,
            MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty());
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> CreateDeferredContext(
        MeasuredRope<T, TMeasure, TMeasureOps> source,
        MeasuredChunk<T, TMeasure, TMeasureOps>? chunk,
        int chunkStart,
        int gapInChunk,
        RopeCursorPrototypeConfiguration configuration,
        MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps> fragmentCache,
        TMeasure measureBefore,
        TMeasure measureAfter)
    {
        var context = new MeasuredRopeZipperContext<T, TMeasure, TMeasureOps>(
            EmptyTree,
            LeftCarry: null!,
            Active: null!,
            Gap: 0,
            RightCarry: null!,
            EmptyTree,
            checked(chunkStart + gapInChunk),
            configuration,
            fragmentCache,
            measureBefore,
            measureAfter,
            source,
            chunk,
            chunkStart,
            gapInChunk);
        Validate(context, source.Count);
        return context;
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Materialize(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source)
    {
        if (source.DeferredSource is not { } deferredSource)
            return source;
        return CreateContext(deferredSource, source.Position, source.Configuration, source.FragmentCache);
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> CreateContextFromLocatedChunk(
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> left,
        MeasuredCursorPreparedFragment<T, TMeasure, TMeasureOps> chunk,
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> right,
        int chunkStart,
        int gapInChunk,
        RopeCursorPrototypeConfiguration configuration,
        MeasuredCursorFragmentCache<T, TMeasure, TMeasureOps> fragmentCache,
        TMeasure leftFragmentMeasure,
        TMeasure rightFragmentMeasure)
    {
        var activeLength = Math.Min(configuration.FocusCapacity, chunk.Length);
        var activeStart = Math.Clamp(
            gapInChunk - (activeLength / 2),
            0,
            chunk.Length - activeLength);

        if (activeStart > 0)
        {
            var preparedPrefix = chunk.Slice(0, activeStart, leftFragmentMeasure);
            var prefix = preparedPrefix.ToMeasuredChunk();
            fragmentCache.Register(prefix, preparedPrefix);
            left = left.Append(prefix);
        }
        var suffixStart = activeStart + activeLength;
        if (suffixStart < chunk.Length)
        {
            var preparedSuffix = chunk.Slice(
                suffixStart,
                chunk.Length - suffixStart,
                rightFragmentMeasure);
            var suffix = preparedSuffix.ToMeasuredChunk();
            fragmentCache.Register(suffix, preparedSuffix);
            right = right.Prepend(suffix);
        }

        var active = chunk.ToBuffer(activeStart, activeLength);
        var empty = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Empty(
            MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty());
        var context = new MeasuredRopeZipperContext<T, TMeasure, TMeasureOps>(
            left,
            empty,
            active,
            gapInChunk - activeStart,
            empty,
            right,
            chunkStart + gapInChunk,
            configuration,
            fragmentCache,
            default!,
            default!,
            DeferredSource: null,
            DeferredChunk: null,
            DeferredChunkStart: 0,
            DeferredGap: 0);
        context = WithMeasures(context);
        Validate(context, checked(left.Measure.First + active.Length + right.Measure.First));
        return context;
    }

    private static MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> CreateEditedVersion(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int count)
    {
        Validate(context, count);
        var measure = Combine(context.MeasureBefore, context.MeasureAfter);
        var version = new MeasuredCursorVersionState<T, TMeasure, TMeasureOps>(
            count,
            measure,
            context,
            snapshot: null);
        return new MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps>(version, context);
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> WithMeasures(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context)
    {
        var beforeFocus = Combine(context.LeftCarry.Measure, context.Active.PrefixMeasure(context.Gap));
        var measureBefore = Combine(context.LeftTree.Measure.Second, beforeFocus);
        var afterFocus = Combine(context.RightCarry.Measure, context.RightTree.Measure.Second);
        var measureAfter = Combine(context.Active.SuffixMeasure(context.Gap), afterFocus);
        return context with { MeasureBefore = measureBefore, MeasureAfter = measureAfter };
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Refill(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context)
    {
        var target = Math.Max(1, context.Configuration.FocusCapacity / 2);
        if (context.Active.Length > context.Configuration.FocusCapacity)
            context = Balance(context, context.Gap >= context.Active.Length / 2);
        if (context.Active.Length >= target)
            return context;

        var needed = target - context.Active.Length;
        var leftTarget = Math.Min(needed / 2, context.Position - context.Gap);
        if (leftTarget > 0)
            context = PullFromLeft(context, leftTarget);
        needed = target - context.Active.Length;
        if (needed > 0)
            context = PullFromRight(context, needed);
        needed = target - context.Active.Length;
        if (needed > 0)
            context = PullFromLeft(context, needed);
        return Balance(context, context.Gap >= context.Active.Length / 2);
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Balance(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        bool preferLeft)
    {
        var overflow = context.Active.Length - context.Configuration.FocusCapacity;
        if (overflow <= 0)
            return context;

        if (preferLeft && overflow <= context.Gap)
            return SpillToLeft(context, overflow);
        if (!preferLeft && overflow <= context.Active.Length - context.Gap)
            return SpillToRight(context, overflow);
        return overflow <= context.Gap
            ? SpillToLeft(context, overflow)
            : SpillToRight(context, overflow);
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> SpillToLeft(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int count)
    {
        var moved = context.Active.CopySlice(0, count);
        var combined = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(context.LeftCarry, moved);
        RopeCursorDiagnostics.RecordCarryCopy(combined.Length);

        var leftTree = context.LeftTree;
        var flushSize = context.Configuration.FlushChunkSize;
        var flushLength = combined.Length - (combined.Length % flushSize);
        for (var offset = 0; offset < flushLength; offset += flushSize)
        {
            var prepared = combined.PrepareFragment(offset, flushSize);
            var chunk = prepared.ToMeasuredChunk();
            context.FragmentCache!.Register(chunk, prepared);
            leftTree = leftTree.Append(chunk);
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        var carry = combined.CopySlice(flushLength, combined.Length - flushLength);
        var active = context.Active.CopySlice(count, context.Active.Length - count);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            LeftTree = leftTree,
            LeftCarry = carry,
            Active = active,
            Gap = context.Gap - count,
        };
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> SpillToRight(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int count)
    {
        var activeLength = context.Active.Length - count;
        var moved = context.Active.CopySlice(activeLength, count);
        var combined = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(moved, context.RightCarry);
        RopeCursorDiagnostics.RecordCarryCopy(combined.Length);

        var flushSize = context.Configuration.FlushChunkSize;
        var carryLength = combined.Length % flushSize;
        var carry = combined.CopySlice(0, carryLength);
        var flushed = EmptyTree;
        for (var offset = carryLength; offset < combined.Length; offset += flushSize)
        {
            var prepared = combined.PrepareFragment(offset, flushSize);
            var chunk = prepared.ToMeasuredChunk();
            context.FragmentCache!.Register(chunk, prepared);
            flushed = flushed.Append(chunk);
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        var active = context.Active.CopySlice(0, activeLength);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            Active = active,
            RightCarry = carry,
            RightTree = flushed.Concat(context.RightTree),
        };
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> PullFromLeft(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int maximum)
    {
        var empty = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Empty(
            MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty());
        var pulled = empty;
        var carry = context.LeftCarry;
        var tree = context.LeftTree;
        while (pulled.Length < maximum && (carry.Length > 0 || !tree.IsEmpty))
        {
            if (carry.Length > 0)
            {
                var take = Math.Min(maximum - pulled.Length, carry.Length);
                var carryFragment = carry.CopySlice(carry.Length - take, take);
                pulled = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(carryFragment, pulled);
                carry = carry.CopySlice(0, carry.Length - take);
                continue;
            }

            RopeCursorDiagnostics.RecordNodeVisits();
            tree.TryViewRight(out var chunk, out var rest);
            var prepared = context.FragmentCache!.GetOrMeasure(chunk);
            var chunkTake = Math.Min(maximum - pulled.Length, prepared.Length);
            var treeFragment = prepared.ToBuffer(prepared.Length - chunkTake, chunkTake);
            pulled = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(treeFragment, pulled);
            if (chunkTake == prepared.Length)
            {
                tree = rest;
            }
            else
            {
                var remainderLength = prepared.Length - chunkTake;
                var preparedRemainder = prepared.Slice(
                    0,
                    remainderLength,
                    prepared.PrefixMeasure(remainderLength));
                var remainder = preparedRemainder.ToMeasuredChunk();
                context.FragmentCache!.Register(remainder, preparedRemainder);
                tree = rest.Append(remainder);
            }
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        if (pulled.Length == 0)
            return context;

        var active = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(pulled, context.Active);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            LeftTree = tree,
            LeftCarry = carry,
            Active = active,
            Gap = context.Gap + pulled.Length,
        };
    }

    private static MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> PullFromRight(
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context,
        int maximum)
    {
        var empty = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Empty(
            MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeEmpty());
        var pulled = empty;
        var carry = context.RightCarry;
        var tree = context.RightTree;
        while (pulled.Length < maximum && (carry.Length > 0 || !tree.IsEmpty))
        {
            if (carry.Length > 0)
            {
                var take = Math.Min(maximum - pulled.Length, carry.Length);
                var carryFragment = carry.CopySlice(0, take);
                pulled = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(pulled, carryFragment);
                carry = carry.CopySlice(take, carry.Length - take);
                continue;
            }

            RopeCursorDiagnostics.RecordNodeVisits();
            tree.TryViewLeft(out var chunk, out var rest);
            var prepared = context.FragmentCache!.GetOrMeasure(chunk);
            var chunkTake = Math.Min(maximum - pulled.Length, prepared.Length);
            var treeFragment = prepared.ToBuffer(0, chunkTake);
            pulled = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(pulled, treeFragment);
            if (chunkTake == prepared.Length)
            {
                tree = rest;
            }
            else
            {
                var remainderLength = prepared.Length - chunkTake;
                var preparedRemainder = prepared.Slice(
                    chunkTake,
                    remainderLength,
                    prepared.SuffixMeasure(chunkTake));
                var remainder = preparedRemainder.ToMeasuredChunk();
                context.FragmentCache!.Register(remainder, preparedRemainder);
                tree = rest.Prepend(remainder);
            }
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        if (pulled.Length == 0)
            return context;

        var active = MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.Concat(context.Active, pulled);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            Active = active,
            RightCarry = carry,
            RightTree = tree,
        };
    }

    private static long EstimateRetainedBufferBytes(
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> seed,
        in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> current)
    {
        var buffers = new HashSet<MeasuredCursorBuffer<T, TMeasure, TMeasureOps>>(
            ReferenceEqualityComparer.Instance);
        long bytes = 0;
        Add(seed.LeftCarry);
        Add(seed.Active);
        Add(seed.RightCarry);
        Add(current.LeftCarry);
        Add(current.Active);
        Add(current.RightCarry);
        return bytes;

        void Add(MeasuredCursorBuffer<T, TMeasure, TMeasureOps>? buffer)
        {
            if (buffer is not null && buffer.Length > 0 && buffers.Add(buffer))
                bytes = checked(bytes + buffer.EstimateRetainedBytes());
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private static TMeasure Combine(TMeasure left, TMeasure right) =>
        MeasuredCursorBuffer<T, TMeasure, TMeasureOps>.InvokeCombine(left, right);

    private static (bool Found, MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Context)
        PreserveSamePosition(
            (bool Found, MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> Context) located,
            in MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> source) =>
        located.Context.Position == source.Position
            ? (located.Found, source)
            : located;
}
