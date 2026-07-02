namespace Tools.DataStructures.FingerTree;

/// <summary>
/// A <see cref="Chunk{T}"/> that additionally caches the combined user measure of its elements, so a
/// <see cref="MeasuredRope{T, TMeasure, TMeasureOps}"/> can read a chunk's measure in O(1). The cached measure is
/// the monoidal combination (via <typeparamref name="TMeasureOps"/>) of every element's measure, computed once
/// at construction.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <typeparam name="TMeasure">User measure type.</typeparam>
/// <typeparam name="TMeasureOps">The measure algebra for <typeparamref name="T"/>.</typeparam>
internal readonly struct MeasuredChunk<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    /// <summary>The underlying immutable element segment.</summary>
    public readonly Chunk<T> Chunk;

    /// <summary>The cached combined user measure of the chunk's elements.</summary>
    public readonly TMeasure Measure;

    /// <summary>Wraps a chunk, computing its combined measure. O(chunk length).</summary>
    /// <param name="chunk">The element segment.</param>
    public MeasuredChunk(Chunk<T> chunk)
    {
        Chunk = chunk;
        Measure = ComputeMeasure(chunk);
    }

    /// <summary>Wraps memory as a measured chunk. O(length).</summary>
    /// <param name="data">The backing memory.</param>
    public MeasuredChunk(ReadOnlyMemory<T> data)
        : this(new Chunk<T>(data))
    {
    }

    private MeasuredChunk(Chunk<T> chunk, TMeasure measure)
    {
        Chunk = chunk;
        Measure = measure;
    }

    /// <summary>The element count.</summary>
    public int Length => Chunk.Length;

    /// <summary>The element at <paramref name="offset"/>. O(1).</summary>
    /// <param name="offset">A zero-based offset.</param>
    public T this[int offset] => Chunk[offset];

    /// <summary>Returns the sub-chunk <c>[offset, offset + length)</c>, recomputing its measure. O(length).</summary>
    /// <param name="offset">Start offset.</param>
    /// <param name="length">Number of elements.</param>
    public MeasuredChunk<T, TMeasure, TMeasureOps> Slice(int offset, int length) => new(Chunk.Slice(offset, length));

    /// <summary>Returns a copy with the element at <paramref name="offset"/> replaced. O(Length).</summary>
    /// <param name="offset">Offset to overwrite.</param>
    /// <param name="value">Replacement element.</param>
    public MeasuredChunk<T, TMeasure, TMeasureOps> SetAt(int offset, T value) => new(Chunk.SetAt(offset, value));

    /// <summary>Returns a copy with <paramref name="value"/> inserted at <paramref name="offset"/>. O(Length).</summary>
    /// <param name="offset">Insertion offset.</param>
    /// <param name="value">Element to insert.</param>
    public MeasuredChunk<T, TMeasure, TMeasureOps> InsertAt(int offset, T value) => new(Chunk.InsertAt(offset, value));

    /// <summary>Returns a copy with the element at <paramref name="offset"/> removed. O(Length).</summary>
    /// <param name="offset">Offset to remove.</param>
    public MeasuredChunk<T, TMeasure, TMeasureOps> RemoveAt(int offset) => new(Chunk.RemoveAt(offset));

    /// <summary>Concatenates two measured chunks; the merged measure is the monoidal combine (no rescan). O(left + right) to copy.</summary>
    /// <param name="left">First chunk.</param>
    /// <param name="right">Second chunk.</param>
    public static MeasuredChunk<T, TMeasure, TMeasureOps> Concat(MeasuredChunk<T, TMeasure, TMeasureOps> left, MeasuredChunk<T, TMeasure, TMeasureOps> right) =>
        new(Chunk<T>.Concat(left.Chunk, right.Chunk), TMeasureOps.Combine(left.Measure, right.Measure));

    private static TMeasure ComputeMeasure(Chunk<T> chunk)
    {
        var measure = TMeasureOps.Empty;
        var span = chunk.Data.Span;
        for (var i = 0; i < span.Length; i++)
            measure = TMeasureOps.Combine(measure, TMeasureOps.Measure(span[i]));
        return measure;
    }
}

/// <summary>
/// Measures a <see cref="MeasuredChunk{T, TMeasure, TMeasureOps}"/> by the product of its element count and its
/// cached user measure, so a finger tree of measured chunks tracks both total length (for positional addressing)
/// and the total user measure (for measure-based navigation) at once.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <typeparam name="TMeasure">User measure type.</typeparam>
/// <typeparam name="TMeasureOps">The measure algebra for <typeparamref name="T"/>.</typeparam>
internal readonly struct MeasuredChunkMeasure<T, TMeasure, TMeasureOps>
    : IMeasure<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    /// <inheritdoc/>
    public static MeasurePair<int, TMeasure> Empty => new(0, TMeasureOps.Empty);

    /// <inheritdoc/>
    public static MeasurePair<int, TMeasure> Measure(MeasuredChunk<T, TMeasure, TMeasureOps> chunk) =>
        new(chunk.Length, chunk.Measure);

    /// <inheritdoc/>
    public static MeasurePair<int, TMeasure> Combine(MeasurePair<int, TMeasure> left, MeasurePair<int, TMeasure> right) =>
        new(left.First + right.First, TMeasureOps.Combine(left.Second, right.Second));
}

/// <summary>
/// Locates the chunk containing a target element index over a measured rope's product measure: the first prefix
/// whose cumulative element count (the first component) exceeds the index. Non-capturing, for zero-allocation
/// positional reads and splits.
/// </summary>
/// <typeparam name="TMeasure">The user measure type carried alongside the count.</typeparam>
/// <param name="index">The zero-based element index to locate.</param>
internal readonly struct PairCountAbovePredicate<TMeasure>(int index) : IMeasurePredicate<MeasurePair<int, TMeasure>>
{
    /// <inheritdoc/>
    public bool Invoke(MeasurePair<int, TMeasure> measure) => measure.First > index;
}
