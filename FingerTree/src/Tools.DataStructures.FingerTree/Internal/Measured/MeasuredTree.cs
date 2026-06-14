namespace Tools.DataStructures.FingerTree;

/// <summary>
/// One level of a strict, general measured finger tree: empty, a single element, or a deep node with a
/// one-through-four element prefix, a middle tree of grouping nodes, and a one-through-four element suffix.
/// </summary>
/// <typeparam name="TElement">Stored leaf element type, threaded through every level for flattening.</typeparam>
/// <typeparam name="TChild">Element type at this level; a leaf wrapper outermost, a node deeper.</typeparam>
/// <typeparam name="TMeasure">Measure (annotation) type.</typeparam>
/// <typeparam name="TMonoid">Static monoid provider used to combine measures.</typeparam>
/// <remarks>
/// <para>
/// This follows Hinze and Paterson's general 2-3 finger tree (digits one-through-four, nodes two-or-three),
/// the structure behind Haskell's <c>Data.FingerTree</c>. The headline operation is
/// <see cref="SplitTree"/> — splitting at the point where a monotone predicate over the accumulated measure
/// first becomes true — which specializes to indexing, priority extraction, and ordered search depending on
/// the chosen measure.
/// </para>
/// <para>
/// The middle subtree of each deep node is held behind a memoized suspension
/// (<see cref="MeasuredMiddle{TElement, TNode, TMeasure, TMonoid}"/>), and each deep node's combined measure is
/// itself lazily memoized. This is Hinze and Paterson's lazy finger tree realized in a strict language: spine
/// repairs are deferred and paid once even when an old version is reused, so the amortized bounds
/// (O(1) endpoints, O(log(min)) concatenation, O(log) split) hold under fully persistent (branching) histories,
/// not merely ephemeral linear use. Because a general monoid has no inverse, a popped subtree's measure cannot
/// be recovered by subtraction (the trick <see cref="FingerTreeDeque{T}"/> uses for its size measure), so
/// <see cref="Measure"/> is O(1) amortized but may force a chain of suspensions O(log n) deep on the first read
/// of a fresh tree; <see cref="First"/> and <see cref="Last"/> read the prefix/suffix digits directly and stay
/// O(1) worst-case, never forcing.
/// </para>
/// </remarks>
internal abstract class MeasuredTree<TElement, TChild, TMeasure, TMonoid>
    where TChild : IMeasuredElement<TElement, TMeasure>
    where TMonoid : IMonoid<TMeasure>
{
    /// <summary>Gets the empty tree at this level.</summary>
    public static MeasuredTree<TElement, TChild, TMeasure, TMonoid> Empty =>
        EmptyMeasuredTree<TElement, TChild, TMeasure, TMonoid>.Instance;

    /// <summary>Gets the empty middle tree (one level down).</summary>
    private protected static MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> EmptyMiddle =>
        EmptyMeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>.Instance;

    /// <summary>Gets the measure of this tree. O(1) amortized; the first read of a deep node may force an
    /// O(log n) chain of suspended spine repairs.</summary>
    public abstract TMeasure Measure { get; }

    /// <summary>Gets a value indicating whether this tree has no elements. O(1).</summary>
    public abstract bool IsEmpty { get; }

    /// <summary>Gets the first element. Requires a non-empty tree. O(1).</summary>
    public abstract TChild First { get; }

    /// <summary>Gets the last element. Requires a non-empty tree. O(1).</summary>
    public abstract TChild Last { get; }

    /// <summary>Returns a tree with <paramref name="value"/> prepended. O(log n) worst, O(1) amortized.</summary>
    /// <param name="value">Element to prepend.</param>
    public abstract MeasuredTree<TElement, TChild, TMeasure, TMonoid> Cons(TChild value);

    /// <summary>Returns a tree with <paramref name="value"/> appended. O(log n) worst, O(1) amortized.</summary>
    /// <param name="value">Element to append.</param>
    public abstract MeasuredTree<TElement, TChild, TMeasure, TMonoid> Snoc(TChild value);

    /// <summary>Removes the first element. O(log n) worst, O(1) amortized.</summary>
    /// <param name="head">The removed first element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public abstract bool TryViewLeft(out TChild head, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest);

    /// <summary>Removes the last element. O(log n) worst, O(1) amortized.</summary>
    /// <param name="last">The removed last element when present; otherwise <see langword="default"/>.</param>
    /// <param name="rest">The remaining tree.</param>
    /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
    public abstract bool TryViewRight(out TChild last, out MeasuredTree<TElement, TChild, TMeasure, TMonoid> rest);

    /// <summary>Appends every stored leaf element, in order, to <paramref name="sink"/>. O(n).</summary>
    /// <param name="sink">Destination collection.</param>
    public abstract void Flatten(ICollection<TElement> sink);

    /// <summary>
    /// Splits a non-empty tree at the element where <paramref name="predicate"/>, applied to the measure
    /// accumulated from the left (starting at <paramref name="accumulator"/>), first becomes true.
    /// </summary>
    /// <param name="predicate">Monotone predicate (false then true) over the accumulated measure.</param>
    /// <param name="accumulator">Measure of everything to the left of this tree.</param>
    /// <returns>The elements before the hit, the hit element, and the elements after it.</returns>
    public abstract (MeasuredTree<TElement, TChild, TMeasure, TMonoid> Left, TChild Hit, MeasuredTree<TElement, TChild, TMeasure, TMonoid> Right)
        SplitTree(Func<TMeasure, bool> predicate, TMeasure accumulator);

    /// <summary>
    /// Locates, without reconstructing any subtree, the element where <paramref name="predicate"/> over the
    /// left-accumulated measure (starting at <paramref name="accumulator"/>) first becomes true, and the
    /// measure accumulated strictly before it. A read-only downward walk: the allocation-free analogue of
    /// <see cref="SplitTree"/> for queries that need only the boundary element or the measure preceding it.
    /// </summary>
    /// <typeparam name="TPredicate">A value-type monotone predicate; passed by the constrained generic so the
    /// descent allocates no closure.</typeparam>
    /// <param name="predicate">Monotone predicate (false then true) over the accumulated measure.</param>
    /// <param name="accumulator">Measure of everything to the left of this tree.</param>
    /// <returns>The measure accumulated before the boundary element, and the boundary element itself.</returns>
    /// <remarks>Requires a non-empty tree for which <c>predicate(accumulator ⊕ Measure)</c> holds. O(log n);
    /// allocates nothing beyond forcing already-deferred spine repairs along the descended path.</remarks>
    public abstract (TMeasure MeasureBefore, TChild Hit) LocateTree<TPredicate>(TPredicate predicate, TMeasure accumulator)
        where TPredicate : struct, IMeasurePredicate<TMeasure>;

    /// <summary>Concatenates this tree with <paramref name="right"/>. O(log min) amortized.</summary>
    /// <param name="right">Tree whose elements follow this tree's elements.</param>
    public MeasuredTree<TElement, TChild, TMeasure, TMonoid> Concat(MeasuredTree<TElement, TChild, TMeasure, TMonoid> right) =>
        Glue([], right);

    /// <summary>
    /// Concatenates this tree, the bridging elements <paramref name="mid"/>, and <paramref name="right"/>
    /// (Hinze and Paterson's <c>app3</c>). The recursion descends both middles, so its depth is bounded by
    /// the shallower operand.
    /// </summary>
    /// <param name="mid">Zero or more elements inserted between the two trees.</param>
    /// <param name="right">Right operand.</param>
    private MeasuredTree<TElement, TChild, TMeasure, TMonoid> Glue(
        TChild[] mid,
        MeasuredTree<TElement, TChild, TMeasure, TMonoid> right)
    {
        if (this is EmptyMeasuredTree<TElement, TChild, TMeasure, TMonoid>)
            return PrependAll(mid, right);
        if (right is EmptyMeasuredTree<TElement, TChild, TMeasure, TMonoid>)
            return AppendAll(this, mid);
        if (this is SingleMeasuredTree<TElement, TChild, TMeasure, TMonoid> leftSingle)
            return PrependAll(mid, right).Cons(leftSingle.Element);
        if (right is SingleMeasuredTree<TElement, TChild, TMeasure, TMonoid> rightSingle)
            return AppendAll(this, mid).Snoc(rightSingle.Element);

        var left = (DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>)this;
        var deepRight = (DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>)right;
        TChild[] combined = [.. left.Suffix, .. mid, .. deepRight.Prefix];
        var bridge = Nodes(combined);
        return Deep(left.Prefix, left.ForceMiddle().Glue(bridge, deepRight.ForceMiddle()), deepRight.Suffix);
    }

    private static MeasuredTree<TElement, TChild, TMeasure, TMonoid> PrependAll(
        TChild[] values,
        MeasuredTree<TElement, TChild, TMeasure, TMonoid> tree)
    {
        for (var i = values.Length - 1; i >= 0; i--)
            tree = tree.Cons(values[i]);
        return tree;
    }

    private static MeasuredTree<TElement, TChild, TMeasure, TMonoid> AppendAll(
        MeasuredTree<TElement, TChild, TMeasure, TMonoid> tree,
        TChild[] values)
    {
        foreach (var value in values)
            tree = tree.Snoc(value);
        return tree;
    }

    /// <summary>Combines the measures of a non-empty element buffer. O(buffer length).</summary>
    /// <param name="values">One or more elements.</param>
    private protected static TMeasure CombineAll(TChild[] values)
    {
        var measure = values[0].Measure;
        for (var i = 1; i < values.Length; i++)
            measure = TMonoid.Combine(measure, values[i].Measure);
        return measure;
    }

    /// <summary>
    /// Splits a one-through-four element buffer at the element where <paramref name="predicate"/> over the
    /// left-accumulated measure first becomes true.
    /// </summary>
    /// <param name="predicate">Monotone predicate.</param>
    /// <param name="accumulator">Measure to the left of the buffer.</param>
    /// <param name="values">The buffer to split.</param>
    private protected static (TChild[] Before, TChild Hit, TChild[] After) SplitBuffer(
        Func<TMeasure, bool> predicate,
        TMeasure accumulator,
        TChild[] values)
    {
        for (var i = 0; i < values.Length - 1; i++)
        {
            accumulator = TMonoid.Combine(accumulator, values[i].Measure);
            if (predicate(accumulator))
                return (values[..i], values[i], values[(i + 1)..]);
        }

        return (values[..^1], values[^1], []);
    }

    /// <summary>
    /// Locates the element of a one-through-four element buffer where <paramref name="predicate"/> over the
    /// left-accumulated measure first becomes true, returning the measure before it and the element — without
    /// allocating the before/after sub-buffers that <see cref="SplitBuffer"/> produces.
    /// </summary>
    /// <param name="predicate">Monotone predicate.</param>
    /// <param name="accumulator">Measure to the left of the buffer.</param>
    /// <param name="values">The buffer to search; the last element is the hit by elimination.</param>
    private protected static (TMeasure MeasureBefore, TChild Hit) LocateBuffer<TPredicate>(
        TPredicate predicate,
        TMeasure accumulator,
        TChild[] values)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        for (var i = 0; i < values.Length - 1; i++)
        {
            var next = TMonoid.Combine(accumulator, values[i].Measure);
            if (predicate.Invoke(next))
                return (accumulator, values[i]);
            accumulator = next;
        }

        return (accumulator, values[^1]);
    }

    /// <summary>Groups a buffer of two or more elements into two-or-three-child nodes.</summary>
    /// <param name="values">Two or more elements.</param>
    private protected static MeasuredNode<TElement, TChild, TMeasure, TMonoid>[] Nodes(TChild[] values)
    {
        var nodes = new List<MeasuredNode<TElement, TChild, TMeasure, TMonoid>>();
        var i = 0;
        while (values.Length - i > 4)
        {
            nodes.Add(new([values[i], values[i + 1], values[i + 2]]));
            i += 3;
        }

        switch (values.Length - i)
        {
            case 2:
                nodes.Add(new([values[i], values[i + 1]]));
                break;
            case 3:
                nodes.Add(new([values[i], values[i + 1], values[i + 2]]));
                break;
            default: // 4
                nodes.Add(new([values[i], values[i + 1]]));
                nodes.Add(new([values[i + 2], values[i + 3]]));
                break;
        }

        return [.. nodes];
    }

    /// <summary>Builds a tree from a zero-through-four element buffer.</summary>
    /// <param name="values">Zero through four elements.</param>
    private protected static MeasuredTree<TElement, TChild, TMeasure, TMonoid> FromBuffer(TChild[] values) =>
        values.Length switch
        {
            0 => Empty,
            1 => Single(values[0]),
            _ => Deep(values[..1], EmptyMiddle, values[1..]),
        };

    /// <summary>
    /// Builds a deep tree whose prefix may be empty, pulling the first middle node up when it is
    /// (Hinze and Paterson's <c>deepL</c>).
    /// </summary>
    /// <param name="prefix">Zero through four prefix elements.</param>
    /// <param name="middle">Middle tree of nodes.</param>
    /// <param name="suffix">One through four suffix elements.</param>
    private protected static MeasuredTree<TElement, TChild, TMeasure, TMonoid> DeepLeft(
        TChild[] prefix,
        MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> middle,
        TChild[] suffix)
    {
        if (prefix.Length > 0)
            return Deep(prefix, middle, suffix);
        if (middle.TryViewLeft(out var node, out var rest))
            return Deep(node.Children, rest, suffix);
        return FromBuffer(suffix);
    }

    /// <summary>
    /// Builds a deep tree whose suffix may be empty, pulling the last middle node up when it is
    /// (Hinze and Paterson's <c>deepR</c>).
    /// </summary>
    /// <param name="prefix">One through four prefix elements.</param>
    /// <param name="middle">Middle tree of nodes.</param>
    /// <param name="suffix">Zero through four suffix elements.</param>
    private protected static MeasuredTree<TElement, TChild, TMeasure, TMonoid> DeepRight(
        TChild[] prefix,
        MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> middle,
        TChild[] suffix)
    {
        if (suffix.Length > 0)
            return Deep(prefix, middle, suffix);
        if (middle.TryViewRight(out var node, out var rest))
            return Deep(prefix, rest, node.Children);
        return FromBuffer(prefix);
    }

    private protected static MeasuredTree<TElement, TChild, TMeasure, TMonoid> Single(TChild value) =>
        new SingleMeasuredTree<TElement, TChild, TMeasure, TMonoid>(value);

    private protected static MeasuredTree<TElement, TChild, TMeasure, TMonoid> Deep(
        TChild[] prefix,
        MeasuredTree<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid> middle,
        TChild[] suffix) =>
        new DeepMeasuredTree<TElement, TChild, TMeasure, TMonoid>(
            prefix,
            new MeasuredMiddle<TElement, MeasuredNode<TElement, TChild, TMeasure, TMonoid>, TMeasure, TMonoid>(middle),
            suffix);
}
