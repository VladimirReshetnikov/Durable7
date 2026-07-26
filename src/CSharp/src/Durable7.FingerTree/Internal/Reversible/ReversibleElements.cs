// The reversible tree's elements, read through the orientation flag that makes reversal a constant-
// time operation.

namespace Durable7.FingerTree;

/// <summary>
/// An element at one level of a <see cref="RevTree{T}"/>: either a <see cref="RevLeaf{T}"/> holding a stored
/// value or a <see cref="RevNode{T}"/> grouping two or three deeper elements. Every element exposes its leaf
/// count and an O(1) <see cref="Mirror"/> (a reversed view), which is what makes whole-deque
/// <see cref="ReversibleDeque{T}.Reverse"/> O(1).
/// </summary>
/// <typeparam name="T">Stored leaf element type.</typeparam>
/// <remarks>
/// All accessors are <em>logical</em>: they read the element in its current orientation, applying the
/// reversal lazily. <see cref="Mirror"/> never copies a subtree — it flips a per-node bit — so reversal of an
/// arbitrarily large element is constant time, at the cost of a branch on every access (the deliberate
/// constant-factor price of this type).
/// </remarks>
internal abstract class RevElem<T>
{
    /// <summary>Gets the number of leaves represented by this element. O(1).</summary>
    public abstract int Size { get; }

    /// <summary>Gets the logical first leaf. O(1).</summary>
    public abstract T First { get; }

    /// <summary>Gets the logical last leaf. O(1).</summary>
    public abstract T Last { get; }

    /// <summary>Returns a reversed view of this element. O(1).</summary>
    public abstract RevElem<T> Mirror();

    /// <summary>Reads the logical leaf at <paramref name="index"/> in <c>0..Size-1</c>. O(height).</summary>
    /// <param name="index">Logical leaf index.</param>
    public abstract T GetLeaf(int index);

    /// <summary>Returns a copy with the logical leaf at <paramref name="index"/> replaced. O(height).</summary>
    /// <param name="index">Logical leaf index.</param>
    /// <param name="value">Replacement value.</param>
    public abstract RevElem<T> SetLeaf(int index, T value);

    /// <summary>Appends every leaf, in logical order, to <paramref name="array"/> starting at <paramref name="offset"/>.</summary>
    /// <param name="array">Destination array.</param>
    /// <param name="offset">Running destination index, advanced by <see cref="Size"/>.</param>
    public abstract void CopyLeaves(T[] array, ref int offset);

    /// <summary>Recomputes the leaf count, throwing on a cached-measure mismatch. Test-only; O(n).</summary>
    public abstract int ValidateAndCount();
}

/// <summary>A stored value at the outermost tree level; reversing a leaf is the identity.</summary>
/// <typeparam name="T">Stored value type.</typeparam>
/// <param name="value">The stored value.</param>
internal sealed class RevLeaf<T>(T value) : RevElem<T>
{
    /// <summary>The stored value.</summary>
    public readonly T Value = value;

    /// <inheritdoc/>
    public override int Size => 1;

    /// <inheritdoc/>
    public override T First => Value;

    /// <inheritdoc/>
    public override T Last => Value;

    /// <inheritdoc/>
    public override RevElem<T> Mirror() => this;

    /// <inheritdoc/>
    public override T GetLeaf(int index) => Value;

    /// <inheritdoc/>
    public override RevElem<T> SetLeaf(int index, T value) => new RevLeaf<T>(value);

    /// <inheritdoc/>
    public override void CopyLeaves(T[] array, ref int offset) => array[offset++] = Value;

    /// <inheritdoc/>
    public override int ValidateAndCount() => 1;
}

/// <summary>
/// A two- or three-child grouping node, with a reversal bit so that <see cref="Mirror"/> is O(1). Children are
/// stored in a fixed (forward) order; the bit selects whether they are read forward or reversed.
/// </summary>
/// <typeparam name="T">Stored leaf element type.</typeparam>
internal sealed class RevNode<T> : RevElem<T>
{
    private readonly RevElem<T>[] _children; // forward order, length 2 or 3
    private readonly int _size;
    private readonly T _firstForward;
    private readonly T _lastForward;
    private readonly bool _reversed;

