namespace Tools.DataStructures.FingerTree;

/// <summary>
/// Provides type-erased child traversal for stack-based struct enumerators over polymorphically recursive trees.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <remarks>
/// Implemented by internal tree and node classes whose children alternate between leaf values and deeper blocks.
/// Enumerators keep an explicit stack of blocks so enumeration costs O(1) amortized per yielded element with an
/// O(log n) stack, independent of the polymorphic-recursion depth of the underlying representation.
/// </remarks>
internal interface IEnumerationBlock<T>
{
    /// <summary>Gets the number of direct children of this block, in left-to-right order.</summary>
    int ChildCount { get; }

    /// <summary>
    /// Reads the child at <paramref name="index"/>.
    /// </summary>
    /// <param name="index">Zero-based direct-child index, less than <see cref="ChildCount"/>.</param>
    /// <param name="leaf">The leaf value when the child is a leaf; otherwise <see langword="default"/>.</param>
    /// <param name="block">The nested block when the child is not a leaf; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the child is a leaf; otherwise <see langword="false"/>.</returns>
    bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block);
}

/// <summary>
/// Contract shared by the elements stored at one finger-tree level: either a <see cref="Leaf{T}"/> at the
/// outermost level or a <see cref="Node{T, TChild}"/> at every deeper level.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TSelf">The implementing element type itself (curiously recurring).</typeparam>
/// <remarks>
/// <para>
/// <see cref="Size"/> and <see cref="LastLeaf"/> are the measures required by the API specification: the
/// leaf count drives indexed lookup and split, and the rightmost-leaf signpost drives logarithmic sorted
/// search. Both must be O(1) for every implementation; nodes cache them at construction.
/// </para>
/// <para>
/// <see cref="FirstLeaf"/> is intentionally not cached on nodes and costs O(height). It is only used at the
/// outermost tree level, where every element is a height-zero <see cref="Leaf{T}"/>, so the public
/// <c>First</c> property remains O(1) worst-case.
/// </para>
/// </remarks>
internal interface ITreeElement<T, TSelf> where TSelf : ITreeElement<T, TSelf>
{
    /// <summary>Gets the number of leaf elements represented by this element. Always O(1).</summary>
    int Size { get; }

    /// <summary>Gets the leftmost leaf value. O(height); O(1) only for <see cref="Leaf{T}"/>.</summary>
    T FirstLeaf { get; }

    /// <summary>Gets the rightmost leaf value. Always O(1); cached on nodes.</summary>
    T LastLeaf { get; }

    /// <summary>Gets this element as an enumeration block, or <see langword="null"/> when it is a leaf.</summary>
    IEnumerationBlock<T>? AsBlock { get; }

    /// <summary>Reads the leaf at the element-relative <paramref name="index"/> in <c>0..Size-1</c>. O(height).</summary>
    /// <param name="index">Element-relative leaf index.</param>
    T GetLeaf(int index);

    /// <summary>Returns a copy with the leaf at <paramref name="index"/> replaced by <paramref name="value"/>. O(height).</summary>
    /// <param name="index">Element-relative leaf index.</param>
    /// <param name="value">Replacement leaf value.</param>
    TSelf SetLeaf(int index, T value);

    /// <summary>
    /// Returns the first element-relative leaf index whose value satisfies <paramref name="predicate"/>, or
    /// <see cref="Size"/> when no leaf satisfies it.
    /// </summary>
    /// <param name="predicate">Monotone predicate over the sorted leaf sequence (false then true).</param>
    /// <remarks>Uses the cached rightmost-leaf signposts to skip fully unsatisfied children in O(1) each.</remarks>
    int BoundIndex(Func<T, bool> predicate);

    /// <summary>Copies all leaves into <paramref name="array"/> starting at <paramref name="offset"/>, advancing it.</summary>
    /// <param name="array">Destination array.</param>
    /// <param name="offset">Running destination index, advanced by <see cref="Size"/>.</param>
    void CopyLeavesTo(T[] array, ref int offset);

    /// <summary>
    /// Recomputes the leaf count from children, throwing <see cref="InvalidOperationException"/> when any
    /// cached measure disagrees with its recomputed value. Test-only; O(n).
    /// </summary>
    int ValidateAndCount();
}

/// <summary>
/// Zero-overhead wrapper that adapts a raw element to <see cref="ITreeElement{T, TSelf}"/> at the
/// outermost finger-tree level.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <param name="value">The stored element.</param>
/// <remarks>
/// A struct with a single field, so digits of leaves store elements inline with no per-element allocation.
/// </remarks>
internal readonly struct Leaf<T>(T value) : ITreeElement<T, Leaf<T>>
{
    /// <summary>Gets the stored element.</summary>
    public T Value { get; } = value;

    /// <inheritdoc/>
    public int Size => 1;

    /// <inheritdoc/>
    public T FirstLeaf => Value;

    /// <inheritdoc/>
    public T LastLeaf => Value;

    /// <inheritdoc/>
    public IEnumerationBlock<T>? AsBlock => null;

    /// <inheritdoc/>
    public T GetLeaf(int index) => Value;

    /// <inheritdoc/>
    public Leaf<T> SetLeaf(int index, T value) => new(value);

    /// <inheritdoc/>
    public int BoundIndex(Func<T, bool> predicate) => predicate(Value) ? 0 : 1;

    /// <inheritdoc/>
    public void CopyLeavesTo(T[] array, ref int offset) => array[offset++] = Value;

    /// <inheritdoc/>
    public int ValidateAndCount() => 1;
}

/// <summary>
/// Shared helpers over <see cref="ITreeElement{T, TSelf}"/> values.
/// </summary>
internal static class TreeElement
{
    /// <summary>
    /// Classifies a child element as a leaf value or a nested enumeration block.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="child">Element to classify.</param>
    /// <param name="leaf">The leaf value when the element is a leaf; otherwise <see langword="default"/>.</param>
    /// <param name="block">The block when the element is a node; otherwise <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the element is a leaf; otherwise <see langword="false"/>.</returns>
    public static bool TryAsLeaf<T, TChild>(TChild child, out T leaf, out IEnumerationBlock<T>? block)
        where TChild : ITreeElement<T, TChild>
    {
        block = child.AsBlock;
        if (block is null)
        {
            leaf = child.LastLeaf;
            return true;
        }

        leaf = default!;
        return false;
    }
}
