// Level-spanning finger-tree operations: digit-to-tree conversion, the smart deep constructors used
// by removal and split, and concatenation.

using System.Diagnostics;

namespace Durable7.FingerTree;

/// <summary>
/// Level-spanning finger-tree operations: digit-to-tree conversion, the smart deep constructors used by
/// removal and split, and concatenation.
/// </summary>
/// <remarks>
/// These are static generic methods rather than virtual members because they construct trees from parts that
/// live at two adjacent levels of the polymorphic recursion (a digit of <c>TChild</c> plus a middle tree of
/// <c>Node&lt;T, TChild&gt;</c>).
/// </remarks>
internal static class TreeOperations
{
    /// <summary>Builds a tree from a digit of one through three elements.</summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="digit">Digit with one through three children.</param>
    public static Tree<T, TChild> FromDigit<T, TChild>(in Digit<T, TChild> digit)
        where TChild : ITreeElement<T, TChild> => digit.Length switch
    {
        1 => new SingleTree<T, TChild>(digit.A),
        2 => new DeepTree<T, TChild>(
            new Digit<T, TChild>(digit.A),
            MiddleTree<T, Node<T, TChild>>.Empty,
            new Digit<T, TChild>(digit.B),
            digit.Size),
        _ => new DeepTree<T, TChild>(
            new Digit<T, TChild>(digit.A),
            MiddleTree<T, Node<T, TChild>>.Empty,
            new Digit<T, TChild>(digit.B, digit.C),
            digit.Size),
    };

    /// <summary>Builds a tree from a possibly empty digit of zero through three elements.</summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="digit">Digit with zero through three children.</param>
    public static Tree<T, TChild> FromPartialDigit<T, TChild>(in Digit<T, TChild> digit)
        where TChild : ITreeElement<T, TChild> =>
        digit.Length == 0 ? EmptyTree<T, TChild>.Instance : FromDigit(digit);

    /// <summary>
    /// Builds a deep-shaped tree whose prefix digit may be empty, pulling a prefix from the middle tree when
    /// needed. This is the paper's <c>more0</c> generalized to split construction.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="prefix">Possibly empty prefix digit with zero through three children.</param>
    /// <param name="middle">Computed middle tree of nodes.</param>
    /// <param name="suffix">Suffix digit with one through three children.</param>
    public static Tree<T, TChild> DeepLeft<T, TChild>(
        in Digit<T, TChild> prefix,
        Tree<T, Node<T, TChild>> middle,
        in Digit<T, TChild> suffix)
        where TChild : ITreeElement<T, TChild>
    {
        if (prefix.Length > 0)
        {
            return new DeepTree<T, TChild>(
                prefix,
                new MiddleTree<T, Node<T, TChild>>(middle),
                suffix,
                prefix.Size + middle.Size + suffix.Size);
        }

        return middle.Size == 0 ? FromDigit(suffix) : PullLeft(middle, suffix);
    }

    /// <summary>
    /// Builds a deep-shaped tree whose suffix digit may be empty, pulling a suffix from the middle tree when
    /// needed. Mirror of <see cref="DeepLeft{T, TChild}"/>.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="prefix">Prefix digit with one through three children.</param>
    /// <param name="middle">Computed middle tree of nodes.</param>
    /// <param name="suffix">Possibly empty suffix digit with zero through three children.</param>
    public static Tree<T, TChild> DeepRight<T, TChild>(
        in Digit<T, TChild> prefix,
        Tree<T, Node<T, TChild>> middle,
        in Digit<T, TChild> suffix)
        where TChild : ITreeElement<T, TChild>
    {
        if (suffix.Length > 0)
        {
            return new DeepTree<T, TChild>(
                prefix,
                new MiddleTree<T, Node<T, TChild>>(middle),
                suffix,
                prefix.Size + middle.Size + suffix.Size);
        }

        return middle.Size == 0 ? FromDigit(prefix) : PullRight(prefix, middle);
    }

