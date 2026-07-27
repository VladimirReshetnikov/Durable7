// An immutable, persistent, general-purpose <em>chunked</em> sequence — a rope.

using System.Collections;

namespace Durable7.FingerTree;

/// <summary>
/// An immutable, persistent, general-purpose <em>chunked</em> sequence — a rope. Elements are held in bounded
/// array chunks at the leaves of a measured finger tree (measured by total element count), giving cache-friendly
/// storage and edits that touch only O(log n) of the structure instead of copying the whole sequence. It is
/// element-agnostic: <c>Rope&lt;char&gt;</c> is a text buffer, <c>Rope&lt;byte&gt;</c> a binary buffer,
/// <c>Rope&lt;T&gt;</c> a big editable list.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <remarks>
/// <para>
/// Complexity (n = element count): <see cref="Count"/>/<see cref="IsEmpty"/>/<see cref="First"/>/<see cref="Last"/>
/// are O(1); <see cref="this[int]"/>, <see cref="SetItem"/>, <see cref="Insert"/>, <see cref="RemoveAt"/>,
/// <see cref="Split"/>, and <see cref="Slice(int, int)"/> are O(log n) (plus an O(chunk-size) array copy at the
/// edited chunk, a bounded constant); <see cref="Concat"/> is O(log(min(n, m))). Bulk construction
/// (<see cref="Create"/>/<see cref="CreateRange"/>/<see cref="FromChunks"/>) and range insertion are O(n)/O(m).
/// </para>
/// <para>
/// Instances are immutable and structurally shared, so every edit returns a new rope cheaply and old versions
/// remain valid — ideal for snapshots and undo. Building a rope one element at a time with
/// <see cref="AddFirst"/>/<see cref="AddLast"/> copies the boundary chunk each call (an O(chunk-size) constant);
/// prefer <see cref="Create"/>/<see cref="CreateRange"/>/<see cref="InsertRange(int, ReadOnlySpan{T})"/> to build
/// or splice in bulk.
/// </para>
/// <para>
/// This positional rope is general over the element type; for an arbitrary monoidal measure (e.g. line counts
/// for text, weighted selection), the underlying <see cref="FingerTree{TElement, TMeasure, TMeasureOps}"/> with a
/// product measure provides it directly.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// var rope = Rope&lt;char&gt;.Create("hello world");
/// var edited = rope.RemoveRange(5, 6).InsertRange(5, ", there!");   // "hello, there!"
/// var (head, tail) = edited.Split(5);                               // "hello" | ", there!"
/// </code>
/// </example>
public sealed partial class Rope<T> : IReadOnlyList<T>
{
    // Chunk-size policy. Chunks never exceed MaxChunkSize (so within-chunk work is bounded); on shrink/concat a
    // chunk below MinChunkSize is coalesced with a neighbour when they fit, keeping the chunk count ~ n/Target
    // and the tree depth O(log n). Bulk construction targets MaxChunkSize chunks.
    private const int MinChunkSize = RopeChunking.MinChunkSize;
    private const int MaxChunkSize = RopeChunking.MaxChunkSize;

    private static readonly Rope<T> EmptyInstance = new(FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>.Empty);

    private readonly FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> _tree;

    private Rope(FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> tree) => _tree = tree;

    /// <summary>Gets the empty rope.</summary>
    public static Rope<T> Empty => EmptyInstance;

    /// <summary>Creates an empty mutable builder for incrementally appending elements.</summary>
    /// <returns>A mutable builder whose first snapshot is <see cref="Empty"/>.</returns>
    public static Builder CreateBuilder() => new(EmptyInstance);

    /// <summary>Creates a mutable append-only builder initialized with this rope as its frozen prefix. O(1).</summary>
    /// <returns>A builder containing this rope's elements.</returns>
    public Builder ToBuilder() => new(this);

    /// <summary>Gets the number of elements. O(1) amortized; the first read of a fresh spine may force memoized deferred work.</summary>
    public int Count => _tree.Measure;

