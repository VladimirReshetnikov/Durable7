using System.Diagnostics;

namespace Tools.DataStructures.FingerTree;

/// <summary>
/// One level of the reversible finger tree: empty, single, or deep. Every operation is written in terms of
/// the <em>logical</em> (orientation-aware) prefix/middle/suffix, so a reversed subtree behaves correctly
/// without being materialized, and <see cref="Mirror"/> is O(1).
/// </summary>
/// <typeparam name="T">Stored leaf element type.</typeparam>
/// <remarks>
/// Size-measured simplified finger tree (one-through-three digits, two-or-three nodes), strict (no lazy
/// spine). Endpoint reads are O(1); push/pop are O(log n) worst, O(1) amortized for linear use; indexing and
/// splitting are O(log n); concatenation is O(log(min)). Reversal is O(1) and all bounds — including
/// mixed-orientation concatenation — are preserved, at a constant-factor cost relative to
/// <see cref="FingerTreeDeque{T}"/> (a reversal-bit branch and small logical-array copies on reversed paths).
/// </remarks>
internal abstract class RevTree<T>
{
    /// <summary>Gets the total leaf count. O(1).</summary>
    public abstract int Size { get; }

    /// <summary>Gets a value indicating whether this level is empty. O(1).</summary>
    public abstract bool IsEmpty { get; }

    /// <summary>Gets the logical first leaf. O(1). Requires a non-empty tree.</summary>
    public abstract T First { get; }

    /// <summary>Gets the logical last leaf. O(1). Requires a non-empty tree.</summary>
    public abstract T Last { get; }

    /// <summary>Returns a reversed view. O(1).</summary>
    public abstract RevTree<T> Mirror();

    /// <summary>Prepends an element (logical front). O(log n) worst, O(1) amortized.</summary>
    /// <param name="value">Element to prepend.</param>
    public abstract RevTree<T> Cons(RevElem<T> value);

    /// <summary>Appends an element (logical back). O(log n) worst, O(1) amortized.</summary>
    /// <param name="value">Element to append.</param>
    public abstract RevTree<T> Snoc(RevElem<T> value);

    /// <summary>Removes the logical first element. O(log n) worst, O(1) amortized.</summary>
    /// <param name="head">The removed element.</param>
    /// <param name="rest">The remaining tree.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public abstract bool ViewL(out RevElem<T> head, out RevTree<T> rest);

    /// <summary>Removes the logical last element. O(log n) worst, O(1) amortized.</summary>
    /// <param name="last">The removed element.</param>
    /// <param name="rest">The remaining tree.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public abstract bool ViewR(out RevElem<T> last, out RevTree<T> rest);

    /// <summary>Reads the logical leaf at <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Tree-relative logical leaf index.</param>
    public abstract T GetLeaf(int index);

    /// <summary>Replaces the logical leaf at <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Tree-relative logical leaf index.</param>
    /// <param name="value">Replacement value.</param>
    public abstract RevTree<T> SetLeaf(int index, T value);

    /// <summary>Splits at the element containing logical leaf <paramref name="index"/>. O(log n).</summary>
    /// <param name="index">Tree-relative logical leaf index.</param>
    /// <param name="left">Elements strictly before the hit element.</param>
    /// <param name="hit">The element containing the indexed leaf.</param>
    /// <param name="indexInHit">Leaf index relative to <paramref name="hit"/>.</param>
    /// <param name="right">Elements strictly after the hit element.</param>
    public abstract void SplitTree(
        int index,
        out RevTree<T> left,
        out RevElem<T> hit,
        out int indexInHit,
        out RevTree<T> right);

    /// <summary>Appends every leaf in logical order to <paramref name="array"/> from <paramref name="offset"/>.</summary>
    /// <param name="array">Destination array.</param>
    /// <param name="offset">Running destination index.</param>
    public abstract void CopyLeaves(T[] array, ref int offset);

    /// <summary>Recomputes the leaf count, validating cached measures and digit lengths. Test-only; O(n).</summary>
    public abstract int ValidateAndCount();

    /// <summary>Gets the deep-level depth. Test-only; O(log n).</summary>
    public abstract int Depth { get; }
}