    /// <summary>
    /// Rebuilds a level whose prefix digit became empty by pulling the first middle node, using the paper's
    /// non-recursive <c>chop</c> when that node is a <see cref="Node3{T, TChild}"/> and suspending the
    /// recursive removal when it is a <see cref="Node2{T, TChild}"/>.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="middle">Computed, non-empty middle tree of nodes.</param>
    /// <param name="suffix">Suffix digit with one through three children.</param>
    /// <remarks>
    /// The <c>Node3</c> case must not recurse: exposing one element and replacing the head node in place
    /// keeps the amortized accounting of the simplified paper. The <c>Node2</c> case leaves a safe
    /// <c>Two</c> prefix at this level and defers the head removal behind a memoized suspension, which is
    /// what makes the amortized bounds persistence-robust per the original paper.
    /// </remarks>
    public static Tree<T, TChild> PullLeft<T, TChild>(
        Tree<T, Node<T, TChild>> middle,
        in Digit<T, TChild> suffix)
        where TChild : ITreeElement<T, TChild>
    {
        Debug.Assert(middle.Size > 0, "PullLeft requires a non-empty middle tree.");
        if (middle.First is Node3<T, TChild> head3)
        {
            return new DeepTree<T, TChild>(
                new Digit<T, TChild>(head3.A),
                new MiddleTree<T, Node<T, TChild>>(
                    middle.ReplaceFirst(new Node2<T, TChild>(head3.B, head3.C))),
                suffix,
                middle.Size + suffix.Size);
        }

        var head2 = (Node2<T, TChild>)middle.First;
        return new DeepTree<T, TChild>(
            new Digit<T, TChild>(head2.A, head2.B),
            new MiddleTree<T, Node<T, TChild>>(
                new LazyMiddle<T, Node<T, TChild>>(new PendingPopFront<T, Node<T, TChild>>(middle))),
            suffix,
            middle.Size + suffix.Size);
    }

    /// <summary>
    /// Rebuilds a level whose suffix digit became empty by pulling the last middle node. Mirror of
    /// <see cref="PullLeft{T, TChild}"/>.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="prefix">Prefix digit with one through three children.</param>
    /// <param name="middle">Computed, non-empty middle tree of nodes.</param>
    public static Tree<T, TChild> PullRight<T, TChild>(
        in Digit<T, TChild> prefix,
        Tree<T, Node<T, TChild>> middle)
        where TChild : ITreeElement<T, TChild>
    {
        Debug.Assert(middle.Size > 0, "PullRight requires a non-empty middle tree.");
        if (middle.Last is Node3<T, TChild> last3)
        {
            return new DeepTree<T, TChild>(
                prefix,
                new MiddleTree<T, Node<T, TChild>>(
                    middle.ReplaceLast(new Node2<T, TChild>(last3.A, last3.B))),
                new Digit<T, TChild>(last3.C),
                prefix.Size + middle.Size);
        }

        var last2 = (Node2<T, TChild>)middle.Last;
        return new DeepTree<T, TChild>(
            prefix,
            new MiddleTree<T, Node<T, TChild>>(
                new LazyMiddle<T, Node<T, TChild>>(new PendingPopBack<T, Node<T, TChild>>(middle))),
            new Digit<T, TChild>(last2.A, last2.B),
            prefix.Size + middle.Size);
    }

    /// <summary>
    /// Concatenates two trees of the same level. O(log(min(left.Size, right.Size) + 1)) amortized;
    /// O(log(left.Size + right.Size)) worst-case for a single call.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="left">Left operand; all of its leaves precede the right operand's leaves.</param>
    /// <param name="right">Right operand.</param>
    /// <remarks>
    /// The recursion descends both middles simultaneously and stops as soon as either side bottoms out, so
    /// the structural depth is bounded by the shallower operand. At the bottom, the carried elements are
    /// pushed onto the deeper operand with the lazy <c>Cons</c>/<c>Snoc</c> (O(1) amortized; O(log n)
    /// worst-case when an overflow must force the old middle), which is the amortized part of the bound.
    /// </remarks>
    public static Tree<T, TChild> Concat<T, TChild>(Tree<T, TChild> left, Tree<T, TChild> right)
        where TChild : ITreeElement<T, TChild> =>
        Glue(left, [], 0, right);

