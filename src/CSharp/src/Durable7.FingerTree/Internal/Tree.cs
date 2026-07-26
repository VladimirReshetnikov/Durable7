// One level of the simplified finger tree: empty, a single element, or a deep node with one-
// through-three element digits at both ends and a suspended middle tree of <see cref="Node{T,
// TChild}"/> elements.

using System.Diagnostics;

namespace Durable7.FingerTree;

/// <summary>
/// One level of the simplified finger tree: empty, a single element, or a deep node with one-through-three
/// element digits at both ends and a suspended middle tree of <see cref="Node{T, TChild}"/> elements.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type at this level; <see cref="Leaf{T}"/> at the outermost level.</typeparam>
/// <remarks>
/// <para>
/// The middle of a deep node is a <c>Tree&lt;T, Node&lt;T, TChild&gt;&gt;</c> behind a
/// <see cref="MiddleTree{T, TChild}"/> suspension, so element height is encoded in the type system
/// (polymorphic recursion) and digits can never mix heights by construction.
/// </para>
/// <para>
/// Endpoint operations follow the simplified paper's potential discipline exactly: a <see cref="Cons"/>
/// overflow from a <c>Three</c> prefix leaves a <c>Two</c> prefix and suspends a push of a
/// <see cref="Node2{T, TChild}"/> into the middle; removal from a <c>One</c> digit pulls from the middle,
/// using the non-recursive <c>chop</c> when the pulled node is a <see cref="Node3{T, TChild}"/> and
/// suspending the recursive removal when it is a <see cref="Node2{T, TChild}"/>. With memoized suspensions
/// these rules give O(log n) worst-case and O(1) amortized endpoint operations, and the amortized bounds
/// hold under fully persistent (branching) version use, per the original paper.
/// </para>
/// <para>
/// Deep nodes cache their total leaf count, and every construction site derives the new count
/// arithmetically from already-known quantities, so size queries and index routing never force a suspended
/// middle.
/// </para>
/// </remarks>
internal abstract class Tree<T, TChild> : IEnumerationBlock<T> where TChild : ITreeElement<T, TChild>
{
    /// <summary>Gets the total leaf count. O(1); cached on deep nodes and never forces the middle.</summary>
    public abstract int Size { get; }

    /// <summary>Gets the leftmost leaf. O(height of the first child); O(1) at the outermost level.</summary>
    public abstract T FirstLeaf { get; }

    /// <summary>Gets the rightmost leaf. O(1). Requires a non-empty tree.</summary>
    public abstract T LastLeaf { get; }

    /// <summary>Gets the first element. O(1). Requires a non-empty tree.</summary>
    public abstract TChild First { get; }

    /// <summary>Gets the last element. O(1). Requires a non-empty tree.</summary>
    public abstract TChild Last { get; }

    /// <summary>
    /// Returns a tree with <paramref name="child"/> prepended. O(log n) worst-case (an overflow forces the
    /// old middle before suspending the push), O(1) amortized under persistent use.
    /// </summary>
    /// <param name="child">Element to prepend.</param>
    public abstract Tree<T, TChild> Cons(TChild child);

    /// <summary>
    /// Returns a tree with <paramref name="child"/> appended. O(log n) worst-case (an overflow forces the
    /// old middle before suspending the push), O(1) amortized under persistent use.
    /// </summary>
    /// <param name="child">Element to append.</param>
    public abstract Tree<T, TChild> Snoc(TChild child);

    /// <summary>
    /// Removes the first element. O(log n) worst-case (forcing a suspension chain), O(1) amortized under
    /// persistent use. Requires a non-empty tree.
    /// </summary>
    /// <param name="removed">The removed first element.</param>
    public abstract Tree<T, TChild> RemoveFirst(out TChild removed);