/// <summary>The empty reversible level.</summary>
/// <typeparam name="T">Stored leaf element type.</typeparam>
internal sealed class RevEmptyTree<T> : RevTree<T>
{
    /// <summary>Gets the canonical empty tree.</summary>
    public static readonly RevEmptyTree<T> Instance = new();

    private RevEmptyTree()
    {
    }

    /// <inheritdoc/>
    public override int Size => 0;

    /// <inheritdoc/>
    public override bool IsEmpty => true;

    /// <inheritdoc/>
    public override T First => throw Empty();

    /// <inheritdoc/>
    public override T Last => throw Empty();

    /// <inheritdoc/>
    public override RevTree<T> Mirror() => this;

    /// <inheritdoc/>
    public override RevTree<T> Cons(RevElem<T> value) => new RevSingleTree<T>(value);

    /// <inheritdoc/>
    public override RevTree<T> Snoc(RevElem<T> value) => new RevSingleTree<T>(value);

    /// <inheritdoc/>
    public override bool ViewL(out RevElem<T> head, out RevTree<T> rest)
    {
        head = null!;
        rest = this;
        return false;
    }

    /// <inheritdoc/>
    public override bool ViewR(out RevElem<T> last, out RevTree<T> rest)
    {
        last = null!;
        rest = this;
        return false;
    }

    /// <inheritdoc/>
    public override T GetLeaf(int index) => throw Empty();

    /// <inheritdoc/>
    public override RevTree<T> SetLeaf(int index, T value) => throw Empty();

    /// <inheritdoc/>
    public override void SplitTree(int index, out RevTree<T> left, out RevElem<T> hit, out int indexInHit, out RevTree<T> right) =>
        throw Empty();

    /// <inheritdoc/>
    public override void CopyLeaves(T[] array, ref int offset)
    {
    }

    /// <inheritdoc/>
    public override int ValidateAndCount() => 0;

    /// <inheritdoc/>
    public override int Depth => 0;

    private static InvalidOperationException Empty() => new("Internal invariant violation: access on an empty reversible level.");
}

/// <summary>A one-element reversible level.</summary>
/// <typeparam name="T">Stored leaf element type.</typeparam>
/// <param name="element">The single element.</param>
internal sealed class RevSingleTree<T>(RevElem<T> element) : RevTree<T>
{
    /// <summary>The single element.</summary>
    public readonly RevElem<T> Element = element;

    /// <inheritdoc/>
    public override int Size => Element.Size;

    /// <inheritdoc/>
    public override bool IsEmpty => false;

    /// <inheritdoc/>
    public override T First => Element.First;

    /// <inheritdoc/>
    public override T Last => Element.Last;

    /// <inheritdoc/>
    public override RevTree<T> Mirror() => new RevSingleTree<T>(Element.Mirror());

    /// <inheritdoc/>
    public override RevTree<T> Cons(RevElem<T> value) => RevTreeOps.MakeDeep([value], RevEmptyTree<T>.Instance, [Element]);

    /// <inheritdoc/>
    public override RevTree<T> Snoc(RevElem<T> value) => RevTreeOps.MakeDeep([Element], RevEmptyTree<T>.Instance, [value]);

    /// <inheritdoc/>
    public override bool ViewL(out RevElem<T> head, out RevTree<T> rest)
    {
        head = Element;
        rest = RevEmptyTree<T>.Instance;
        return true;
    }

    /// <inheritdoc/>
    public override bool ViewR(out RevElem<T> last, out RevTree<T> rest)
    {
        last = Element;
        rest = RevEmptyTree<T>.Instance;
        return true;
    }

    /// <inheritdoc/>
    public override T GetLeaf(int index) => Element.GetLeaf(index);

    /// <inheritdoc/>
    public override RevTree<T> SetLeaf(int index, T value) => new RevSingleTree<T>(Element.SetLeaf(index, value));

    /// <inheritdoc/>
    public override void SplitTree(int index, out RevTree<T> left, out RevElem<T> hit, out int indexInHit, out RevTree<T> right)
    {
        left = RevEmptyTree<T>.Instance;
        hit = Element;
        indexInHit = index;
        right = RevEmptyTree<T>.Instance;
    }

    /// <inheritdoc/>
    public override void CopyLeaves(T[] array, ref int offset) => Element.CopyLeaves(array, ref offset);

