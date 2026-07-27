// Represents an immutable measured sequence whose contiguous ranges can be transformed by lazily
// composed algebraic tags.

using System.Collections;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Durable7.FingerTree;

/// <summary>
/// Represents an immutable measured sequence whose contiguous ranges can be transformed by lazily
/// composed algebraic tags.
/// </summary>
/// <typeparam name="TElement">The sequence element type.</typeparam>
/// <typeparam name="TMeasure">The cached ordered-measure type.</typeparam>
/// <typeparam name="TTag">The range-update tag type.</typeparam>
/// <typeparam name="TOps">
/// The static measure and tag algebra. Its operations must obey the laws documented by
/// <see cref="IRangeUpdateAlgebra{TElement,TMeasure,TTag}"/>.
/// </typeparam>
/// <remarks>
/// <para>
/// The representation is a path-copied implicit AVL tree. Every node caches its height, element
/// count, and ordered logical measure. A pending tag has already been applied to that node's value
/// and cached measure but not to its children. Structural descent pushes tags immutably; read-only
/// descent instead carries composed tags as local state and allocates no persistent nodes.
/// </para>
/// <para>
/// Applying a nonidentity tag to the complete sequence is O(1). Indexed reads, point edits,
/// splitting, concatenation, arbitrary range updates, and arbitrary range queries are O(log n)
/// worst-case. Enumeration is O(n) with an O(log n) traversal stack. Updates leave every input
/// version usable and reuse unaffected structure. Descending through a tagged node can also allocate
/// a replacement wrapper root for an off-spine child; that child's interior remains shared.
/// </para>
/// <para>
/// Pending tags are represented by a separate presence bit; <typeparamref name="TTag"/>'s default
/// value and <see cref="IRangeUpdateAlgebra{TElement,TMeasure,TTag}.IdentityTag"/> are never used as
/// absence sentinels. When actual composition produces a tag recognized by
/// <see cref="IRangeUpdateAlgebra{TElement,TMeasure,TTag}.IsIdentity"/>, the pending marker is
/// cleared while retaining the already transformed logical value and measure. This canonical form
/// is sound precisely when the algebra's identity-recognition law holds.
/// </para>
/// <para>
/// Instances are immutable and safe for concurrent reads. User policy operations are expected to
/// be deterministic, side-effect-free, and safe for the caller's concurrency pattern. A throwing
/// policy operation publishes no partially built sequence and cannot mutate an older version.
/// </para>
/// </remarks>
/// <example>
/// <code>
/// // AddTag and SumWithAdd below are user-defined policy types implementing the documented algebra.
/// var values = RangeUpdateSequence&lt;int, long, AddTag, SumWithAdd&gt;
///     .Create([1, 2, 3, 4]);
/// var changed = values.ApplyRange(1, 2, new AddTag(10));
///
/// // values  is still [1, 2, 3, 4]
/// // changed is       [1, 12, 13, 4]
/// // changed.MeasureRange(1, 2) is 25
/// </code>
/// </example>
[DebuggerDisplay("Count = {Count}, Height = {_root == null ? 0 : _root.Height}, Measure = {Measure}")]
public sealed partial class RangeUpdateSequence<TElement, TMeasure, TTag, TOps>
    : IReadOnlyList<TElement>
    where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
{
    private static readonly TMeasure EmptyMeasure = TOps.Empty;
    private static readonly RangeUpdateSequence<TElement, TMeasure, TTag, TOps> EmptyInstance =
        new(root: null, recordAllocation: false);

    private readonly Node? _root;

    private RangeUpdateSequence(Node? root, bool recordAllocation = true)
    {
        if (recordAllocation)
            RangeUpdateSequenceDiagnostics.RecordFacadeAllocation();
        _root = root;
    }

    /// <summary>Gets the canonical empty sequence.</summary>
    /// <remarks>
    /// The empty measure is obtained from <typeparamref name="TOps"/> once per closed generic type
    /// and cached. Operations whose result is empty return this shared instance.
    /// </remarks>
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Empty => EmptyInstance;

    /// <summary>Creates a sequence containing a span's elements in order.</summary>
    /// <param name="items">The elements to store.</param>
    /// <returns>A balanced sequence containing <paramref name="items"/> in the same order.</returns>
    /// <remarks>O(n) policy calls and node allocations; the resulting AVL has minimal height.</remarks>
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Create(
        ReadOnlySpan<TElement> items)
    {
        if (items.IsEmpty)
            return EmptyInstance;
        return Wrap(BuildBalanced(items));
    }

    /// <summary>Creates a sequence by eagerly enumerating a source once.</summary>
    /// <param name="items">The elements to store in enumeration order.</param>
    /// <returns>A balanced sequence containing the enumerated elements.</returns>
    /// <remarks>
    /// O(n) time and temporary storage. Enumeration completes before element-measure callbacks
    /// begin, so a throwing source enumerator publishes no result and invokes no partial tree build.
    /// Passing a sequence of this exact closed generic type returns that immutable sequence by
    /// reference without enumeration or policy callbacks.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> CreateRange(
        IEnumerable<TElement> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items is RangeUpdateSequence<TElement, TMeasure, TTag, TOps> sequence)
            return sequence;

        var materialized = new List<TElement>();
        foreach (var item in items)
            materialized.Add(item);

        return materialized.Count == 0
            ? EmptyInstance
            : Wrap(BuildBalanced(CollectionsMarshal.AsSpan(materialized)));
    }

    /// <summary>Gets the number of elements in the sequence.</summary>
    /// <remarks>O(1); the count is cached at the root.</remarks>
    public int Count => _root?.Count ?? 0;

    /// <summary>Gets a value indicating whether the sequence contains no elements.</summary>
    /// <remarks>O(1).</remarks>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the ordered measure of the entire logical sequence.</summary>
    /// <remarks>O(1); pending range tags are already reflected in the cached root measure.</remarks>
    public TMeasure Measure => _root is null ? EmptyMeasure : _root.Measure;

    /// <summary>Gets the logical element at a zero-based index.</summary>
    /// <param name="index">The zero-based element index.</param>
    /// <returns>The element at <paramref name="index"/>, with every inherited tag applied.</returns>
    /// <remarks>
    /// O(log n) worst-case. The lookup carries pending tags down the path and allocates no persistent
    /// nodes or mutable cache.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the sequence.</exception>
    public TElement this[int index]
    {
        get
        {
            CheckElementIndex(index);
            return GetElement(_root!, index);
        }
    }

    /// <summary>Returns a sequence with one element inserted at the beginning.</summary>
    /// <param name="item">The element to prepend.</param>
    /// <returns>The updated sequence.</returns>
    /// <remarks>O(log n) worst-case and O(log n) new nodes.</remarks>
    /// <exception cref="OverflowException">The sequence already contains <see cref="int.MaxValue"/> elements.</exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Prepend(TElement item) =>
        Insert(0, item);

    /// <summary>Returns a sequence with one element inserted at the end.</summary>
    /// <param name="item">The element to append.</param>
    /// <returns>The updated sequence.</returns>
    /// <remarks>O(log n) worst-case and O(log n) new nodes.</remarks>
    /// <exception cref="OverflowException">The sequence already contains <see cref="int.MaxValue"/> elements.</exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Append(TElement item) =>
        Insert(Count, item);

    /// <summary>Returns a sequence with an element inserted at a zero-based boundary.</summary>
    /// <param name="index">The number of existing elements placed before <paramref name="item"/>.</param>
    /// <param name="item">The element to insert.</param>
    /// <returns>The updated sequence.</returns>
    /// <remarks>
    /// O(log n) worst-case and O(log n) new nodes. Tags on the traversed spine are pushed before the
    /// insertion, so updates applied to the old sequence never affect the new element.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> is not between zero and <see cref="Count"/>, inclusive.
    /// </exception>
    /// <exception cref="OverflowException">The sequence already contains <see cref="int.MaxValue"/> elements.</exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Insert(int index, TElement item)
    {
        CheckBoundaryIndex(index);
        CheckRoomForOneMore();
        return Wrap(InsertCore(_root, index, item));
    }

    /// <summary>Returns a sequence with one logical element replaced.</summary>
    /// <param name="index">The zero-based index to replace.</param>
    /// <param name="item">The replacement element.</param>
    /// <returns>The updated sequence.</returns>
    /// <remarks>
    /// O(log n) worst-case and O(log n) new nodes. The generic core has no element-equality policy,
    /// so it does not promise an identity shortcut for an equal replacement. Old range tags are
    /// pushed off the path before replacement and therefore do not transform <paramref name="item"/>.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the sequence.</exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> SetItem(int index, TElement item)
    {
        CheckElementIndex(index);
        return Wrap(SetCore(_root!, index, item));
    }

    /// <summary>Returns a sequence without the element at a zero-based index.</summary>
    /// <param name="index">The zero-based index to remove.</param>
    /// <returns>The updated sequence, or <see cref="Empty"/> when the last element was removed.</returns>
    /// <remarks>O(log n) worst-case and O(log n) new nodes.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the sequence.</exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> RemoveAt(int index)
    {
        CheckElementIndex(index);
        return Wrap(RemoveCore(_root!, index));
    }

    /// <summary>Concatenates another sequence after this sequence.</summary>
    /// <param name="other">The suffix sequence.</param>
    /// <returns>The concatenated sequence.</returns>
    /// <remarks>
    /// O(log n) worst-case in the height difference and O(log n) new nodes. Empty operands retain
    /// the nonempty operand by reference. The combined count is checked before any tag or measure
    /// policy callback.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The combined element count exceeds <see cref="int.MaxValue"/>.</exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Concat(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (other.Count > int.MaxValue - Count)
            throw CountOverflowError();
        if (IsEmpty)
            return other;
        if (other.IsEmpty)
            return this;

        var (pivot, right) = ExtractMinimum(other._root!);
        return Wrap(Join(_root, pivot, right));
    }

    /// <summary>Splits the sequence at a zero-based boundary.</summary>
    /// <param name="index">The number of elements placed in the left result.</param>
    /// <returns>The prefix before <paramref name="index"/> and the suffix beginning there.</returns>
    /// <remarks>
    /// O(log n) worst-case and O(log n) new nodes. Splitting at either endpoint returns this sequence
    /// unchanged on the corresponding side and does not push a pending root tag.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> is not between zero and <see cref="Count"/>, inclusive.
    /// </exception>
    public (
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Left,
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Right) SplitAt(int index)
    {
        CheckBoundaryIndex(index);
        if (index == 0)
            return (EmptyInstance, this);
        if (index == Count)
            return (this, EmptyInstance);

        var (left, right) = Split(_root, index);
        return (Wrap(left), Wrap(right));
    }

    /// <summary>Returns a contiguous subsequence.</summary>
    /// <param name="index">The zero-based start boundary.</param>
    /// <param name="count">The number of elements to retain.</param>
    /// <returns>The requested range in its original order.</returns>
    /// <remarks>
    /// O(log n) worst-case and O(log n) new nodes. An empty range returns <see cref="Empty"/>; the
    /// complete range returns this instance.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> or <paramref name="count"/> does not describe a range within this
    /// sequence.
    /// </exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> GetRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return EmptyInstance;
        if (count == Count)
            return this;

        var (_, tail) = Split(_root, index);
        var (middle, _) = Split(tail, count);
        return Wrap(middle);
    }

    /// <summary>Applies a tag to every element in a contiguous range.</summary>
    /// <param name="index">The zero-based start boundary.</param>
    /// <param name="count">The number of elements to transform.</param>
    /// <param name="tag">The tag to apply after all updates already represented by this version.</param>
    /// <returns>The updated sequence.</returns>
    /// <remarks>
    /// The range is validated before any policy callback. An empty range returns this instance
    /// without invoking <see cref="IRangeUpdateAlgebra{TElement,TMeasure,TTag}.IsIdentity"/>. A
    /// recognized identity also returns this instance. A nonidentity whole-sequence update is O(1)
    /// and allocates one node; any other nonempty update is O(log n) with O(log n) new nodes.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> or <paramref name="count"/> does not describe a range within this
    /// sequence.
    /// </exception>
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> ApplyRange(
        int index,
        int count,
        TTag tag)
    {
        CheckRange(index, count);
        if (count == 0)
            return this;
        if (IsIdentityTag(tag))
            return this;
        if (count == Count)
            return Wrap(ApplySubtree(_root!, tag));

        var (left, tail) = Split(_root, index);
        var (middle, right) = Split(tail, count);
        middle = ApplySubtree(middle!, tag);
        return Wrap(JoinConcatenated(JoinConcatenated(left, middle), right));
    }

    /// <summary>Returns the ordered measure of a contiguous logical range.</summary>
    /// <param name="index">The zero-based start boundary.</param>
    /// <param name="count">The number of elements to measure.</param>
    /// <returns>The requested range's measure, or the cached empty measure for an empty range.</returns>
    /// <remarks>
    /// O(log n) worst-case for a nonempty proper range. The traversal consumes cached measures for
    /// fully covered subtrees, carries inherited tags logically, and allocates no persistent nodes.
    /// An empty range invokes no element-measure or tag callback after generic initialization; a
    /// complete range returns the cached root measure directly.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="index"/> or <paramref name="count"/> does not describe a range within this
    /// sequence.
    /// </exception>
    public TMeasure MeasureRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == 0)
            return EmptyMeasure;
        if (count == Count)
            return _root!.Measure;
        return MeasureRangeCore(
            _root!,
            index,
            count,
            hasInheritedTag: false,
            inheritedTag: default!);
    }

    /// <summary>Gets an enumerator over the logical sequence in index order.</summary>
    /// <returns>An enumerator carrying pending tags without materializing an updated tree.</returns>
    /// <remarks>
    /// Enumeration is O(n) total with an O(log n) explicit traversal stack. Creating a nonempty
    /// enumerator allocates one shared traversal-state object and its initial frame array; normal
    /// pattern-based enumeration does not box the public struct.
    /// </remarks>
    public Enumerator GetEnumerator() => new(this);

    IEnumerator<TElement> IEnumerable<TElement>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Wrap(Node? root) =>
        root is null ? EmptyInstance : new RangeUpdateSequence<TElement, TMeasure, TTag, TOps>(root);

    private void CheckElementIndex(int index)
    {
        if ((uint)index >= (uint)Count)
            throw ElementIndexError(index);
    }

    private void CheckBoundaryIndex(int index)
    {
        if ((uint)index > (uint)Count)
            throw BoundaryIndexError(index);
    }

    private void CheckRange(int index, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (index > Count)
            throw BoundaryIndexError(index);
        if (count > Count - index)
            throw RangeCountError(count);
    }

    private void CheckRoomForOneMore()
    {
        if (Count == int.MaxValue)
            throw CountOverflowError();
    }

    private static ArgumentOutOfRangeException ElementIndexError(int index) =>
        new(nameof(index), index, "Index is outside the valid range of the sequence.");

    private static ArgumentOutOfRangeException BoundaryIndexError(int index) =>
        new(nameof(index), index, "Index is outside the valid boundary range of the sequence.");

    private static ArgumentOutOfRangeException RangeCountError(int count) =>
        new(nameof(count), count, "The range extends past the end of the sequence.");

    private static OverflowException CountOverflowError() =>
        new("The operation would create a sequence with more than Int32.MaxValue elements.");

    /// <summary>
    /// Enumerates a <see cref="RangeUpdateSequence{TElement,TMeasure,TTag,TOps}"/> without allocating
    /// pushed tree nodes or boxing during pattern-based enumeration.
    /// </summary>
    /// <remarks>
    /// Value copies of an in-progress enumerator share one traversal stack. Advancing one copy makes
    /// every stale copy fail fast on its next <see cref="MoveNext"/> rather than silently skipping or
    /// duplicating elements. Create a new enumerator when independent traversal is required.
    /// </remarks>
    public struct Enumerator : IEnumerator<TElement>
    {
        private readonly TraversalState? _state;
        private int _cursor;
        private TElement _current;

        /// <summary>Creates a new enumerator.</summary>
        internal Enumerator(RangeUpdateSequence<TElement, TMeasure, TTag, TOps> sequence)
        {
            var root = sequence._root;
            _state = root is null ? null : new TraversalState(root);
            _cursor = 0;
            _current = default!;
        }

        /// <summary>Gets the current logical element.</summary>
        /// <remarks>
        /// Returns the default value before the first successful <see cref="MoveNext"/> and after
        /// enumeration ends.
        /// </remarks>
        public readonly TElement Current => _current;

        readonly object? IEnumerator.Current => Current;

        /// <summary>Releases resources held by the enumerator.</summary>
        public void Dispose()
        {
        }

        /// <summary>Advances to the next logical element.</summary>
        /// <returns><see langword="true"/> when an element is available; otherwise, <see langword="false"/>.</returns>
        /// <exception cref="InvalidOperationException">
        /// Another value copy has advanced the shared traversal state.
        /// </exception>
        public bool MoveNext()
        {
            var state = _state;
            if (state is null)
                return false;
            if (_cursor != state.Cursor)
                throw CopyDivergedError();

            while (state.Depth > 0)
            {
                ref var frame = ref state.Frames[state.Depth - 1];
                switch (frame.Stage)
                {
                    case 0:
                        if (frame.Node.Left is not null)
                        {
                            frame.EnsureChildTag();
                            var left = frame.Node.Left;
                            var hasTag = frame.ChildHasInheritedTag;
                            var tag = frame.ChildInheritedTag;
                            frame.Stage = 1;
                            state.Push(left, hasTag, tag);
                        }
                        else
                        {
                            frame.Stage = 1;
                        }
                        break;

                    case 1:
                    {
                        var value = frame.HasInheritedTag
                            ? ApplyElementTag(frame.InheritedTag, frame.Node.Value)
                            : frame.Node.Value;
                        frame.Stage = 2;
                        _current = value;
                        _cursor = ++state.Cursor;
                        return true;
                    }

                    case 2:
                        if (frame.Node.Right is not null)
                        {
                            frame.EnsureChildTag();
                            var right = frame.Node.Right;
                            var hasTag = frame.ChildHasInheritedTag;
                            var tag = frame.ChildInheritedTag;
                            frame.Stage = 3;
                            state.Push(right, hasTag, tag);
                        }
                        else
                        {
                            frame.Stage = 3;
                        }
                        break;

                    default:
                        frame = default;
                        state.Depth--;
                        break;
                }
            }

            _current = default!;
            return false;
        }

        /// <summary>Not supported; create a new enumerator instead.</summary>
        /// <exception cref="NotSupportedException">Always thrown.</exception>
        void IEnumerator.Reset() => throw new NotSupportedException();

        private static InvalidOperationException CopyDivergedError() =>
            new("Another value copy of this enumerator has been advanced. Copies share one traversal "
                + "stack and cannot be advanced independently; create a new enumerator instead.");

        private sealed class TraversalState
        {
            /// <summary>Gets the traversal's stack of frames.</summary>
            internal Frame[] Frames = new Frame[8];
            /// <summary>Gets the structure's height.</summary>
            internal int Depth;
            /// <summary>Gets a cursor over this collection version.</summary>
            internal int Cursor;

            /// <summary>Creates a new traversal state.</summary>
            internal TraversalState(Node root) =>
                Push(root, hasInheritedTag: false, inheritedTag: default!);

            /// <summary>Returns a collection with the element added.</summary>
            internal void Push(Node node, bool hasInheritedTag, TTag inheritedTag)
            {
                if (Depth == Frames.Length)
                    Array.Resize(ref Frames, checked(Depth * 2));
                Frames[Depth++] = new Frame(node, hasInheritedTag, inheritedTag);
            }
        }

        private struct Frame(Node node, bool hasInheritedTag, TTag inheritedTag)
        {
            /// <summary>Gets the underlying node.</summary>
            internal readonly Node Node = node;
            /// <summary>Gets a value indicating whether inherited tag.</summary>
            internal readonly bool HasInheritedTag = hasInheritedTag;
            /// <summary>Gets the inherited tag.</summary>
            internal readonly TTag InheritedTag = inheritedTag;
            /// <summary>Gets the stage.</summary>
            internal byte Stage;
            /// <summary>Gets the child tag computed.</summary>
            internal bool ChildTagComputed;
            /// <summary>Gets the child has inherited tag.</summary>
            internal bool ChildHasInheritedTag;
            /// <summary>Gets the child inherited tag.</summary>
            internal TTag ChildInheritedTag = default!;

            /// <summary>Ensures the child tag.</summary>
            internal void EnsureChildTag()
            {
                if (ChildTagComputed)
                    return;

                GetChildInheritance(
                    Node,
                    HasInheritedTag,
                    InheritedTag,
                    out var hasChildTag,
                    out var childTag);
                ChildHasInheritedTag = hasChildTag;
                ChildInheritedTag = childTag;
                ChildTagComputed = true;
            }
        }
    }
}