    /// <summary>
    /// Removes the last element. O(log n) worst-case (forcing a suspension chain), O(1) amortized under
    /// persistent use. Requires a non-empty tree.
    /// </summary>
    /// <param name="removed">The removed last element.</param>
    public abstract Tree<T, TChild> RemoveLast(out TChild removed);

    /// <summary>Replaces the first element in O(1) worst-case; the leaf count may change.</summary>
    /// <param name="child">Replacement element.</param>
    public abstract Tree<T, TChild> ReplaceFirst(TChild child);

    /// <summary>Replaces the last element in O(1) worst-case; the leaf count may change.</summary>
    /// <param name="child">Replacement element.</param>
    public abstract Tree<T, TChild> ReplaceLast(TChild child);

    /// <summary>
    /// Reads the leaf at <paramref name="index"/>. O(log min(index + 1, Size - index)) amortized; a single
    /// call may additionally force pending spine work. Never forces middles outside the descent path.
    /// </summary>
    /// <param name="index">Tree-relative leaf index, less than <see cref="Size"/>.</param>
    public abstract T GetLeaf(int index);

    /// <summary>Replaces the leaf at <paramref name="index"/>. O(log min(index + 1, Size - index)) amortized.</summary>
    /// <param name="index">Tree-relative leaf index, less than <see cref="Size"/>.</param>
    /// <param name="value">Replacement leaf value.</param>
    public abstract Tree<T, TChild> SetLeaf(int index, T value);

    /// <summary>
    /// Splits the tree around the element containing leaf <paramref name="index"/>.
    /// O(log min(index + 1, Size - index)) amortized including reconstruction; O(log n) worst-case for a
    /// single call.
    /// </summary>
    /// <param name="index">Tree-relative leaf index, less than <see cref="Size"/>.</param>
    /// <param name="left">Tree of all elements strictly before the hit element.</param>
    /// <param name="hit">The element containing the indexed leaf.</param>
    /// <param name="indexInHit">Leaf index relative to <paramref name="hit"/>.</param>
    /// <param name="right">Tree of all elements strictly after the hit element.</param>
    public abstract void SplitChild(
        int index,
        out Tree<T, TChild> left,
        out TChild hit,
        out int indexInHit,
        out Tree<T, TChild> right);

    /// <summary>
    /// Splits around the first element containing a leaf that satisfies a monotone predicate,
    /// combining signpost-guided location and boundary reconstruction in one descent.
    /// </summary>
    public abstract bool TrySplitBoundChild(
        Func<T, bool> predicate,
        out Tree<T, TChild> left,
        out TChild hit,
        out Tree<T, TChild> right);

    /// <summary>
    /// Returns the first leaf index whose value satisfies <paramref name="predicate"/>, or <see cref="Size"/>
    /// when no leaf satisfies it. Requires the leaf sequence to be monotone under the predicate. The
    /// predicate-call count is O(log min(k + 1, Size - k + 1)) worst-case for result index k.
    /// </summary>
    /// <param name="predicate">Monotone predicate (false then true) over the leaf sequence.</param>
    public abstract int BoundIndex(Func<T, bool> predicate);

    /// <summary>Copies all leaves into <paramref name="array"/> starting at <paramref name="offset"/>, advancing it.</summary>
    /// <param name="array">Destination array.</param>
    /// <param name="offset">Running destination index, advanced by <see cref="Size"/>.</param>
    public abstract void CopyLeavesTo(T[] array, ref int offset);

    /// <summary>
    /// Recomputes the leaf count, forcing every suspension and throwing
    /// <see cref="InvalidOperationException"/> when any cached measure or digit-length invariant is
    /// violated. Test-only; O(n).
    /// </summary>
    public abstract int ValidateAndCount();

    /// <summary>Gets the number of deep levels below this tree's root, forcing middles. Test-only; O(log n).</summary>
    public abstract int Depth { get; }

    /// <inheritdoc/>
    public abstract int ChildCount { get; }

    /// <inheritdoc/>
    public abstract bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block);
}

