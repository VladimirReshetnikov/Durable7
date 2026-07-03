using System.Diagnostics;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// A middle-tree element holding two or three children one level closer to the leaves, with cached
/// leaf-count and rightmost-leaf measures.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the children, one level below this node.</typeparam>
/// <param name="size">Total leaf count of the children.</param>
/// <param name="lastLeaf">Rightmost leaf value of the last child.</param>
/// <remarks>
/// Both measures are computed once at construction from the children's already cached measures, so node
/// construction is O(1) and measure reads are O(1) regardless of node height. The cached rightmost leaf is
/// the sorted-search signpost required by the API specification.
/// </remarks>
internal abstract class Node<T, TChild>(int size, T lastLeaf)
    : ITreeElement<T, Node<T, TChild>>, IEnumerationBlock<T>
    where TChild : ITreeElement<T, TChild>
{
    /// <inheritdoc/>
    public int Size { get; } = size;

    /// <inheritdoc/>
    public T LastLeaf { get; } = lastLeaf;

    /// <inheritdoc/>
    public abstract T FirstLeaf { get; }

    /// <inheritdoc/>
    public IEnumerationBlock<T>? AsBlock => this;

    /// <inheritdoc/>
    public abstract T GetLeaf(int index);

    /// <inheritdoc/>
    public abstract Node<T, TChild> SetLeaf(int index, T value);

    /// <inheritdoc/>
    public abstract int BoundIndex(Func<T, bool> predicate);

    /// <inheritdoc/>
    public abstract void CopyLeavesTo(T[] array, ref int offset);

    /// <inheritdoc/>
    public abstract int ValidateAndCount();

    /// <summary>Converts the node's children into a digit of the same arity.</summary>
    public abstract Digit<T, TChild> ToDigit();

    /// <summary>
    /// Locates the child containing the node-relative leaf <paramref name="leafIndex"/> and the surrounding
    /// partial digits, mirroring <see cref="Digit{T, TChild}.Split"/>.
    /// </summary>
    /// <param name="leafIndex">Node-relative leaf index, less than <see cref="Size"/>.</param>
    /// <param name="before">Children strictly before the hit child.</param>
    /// <param name="hit">The child containing the indexed leaf.</param>
    /// <param name="indexInHit">Leaf index relative to <paramref name="hit"/>.</param>
    /// <param name="after">Children strictly after the hit child.</param>
    public abstract void Split(
        int leafIndex,
        out Digit<T, TChild> before,
        out TChild hit,
        out int indexInHit,
        out Digit<T, TChild> after);

    /// <inheritdoc/>
    public abstract int ChildCount { get; }

    /// <inheritdoc/>
    public abstract bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block);

    /// <summary>Verifies this node's cached measures against the recomputed child measures.</summary>
    /// <param name="computedSize">Leaf count recomputed from the children.</param>
    /// <param name="lastChild">The last child, whose rightmost leaf must match the cached signpost.</param>
    private protected void ValidateMeasures(int computedSize, TChild lastChild)
    {
        if (computedSize != Size)
        {
            throw new InvalidOperationException(
                $"Node cached size {Size} disagrees with recomputed child total {computedSize}.");
        }

        if (!EqualityComparer<T>.Default.Equals(LastLeaf, lastChild.LastLeaf))
        {
            throw new InvalidOperationException("Node cached rightmost leaf disagrees with its last child.");
        }
    }
}

/// <summary>
/// A two-child middle-tree node.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the children.</typeparam>
/// <param name="a">First child.</param>
/// <param name="b">Second child.</param>
internal sealed class Node2<T, TChild>(TChild a, TChild b)
    : Node<T, TChild>(a.Size + b.Size, b.LastLeaf)
    where TChild : ITreeElement<T, TChild>
{
    /// <summary>First child.</summary>
    public readonly TChild A = a;

    /// <summary>Second child.</summary>
    public readonly TChild B = b;

    /// <inheritdoc/>
    public override T FirstLeaf => A.FirstLeaf;

    /// <inheritdoc/>
    public override T GetLeaf(int index) =>
        index < A.Size ? A.GetLeaf(index) : B.GetLeaf(index - A.Size);

    /// <inheritdoc/>
    public override Node<T, TChild> SetLeaf(int index, T value) =>
        index < A.Size
            ? new Node2<T, TChild>(A.SetLeaf(index, value), B)
            : new Node2<T, TChild>(A, B.SetLeaf(index - A.Size, value));

    /// <inheritdoc/>
    public override int BoundIndex(Func<T, bool> predicate)
    {
        if (predicate(A.LastLeaf))
            return A.BoundIndex(predicate);
        if (predicate(B.LastLeaf))
            return A.Size + B.BoundIndex(predicate);
        return Size;
    }

    /// <inheritdoc/>
    public override void CopyLeavesTo(T[] array, ref int offset)
    {
        A.CopyLeavesTo(array, ref offset);
        B.CopyLeavesTo(array, ref offset);
    }

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        var computed = A.ValidateAndCount() + B.ValidateAndCount();
        ValidateMeasures(computed, B);
        return computed;
    }

    /// <inheritdoc/>
    public override Digit<T, TChild> ToDigit() => new(A, B);

    /// <inheritdoc/>
    public override void Split(
        int leafIndex,
        out Digit<T, TChild> before,
        out TChild hit,
        out int indexInHit,
        out Digit<T, TChild> after)
    {
        Debug.Assert(leafIndex >= 0 && leafIndex < Size, "Node split index must address a stored leaf.");
        if (leafIndex < A.Size)
        {
            before = Digit<T, TChild>.Empty;
            hit = A;
            indexInHit = leafIndex;
            after = new Digit<T, TChild>(B);
        }
        else
        {
            before = new Digit<T, TChild>(A);
            hit = B;
            indexInHit = leafIndex - A.Size;
            after = Digit<T, TChild>.Empty;
        }
    }

    /// <inheritdoc/>
    public override int ChildCount => 2;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block) =>
        TreeElement.TryAsLeaf(index == 0 ? A : B, out leaf, out block);
}