    /// <inheritdoc/>
    public override int ValidateAndCount() => Element.ValidateAndCount();

    /// <inheritdoc/>
    public override int Depth => 0;
}

/// <summary>A deep reversible level with a reversal bit and a cached total leaf count.</summary>
/// <typeparam name="T">Stored leaf element type.</typeparam>
internal sealed class RevDeepTree<T> : RevTree<T>
{
    private readonly RevElem<T>[] _prefix; // forward, length 1-3
    private readonly RevTree<T> _middle;
    private readonly RevElem<T>[] _suffix; // forward, length 1-3
    private readonly int _size;
    private readonly bool _reversed;

    /// <summary>Creates a forward deep level with an explicit total size.</summary>
    /// <param name="prefix">Prefix digit (one through three elements).</param>
    /// <param name="middle">Middle tree of nodes.</param>
    /// <param name="suffix">Suffix digit (one through three elements).</param>
    /// <param name="size">Total leaf count.</param>
    public RevDeepTree(RevElem<T>[] prefix, RevTree<T> middle, RevElem<T>[] suffix, int size)
    {
        Debug.Assert(prefix.Length is >= 1 and <= 3, "Prefix must hold one through three elements.");
        Debug.Assert(suffix.Length is >= 1 and <= 3, "Suffix must hold one through three elements.");
        _prefix = prefix;
        _middle = middle;
        _suffix = suffix;
        _size = size;
        _reversed = false;
    }

    private RevDeepTree(RevElem<T>[] prefix, RevTree<T> middle, RevElem<T>[] suffix, int size, bool reversed)
    {
        _prefix = prefix;
        _middle = middle;
        _suffix = suffix;
        _size = size;
        _reversed = reversed;
    }

    /// <inheritdoc/>
    public override int Size => _size;

    /// <inheritdoc/>
    public override bool IsEmpty => false;

    /// <inheritdoc/>
    public override T First => _reversed ? _suffix[^1].Last : _prefix[0].First;

    /// <inheritdoc/>
    public override T Last => _reversed ? _prefix[0].First : _suffix[^1].Last;

    /// <summary>Returns the prefix digit in logical order. O(digit length).</summary>
    public RevElem<T>[] LogicalPrefix() => _reversed ? RevTreeOps.MirrorReversed(_suffix) : _prefix;

    /// <summary>Returns the suffix digit in logical order. O(digit length).</summary>
    public RevElem<T>[] LogicalSuffix() => _reversed ? RevTreeOps.MirrorReversed(_prefix) : _suffix;

    /// <summary>Returns the middle tree in logical orientation. O(1).</summary>
    public RevTree<T> LogicalMiddle() => _reversed ? _middle.Mirror() : _middle;

    /// <inheritdoc/>
    public override RevTree<T> Mirror() => new RevDeepTree<T>(_prefix, _middle, _suffix, _size, !_reversed);

    /// <inheritdoc/>
    public override RevTree<T> Cons(RevElem<T> value)
    {
        var prefix = LogicalPrefix();
        if (prefix.Length < 3)
            return RevTreeOps.MakeDeep([value, .. prefix], LogicalMiddle(), LogicalSuffix());

        var pushed = new RevNode<T>([prefix[1], prefix[2]]);
        return RevTreeOps.MakeDeep([value, prefix[0]], LogicalMiddle().Cons(pushed), LogicalSuffix());
    }

    /// <inheritdoc/>
    public override RevTree<T> Snoc(RevElem<T> value)
    {
        var suffix = LogicalSuffix();
        if (suffix.Length < 3)
            return RevTreeOps.MakeDeep(LogicalPrefix(), LogicalMiddle(), [.. suffix, value]);

        var pushed = new RevNode<T>([suffix[0], suffix[1]]);
        return RevTreeOps.MakeDeep(LogicalPrefix(), LogicalMiddle().Snoc(pushed), [suffix[2], value]);
    }

    /// <inheritdoc/>
    public override bool ViewL(out RevElem<T> head, out RevTree<T> rest)
    {
        var prefix = LogicalPrefix();
        head = prefix[0];
        rest = prefix.Length > 1
            ? RevTreeOps.MakeDeep(prefix[1..], LogicalMiddle(), LogicalSuffix())
            : RevTreeOps.DeepL([], LogicalMiddle(), LogicalSuffix());
        return true;
    }

