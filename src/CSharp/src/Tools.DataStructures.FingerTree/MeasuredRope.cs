using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// An immutable, persistent chunked sequence — like <see cref="Rope{T}"/> — that additionally tracks an
/// arbitrary monoidal <typeparamref name="TMeasure"/> over its elements, so it supports O(log n) navigation by
/// that measure as well as by position. The classic use is a text buffer with line counts (locate the start of
/// a line, or the line containing an offset, in O(log n)); the same machinery handles weighted selection, byte
/// offsets over variable-width elements, or any monotone measure.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <typeparam name="TMeasure">User measure (annotation) type.</typeparam>
/// <typeparam name="TMeasureOps">The measure algebra assigning a <typeparamref name="TMeasure"/> to each element
/// and combining them monoidally.</typeparam>
/// <remarks>
/// <para>
/// Internally a <c>FingerTree</c> of measured array chunks, measured by the product of element count and the user
/// measure: positional operations split on the count component, measure navigation on the user component, and
/// each chunk caches its combined measure so chunk-level reads are O(1). Complexity matches <see cref="Rope{T}"/>
/// for positional operations (O(1) ends, O(log n) indexed edit/split, O(log(min)) concat), and
/// <see cref="SplitByMeasure"/> / <see cref="TryLocateByMeasure"/> / <see cref="PrefixMeasure"/> are O(log n)
/// (with a bounded within-chunk scan). Instances are immutable and structurally shared.
/// </para>
/// <para>For a purely positional sequence with no user measure, use <see cref="Rope{T}"/>.</para>
/// </remarks>
/// <example>
/// A line-aware text buffer:
/// <code>
/// readonly struct LineMeasure : IMeasure&lt;char, int&gt;
/// {
///     public static int Empty =&gt; 0;
///     public static int Measure(char c) =&gt; c == '\n' ? 1 : 0;
///     public static int Combine(int a, int b) =&gt; a + b;
/// }
///
/// var text = MeasuredRope&lt;char, int, LineMeasure&gt;.Create("line0\nline1\nline2".AsSpan());
/// var lineOfOffset = text.PrefixMeasure(8);             // 1 — offset 8 is on line 1
/// text.TryLocateByMeasure(newlines =&gt; newlines &gt;= 2, out var idx, out _, out _);  // idx = index of the 2nd '\n'
/// </code>
/// </example>
public sealed partial class MeasuredRope<T, TMeasure, TMeasureOps> : IReadOnlyList<T>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private const int MinChunkSize = RopeChunking.MinChunkSize;
    private const int MaxChunkSize = RopeChunking.MaxChunkSize;

    private static readonly MeasuredRope<T, TMeasure, TMeasureOps> EmptyInstance =
        new(FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>>.Empty);

    private readonly FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> _tree;

    private MeasuredRope(FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> tree) =>
        _tree = tree;

    /// <summary>Gets the empty rope.</summary>
    public static MeasuredRope<T, TMeasure, TMeasureOps> Empty => EmptyInstance;

    /// <summary>Creates an empty mutable builder for incrementally appending elements.</summary>
    /// <returns>A mutable builder whose first snapshot is <see cref="Empty"/>.</returns>
    public static Builder CreateBuilder() => new(EmptyInstance);

    /// <summary>Creates a mutable append-only builder initialized with this rope as its frozen prefix. O(1).</summary>
    /// <returns>A builder containing this rope's elements and measure.</returns>
    public Builder ToBuilder() => new(this);

    /// <summary>Gets the number of elements. O(1).</summary>
    public int Count => _tree.Measure.First;

    /// <summary>Gets a value indicating whether the rope has no elements. O(1).</summary>
    public bool IsEmpty => _tree.IsEmpty;

    /// <summary>Gets the combined user measure of all elements. O(1).</summary>
    public TMeasure Measure => _tree.Measure.Second;

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
            _tree.TryLocate(new PairCountAbovePredicate<TMeasure>(index), out var before, out var chunk);
            return chunk[index - before.First];
        }
    }

    /// <summary>Returns a rope with the element at <paramref name="index"/> replaced. O(log n).</summary>
    /// <param name="index">Zero-based element index.</param>
    /// <param name="value">Replacement element.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count - 1</c>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> SetItem(int index, T value)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        _tree.TrySplitFind(new PairCountAbovePredicate<TMeasure>(index), out var left, out var chunk, out var right);
        var offset = index - left.Measure.First;
        return Wrap(left.Append(chunk.SetAt(offset, value)).Concat(right));
    }

    /// <summary>Returns a rope with <paramref name="element"/> prepended. O(log n).</summary>
    /// <param name="element">Element to prepend.</param>
    public MeasuredRope<T, TMeasure, TMeasureOps> AddFirst(T element) => Insert(0, element);

    /// <summary>Returns a rope with <paramref name="element"/> appended. O(log n).</summary>
    /// <param name="element">Element to append.</param>
    public MeasuredRope<T, TMeasure, TMeasureOps> AddLast(T element) => Insert(Count, element);

    /// <summary>Returns a rope with the first element removed. O(log n).</summary>
    /// <exception cref="InvalidOperationException">The rope is empty.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> RemoveFirst() => IsEmpty ? throw EmptyError() : RemoveAt(0);

    /// <summary>Returns a rope with the last element removed. O(log n).</summary>
    /// <exception cref="InvalidOperationException">The rope is empty.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> RemoveLast() => IsEmpty ? throw EmptyError() : RemoveAt(Count - 1);

    /// <summary>Returns a rope with <paramref name="element"/> inserted at <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="element">Element to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> Insert(int index, T element)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (_tree.IsEmpty)
            return Wrap(_tree.Append(new MeasuredChunk<T, TMeasure, TMeasureOps>(new[] { element })));

        if (index == Count)
        {
            _tree.TryViewRight(out var last, out var rest);
            return last.Length < MaxChunkSize
                ? Wrap(rest.Append(last.InsertAt(last.Length, element)))
                : Wrap(_tree.Append(new MeasuredChunk<T, TMeasure, TMeasureOps>(new[] { element })));
        }

        _tree.TrySplitFind(new PairCountAbovePredicate<TMeasure>(index), out var left, out var chunk, out var right);
        var offset = index - left.Measure.First;
        return Wrap(JoinGrown(left, chunk.InsertAt(offset, element), right));
    }

    /// <summary>Returns a rope with <paramref name="elements"/> inserted at <paramref name="index"/>. O(log n + m).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="elements">Elements to insert.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> InsertRange(int index, ReadOnlySpan<T> elements)
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
    public MeasuredRope<T, TMeasure, TMeasureOps> InsertRange(int index, IEnumerable<T> elements)
    {
        ArgumentNullException.ThrowIfNull(elements);
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        var middle = CreateRange(elements);
        return middle.IsEmpty ? this : InsertRange(index, middle);
    }

    /// <summary>Returns a rope with <paramref name="elements"/> spliced in at <paramref name="index"/>. O(log(min)).</summary>
    /// <param name="index">Insertion index in <c>0 .. Count</c>.</param>
    /// <param name="elements">Rope to splice in.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    /// <exception cref="ArgumentNullException"><paramref name="elements"/> is <see langword="null"/>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> InsertRange(int index, MeasuredRope<T, TMeasure, TMeasureOps> elements)
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
    public MeasuredRope<T, TMeasure, TMeasureOps> RemoveAt(int index)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index);
        _tree.TrySplitFind(new PairCountAbovePredicate<TMeasure>(index), out var left, out var chunk, out var right);
        if (chunk.Length == 1)
            return Wrap(left.Concat(right));
        var offset = index - left.Measure.First;
        return Wrap(JoinShrunk(left, chunk.RemoveAt(offset), right));
    }

    /// <summary>Returns a rope with the <paramref name="count"/> elements at <paramref name="index"/> removed. O(log n).</summary>
    /// <param name="index">Start index in <c>0 .. Count</c>.</param>
    /// <param name="count">Number of elements to remove.</param>
    /// <exception cref="ArgumentOutOfRangeException">The range is outside <c>0 .. Count</c>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> RemoveRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return this;
        var (left, rest) = Split(index);
        var (_, right) = rest.Split(count);
        return left.Concat(right);
    }

    /// <summary>Returns the sub-rope <c>[index, index + count)</c>, sharing structure. O(log n).</summary>
    /// <param name="index">Start index in <c>0 .. Count</c>.</param>
    /// <param name="count">Number of elements.</param>
    /// <exception cref="ArgumentOutOfRangeException">The range is outside <c>0 .. Count</c>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> Slice(int index, int count)
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
    public MeasuredRope<T, TMeasure, TMeasureOps> Slice(int index) => Slice(index, Count - index);

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

    /// <summary>Splits the rope into <c>[0, index)</c> and <c>[index, Count)</c> by position. O(log n).</summary>
    /// <param name="index">Split index in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside <c>0 .. Count</c>.</exception>
    public (MeasuredRope<T, TMeasure, TMeasureOps> Left, MeasuredRope<T, TMeasure, TMeasureOps> Right) Split(int index)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index);
        if (index == 0)
            return (EmptyInstance, this);
        if (index == Count)
            return (this, EmptyInstance);

        _tree.TrySplitFind(new PairCountAbovePredicate<TMeasure>(index), out var left, out var chunk, out var right);
        var offset = index - left.Measure.First;
        var leftTree = offset > 0 ? left.Append(chunk.Slice(0, offset)) : left;
        var rightTree = right.Prepend(chunk.Slice(offset, chunk.Length - offset));
        return (Wrap(leftTree), Wrap(rightTree));
    }

    /// <summary>Concatenates this rope with <paramref name="other"/>. O(log(min(n, m))).</summary>
    /// <param name="other">Rope whose elements follow this rope's.</param>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public MeasuredRope<T, TMeasure, TMeasureOps> Concat(MeasuredRope<T, TMeasure, TMeasureOps> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other._tree.IsEmpty)
            return this;
        if (_tree.IsEmpty)
            return other;

        _tree.TryViewRight(out var lastLeft, out var leftRest);
        other._tree.TryViewLeft(out var firstRight, out var rightRest);
        if (lastLeft.Length + firstRight.Length <= MaxChunkSize)
            return Wrap(leftRest.Append(MeasuredChunk<T, TMeasure, TMeasureOps>.Concat(lastLeft, firstRight)).Concat(rightRest));
        return Wrap(_tree.Concat(other._tree));
    }

    /// <summary>
    /// Returns the combined user measure of the first <paramref name="count"/> elements. O(log n). For a line
    /// measure over text this is the line index of the character at offset <paramref name="count"/>.
    /// </summary>
    /// <param name="count">Prefix length in <c>0 .. Count</c>.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is outside <c>0 .. Count</c>.</exception>
    public TMeasure PrefixMeasure(int count)
    {
        if ((uint)count > (uint)Count)
            throw IndexError(count);
        if (count == 0)
            return TMeasureOps.Empty;
        if (count == Count)
            return _tree.Measure.Second;

        _tree.TryLocate(new PairCountAbovePredicate<TMeasure>(count), out var before, out var chunk);
        return TMeasureOps.Combine(before.Second, MeasureOfPrefix(chunk, count - before.First));
    }

    /// <summary>
    /// Splits the rope at the element where <paramref name="predicate"/> over the accumulated user measure first
    /// becomes true: <c>Left</c> is the longest prefix whose accumulated measure never satisfies it. O(log n).
    /// </summary>
    /// <param name="predicate">A monotone predicate over the accumulated user measure.</param>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    public (MeasuredRope<T, TMeasure, TMeasureOps> Left, MeasuredRope<T, TMeasure, TMeasureOps> Right) SplitByMeasure(Func<TMeasure, bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        return SplitByMeasure(new FuncMeasurePredicate<TMeasure>(predicate));
    }

    /// <summary>Value-type-predicate overload of <see cref="SplitByMeasure(Func{TMeasure, bool})"/>: splits with
    /// no delegate allocation, including the within-chunk scan, so a hot split path is fully closure-free. O(log n).</summary>
    public (MeasuredRope<T, TMeasure, TMeasureOps> Left, MeasuredRope<T, TMeasure, TMeasureOps> Right) SplitByMeasure<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        if (!_tree.TrySplitFind(new SecondComponentPredicate<TPredicate, int, TMeasure>(predicate), out var left, out var chunk, out var right))
            return (this, EmptyInstance);

        var offset = ChunkSplitOffset(chunk, left.Measure.Second, predicate);
        var leftTree = offset > 0 ? left.Append(chunk.Slice(0, offset)) : left;
        var rightTree = right.Prepend(chunk.Slice(offset, chunk.Length - offset));
        return (Wrap(leftTree), Wrap(rightTree));
    }

    /// <summary>
    /// Locates the element where <paramref name="predicate"/> over the accumulated user measure first becomes
    /// true, reporting its index and the measure accumulated before it — without reconstructing subtrees. O(log n).
    /// </summary>
    /// <param name="predicate">A monotone predicate over the accumulated user measure.</param>
    /// <param name="index">The zero-based index of the boundary element when found; otherwise <see cref="Count"/>.</param>
    /// <param name="measureBefore">The user measure accumulated strictly before the boundary element; the whole
    /// measure when none is found.</param>
    /// <param name="element">The boundary element when found; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    public bool TryLocateByMeasure(Func<TMeasure, bool> predicate, out int index, out TMeasure measureBefore, [MaybeNullWhen(false)] out T element)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        return TryLocateByMeasure(new FuncMeasurePredicate<TMeasure>(predicate), out index, out measureBefore, out element);
    }

    /// <summary>Value-type-predicate overload of <see cref="TryLocateByMeasure(Func{TMeasure, bool}, out int, out
    /// TMeasure, out T)"/>: locates with no delegate allocation, including the within-chunk scan. O(log n).</summary>
    public bool TryLocateByMeasure<TPredicate>(TPredicate predicate, out int index, out TMeasure measureBefore, [MaybeNullWhen(false)] out T element)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        if (!_tree.TryLocate(new SecondComponentPredicate<TPredicate, int, TMeasure>(predicate), out var before, out var chunk))
        {
            index = Count;
            measureBefore = _tree.Measure.Second;
            element = default;
            return false;
        }

        var offset = ChunkSplitOffset(chunk, before.Second, predicate);
        index = before.First + offset;
        measureBefore = TMeasureOps.Combine(before.Second, MeasureOfPrefix(chunk, offset));
        element = chunk[offset];
        return true;
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
            chunk.Chunk.Data.Span.CopyTo(destination.Slice(position, chunk.Length));
            position += chunk.Length;
        }
    }

    /// <summary>Returns a rope with the same elements but freshly allocated chunks, releasing shared backings. O(n).</summary>
    public MeasuredRope<T, TMeasure, TMeasureOps> Compact() => IsEmpty ? EmptyInstance : Create(ToArray());

    /// <summary>Creates a rope from the given elements. O(n).</summary>
    /// <param name="elements">Elements to store, in order.</param>
    public static MeasuredRope<T, TMeasure, TMeasureOps> Create(params ReadOnlySpan<T> elements)
    {
        if (elements.IsEmpty)
            return EmptyInstance;
        var builder = new MeasuredChunkBuilder<T, TMeasure, TMeasureOps>();
        builder.Add(elements);
        return FromFrozenChunks(builder.FreezeChunks());
    }

    /// <summary>Creates a rope from an enumerable source, enumerated once. O(n).</summary>
    /// <param name="elements">Elements to store, in order.</param>
    /// <exception cref="ArgumentNullException"><paramref name="elements"/> is <see langword="null"/>.</exception>
    public static MeasuredRope<T, TMeasure, TMeasureOps> CreateRange(IEnumerable<T> elements)
    {
        ArgumentNullException.ThrowIfNull(elements);
        if (elements is MeasuredRope<T, TMeasure, TMeasureOps> rope)
            return rope;

        var builder = new MeasuredChunkBuilder<T, TMeasure, TMeasureOps>();
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

    /// <summary>Returns an enumerator over the elements in order.</summary>
    public IEnumerator<T> GetEnumerator()
    {
        foreach (var chunk in _tree)
        {
            var data = chunk.Chunk.Data;
            for (var i = 0; i < data.Length; i++)
                yield return data.Span[i];
        }
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>
    /// Verifies the structural invariants (test-only): chunks are non-empty and no larger than the maximum size,
    /// the cached count and user measures equal their recomputed values, and the tree measure is consistent.
    /// </summary>
    /// <exception cref="InvalidOperationException">An invariant is violated.</exception>
    internal void ValidateInvariants()
    {
        var totalCount = 0;
        var totalMeasure = TMeasureOps.Empty;
        foreach (var chunk in _tree)
        {
            if (chunk.Length <= 0)
                throw new InvalidOperationException("MeasuredRope invariant violated: an empty chunk is present.");
            if (chunk.Length > MaxChunkSize)
                throw new InvalidOperationException($"MeasuredRope invariant violated: a chunk of length {chunk.Length} exceeds the maximum {MaxChunkSize}.");

            var recomputed = TMeasureOps.Empty;
            var span = chunk.Chunk.Data.Span;
            for (var i = 0; i < span.Length; i++)
                recomputed = TMeasureOps.Combine(recomputed, TMeasureOps.Measure(span[i]));
            if (!EqualityComparer<TMeasure>.Default.Equals(recomputed, chunk.Measure))
                throw new InvalidOperationException("MeasuredRope invariant violated: a chunk's cached measure is stale.");

            totalCount += chunk.Length;
            totalMeasure = TMeasureOps.Combine(totalMeasure, chunk.Measure);
        }

        if (totalCount != Count)
            throw new InvalidOperationException($"MeasuredRope invariant violated: summed chunk length {totalCount} does not equal Count {Count}.");
        if (!EqualityComparer<TMeasure>.Default.Equals(totalMeasure, Measure))
            throw new InvalidOperationException("MeasuredRope invariant violated: the tree's cached measure is stale.");
    }

    private static MeasuredRope<T, TMeasure, TMeasureOps> Wrap(
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> tree) =>
        tree.IsEmpty ? EmptyInstance : new MeasuredRope<T, TMeasure, TMeasureOps>(tree);

    internal static MeasuredRope<T, TMeasure, TMeasureOps> FromFrozenChunks(Chunk<T>[] chunks)
    {
        if (chunks.Length == 0)
            return EmptyInstance;

        var measured = new MeasuredChunk<T, TMeasure, TMeasureOps>[chunks.Length];
        for (var i = 0; i < chunks.Length; i++)
            measured[i] = new MeasuredChunk<T, TMeasure, TMeasureOps>(chunks[i]);
        return FromFrozenChunks(measured);
    }

    internal static MeasuredRope<T, TMeasure, TMeasureOps> FromFrozenChunks(MeasuredChunk<T, TMeasure, TMeasureOps>[] chunks)
    {
        var tree = FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>>.Empty;
        foreach (var chunk in chunks)
        {
            if (chunk.Length == 0)
                continue;
            if (chunk.Length > MaxChunkSize)
                throw new InvalidOperationException($"MeasuredRope invariant violated: a frozen chunk of length {chunk.Length} exceeds the maximum {MaxChunkSize}.");
            tree = tree.Append(chunk);
        }

        return Wrap(tree);
    }

    // Scans a chunk for the first offset at which the accumulated measure (starting from accBefore) satisfies the
    // predicate; that element is the split boundary. The caller guarantees some element in the chunk flips it.
    private static int ChunkSplitOffset<TPredicate>(MeasuredChunk<T, TMeasure, TMeasureOps> chunk, TMeasure accBefore, TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var acc = accBefore;
        var span = chunk.Chunk.Data.Span;
        for (var offset = 0; offset < span.Length; offset++)
        {
            acc = TMeasureOps.Combine(acc, TMeasureOps.Measure(span[offset]));
            if (predicate.Invoke(acc))
                return offset;
        }

        return span.Length - 1;   // unreachable when the predicate flips inside the chunk
    }

    private static TMeasure MeasureOfPrefix(MeasuredChunk<T, TMeasure, TMeasureOps> chunk, int length)
    {
        var measure = TMeasureOps.Empty;
        var span = chunk.Chunk.Data.Span;
        for (var i = 0; i < length; i++)
            measure = TMeasureOps.Combine(measure, TMeasureOps.Measure(span[i]));
        return measure;
    }

    private static FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> JoinGrown(
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> left,
        MeasuredChunk<T, TMeasure, TMeasureOps> grown,
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> right)
    {
        if (grown.Length <= MaxChunkSize)
            return left.Append(grown).Concat(right);

        var half = grown.Length / 2;
        return left.Append(grown.Slice(0, half)).Append(grown.Slice(half, grown.Length - half)).Concat(right);
    }

    private static FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> JoinShrunk(
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> left,
        MeasuredChunk<T, TMeasure, TMeasureOps> shrunk,
        FingerTree<MeasuredChunk<T, TMeasure, TMeasureOps>, MeasurePair<int, TMeasure>, MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> right)
    {
        if (shrunk.Length >= MinChunkSize)
            return left.Append(shrunk).Concat(right);

        if (left.TryViewRight(out var lastLeft, out var leftRest) && lastLeft.Length + shrunk.Length <= MaxChunkSize)
            return leftRest.Append(MeasuredChunk<T, TMeasure, TMeasureOps>.Concat(lastLeft, shrunk)).Concat(right);

        if (right.TryViewLeft(out var firstRight, out var rightRest) && shrunk.Length + firstRight.Length <= MaxChunkSize)
            return left.Append(MeasuredChunk<T, TMeasure, TMeasureOps>.Concat(shrunk, firstRight)).Concat(rightRest);

        return left.Append(shrunk).Concat(right);
    }

    private static InvalidOperationException EmptyError() => new("The rope is empty.");

    private static ArgumentOutOfRangeException IndexError(int index) =>
        new(nameof(index), index, "Index is outside the rope's range.");

    private void CheckRange(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if ((uint)index > (uint)Count || count > Count - index)
            throw RangeError(index, count);
    }

    private static ArgumentOutOfRangeException RangeError(int index, int count) =>
        new(nameof(index), (index, count), "The requested range is outside the rope's bounds.");
}