/// <summary>
/// A three-child middle-tree node.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type of the children.</typeparam>
/// <param name="a">First child.</param>
/// <param name="b">Second child.</param>
/// <param name="c">Third child.</param>
internal sealed class Node3<T, TChild>(TChild a, TChild b, TChild c)
    : Node<T, TChild>(a.Size + b.Size + c.Size, c.LastLeaf)
    where TChild : ITreeElement<T, TChild>
{
    /// <summary>First child.</summary>
    public readonly TChild A = a;

    /// <summary>Second child.</summary>
    public readonly TChild B = b;

    /// <summary>Third child.</summary>
    public readonly TChild C = c;

    /// <inheritdoc/>
    public override T FirstLeaf => A.FirstLeaf;

    /// <inheritdoc/>
    public override T GetLeaf(int index)
    {
        if (index < A.Size)
            return A.GetLeaf(index);
        index -= A.Size;
        return index < B.Size ? B.GetLeaf(index) : C.GetLeaf(index - B.Size);
    }

    /// <inheritdoc/>
    public override Node<T, TChild> SetLeaf(int index, T value)
    {
        if (index < A.Size)
            return new Node3<T, TChild>(A.SetLeaf(index, value), B, C);
        index -= A.Size;
        return index < B.Size
            ? new Node3<T, TChild>(A, B.SetLeaf(index, value), C)
            : new Node3<T, TChild>(A, B, C.SetLeaf(index - B.Size, value));
    }

    /// <inheritdoc/>
    public override int BoundIndex(Func<T, bool> predicate)
    {
        if (predicate(A.LastLeaf))
            return A.BoundIndex(predicate);
        if (predicate(B.LastLeaf))
            return A.Size + B.BoundIndex(predicate);
        if (predicate(C.LastLeaf))
            return A.Size + B.Size + C.BoundIndex(predicate);
        return Size;
    }

    /// <inheritdoc/>
    public override void CopyLeavesTo(T[] array, ref int offset)
    {
        A.CopyLeavesTo(array, ref offset);
        B.CopyLeavesTo(array, ref offset);
        C.CopyLeavesTo(array, ref offset);
    }

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        var computed = A.ValidateAndCount() + B.ValidateAndCount() + C.ValidateAndCount();
        ValidateMeasures(computed, C);
        return computed;
    }

    /// <inheritdoc/>
    public override Digit<T, TChild> ToDigit() => new(A, B, C);

    /// <inheritdoc/>
    public override void Split(
        int leafIndex,
        out Digit<T, TChild> before,
        out TChild hit,
        out int indexInHit,
        out Digit<T, TChild> after)
    {
        Debug.Assert(leafIndex >= 0 && leafIndex < Size, "Node split index must address a stored leaf.");
        if (leafIndex < A.Size)
        {
            before = Digit<T, TChild>.Empty;
            hit = A;
            indexInHit = leafIndex;
            after = new Digit<T, TChild>(B, C);
            return;
        }

        leafIndex -= A.Size;
        if (leafIndex < B.Size)
        {
            before = new Digit<T, TChild>(A);
            hit = B;
            indexInHit = leafIndex;
            after = new Digit<T, TChild>(C);
            return;
        }

        before = new Digit<T, TChild>(A, B);
        hit = C;
        indexInHit = leafIndex - B.Size;
        after = Digit<T, TChild>.Empty;
    }

    /// <inheritdoc/>
    public override int ChildCount => 3;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block) =>
        TreeElement.TryAsLeaf(index switch { 0 => A, 1 => B, _ => C }, out leaf, out block);
}