/// <summary>
/// The empty finger-tree level.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
internal sealed class EmptyTree<T, TChild> : Tree<T, TChild> where TChild : ITreeElement<T, TChild>
{
    /// <summary>Gets the canonical empty tree for this level.</summary>
    public static readonly EmptyTree<T, TChild> Instance = new();

    private EmptyTree()
    {
    }

    /// <inheritdoc/>
    public override int Size => 0;

    /// <inheritdoc/>
    public override T FirstLeaf => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override T LastLeaf => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override TChild First => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override TChild Last => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override Tree<T, TChild> Cons(TChild child) => new SingleTree<T, TChild>(child);

    /// <inheritdoc/>
    public override Tree<T, TChild> Snoc(TChild child) => new SingleTree<T, TChild>(child);

    /// <inheritdoc/>
    public override Tree<T, TChild> RemoveFirst(out TChild removed) => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override Tree<T, TChild> RemoveLast(out TChild removed) => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override Tree<T, TChild> ReplaceFirst(TChild child) => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override Tree<T, TChild> ReplaceLast(TChild child) => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override T GetLeaf(int index) => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override Tree<T, TChild> SetLeaf(int index, T value) => throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override void SplitChild(
        int index,
        out Tree<T, TChild> left,
        out TChild hit,
        out int indexInHit,
        out Tree<T, TChild> right) =>
        throw UnreachableEmptyAccess();

    /// <inheritdoc/>
    public override bool TrySplitBoundChild(
        Func<T, bool> predicate,
        out Tree<T, TChild> left,
        out TChild hit,
        out Tree<T, TChild> right)
    {
        left = this;
        hit = default!;
        right = this;
        return false;
    }

    /// <inheritdoc/>
    public override int BoundIndex(Func<T, bool> predicate) => 0;

    /// <inheritdoc/>
    public override void CopyLeavesTo(T[] array, ref int offset)
    {
    }

    /// <inheritdoc/>
    public override int ValidateAndCount() => 0;

    /// <inheritdoc/>
    public override int Depth => 0;

    /// <inheritdoc/>
    public override int ChildCount => 0;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block) =>
        throw UnreachableEmptyAccess();

    private static InvalidOperationException UnreachableEmptyAccess() =>
        new("Internal invariant violation: element access on an empty finger-tree level.");
}