    /// <summary>Creates a forward node from two or three children, caching its measures.</summary>
    /// <param name="children">Two or three children, in forward order.</param>
    public RevNode(RevElem<T>[] children)
    {
        _children = children;
        var size = 0;
        foreach (var child in children)
            size += child.Size;
        _size = size;
        _firstForward = children[0].First;
        _lastForward = children[^1].Last;
        _reversed = false;
    }

    private RevNode(RevElem<T>[] children, int size, T firstForward, T lastForward, bool reversed)
    {
        _children = children;
        _size = size;
        _firstForward = firstForward;
        _lastForward = lastForward;
        _reversed = reversed;
    }

    /// <summary>Gets the number of children (two or three).</summary>
    public int Count => _children.Length;

    /// <summary>Gets the number of direct children exposed to the stack enumerator.</summary>
    public int EnumerationChildCount => Count;

    /// <inheritdoc/>
    public override int Size => _size;

    /// <inheritdoc/>
    public override T First => _reversed ? _lastForward : _firstForward;

    /// <inheritdoc/>
    public override T Last => _reversed ? _firstForward : _lastForward;

    /// <inheritdoc/>
    public override RevElem<T> Mirror() => new RevNode<T>(_children, _size, _firstForward, _lastForward, !_reversed);

    /// <summary>Gets the logical child at <paramref name="index"/> (mirrored when this node is reversed). O(1).</summary>
    /// <param name="index">Zero-based logical child position.</param>
    public RevElem<T> LogicalChild(int index) =>
        _reversed ? _children[Count - 1 - index].Mirror() : _children[index];

    /// <summary>
    /// Reads a direct child for stack enumeration, carrying a mirrored-view bit instead of allocating a
    /// mirrored wrapper for reversed children.
    /// </summary>
    /// <param name="index">Zero-based direct-child index in the requested logical orientation.</param>
    /// <param name="mirrored">Whether this node should be viewed through an additional mirror.</param>
    /// <param name="leaf">The leaf value when the child is a leaf; otherwise <see langword="default"/>.</param>
    /// <param name="node">The child node when the child is not a leaf; otherwise <see langword="null"/>.</param>
    /// <param name="childMirrored">Whether the child node should be viewed through an additional mirror.</param>
    /// <returns><see langword="true"/> when the child is a leaf; otherwise <see langword="false"/>.</returns>
    public bool TryGetEnumerationChild(
        int index,
        bool mirrored,
        out T leaf,
        out RevNode<T>? node,
        out bool childMirrored)
    {
        var viewReversed = _reversed ^ mirrored;
        var child = viewReversed ? _children[Count - 1 - index] : _children[index];
        return RevTreeOps.TryGetEnumerationChild(child, viewReversed, out leaf, out node, out childMirrored);
    }

    /// <summary>Returns the children in logical order. O(child count).</summary>
    public RevElem<T>[] LogicalChildren()
    {
        var result = new RevElem<T>[Count];
        for (var i = 0; i < Count; i++)
            result[i] = LogicalChild(i);
        return result;
    }

    /// <inheritdoc/>
    public override T GetLeaf(int index)
    {
        for (var i = 0; ; i++)
        {
            var child = LogicalChild(i);
            if (index < child.Size)
                return child.GetLeaf(index);
            index -= child.Size;
        }
    }

    /// <inheritdoc/>
    public override RevElem<T> SetLeaf(int index, T value)
    {
        var logical = LogicalChildren();
        for (var i = 0; ; i++)
        {
            if (index < logical[i].Size)
            {
                logical[i] = logical[i].SetLeaf(index, value);
                break;
            }

            index -= logical[i].Size;
        }

        return new RevNode<T>(logical);
    }

    /// <inheritdoc/>
    public override void CopyLeaves(T[] array, ref int offset)
    {
        for (var i = 0; i < Count; i++)
            LogicalChild(i).CopyLeaves(array, ref offset);
    }

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        if (Count is < 2 or > 3)
            throw new InvalidOperationException($"Node child count {Count} is outside 2..3.");
        var computed = 0;
        foreach (var child in _children)
            computed += child.ValidateAndCount();
        if (computed != _size)
            throw new InvalidOperationException($"Node cached size {_size} disagrees with recomputed total {computed}.");
        return computed;
    }
}