    /// <summary>Gets a value indicating whether the rope has no elements. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the first element. O(1).</summary>
    /// <exception cref="InvalidOperationException">The rope is empty.</exception>
    public T First => _tree.TryViewLeft(out var chunk, out _) ? chunk[0] : throw EmptyError();

    /// <summary>Gets the last element. O(1).</summary>
    /// <exception cref="InvalidOperationException">The rope is empty.</exception>
    public T Last => _tree.TryViewRight(out var chunk, out _) ? chunk[chunk.Length - 1] : throw EmptyError();

    /// <summary>Gets the element at <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count - 1</c>.</exception>
    public T this[int index]
    {
        get
        {
            if ((uint)index >= (uint)Count)
                throw IndexError(index);
            _tree.TryLocate(new ElementIndexPredicate(index), out var before, out var chunk);
            return chunk[index - before];
        }
    }

    /// <summary>Returns a rope with the element at <paramref name="index"/> replaced by <paramref name="value"/>. O(log n).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="value">Replacement element.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count - 1</c>.</exception>
    public Rope<T> SetItem(int index, T value)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        _tree.TrySplitFind(new ElementIndexPredicate(index), out var left, out var chunk, out var right);
        var offset = index - left.Measure;
        return Wrap(left.Append(chunk.SetAt(offset, value)).Concat(right));
    }

    /// <summary>Returns a rope with <paramref name="element"/> prepended. O(log n), with a bounded chunk copy.</summary>
    /// <param name="element">Element to prepend.</param>
    public Rope<T> AddFirst(T element) => Insert(0, element);

    /// <summary>Returns a rope with <paramref name="element"/> appended. O(log n), with a bounded chunk copy.</summary>
    /// <param name="element">Element to append.</param>
    public Rope<T> AddLast(T element) => Insert(Count, element);

    /// <summary>Returns a rope with the first element removed. O(log n).</summary>
    /// <exception cref="InvalidOperationException">The rope is empty.</exception>
    public Rope<T> RemoveFirst() => IsEmpty ? throw EmptyError() : RemoveAt(0);

    /// <summary>Returns a rope with the last element removed. O(log n).</summary>
    /// <exception cref="InvalidOperationException">The rope is empty.</exception>
    public Rope<T> RemoveLast() => IsEmpty ? throw EmptyError() : RemoveAt(Count - 1);

    /// <summary>Returns a rope with <paramref name="element"/> inserted at <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="element">Element to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    public Rope<T> Insert(int index, T element)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);

        if (_tree.IsEmpty)
            return Wrap(_tree.Append(new Chunk<T>(new[] { element })));

        if (index == Count)
        {
            _tree.TryViewRight(out var last, out var rest);
            return last.Length < MaxChunkSize
                ? Wrap(rest.Append(last.InsertAt(last.Length, element)))
                : Wrap(_tree.Append(new Chunk<T>(new[] { element })));
        }

        _tree.TrySplitFind(new ElementIndexPredicate(index), out var left, out var chunk, out var right);
        var offset = index - left.Measure;
        return Wrap(JoinGrown(left, chunk.InsertAt(offset, element), right));
    }

    /// <summary>Returns a rope with <paramref name="elements"/> inserted at <paramref name="index"/>. O(log n + m).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="elements">Elements to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    public Rope<T> InsertRange(int index, ReadOnlySpan<T> elements)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (elements.IsEmpty)
            return this;
        var (left, right) = Split(index);
        return left.Concat(Create(elements)).Concat(right);
    }

    /// <summary>Returns a rope with <paramref name="elements"/> inserted at <paramref name="index"/>. O(m + log(n + m)).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="elements">Elements to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    /// <exception cref="ArgumentNullException"><paramref name="elements"/> is <see langword="null"/>.</exception>
    public Rope<T> InsertRange(int index, IEnumerable<T> elements)
    {
        ArgumentNullException.ThrowIfNull(elements);
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        var middle = CreateRange(elements);
        return middle.IsEmpty ? this : InsertRange(index, middle);
    }

    /// <summary>Returns a rope with <paramref name="elements"/> spliced in at <paramref name="index"/>. O(log(min) ).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="elements">Rope to splice in.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    /// <exception cref="ArgumentNullException"><paramref name="elements"/> is <see langword="null"/>.</exception>
    public Rope<T> InsertRange(int index, Rope<T> elements)
    {
        ArgumentNullException.ThrowIfNull(elements);
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (elements.IsEmpty)
            return this;
        var (left, right) = Split(index);
        return left.Concat(elements).Concat(right);
    }

    /// <summary>Returns a rope with the element at <paramref name="index"/> removed. O(log n).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count - 1</c>.</exception>
    public Rope<T> RemoveAt(int index)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        _tree.TrySplitFind(new ElementIndexPredicate(index), out var left, out var chunk, out var right);
        if (chunk.Length == 1)
            return Wrap(left.Concat(right));
        var offset = index - left.Measure;
        return Wrap(JoinShrunk(left, chunk.RemoveAt(offset), right));
    }

    /// <summary>Returns a rope with the <paramref name="count"/> elements at <paramref name="index"/> removed. O(log n).</summary>
    /// <param name="index">Start index in <c>0 .. Count</c>.</param>
    /// <param name="count">Number of elements to remove.</param>
    /// <exception cref="ArgumentOutOfRangeException">The range is outside <c>0 .. Count</c>.</exception>
    public Rope<T> RemoveRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return this;
        var (left, rest) = Split(index);
        var (_, right) = rest.Split(count);
        return left.Concat(right);
    }

    /// <summary>Returns the sub-rope <c>[index, index + count)</c>, sharing structure with this rope. O(log n).</summary>
    /// <param name="index">Start index in <c>0 .. Count</c>.</param>
    /// <param name="count">Number of elements.</param>
    /// <exception cref="ArgumentOutOfRangeException">The range is outside <c>0 .. Count</c>.</exception>
    public Rope<T> Slice(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return EmptyInstance;
        if (index == 0 && count == Count)
            return this;
        var (_, rest) = Split(index);
        var (middle, _) = rest.Split(count);
        return middle;
    }

    /// <summary>Returns the sub-rope from <paramref name="index"/> to the end. O(log n).</summary>
    /// <param name="index">Start index in <c>0 .. Count</c>.</param>
    public Rope<T> Slice(int index) => Slice(index, Count - index);

    /// <summary>Copies the <c>[index, index + count)</c> elements to a new array. O(count).</summary>
    /// <param name="index">Start index.</param>
    /// <param name="count">Number of elements.</param>
    /// <exception cref="ArgumentOutOfRangeException">The range is outside <c>0 .. Count</c>.</exception>
    public T[] GetRange(int index, int count)
    {
        CheckRange(index, count);
        var array = new T[count];
        CopyTo(index, array);
        return array;
    }

    /// <summary>Splits the rope into <c>[0, index)</c> and <c>[index, Count)</c>. O(log n).</summary>
    /// <param name="index">Split index in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    public (Rope<T> Left, Rope<T> Right) Split(int index)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (index == 0)
            return (EmptyInstance, this);
        if (index == Count)
            return (this, EmptyInstance);

        _tree.TrySplitFind(new ElementIndexPredicate(index), out var left, out var chunk, out var right);
        var offset = index - left.Measure;
        var leftTree = offset > 0 ? left.Append(chunk.Slice(0, offset)) : left;
        var rightTree = right.Prepend(chunk.Slice(offset, chunk.Length - offset));
        return (Wrap(leftTree), Wrap(rightTree));
    }

    /// <summary>Concatenates this rope with <paramref name="other"/>. O(log(min(n, m))).</summary>
    /// <param name="other">Rope whose elements follow this rope's.</param>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined count would exceed <see cref="int.MaxValue"/>.</exception>
    public Rope<T> Concat(Rope<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other._tree.IsEmpty)
            return this;
        if (_tree.IsEmpty)
            return other;
        if (other.Count > int.MaxValue - Count)
            throw new OverflowException("The operation would create a rope with more than Int32.MaxValue elements.");

        // Coalesce the boundary chunks when they fit, so repeated concatenation does not leave a seam of small
        // chunks between the operands.
        _tree.TryViewRight(out var lastLeft, out var leftRest);
        other._tree.TryViewLeft(out var firstRight, out var rightRest);
        if (lastLeft.Length + firstRight.Length <= MaxChunkSize)
            return Wrap(leftRest.Append(Chunk<T>.Concat(lastLeft, firstRight)).Concat(rightRest));
        return Wrap(_tree.Concat(other._tree));
    }

    /// <summary>Returns a copy of all elements as an array, in order. O(n).</summary>
    public T[] ToArray()
    {
        var array = new T[Count];
        CopyTo(0, array);
        return array;
    }

    /// <summary>Copies elements starting at <paramref name="index"/> into <paramref name="destination"/>. O(destination.Length).</summary>
    /// <param name="index">Start index.</param>
    /// <param name="destination">Span to fill; its length is the number of elements copied.</param>
    /// <exception cref="ArgumentOutOfRangeException">The requested range is outside <c>0 .. Count</c>.</exception>
    public void CopyTo(int index, Span<T> destination)
    {
        CheckRange(index, destination.Length);
        if (destination.IsEmpty)
            return;
        var source = Slice(index, destination.Length);
        var position = 0;
        foreach (var chunk in source._tree)
        {
            chunk.Data.Span.CopyTo(destination.Slice(position, chunk.Length));
            position += chunk.Length;
        }
    }

    /// <summary>
    /// Returns a rope with the same elements but freshly allocated chunks, releasing backing arrays still
    /// shared from earlier slices. O(n). Use after slice-heavy workloads that retain large backing buffers.
    /// </summary>
    public Rope<T> Compact() => IsEmpty ? EmptyInstance : Create(ToArray());

    /// <summary>Creates a rope from the given elements. O(n).</summary>
    /// <param name="elements">Elements to store, in order.</param>
    public static Rope<T> Create(params ReadOnlySpan<T> elements)
    {
        if (elements.IsEmpty)
            return EmptyInstance;
        var builder = new ChunkBuilder<T>();
        builder.Add(elements);
        return FromFrozenChunks(builder.FreezeChunks());
    }

    /// <summary>Creates a rope from an enumerable source, enumerated once. O(n).</summary>
    /// <param name="elements">Elements to store, in order.</param>
    /// <exception cref="ArgumentNullException"><paramref name="elements"/> is <see langword="null"/>.</exception>
    public static Rope<T> CreateRange(IEnumerable<T> elements)
    {
        ArgumentNullException.ThrowIfNull(elements);
        if (elements is Rope<T> rope)
            return rope;

        var builder = new ChunkBuilder<T>();
        if (elements is T[] array)
        {
            builder.Add(array);
        }
        else
        {
            foreach (var element in elements)
                builder.Add(element);
        }

        return FromFrozenChunks(builder.FreezeChunks());
    }

    /// <summary>Creates a rope from existing memory blocks, copying them into rope-owned chunks. O(total length).</summary>
    /// <param name="chunks">Memory blocks to import in order; empty blocks are skipped.</param>
    /// <remarks>
    /// Each block is copied when imported, so later mutation of an array that backed a <see cref="ReadOnlyMemory{T}"/>
    /// argument cannot change the immutable rope snapshot.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="chunks"/> is <see langword="null"/>.</exception>
    public static Rope<T> FromChunks(params ReadOnlyMemory<T>[] chunks)
    {
        ArgumentNullException.ThrowIfNull(chunks);
        var builder = new ChunkBuilder<T>();
        foreach (var chunk in chunks)
        {
            if (chunk.Length == 0)
                continue;
            builder.Add(chunk.Span);
        }

        return FromFrozenChunks(builder.FreezeChunks());
    }

    /// <summary>Returns an enumerator over the elements in order.</summary>
    /// <returns>An enumerator yielding elements left to right, chunk by chunk.</returns>
    public IEnumerator<T> GetEnumerator()
    {
        foreach (var chunk in _tree)
        {
            var data = chunk.Data;
            for (var i = 0; i < data.Length; i++)
                yield return data.Span[i];
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>
    /// Verifies the structural invariants (test-only): every chunk is non-empty and no larger than the maximum
    /// chunk size, and the tree's cached measure equals the summed chunk lengths.
    /// </summary>
    /// <exception cref="InvalidOperationException">An invariant is violated.</exception>
    internal void ValidateInvariants()
    {
        var total = 0;
        foreach (var chunk in _tree)
        {
            if (chunk.Length <= 0)
                throw new InvalidOperationException("Rope invariant violated: an empty chunk is present.");
            if (chunk.Length > MaxChunkSize)
                throw new InvalidOperationException($"Rope invariant violated: a chunk of length {chunk.Length} exceeds the maximum {MaxChunkSize}.");
            total += chunk.Length;
        }

        if (total != Count)
            throw new InvalidOperationException($"Rope invariant violated: summed chunk length {total} does not equal Count {Count}.");
    }

    private static Rope<T> Wrap(FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> tree) =>
        tree.IsEmpty ? EmptyInstance : new Rope<T>(tree);

    /// <summary>Builds a rope from already-frozen chunks, without re-chunking them.</summary>
    internal static Rope<T> FromFrozenChunks(Chunk<T>[] chunks)
    {
        var tree = FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>.Empty;
        foreach (var chunk in chunks)
        {
            if (chunk.Length == 0)
                continue;
            if (chunk.Length > MaxChunkSize)
                throw new InvalidOperationException($"Rope invariant violated: a frozen chunk of length {chunk.Length} exceeds the maximum {MaxChunkSize}.");
            tree = tree.Append(chunk);
        }

        return Wrap(tree);
    }

    /// <summary>Reattaches a chunk that grew, splitting it in two when it exceeds the maximum size.</summary>
    private static FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> JoinGrown(
        FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> left,
        Chunk<T> grown,
        FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> right)
    {
        if (grown.Length <= MaxChunkSize)
            return left.Append(grown).Concat(right);

        var half = grown.Length / 2;
        return left.Append(grown.Slice(0, half)).Append(grown.Slice(half, grown.Length - half)).Concat(right);
    }

    /// <summary>Reattaches a chunk that shrank, coalescing it with a neighbour when it falls below the minimum size.</summary>
    private static FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> JoinShrunk(
        FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> left,
        Chunk<T> shrunk,
        FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> right)
    {
        if (shrunk.Length >= MinChunkSize)
            return left.Append(shrunk).Concat(right);

        if (left.TryViewRight(out var lastLeft, out var leftRest) && lastLeft.Length + shrunk.Length <= MaxChunkSize)
            return leftRest.Append(Chunk<T>.Concat(lastLeft, shrunk)).Concat(right);

        if (right.TryViewLeft(out var firstRight, out var rightRest) && shrunk.Length + firstRight.Length <= MaxChunkSize)
            return left.Append(Chunk<T>.Concat(shrunk, firstRight)).Concat(rightRest);

        return left.Append(shrunk).Concat(right);
    }

    private static InvalidOperationException EmptyError() => new("The rope is empty.");

    private static ArgumentOutOfRangeException IndexError(int index) =>
        new(nameof(index), index, "Index is outside the rope's range.");

    private void CheckRange(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (count > Count - index)
            throw new ArgumentOutOfRangeException(nameof(count), count, "The range extends past the end of the rope.");
    }
}