/// <summary>
/// A one-element finger-tree level.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
/// <param name="element">The stored element.</param>
internal sealed class SingleTree<T, TChild>(TChild element) : Tree<T, TChild>
    where TChild : ITreeElement<T, TChild>
{
    /// <summary>The only element of this level.</summary>
    public readonly TChild Element = element;

    /// <inheritdoc/>
    public override int Size => Element.Size;

    /// <inheritdoc/>
    public override T FirstLeaf => Element.FirstLeaf;

    /// <inheritdoc/>
    public override T LastLeaf => Element.LastLeaf;

    /// <inheritdoc/>
    public override TChild First => Element;

    /// <inheritdoc/>
    public override TChild Last => Element;

    /// <inheritdoc/>
    public override Tree<T, TChild> Cons(TChild child) =>
        new DeepTree<T, TChild>(
            new Digit<T, TChild>(child),
            MiddleTree<T, Node<T, TChild>>.Empty,
            new Digit<T, TChild>(Element),
            child.Size + Element.Size);

    /// <inheritdoc/>
    public override Tree<T, TChild> Snoc(TChild child) =>
        new DeepTree<T, TChild>(
            new Digit<T, TChild>(Element),
            MiddleTree<T, Node<T, TChild>>.Empty,
            new Digit<T, TChild>(child),
            Element.Size + child.Size);

    /// <inheritdoc/>
    public override Tree<T, TChild> RemoveFirst(out TChild removed)
    {
        removed = Element;
        return EmptyTree<T, TChild>.Instance;
    }

    /// <inheritdoc/>
    public override Tree<T, TChild> RemoveLast(out TChild removed)
    {
        removed = Element;
        return EmptyTree<T, TChild>.Instance;
    }

    /// <inheritdoc/>
    public override Tree<T, TChild> ReplaceFirst(TChild child) => new SingleTree<T, TChild>(child);

    /// <inheritdoc/>
    public override Tree<T, TChild> ReplaceLast(TChild child) => new SingleTree<T, TChild>(child);

    /// <inheritdoc/>
    public override T GetLeaf(int index) => Element.GetLeaf(index);

    /// <inheritdoc/>
    public override Tree<T, TChild> SetLeaf(int index, T value) =>
        new SingleTree<T, TChild>(Element.SetLeaf(index, value));

    /// <inheritdoc/>
    public override void SplitChild(
        int index,
        out Tree<T, TChild> left,
        out TChild hit,
        out int indexInHit,
        out Tree<T, TChild> right)
    {
        left = EmptyTree<T, TChild>.Instance;
        hit = Element;
        indexInHit = index;
        right = EmptyTree<T, TChild>.Instance;
    }

    /// <inheritdoc/>
    public override bool TrySplitBoundChild(
        Func<T, bool> predicate,
        out Tree<T, TChild> left,
        out TChild hit,
        out Tree<T, TChild> right)
    {
        left = EmptyTree<T, TChild>.Instance;
        right = EmptyTree<T, TChild>.Instance;
        if (predicate(Element.LastLeaf))
        {
            hit = Element;
            return true;
        }

        hit = default!;
        return false;
    }

    /// <inheritdoc/>
    public override int BoundIndex(Func<T, bool> predicate) =>
        predicate(Element.LastLeaf) ? Element.BoundIndex(predicate) : Size;

    /// <inheritdoc/>
    public override void CopyLeavesTo(T[] array, ref int offset) => Element.CopyLeavesTo(array, ref offset);

    /// <inheritdoc/>
    public override int ValidateAndCount() => Element.ValidateAndCount();

    /// <inheritdoc/>
    public override int Depth => 0;

    /// <inheritdoc/>
    public override int ChildCount => 1;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block) =>
        TreeElement.TryAsLeaf(Element, out leaf, out block);
}

/// <summary>
/// A deep finger-tree level: a one-through-three element prefix digit, a suspended middle tree of nodes, and
/// a one-through-three element suffix digit, with a cached total leaf count.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type at this level.</typeparam>
internal sealed class DeepTree<T, TChild> : Tree<T, TChild> where TChild : ITreeElement<T, TChild>
{
    private readonly MiddleTree<T, Node<T, TChild>> _middle;

    /// <summary>Prefix digit; always one through three children.</summary>
    public readonly Digit<T, TChild> Prefix;

    /// <summary>Suffix digit; always one through three children.</summary>
    public readonly Digit<T, TChild> Suffix;

    /// <summary>Initializes a deep level with an explicitly provided total leaf count.</summary>
    /// <param name="prefix">Prefix digit with one through three children.</param>
    /// <param name="middle">Middle state, computed or suspended.</param>
    /// <param name="suffix">Suffix digit with one through three children.</param>
    /// <param name="size">
    /// Total leaf count of the level. Callers derive it arithmetically from already-known quantities so
    /// construction never forces a suspended middle.
    /// </param>
    public DeepTree(
        in Digit<T, TChild> prefix,
        MiddleTree<T, Node<T, TChild>> middle,
        in Digit<T, TChild> suffix,
        int size)
    {
        Debug.Assert(prefix.Length is >= 1 and <= 3, "Deep prefix digit must hold one through three children.");
        Debug.Assert(suffix.Length is >= 1 and <= 3, "Deep suffix digit must hold one through three children.");
        Prefix = prefix;
        _middle = middle;
        Suffix = suffix;
        Size = size;
    }

    /// <inheritdoc/>
    public override int Size { get; }