    /// <inheritdoc/>
    public override bool ViewR(out RevElem<T> last, out RevTree<T> rest)
    {
        var suffix = LogicalSuffix();
        last = suffix[^1];
        rest = suffix.Length > 1
            ? RevTreeOps.MakeDeep(LogicalPrefix(), LogicalMiddle(), suffix[..^1])
            : RevTreeOps.DeepR(LogicalPrefix(), LogicalMiddle(), []);
        return true;
    }

    /// <inheritdoc/>
    public override T GetLeaf(int index)
    {
        var prefix = LogicalPrefix();
        var prefixSize = RevTreeOps.SumSizes(prefix);
        if (index < prefixSize)
            return RevTreeOps.GetInDigit(prefix, index);

        index -= prefixSize;
        var middle = LogicalMiddle();
        if (index < middle.Size)
            return middle.GetLeaf(index);

        index -= middle.Size;
        return RevTreeOps.GetInDigit(LogicalSuffix(), index);
    }

    /// <inheritdoc/>
    public override RevTree<T> SetLeaf(int index, T value)
    {
        var prefix = LogicalPrefix();
        var prefixSize = RevTreeOps.SumSizes(prefix);
        if (index < prefixSize)
            return RevTreeOps.MakeDeep(RevTreeOps.SetInDigit(prefix, index, value), LogicalMiddle(), LogicalSuffix());

        index -= prefixSize;
        var middle = LogicalMiddle();
        if (index < middle.Size)
            return RevTreeOps.MakeDeep(prefix, middle.SetLeaf(index, value), LogicalSuffix());

        index -= middle.Size;
        return RevTreeOps.MakeDeep(prefix, middle, RevTreeOps.SetInDigit(LogicalSuffix(), index, value));
    }

    /// <inheritdoc/>
    public override void SplitTree(int index, out RevTree<T> left, out RevElem<T> hit, out int indexInHit, out RevTree<T> right)
    {
        var prefix = LogicalPrefix();
        var prefixSize = RevTreeOps.SumSizes(prefix);
        if (index < prefixSize)
        {
            RevTreeOps.SplitDigit(prefix, index, out var before, out hit, out indexInHit, out var after);
            left = RevTreeOps.FromDigit(before);
            right = RevTreeOps.DeepL(after, LogicalMiddle(), LogicalSuffix());
            return;
        }

        index -= prefixSize;
        var middle = LogicalMiddle();
        if (index < middle.Size)
        {
            middle.SplitTree(index, out var middleLeft, out var node, out var indexInNode, out var middleRight);
            var children = ((RevNode<T>)node).LogicalChildren();
            RevTreeOps.SplitDigit(children, indexInNode, out var before, out hit, out indexInHit, out var after);
            left = RevTreeOps.DeepR(LogicalPrefix(), middleLeft, before);
            right = RevTreeOps.DeepL(after, middleRight, LogicalSuffix());
            return;
        }

        index -= middle.Size;
        var suffix = LogicalSuffix();
        RevTreeOps.SplitDigit(suffix, index, out var suffixBefore, out hit, out indexInHit, out var suffixAfter);
        left = RevTreeOps.DeepR(LogicalPrefix(), middle, suffixBefore);
        right = RevTreeOps.FromDigit(suffixAfter);
    }

    /// <inheritdoc/>
    public override void CopyLeaves(T[] array, ref int offset)
    {
        var prefix = LogicalPrefix();
        foreach (var element in prefix)
            element.CopyLeaves(array, ref offset);
        LogicalMiddle().CopyLeaves(array, ref offset);
        foreach (var element in LogicalSuffix())
            element.CopyLeaves(array, ref offset);
    }

    /// <inheritdoc/>
    public override int ValidateAndCount()
    {
        var prefix = LogicalPrefix();
        var suffix = LogicalSuffix();
        if (prefix.Length is < 1 or > 3)
            throw new InvalidOperationException($"Prefix length {prefix.Length} is outside 1..3.");
        if (suffix.Length is < 1 or > 3)
            throw new InvalidOperationException($"Suffix length {suffix.Length} is outside 1..3.");

        var computed = 0;
        foreach (var element in prefix)
            computed += element.ValidateAndCount();
        computed += LogicalMiddle().ValidateAndCount();
        foreach (var element in suffix)
            computed += element.ValidateAndCount();
        if (computed != _size)
            throw new InvalidOperationException($"Deep cached size {_size} disagrees with recomputed total {computed}.");
        return computed;
    }

