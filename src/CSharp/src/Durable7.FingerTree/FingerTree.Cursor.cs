// Gap cursors over the finger tree.

using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

public sealed partial class FingerTree<TElement, TMeasure, TMeasureOps>
{
    /// <summary>Creates a measure-aware cursor before the first element. O(1).</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> GetCursorAtStart() =>
        FingerTreeCursor<TElement, TMeasure, TMeasureOps>.Create(this, Empty, this);

    /// <summary>Creates a measure-aware cursor after the final element. O(1).</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> GetCursorAtEnd() =>
        FingerTreeCursor<TElement, TMeasure, TMeasureOps>.Create(this, this, Empty);

    /// <summary>
    /// Locates the gap before the first element whose inclusive prefix satisfies a monotone predicate.
    /// A miss returns <see langword="false"/> and a usable end cursor.
    /// </summary>
    public bool TryGetCursor<TPredicate>(
        TPredicate predicate,
        out FingerTreeCursor<TElement, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var (left, right) = Split(predicate);
        cursor = FingerTreeCursor<TElement, TMeasure, TMeasureOps>.Create(this, left, right);
        return !right.IsEmpty;
    }

    /// <summary>Gets left measured, reporting whether it succeeded.</summary>
    internal bool TryViewLeftMeasured(
        out MeasuredLeaf<TElement, TMeasure, TMeasureOps> leaf,
        out FingerTree<TElement, TMeasure, TMeasureOps> rest)
    {
        if (_root.TryViewLeft(out leaf, out var remaining))
        {
            rest = Wrap(remaining);
            return true;
        }

        leaf = default;
        rest = Empty;
        return false;
    }

    /// <summary>Gets right measured, reporting whether it succeeded.</summary>
    internal bool TryViewRightMeasured(
        out MeasuredLeaf<TElement, TMeasure, TMeasureOps> leaf,
        out FingerTree<TElement, TMeasure, TMeasureOps> rest)
    {
        if (_root.TryViewRight(out leaf, out var remaining))
        {
            rest = Wrap(remaining);
            return true;
        }

        leaf = default;
        rest = Empty;
        return false;
    }

    /// <summary>Prepends to a measured tree, recomputing only the measures the change reaches.</summary>
    internal FingerTree<TElement, TMeasure, TMeasureOps> PrependMeasured(
        MeasuredLeaf<TElement, TMeasure, TMeasureOps> leaf) => new(_root.Cons(leaf));

    /// <summary>Appends to a measured tree, recomputing only the measures the change reaches.</summary>
    internal FingerTree<TElement, TMeasure, TMeasureOps> AppendMeasured(
        MeasuredLeaf<TElement, TMeasure, TMeasureOps> leaf) => new(_root.Snoc(leaf));
}