    /// <summary>Gets the middle's leaf count without forcing it. O(1).</summary>
    public int MiddleSize => Size - Prefix.Size - Suffix.Size;

    /// <summary>Returns the middle tree, running and memoizing its pending operation on first use.</summary>
    public Tree<T, Node<T, TChild>> ForceMiddle() => _middle.Force();

    /// <inheritdoc/>
    public override T FirstLeaf => Prefix.A.FirstLeaf;

    /// <inheritdoc/>
    public override T LastLeaf => Suffix.Last.LastLeaf;

    /// <inheritdoc/>
    public override TChild First => Prefix.A;

    /// <inheritdoc/>
    public override TChild Last => Suffix.Last;

    /// <inheritdoc/>
    public override Tree<T, TChild> Cons(TChild child) => Prefix.Length switch
    {
        1 or 2 => new DeepTree<T, TChild>(Prefix.Cons(child), _middle, Suffix, Size + child.Size),
        // Overflow: leave a safe Two prefix here and suspend pushing the far pair down as a Node2. The old
        // middle is forced first so suspensions never chain deeper than one deferred operation.
        _ => new DeepTree<T, TChild>(
            new Digit<T, TChild>(child, Prefix.A),
            Suspend(new PendingPushFront<T, Node<T, TChild>>(ForceMiddle(), new Node2<T, TChild>(Prefix.B, Prefix.C))),
            Suffix,
            Size + child.Size),
    };

    /// <inheritdoc/>
    public override Tree<T, TChild> Snoc(TChild child) => Suffix.Length switch
    {
        1 or 2 => new DeepTree<T, TChild>(Prefix, _middle, Suffix.Snoc(child), Size + child.Size),
        // Overflow: leave a safe Two suffix here and suspend pushing the far pair down as a Node2. The old
        // middle is forced first so suspensions never chain deeper than one deferred operation.
        _ => new DeepTree<T, TChild>(
            Prefix,
            Suspend(new PendingPushBack<T, Node<T, TChild>>(ForceMiddle(), new Node2<T, TChild>(Suffix.A, Suffix.B))),
            new Digit<T, TChild>(Suffix.C, child),
            Size + child.Size),
    };

    /// <inheritdoc/>
    public override Tree<T, TChild> RemoveFirst(out TChild removed)
    {
        removed = Prefix.A;
        if (Prefix.Length > 1)
            return new DeepTree<T, TChild>(Prefix.RemoveFirst(), _middle, Suffix, Size - removed.Size);
        return MiddleSize == 0
            ? TreeOperations.FromDigit(Suffix)
            : TreeOperations.PullLeft(ForceMiddle(), Suffix);
    }

    /// <inheritdoc/>
    public override Tree<T, TChild> RemoveLast(out TChild removed)
    {
        removed = Suffix.Last;
        if (Suffix.Length > 1)
            return new DeepTree<T, TChild>(Prefix, _middle, Suffix.RemoveLast(), Size - removed.Size);
        return MiddleSize == 0
            ? TreeOperations.FromDigit(Prefix)
            : TreeOperations.PullRight(Prefix, ForceMiddle());
    }

    /// <inheritdoc/>
    public override Tree<T, TChild> ReplaceFirst(TChild child) =>
        new DeepTree<T, TChild>(
            Prefix.ReplaceFirst(child),
            _middle,
            Suffix,
            Size - Prefix.A.Size + child.Size);

    /// <inheritdoc/>
    public override Tree<T, TChild> ReplaceLast(TChild child) =>
        new DeepTree<T, TChild>(
            Prefix,
            _middle,
            Suffix.ReplaceLast(child),
            Size - Suffix.Last.Size + child.Size);