    /// <inheritdoc/>
    public override int Depth => 1 + LogicalMiddle().Depth;
}

/// <summary>Level-spanning helpers for the reversible tree: digit operations, smart constructors, and glue.</summary>
internal static class RevTreeOps
{
    /// <summary>Sums the leaf counts of a digit. O(digit length).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Digit elements.</param>
    public static int SumSizes<T>(RevElem<T>[] elements)
    {
        var size = 0;
        foreach (var element in elements)
            size += element.Size;
        return size;
    }

    /// <summary>Returns a digit reversed in order with each element mirrored. O(digit length).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Digit to mirror.</param>
    public static RevElem<T>[] MirrorReversed<T>(RevElem<T>[] elements)
    {
        var n = elements.Length;
        var result = new RevElem<T>[n];
        for (var i = 0; i < n; i++)
            result[i] = elements[n - 1 - i].Mirror();
        return result;
    }

    /// <summary>Builds a forward deep tree, computing its size from the parts. O(1).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="prefix">Prefix digit (one through three elements).</param>
    /// <param name="middle">Middle tree.</param>
    /// <param name="suffix">Suffix digit (one through three elements).</param>
    public static RevTree<T> MakeDeep<T>(RevElem<T>[] prefix, RevTree<T> middle, RevElem<T>[] suffix) =>
        new RevDeepTree<T>(prefix, middle, suffix, SumSizes(prefix) + middle.Size + SumSizes(suffix));

    /// <summary>Builds a tree from a one-through-three element digit. O(1).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Digit elements.</param>
    public static RevTree<T> FromDigit<T>(RevElem<T>[] elements) =>
        elements.Length switch
        {
            0 => RevEmptyTree<T>.Instance,
            1 => new RevSingleTree<T>(elements[0]),
            _ => MakeDeep(elements[..1], RevEmptyTree<T>.Instance, elements[1..]),
        };

    /// <summary>Builds a deep tree whose prefix may be empty, pulling from the middle when it is.</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="prefix">Possibly empty prefix digit (zero through three elements).</param>
    /// <param name="middle">Middle tree.</param>
    /// <param name="suffix">Suffix digit (one through three elements).</param>
    public static RevTree<T> DeepL<T>(RevElem<T>[] prefix, RevTree<T> middle, RevElem<T>[] suffix)
    {
        if (prefix.Length > 0)
            return MakeDeep(prefix, middle, suffix);
        if (middle.ViewL(out var node, out var rest))
            return MakeDeep(((RevNode<T>)node).LogicalChildren(), rest, suffix);
        return FromDigit(suffix);
    }

    /// <summary>Builds a deep tree whose suffix may be empty, pulling from the middle when it is.</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="prefix">Prefix digit (one through three elements).</param>
    /// <param name="middle">Middle tree.</param>
    /// <param name="suffix">Possibly empty suffix digit (zero through three elements).</param>
    public static RevTree<T> DeepR<T>(RevElem<T>[] prefix, RevTree<T> middle, RevElem<T>[] suffix)
    {
        if (suffix.Length > 0)
            return MakeDeep(prefix, middle, suffix);
        if (middle.ViewR(out var node, out var rest))
            return MakeDeep(prefix, rest, ((RevNode<T>)node).LogicalChildren());
        return FromDigit(prefix);
    }

    /// <summary>Reads the leaf at a digit-relative index. O(digit length + height).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Digit elements.</param>
    /// <param name="index">Digit-relative leaf index.</param>
    public static T GetInDigit<T>(RevElem<T>[] elements, int index)
    {
        for (var i = 0; ; i++)
        {
            if (index < elements[i].Size)
                return elements[i].GetLeaf(index);
            index -= elements[i].Size;
        }
    }