/// <summary>
/// Immutable split cursor over one exact general measured finger-tree version. It uses measures and
/// neighbors rather than fabricating an element count for arbitrary monoids.
/// </summary>
public readonly struct FingerTreeCursor<TElement, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<TElement, TMeasure>
{
    private readonly FingerTree<TElement, TMeasure, TMeasureOps>? _snapshot;
    private readonly FingerTree<TElement, TMeasure, TMeasureOps>? _left;
    private readonly FingerTree<TElement, TMeasure, TMeasureOps>? _right;

    private FingerTreeCursor(
        FingerTree<TElement, TMeasure, TMeasureOps> snapshot,
        FingerTree<TElement, TMeasure, TMeasureOps> left,
        FingerTree<TElement, TMeasure, TMeasureOps> right)
    {
        _snapshot = snapshot;
        _left = left;
        _right = right;
    }

    internal static FingerTreeCursor<TElement, TMeasure, TMeasureOps> Create(
        FingerTree<TElement, TMeasure, TMeasureOps> snapshot,
        FingerTree<TElement, TMeasure, TMeasureOps> left,
        FingerTree<TElement, TMeasure, TMeasureOps> right) => new(snapshot, left, right);

    private FingerTree<TElement, TMeasure, TMeasureOps> SnapshotValue =>
        _snapshot ?? throw UninitializedError();

    private FingerTree<TElement, TMeasure, TMeasureOps> Left
    {
        get { _ = SnapshotValue; return _left!; }
    }

    private FingerTree<TElement, TMeasure, TMeasureOps> Right
    {
        get { _ = SnapshotValue; return _right!; }
    }

    /// <summary>Gets whether no element precedes the gap.</summary>
    public bool IsAtStart => Left.IsEmpty;
    /// <summary>Gets whether no element follows the gap.</summary>
    public bool IsAtEnd => Right.IsEmpty;
    /// <summary>Gets the ordered measure of all elements before the gap.</summary>
    public TMeasure MeasureBefore => Left.Measure;
    /// <summary>Gets the ordered measure of all elements after the gap.</summary>
    public TMeasure MeasureAfter => Right.Measure;

    /// <summary>Attempts to read the element immediately before the gap.</summary>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out TElement value)
    {
        if (Left.IsEmpty) { value = default; return false; }
        value = Left.Last;
        return true;
    }

    /// <summary>Attempts to read the element immediately after the gap.</summary>
    public bool TryPeekNext([MaybeNullWhen(false)] out TElement value)
    {
        if (Right.IsEmpty) { value = default; return false; }
        value = Right.First;
        return true;
    }

    /// <summary>Moves the gap across its previous element without remeasuring that element.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> MovePrevious()
    {
        if (!Left.TryViewRightMeasured(out var leaf, out var left))
            throw new InvalidOperationException("The finger-tree cursor is already at the start.");
        return new(SnapshotValue, left, Right.PrependMeasured(leaf));
    }

    /// <summary>Moves the gap across its next element without remeasuring that element.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> MoveNext()
    {
        if (!Right.TryViewLeftMeasured(out var leaf, out var right))
            throw new InvalidOperationException("The finger-tree cursor is already at the end.");
        return new(SnapshotValue, Left.AppendMeasured(leaf), right);
    }

    /// <summary>Seeks to the gap selected by a monotone inclusive-prefix measure predicate.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> SeekByMeasure<TPredicate>(
        TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var snapshot = SnapshotValue;
        var (left, right) = snapshot.Split(predicate);
        return new(snapshot, left, right);
    }

    /// <summary>Inserts an element at the gap and returns the gap immediately after it.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> Insert(TElement value)
    {
        var left = Left.Append(value);
        var snapshot = left.Concat(Right);
        return new(snapshot, left, Right);
    }

    /// <summary>Deletes the previous element and moves the gap left.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> DeletePrevious()
    {
        if (!Left.TryViewRightMeasured(out _, out var left))
            throw new InvalidOperationException("The finger-tree cursor has no previous element.");
        var snapshot = left.Concat(Right);
        return new(snapshot, left, Right);
    }

    /// <summary>Deletes the next element and keeps the gap fixed.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> DeleteNext()
    {
        if (!Right.TryViewLeftMeasured(out _, out var right))
            throw new InvalidOperationException("The finger-tree cursor has no next element.");
        var snapshot = Left.Concat(right);
        return new(snapshot, Left, right);
    }

    /// <summary>Replaces the next element and keeps the gap fixed.</summary>
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> ReplaceNext(TElement value)
    {
        if (!Right.TryViewLeftMeasured(out _, out var rest))
            throw new InvalidOperationException("The finger-tree cursor has no next element.");
        var right = rest.Prepend(value);
        var snapshot = Left.Concat(right);
        return new(snapshot, Left, right);
    }

    /// <summary>Returns the exact persistent tree version represented by this cursor.</summary>
    public FingerTree<TElement, TMeasure, TMeasureOps> Snapshot() => SnapshotValue;

    private static InvalidOperationException UninitializedError() =>
        new("The default value is not an initialized finger-tree cursor.");
}