    /// <inheritdoc/>
    public override T GetLeaf(int index)
    {
        var prefixSize = Prefix.Size;
        if (index < prefixSize)
        {
            for (var position = 0; ; position++)
            {
                var child = Prefix.ChildAt(position);
                if (index < child.Size)
                    return child.GetLeaf(index);
                index -= child.Size;
            }
        }

        index -= prefixSize;
        var middleSize = MiddleSize;
        if (index < middleSize)
            return ForceMiddle().GetLeaf(index);

        index -= middleSize;
        for (var position = 0; ; position++)
        {
            var child = Suffix.ChildAt(position);
            if (index < child.Size)
                return child.GetLeaf(index);
            index -= child.Size;
        }
    }

    /// <inheritdoc/>
    public override Tree<T, TChild> SetLeaf(int index, T value)
    {
        var prefixSize = Prefix.Size;
        if (index < prefixSize)
        {
            for (var position = 0; ; position++)
            {
                var child = Prefix.ChildAt(position);
                if (index < child.Size)
                {
                    return new DeepTree<T, TChild>(
                        Prefix.WithChildAt(position, child.SetLeaf(index, value)),
                        _middle,
                        Suffix,
                        Size);
                }

                index -= child.Size;
            }
        }

        index -= prefixSize;
        var middleSize = MiddleSize;
        if (index < middleSize)
        {
            return new DeepTree<T, TChild>(
                Prefix,
                new MiddleTree<T, Node<T, TChild>>(ForceMiddle().SetLeaf(index, value)),
                Suffix,
                Size);
        }

        index -= middleSize;
        for (var position = 0; ; position++)
        {
            var child = Suffix.ChildAt(position);
            if (index < child.Size)
            {
                return new DeepTree<T, TChild>(
                    Prefix,
                    _middle,
                    Suffix.WithChildAt(position, child.SetLeaf(index, value)),
                    Size);
            }

            index -= child.Size;
        }
    }

    /// <inheritdoc/>
    public override void SplitChild(
        int index,
        out Tree<T, TChild> left,
        out TChild hit,
        out int indexInHit,
        out Tree<T, TChild> right)
    {
        var prefixSize = Prefix.Size;
        if (index < prefixSize)
        {
            Prefix.Split(index, out var before, out hit, out indexInHit, out var after);
            left = TreeOperations.FromPartialDigit(before);
            right = TreeOperations.DeepLeft(after, ForceMiddle(), Suffix);
            return;
        }

        index -= prefixSize;
        var middleSize = MiddleSize;
        if (index < middleSize)
        {
            ForceMiddle().SplitChild(index, out var middleLeft, out var hitNode, out var indexInNode, out var middleRight);
            hitNode.Split(indexInNode, out var before, out hit, out indexInHit, out var after);
            left = TreeOperations.DeepRight(Prefix, middleLeft, before);
            right = TreeOperations.DeepLeft(after, middleRight, Suffix);
            return;
        }

        index -= middleSize;
        Suffix.Split(index, out var beforeSuffix, out hit, out indexInHit, out var afterSuffix);
        left = TreeOperations.DeepRight(Prefix, ForceMiddle(), beforeSuffix);
        right = TreeOperations.FromPartialDigit(afterSuffix);
    }

    /// <inheritdoc/>
    public override bool TrySplitBoundChild(
        Func<T, bool> predicate,
        out Tree<T, TChild> left,
        out TChild hit,
        out Tree<T, TChild> right)
    {
        for (var position = 0; position < Prefix.Length; position++)
        {
            var child = Prefix.ChildAt(position);
            if (!predicate(child.LastLeaf))
                continue;

            left = TreeOperations.FromPartialDigit(Prefix.TakeBefore(position));
            hit = child;
            right = TreeOperations.DeepLeft(Prefix.TakeAfter(position), ForceMiddle(), Suffix);
            return true;
        }

        if (MiddleSize > 0)
        {
            var middle = ForceMiddle();
            if (predicate(middle.LastLeaf))
            {
                var found = middle.TrySplitBoundChild(
                    predicate, out var middleLeft, out var hitNode, out var middleRight);
                Debug.Assert(found, "A middle bound signpost must lead to a matching node.");
                hitNode.SplitBound(predicate, out var before, out hit, out var after);
                left = TreeOperations.DeepRight(Prefix, middleLeft, before);
                right = TreeOperations.DeepLeft(after, middleRight, Suffix);
                return true;
            }
        }

        for (var position = 0; position < Suffix.Length; position++)
        {
            var child = Suffix.ChildAt(position);
            if (!predicate(child.LastLeaf))
                continue;

            left = TreeOperations.DeepRight(Prefix, ForceMiddle(), Suffix.TakeBefore(position));
            hit = child;
            right = TreeOperations.FromPartialDigit(Suffix.TakeAfter(position));
            return true;
        }

        left = this;
        hit = default!;
        right = EmptyTree<T, TChild>.Instance;
        return false;
    }