    /// <summary>
    /// Concatenates two trees with zero through three carried elements in between (the paper's
    /// <c>glue</c>).
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="left">Left tree.</param>
    /// <param name="carry">Buffer holding the carried elements.</param>
    /// <param name="carryCount">Number of carried elements, zero through three.</param>
    /// <param name="right">Right tree.</param>
    private static Tree<T, TChild> Glue<T, TChild>(
        Tree<T, TChild> left,
        TChild[] carry,
        int carryCount,
        Tree<T, TChild> right)
        where TChild : ITreeElement<T, TChild>
    {
        Debug.Assert(carryCount <= 3, "Glue carries at most three elements between levels.");
        switch (left)
        {
            case EmptyTree<T, TChild>:
                for (var index = carryCount - 1; index >= 0; index--)
                    right = right.Cons(carry[index]);
                return right;

            case SingleTree<T, TChild> single:
                for (var index = carryCount - 1; index >= 0; index--)
                    right = right.Cons(carry[index]);
                return right.Cons(single.Element);
        }

        switch (right)
        {
            case EmptyTree<T, TChild>:
                for (var index = 0; index < carryCount; index++)
                    left = left.Snoc(carry[index]);
                return left;

            case SingleTree<T, TChild> single:
                for (var index = 0; index < carryCount; index++)
                    left = left.Snoc(carry[index]);
                return left.Snoc(single.Element);
        }

        var deepLeft = (DeepTree<T, TChild>)left;
        var deepRight = (DeepTree<T, TChild>)right;

        // Combine left suffix + carry + right prefix (two through nine elements) and chunk them into
        // one through three nodes that become the carry for the next level down.
        var combined = new TChild[9];
        var combinedCount = 0;
        var carrySize = 0;
        for (var position = 0; position < deepLeft.Suffix.Length; position++)
            combined[combinedCount++] = deepLeft.Suffix.ChildAt(position);
        for (var index = 0; index < carryCount; index++)
        {
            carrySize += carry[index].Size;
            combined[combinedCount++] = carry[index];
        }

        for (var position = 0; position < deepRight.Prefix.Length; position++)
            combined[combinedCount++] = deepRight.Prefix.ChildAt(position);

        var nodes = new Node<T, TChild>[3];
        var nodeCount = ToNodes(combined, combinedCount, nodes);

        return new DeepTree<T, TChild>(
            deepLeft.Prefix,
            new MiddleTree<T, Node<T, TChild>>(
                Glue(deepLeft.ForceMiddle(), nodes, nodeCount, deepRight.ForceMiddle())),
            deepRight.Suffix,
            deepLeft.Size + carrySize + deepRight.Size);
    }

    /// <summary>
    /// Chunks two through nine elements into one through three nodes (the paper's <c>toTuples</c>):
    /// two-element and four-element remainders become <see cref="Node2{T, TChild}"/> chunks, everything else
    /// takes a <see cref="Node3{T, TChild}"/> first.
    /// </summary>
    /// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
    /// <typeparam name="TChild">Element type at the current level.</typeparam>
    /// <param name="source">Buffer holding the elements to chunk.</param>
    /// <param name="count">Number of elements, two through nine.</param>
    /// <param name="destination">Receives the chunked nodes.</param>
    /// <returns>The number of nodes written.</returns>
    private static int ToNodes<T, TChild>(TChild[] source, int count, Node<T, TChild>[] destination)
        where TChild : ITreeElement<T, TChild>
    {
        Debug.Assert(count is >= 2 and <= 9, "Node chunking is defined for two through nine elements.");
        var written = 0;
        var index = 0;
        while (index < count)
        {
            var remaining = count - index;
            if (remaining is 2 or 4)
            {
                destination[written++] = new Node2<T, TChild>(source[index], source[index + 1]);
                index += 2;
            }
            else
            {
                destination[written++] = new Node3<T, TChild>(source[index], source[index + 1], source[index + 2]);
                index += 3;
            }
        }

        return written;
    }
}
