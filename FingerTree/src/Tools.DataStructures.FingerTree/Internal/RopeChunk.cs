namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An immutable segment of <typeparamref name="T"/> elements — the leaf payload of a <see cref="Rope{T}"/>.
/// Wraps a <see cref="ReadOnlyMemory{T}"/> so slicing is O(1) (no copy), and caches the element count so the
/// length measure is read without touching the span.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <remarks>
/// The backing memory is never mutated after construction; the array-producing helpers (<see cref="SetAt"/>,
/// <see cref="InsertAt"/>, <see cref="RemoveAt"/>, <see cref="Concat"/>) each allocate a fresh array, keeping
/// every chunk safe to share across persistent versions. <see cref="Slice"/> shares the backing array.
/// </remarks>
internal readonly struct Chunk<T>
{
    /// <summary>The immutable backing memory for this chunk.</summary>
    public readonly ReadOnlyMemory<T> Data;

    /// <summary>The element count, cached from <see cref="Data"/>'s length.</summary>
    public readonly int Length;

    /// <summary>Wraps a block of memory as a chunk.</summary>
    /// <param name="data">The backing memory; treated as immutable.</param>
    public Chunk(ReadOnlyMemory<T> data)
    {
        Data = data;
        Length = data.Length;
    }

    /// <summary>Gets the element at <paramref name="offset"/> within the chunk. O(1).</summary>
    /// <param name="offset">A zero-based offset in <c>0 .. Length - 1</c>.</param>
    public T this[int offset] => Data.Span[offset];

    /// <summary>Returns the sub-chunk <c>[offset, offset + length)</c>, sharing the backing array. O(1).</summary>
    /// <param name="offset">Start offset.</param>
    /// <param name="length">Number of elements.</param>
    public Chunk<T> Slice(int offset, int length) => new(Data.Slice(offset, length));

    /// <summary>Returns a copy with the element at <paramref name="offset"/> replaced. O(Length).</summary>
    /// <param name="offset">Offset to overwrite.</param>
    /// <param name="value">Replacement element.</param>
    public Chunk<T> SetAt(int offset, T value)
    {
        var array = Data.ToArray();
        array[offset] = value;
        return new Chunk<T>(array);
    }

    /// <summary>Returns a copy with <paramref name="value"/> inserted at <paramref name="offset"/>. O(Length).</summary>
    /// <param name="offset">Insertion offset in <c>0 .. Length</c>.</param>
    /// <param name="value">Element to insert.</param>
    public Chunk<T> InsertAt(int offset, T value)
    {
        var array = new T[Length + 1];
        Data.Span[..offset].CopyTo(array);
        array[offset] = value;
        Data.Span[offset..].CopyTo(array.AsSpan(offset + 1));
        return new Chunk<T>(array);
    }

    /// <summary>Returns a copy with the element at <paramref name="offset"/> removed. O(Length).</summary>
    /// <param name="offset">Offset to remove.</param>
    public Chunk<T> RemoveAt(int offset)
    {
        var array = new T[Length - 1];
        Data.Span[..offset].CopyTo(array);
        Data.Span[(offset + 1)..].CopyTo(array.AsSpan(offset));
        return new Chunk<T>(array);
    }

    /// <summary>Concatenates two chunks into a fresh chunk. O(left.Length + right.Length).</summary>
    /// <param name="left">First chunk.</param>
    /// <param name="right">Second chunk.</param>
    public static Chunk<T> Concat(Chunk<T> left, Chunk<T> right)
    {
        var array = new T[left.Length + right.Length];
        left.Data.Span.CopyTo(array);
        right.Data.Span.CopyTo(array.AsSpan(left.Length));
        return new Chunk<T>(array);
    }
}

/// <summary>
/// Measures a <see cref="Chunk{T}"/> by its element count, so a finger tree of chunks is measured by its total
/// element count and addressing by element position is a monotone split over the running total.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
internal readonly struct ChunkLengthMeasure<T> : IMeasure<Chunk<T>, int>
{
    /// <inheritdoc/>
    public static int Empty => 0;

    /// <inheritdoc/>
    public static int Measure(Chunk<T> chunk) => chunk.Length;

    /// <inheritdoc/>
    public static int Combine(int left, int right) => left + right;
}

/// <summary>
/// Locates the chunk containing a target element index: the first prefix whose cumulative element count exceeds
/// it. A non-capturing predicate so the rope's positional reads and splits allocate no closure.
/// </summary>
/// <param name="index">The zero-based element index to locate.</param>
internal readonly struct ElementIndexPredicate(int index) : IMeasurePredicate<int>
{
    /// <inheritdoc/>
    public bool Invoke(int cumulativeCount) => cumulativeCount > index;
}