    /// <inheritdoc/>
    public override int BoundIndex(Func<T, bool> predicate)
    {
        var accumulated = 0;
        for (var position = 0; position < Prefix.Length; position++)
        {
            var child = Prefix.ChildAt(position);
            if (predicate(child.LastLeaf))
                return accumulated + child.BoundIndex(predicate);
            accumulated += child.Size;
        }

        if (MiddleSize > 0)
        {
            var middle = ForceMiddle();
            if (predicate(middle.LastLeaf))
                return accumulated + middle.BoundIndex(predicate);
            accumulated += middle.Size;
        }

        for (var position = 0; position < Suffix.Length; position++)
        {
            var child = Suffix.ChildAt(position);
            if (predicate(child.LastLeaf))
                return accumulated + child.BoundIndex(predicate);
            accumulated += child.Size;
        }

        return accumulated;
    }

    /// <inheritdoc/>
    public override void CopyLeavesTo(T[] array, ref int offset)
    {
        for (var position = 0; position < Prefix.Length; position++)
            Prefix.ChildAt(position).CopyLeavesTo(array, ref offset);
        ForceMiddle().CopyLeavesTo(array, ref offset);
        for (var position = 0; position < Suffix.Length; position++)
            Suffix.ChildAt(position).CopyLeavesTo(array, ref offset);
    }

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        if (Prefix.Length is < 1 or > 3)
            throw new InvalidOperationException($"Deep prefix digit length {Prefix.Length} is outside 1..3.");
        if (Suffix.Length is < 1 or > 3)
            throw new InvalidOperationException($"Deep suffix digit length {Suffix.Length} is outside 1..3.");

        var computed = 0;
        for (var position = 0; position < Prefix.Length; position++)
            computed += Prefix.ChildAt(position).ValidateAndCount();
        computed += ForceMiddle().ValidateAndCount();
        for (var position = 0; position < Suffix.Length; position++)
            computed += Suffix.ChildAt(position).ValidateAndCount();

        if (computed != Size)
        {
            throw new InvalidOperationException(
                $"Deep cached size {Size} disagrees with recomputed leaf total {computed}.");
        }

        return computed;
    }

    /// <inheritdoc/>
    public override int Depth => 1 + ForceMiddle().Depth;

    /// <inheritdoc/>
    public override int ChildCount => Prefix.Length + 1 + Suffix.Length;

    /// <inheritdoc/>
    public override bool TryGetChild(int index, out T leaf, out IEnumerationBlock<T>? block)
    {
        if (index < Prefix.Length)
            return TreeElement.TryAsLeaf(Prefix.ChildAt(index), out leaf, out block);

        if (index == Prefix.Length)
        {
            leaf = default!;
            block = ForceMiddle();
            return false;
        }

        return TreeElement.TryAsLeaf(Suffix.ChildAt(index - Prefix.Length - 1), out leaf, out block);
    }

    private static MiddleTree<T, Node<T, TChild>> Suspend(PendingMiddle<T, Node<T, TChild>> pending) =>
        new(new LazyMiddle<T, Node<T, TChild>>(pending));
}