    /// <summary>Returns a digit with the leaf at a digit-relative index replaced. O(digit length + height).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Digit elements.</param>
    /// <param name="index">Digit-relative leaf index.</param>
    /// <param name="value">Replacement value.</param>
    public static RevElem<T>[] SetInDigit<T>(RevElem<T>[] elements, int index, T value)
    {
        var result = (RevElem<T>[])elements.Clone();
        for (var i = 0; ; i++)
        {
            if (index < result[i].Size)
            {
                result[i] = result[i].SetLeaf(index, value);
                return result;
            }

            index -= result[i].Size;
        }
    }

    /// <summary>Splits a digit at a digit-relative leaf index. O(digit length).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Digit elements.</param>
    /// <param name="index">Digit-relative leaf index.</param>
    /// <param name="before">Elements strictly before the hit.</param>
    /// <param name="hit">The element containing the indexed leaf.</param>
    /// <param name="indexInHit">Leaf index relative to <paramref name="hit"/>.</param>
    /// <param name="after">Elements strictly after the hit.</param>
    public static void SplitDigit<T>(
        RevElem<T>[] elements,
        int index,
        out RevElem<T>[] before,
        out RevElem<T> hit,
        out int indexInHit,
        out RevElem<T>[] after)
    {
        for (var i = 0; i < elements.Length - 1; i++)
        {
            if (index < elements[i].Size)
            {
                before = elements[..i];
                hit = elements[i];
                indexInHit = index;
                after = elements[(i + 1)..];
                return;
            }

            index -= elements[i].Size;
        }

        before = elements[..^1];
        hit = elements[^1];
        indexInHit = index;
        after = [];
    }

    /// <summary>Chunks two or more elements into two- or three-child nodes. O(count).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="elements">Elements to chunk.</param>
    public static RevElem<T>[] Nodes<T>(RevElem<T>[] elements)
    {
        var nodes = new List<RevElem<T>>();
        var i = 0;
        while (elements.Length - i > 4)
        {
            nodes.Add(new RevNode<T>([elements[i], elements[i + 1], elements[i + 2]]));
            i += 3;
        }

        switch (elements.Length - i)
        {
            case 2:
                nodes.Add(new RevNode<T>([elements[i], elements[i + 1]]));
                break;
            case 3:
                nodes.Add(new RevNode<T>([elements[i], elements[i + 1], elements[i + 2]]));
                break;
            default: // 4
                nodes.Add(new RevNode<T>([elements[i], elements[i + 1]]));
                nodes.Add(new RevNode<T>([elements[i + 2], elements[i + 3]]));
                break;
        }

        return [.. nodes];
    }

    /// <summary>Concatenates two trees. O(log(min)) including mixed orientations (logical accessors handle reversal).</summary>
    /// <typeparam name="T">Stored leaf element type.</typeparam>
    /// <param name="left">Left operand.</param>
    /// <param name="right">Right operand.</param>
    public static RevTree<T> Concat<T>(RevTree<T> left, RevTree<T> right) => Glue(left, [], right);

    private static RevTree<T> Glue<T>(RevTree<T> left, RevElem<T>[] mid, RevTree<T> right)
    {
        if (left is RevEmptyTree<T>)
        {
            for (var i = mid.Length - 1; i >= 0; i--)
                right = right.Cons(mid[i]);
            return right;
        }

        if (right is RevEmptyTree<T>)
        {
            foreach (var element in mid)
                left = left.Snoc(element);
            return left;
        }

        if (left is RevSingleTree<T> leftSingle)
        {
            for (var i = mid.Length - 1; i >= 0; i--)
                right = right.Cons(mid[i]);
            return right.Cons(leftSingle.Element);
        }

        if (right is RevSingleTree<T> rightSingle)
        {
            foreach (var element in mid)
                left = left.Snoc(element);
            return left.Snoc(rightSingle.Element);
        }

        var deepLeft = (RevDeepTree<T>)left;
        var deepRight = (RevDeepTree<T>)right;
        RevElem<T>[] combined = [.. deepLeft.LogicalSuffix(), .. mid, .. deepRight.LogicalPrefix()];
        var bridge = Nodes(combined);
        return MakeDeep(deepLeft.LogicalPrefix(), Glue(deepLeft.LogicalMiddle(), bridge, deepRight.LogicalMiddle()), deepRight.LogicalSuffix());
    }
}
