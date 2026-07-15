using System.Collections;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using Tools.DataStructures.FingerTree;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.Ordered;

/// <summary>
/// Represents an immutable insertion-ordered set with comparer-defined hashed membership and
/// explicit positional reordering.
/// </summary>
/// <typeparam name="T">The type of elements stored in the set.</typeparam>
/// <remarks>
/// <para>
/// The set couples a persistent CHAMP index with a persistent finger-tree sequence. Membership and
/// stored-representative lookup use the equality comparer; enumeration and positional operations use
/// insertion or explicitly requested order. Every update returns an immutable snapshot and shares
/// untouched substrate structure with earlier versions.
/// </para>
/// <para>
/// Adding an equivalent value never moves an element or replaces its first stored representative.
/// Movement is available only through the explicit <see cref="MoveToFirst"/>,
/// <see cref="MoveToLast"/>, and <see cref="MoveTo"/> operations. Ordered means insertion order,
/// not comparison order; <see cref="Sort"/> performs a stable one-shot reorder and later additions
/// append normally.
/// </para>
/// <para>
/// Instances are safe for concurrent readers. Equality-comparer or ordering-comparer failures leave
/// every published version unchanged.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
[DebuggerTypeProxy(typeof(PersistentOrderedSetDebugView<>))]
public sealed partial class PersistentOrderedSet<T> : IReadOnlySet<T>
{
    // Private order-maintenance policy. Its exact spacing and relabel cadence are deliberately not
    // part of the public contract. The independently owned algorithm follows the general sparse-label
    // design recorded in the benchmark-independent structures proposal.
    private const long StampStride = 1L << 20;

    private static readonly PersistentOrderedSet<T> EmptyInstance = new(
        FingerTreeDeque<Entry>.Empty,
        PersistentHashMap<T, long>.Empty);

    private readonly FingerTreeDeque<Entry> _order;
    private readonly PersistentHashMap<T, long> _stamps;

    private PersistentOrderedSet(FingerTreeDeque<Entry> order, PersistentHashMap<T, long> stamps)
    {
        Debug.Assert(order.Count == stamps.Count, "The order and membership indexes must stay in lockstep.");
        _order = order;
        _stamps = stamps;
    }

    private readonly record struct Entry(long Stamp, T Item);

    private sealed class StampOrder : IComparer<Entry>
    {
        public static readonly StampOrder Instance = new();

        public int Compare(Entry x, Entry y) => x.Stamp.CompareTo(y.Stamp);
    }

    private sealed class StableItemOrder(IComparer<T> comparer) : IComparer<Entry>
    {
        public int Compare(Entry x, Entry y)
        {
            var byItem = comparer.Compare(x.Item, y.Item);
            return byItem != 0 ? byItem : x.Stamp.CompareTo(y.Stamp);
        }
    }

    /// <summary>
    /// Gets the shared empty set using <see cref="EqualityComparer{T}.Default"/>.
    /// </summary>
    public static PersistentOrderedSet<T> Empty => EmptyInstance;

    /// <summary>
    /// Gets the number of distinct comparer equivalence classes in the set.
    /// </summary>
    public int Count => _order.Count;

    /// <summary>
    /// Gets whether the set is empty.
    /// </summary>
    public bool IsEmpty => _order.IsEmpty;

    /// <summary>
    /// Gets the equality comparer that defines membership and representative equivalence.
    /// </summary>
    public IEqualityComparer<T> Comparer => _stamps.Comparer;

    /// <summary>
    /// Gets the first stored representative in ordered-set order.
    /// </summary>
    /// <exception cref="InvalidOperationException">The set is empty.</exception>
    public T First => IsEmpty ? throw EmptyError() : _order.First.Item;

    /// <summary>
    /// Gets the last stored representative in ordered-set order.
    /// </summary>
    /// <exception cref="InvalidOperationException">The set is empty.</exception>
    public T Last => IsEmpty ? throw EmptyError() : _order.Last.Item;

    /// <summary>
    /// Gets the stored representative at a zero-based position.
    /// </summary>
    /// <param name="index">The zero-based position.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the set.</exception>
    public T this[int index] => GetAt(index);

    /// <summary>
    /// Creates an empty set using the specified equality comparer.
    /// </summary>
    /// <param name="comparer">
    /// The membership comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>An empty ordered set retaining the effective comparer object.</returns>
    public static PersistentOrderedSet<T> Create(IEqualityComparer<T>? comparer = null) =>
        Wrap(FingerTreeDeque<Entry>.Empty, PersistentHashMap<T, long>.Create(comparer));

    /// <summary>
    /// Creates an ordered set from an enumerable source.
    /// </summary>
    /// <param name="items">Values processed in enumeration order.</param>
    /// <param name="comparer">
    /// The membership comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>
    /// A set containing the first representative of each equivalence class at its first occurrence's
    /// position.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// Enumerates the source once. Duplicate inputs are logical no-ops. The final sequence and index
    /// are each bulk-built once after comparer-aware staging.
    /// </remarks>
    public static PersistentOrderedSet<T> CreateRange(
        IEnumerable<T> items,
        IEqualityComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var seen = PersistentHashMap<T, byte>.Create(comparer);
        List<T> distinct = [];
        foreach (var item in items)
        {
            if (!seen.TryAdd(item, 0, out var next))
                continue;

            seen = next;
            distinct.Add(item);
        }

        return BuildFromItems(distinct, seen.Comparer);
    }

    /// <summary>
    /// Determines whether an equivalent value is present.
    /// </summary>
    /// <param name="item">The value to locate.</param>
    /// <returns><see langword="true"/> when an equivalent value is present.</returns>
    public bool Contains(T item) => _stamps.ContainsKey(item);

    /// <summary>
    /// Searches for the stored representative equivalent to a lookup value.
    /// </summary>
    /// <param name="equalValue">The lookup value.</param>
    /// <param name="actualValue">
    /// The first stored representative on success, or <paramref name="equalValue"/> on failure.
    /// </param>
    /// <returns><see langword="true"/> when an equivalent value is present.</returns>
    public bool TryGetValue(T equalValue, out T actualValue) =>
        _stamps.TryGetKey(equalValue, out actualValue);

    /// <summary>
    /// Gets the stored representative at a zero-based position.
    /// </summary>
    /// <param name="index">The zero-based position.</param>
    /// <returns>The stored representative at <paramref name="index"/>.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the set.</exception>
    /// <remarks>
    /// O(log n) worst case and O(1 + log min(index + 1, Count - index)) amortized; endpoints are O(1).
    /// </remarks>
    public T GetAt(int index) => _order[index].Item;

    /// <summary>
    /// Finds the zero-based position of an equivalence class.
    /// </summary>
    /// <param name="equalValue">The lookup value.</param>
    /// <returns>The position of its stored representative, or -1 when absent.</returns>
    public int IndexOf(T equalValue) =>
        _stamps.TryGetValue(equalValue, out var stamp) ? IndexOfStamp(stamp) : -1;

    /// <summary>
    /// Appends an absent value without moving or replacing an existing equivalent representative.
    /// </summary>
    /// <param name="item">The value to add.</param>
    /// <returns>The successor set, or this instance when an equivalent value already exists.</returns>
    public PersistentOrderedSet<T> Add(T item) => InsertCore(Count, item);

    /// <summary>
    /// Prepends an absent value without moving or replacing an existing equivalent representative.
    /// </summary>
    /// <param name="item">The value to add.</param>
    /// <returns>The successor set, or this instance when an equivalent value already exists.</returns>
    public PersistentOrderedSet<T> AddFirst(T item) => InsertCore(0, item);

    /// <summary>
    /// Inserts an absent value before a zero-based position.
    /// </summary>
    /// <param name="index">An insertion position in the inclusive range 0 through <see cref="Count"/>.</param>
    /// <param name="item">The value to add.</param>
    /// <returns>
    /// The successor set. An existing equivalent representative retains its old position and this
    /// instance is returned.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside 0 through Count.</exception>
    /// <remarks>
    /// Positional validation precedes hashing or equality callbacks, including on the duplicate no-op path.
    /// </remarks>
    public PersistentOrderedSet<T> Insert(int index, T item)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index, allowEnd: true);
        return InsertCore(index, item);
    }

    /// <summary>
    /// Moves an existing equivalence class to the first position while retaining its stored representative.
    /// </summary>
    /// <param name="equalValue">The value whose class to move.</param>
    /// <returns>The reordered set, or this instance when the class is already first.</returns>
    /// <exception cref="KeyNotFoundException">The equivalence class is absent.</exception>
    public PersistentOrderedSet<T> MoveToFirst(T equalValue) => MoveExisting(0, equalValue);

    /// <summary>
    /// Moves an existing equivalence class to the last position while retaining its stored representative.
    /// </summary>
    /// <param name="equalValue">The value whose class to move.</param>
    /// <returns>The reordered set, or this instance when the class is already last.</returns>
    /// <exception cref="KeyNotFoundException">The equivalence class is absent.</exception>
    public PersistentOrderedSet<T> MoveToLast(T equalValue) => MoveExisting(Count - 1, equalValue);

    /// <summary>
    /// Moves an existing equivalence class to a final zero-based result position.
    /// </summary>
    /// <param name="index">The class's final position in the result.</param>
    /// <param name="equalValue">The value whose class to move.</param>
    /// <returns>The reordered set, or this instance when the class already occupies the position.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the current set.</exception>
    /// <exception cref="KeyNotFoundException">The equivalence class is absent.</exception>
    /// <remarks>Positional validation precedes hashing or equality callbacks.</remarks>
    public PersistentOrderedSet<T> MoveTo(int index, T equalValue)
    {
        if ((uint)index >= (uint)Count)
            throw IndexError(index, allowEnd: false);
        return MoveExisting(index, equalValue);
    }

    /// <summary>
    /// Removes an equivalence class when present.
    /// </summary>
    /// <param name="equalValue">The value whose class to remove.</param>
    /// <returns>The successor set, or this instance when the class is absent.</returns>
    public PersistentOrderedSet<T> Remove(T equalValue) =>
        TryRemove(equalValue, out var result) ? result : this;

    /// <summary>
    /// Tries to remove an equivalence class.
    /// </summary>
    /// <param name="equalValue">The value whose class to remove.</param>
    /// <param name="result">The successor on success, or this instance when absent.</param>
    /// <returns><see langword="true"/> when an equivalence class was removed.</returns>
    public bool TryRemove(T equalValue, out PersistentOrderedSet<T> result)
    {
        if (!_stamps.TryRemove(equalValue, out var stamps, out var stamp))
        {
            result = this;
            return false;
        }

        var index = IndexOfStamp(stamp);
        result = Wrap(_order.RemoveAt(index), stamps);
        return true;
    }

    /// <summary>
    /// Removes the representative at a zero-based position.
    /// </summary>
    /// <param name="index">The position to remove.</param>
    /// <returns>The successor set.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the set.</exception>
    public PersistentOrderedSet<T> RemoveAt(int index)
    {
        var entry = _order[index];
        if (!_stamps.TryRemove(entry.Item, out var stamps, out var stamp) || stamp != entry.Stamp)
            throw IndexDisagreementError();
        return Wrap(_order.RemoveAt(index), stamps);
    }

    /// <summary>
    /// Removes the first representative.
    /// </summary>
    /// <returns>The successor set.</returns>
    /// <exception cref="InvalidOperationException">The set is empty.</exception>
    public PersistentOrderedSet<T> RemoveFirst() => IsEmpty ? throw EmptyError() : RemoveAt(0);

    /// <summary>
    /// Removes the last representative.
    /// </summary>
    /// <returns>The successor set.</returns>
    /// <exception cref="InvalidOperationException">The set is empty.</exception>
    public PersistentOrderedSet<T> RemoveLast() => IsEmpty ? throw EmptyError() : RemoveAt(Count - 1);

    /// <summary>
    /// Returns an empty set retaining this set's comparer.
    /// </summary>
    /// <returns>This instance when already empty; otherwise, a comparer-preserving empty set.</returns>
    public PersistentOrderedSet<T> Clear() => IsEmpty ? this : Create(Comparer);

    /// <summary>
    /// Extracts a contiguous range in ordered-set order.
    /// </summary>
    /// <param name="index">The zero-based range start.</param>
    /// <param name="count">The number of representatives to retain.</param>
    /// <returns>
    /// The receiver for the full range; a comparer-preserving empty set for an empty range; otherwise,
    /// the requested range as an independently indexed ordered set.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">The range does not lie within the set.</exception>
    /// <remarks>
    /// O(log n) sequence work plus O(min(kept, removed) (w + c)) index reconciliation. The retained
    /// sequence shares unchanged finger-tree structure with the source; the smaller of the kept and
    /// removed sides drives membership-index work.
    /// </remarks>
    public PersistentOrderedSet<T> GetRange(int index, int count)
    {
        CheckRange(index, count);
        if (count == Count)
            return this;
        if (count == 0)
            return Create(Comparer);

        var split = _order.SplitRange(index, count);
        var kept = split.Range;
        var removedCount = Count - count;
        PersistentHashMap<T, long> stamps;
        if (count <= removedCount)
        {
            stamps = BuildIndex(kept, Comparer);
        }
        else
        {
            stamps = _stamps;
            foreach (var entry in split.Before)
                stamps = stamps.Remove(entry.Item);
            foreach (var entry in split.After)
                stamps = stamps.Remove(entry.Item);
        }

        return new(kept, stamps);
    }

    /// <summary>
    /// Retains the first <paramref name="count"/> representatives.
    /// </summary>
    /// <param name="count">A count in the inclusive range 0 through <see cref="Count"/>.</param>
    /// <returns>
    /// The receiver when <paramref name="count"/> equals <see cref="Count"/>; a comparer-preserving
    /// empty set when it is zero; otherwise, the retained prefix.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is outside 0 through Count.</exception>
    public PersistentOrderedSet<T> Take(int count)
    {
        CheckCount(count);
        return GetRange(0, count);
    }

    /// <summary>
    /// Removes the first <paramref name="count"/> representatives.
    /// </summary>
    /// <param name="count">A count in the inclusive range 0 through <see cref="Count"/>.</param>
    /// <returns>
    /// The receiver when <paramref name="count"/> is zero; a comparer-preserving empty set when it
    /// equals <see cref="Count"/>; otherwise, the retained suffix.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is outside 0 through Count.</exception>
    public PersistentOrderedSet<T> Drop(int count)
    {
        CheckCount(count);
        return GetRange(count, Count - count);
    }

    /// <summary>
    /// Reverses the representative order.
    /// </summary>
    /// <returns>This instance for counts zero and one; otherwise, a freshly indexed reversed set.</returns>
    public PersistentOrderedSet<T> Reverse()
    {
        if (Count <= 1)
            return this;

        var entries = _order.ToArray();
        Array.Reverse(entries);
        return RebuildEntries(entries);
    }

    /// <summary>
    /// Stably reorders representatives once by an ordering comparer.
    /// </summary>
    /// <param name="comparer">
    /// The ordering comparer, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.
    /// This comparer does not replace the set's equality comparer.
    /// </param>
    /// <returns>
    /// This instance when the stable order is unchanged; otherwise, a freshly indexed ordered set.
    /// </returns>
    /// <remarks>
    /// Later additions append normally; the result is not a comparison-sorted-set data structure.
    /// Comparer failure leaves the source unchanged. Sorting performs O(n log n) ordering-comparer
    /// calls and, when the order changes, O(n (w + c)) rebuild work. Unchanged detection compares the
    /// original unique order stamps, not item equality.
    /// </remarks>
    public PersistentOrderedSet<T> Sort(IComparer<T>? comparer = null)
    {
        if (Count <= 1)
            return this;

        var entries = _order.ToArray();
        Array.Sort(entries, new StableItemOrder(comparer ?? System.Collections.Generic.Comparer<T>.Default));

        var unchanged = true;
        for (var i = 1; i < entries.Length; i++)
        {
            if (entries[i - 1].Stamp < entries[i].Stamp)
                continue;
            unchanged = false;
            break;
        }

        return unchanged ? this : RebuildEntries(entries);
    }

    /// <summary>
    /// Copies the stored representatives to a new array in ordered-set order.
    /// </summary>
    /// <returns>A fresh array of length <see cref="Count"/>.</returns>
    public T[] ToArray()
    {
        var result = new T[Count];
        var index = 0;
        foreach (var entry in _order)
            result[index++] = entry.Item;
        return result;
    }

    /// <summary>
    /// Returns a struct enumerator over stored representatives in ordered-set order.
    /// </summary>
    /// <returns>An enumerator positioned before the first representative.</returns>
    public Enumerator GetEnumerator() => new(this);

    IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>
    /// Enumerates one immutable ordered-set snapshot without hashing.
    /// </summary>
    /// <remarks>
    /// Enumeration is O(n) total and O(1) amortized per yield with O(log n) traversal state. Value
    /// copies share that state; after one copy advances, advancing a stale copy throws
    /// <see cref="InvalidOperationException"/>. Obtain separate enumerators for independent traversal.
    /// </remarks>
    public struct Enumerator : IEnumerator<T>
    {
        private FingerTreeDeque<Entry>.Enumerator _inner;

        internal Enumerator(PersistentOrderedSet<T> owner) => _inner = owner._order.GetEnumerator();

        /// <summary>
        /// Gets the representative at the current enumerator position.
        /// </summary>
        /// <remarks>
        /// Undefined before the first successful <see cref="MoveNext"/> and after enumeration ends.
        /// </remarks>
        public readonly T Current => _inner.Current.Item;

        readonly object? IEnumerator.Current => Current;

        /// <summary>
        /// Advances to the next representative.
        /// </summary>
        /// <returns><see langword="true"/> when a representative was produced.</returns>
        public bool MoveNext() => _inner.MoveNext();

        /// <summary>
        /// Releases no resources.
        /// </summary>
        public readonly void Dispose()
        {
        }

        readonly void IEnumerator.Reset() => throw new NotSupportedException();
    }

    private PersistentOrderedSet<T> InsertCore(int index, T item)
    {
        if (TryPickStamp(_order, index, out var stamp))
        {
            if (!_stamps.TryAdd(item, stamp, out var stamps))
                return this;

            var entry = new Entry(stamp, item);
            var order = index == 0
                ? _order.AddFirst(entry)
                : index == Count
                    ? _order.AddLast(entry)
                    : _order.InsertAt(index, entry);
            return new(order, stamps);
        }

        // A provisional value is sufficient for duplicate detection; a successful add is discarded
        // because the relabel rebuild assigns every stamp canonically in one unpublished pass.
        if (!_stamps.TryAdd(item, 0, out _))
            return this;
        return RebuildInserted(_order, index, item);
    }

    private PersistentOrderedSet<T> MoveExisting(int finalIndex, T equalValue)
    {
        if (!_stamps.TryGetValue(equalValue, out var oldStamp))
            throw MissingItemError(equalValue);

        var oldIndex = IndexOfStamp(oldStamp);
        if (oldIndex == finalIndex)
            return this;

        var stored = _order[oldIndex].Item;
        var trimmed = _order.RemoveAt(oldIndex);
        if (!TryPickStamp(trimmed, finalIndex, out var newStamp))
            return RebuildInserted(trimmed, finalIndex, stored);

        var entry = new Entry(newStamp, stored);
        var order = finalIndex == 0
            ? trimmed.AddFirst(entry)
            : finalIndex == trimmed.Count
                ? trimmed.AddLast(entry)
                : trimmed.InsertAt(finalIndex, entry);
        return new(order, _stamps.SetItem(stored, newStamp));
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private int IndexOfStamp(long stamp)
    {
        var index = _order.SortedLowerBound(new Entry(stamp, default!), StampOrder.Instance);
        if ((uint)index >= (uint)Count || _order[index].Stamp != stamp)
            throw IndexDisagreementError();
        return index;
    }

    private static bool TryPickStamp(FingerTreeDeque<Entry> order, int index, out long stamp)
    {
        stamp = 0;
        if (order.IsEmpty)
            return true;

        if (index == 0)
        {
            var first = order.First.Stamp;
            if (first < long.MinValue + StampStride)
                return false;
            stamp = first - StampStride;
            return true;
        }

        if (index == order.Count)
        {
            var last = order.Last.Stamp;
            if (last > long.MaxValue - StampStride)
                return false;
            stamp = last + StampStride;
            return true;
        }

        var left = order[index - 1].Stamp;
        var right = order[index].Stamp;
        var gap = unchecked((ulong)(right - left));
        if (gap < 2)
            return false;
        stamp = unchecked(left + (long)(gap >> 1));
        return true;
    }

    private PersistentOrderedSet<T> RebuildInserted(FingerTreeDeque<Entry> order, int index, T item)
    {
        if (order.Count == int.MaxValue)
            throw CountOverflowError();

        var entries = new Entry[order.Count + 1];
        var target = 0;
        foreach (var entry in order)
        {
            if (target == index)
                target++;
            entries[target++] = entry;
        }
        entries[index] = new Entry(0, item);
        return RebuildEntries(entries);
    }

    private PersistentOrderedSet<T> RebuildEntries(Entry[] entries)
    {
        if (entries.Length == 0)
            return Create(Comparer);

        var pairs = new KeyValuePair<T, long>[entries.Length];
        for (var i = 0; i < entries.Length; i++)
        {
            var stamp = checked((long)i * StampStride);
            entries[i] = new Entry(stamp, entries[i].Item);
            pairs[i] = KeyValuePair.Create(entries[i].Item, stamp);
        }

        var stamps = PersistentHashMap<T, long>.CreateRange(pairs, Comparer);
        return new(FingerTreeDeque<Entry>.Create(entries), stamps);
    }

    private static PersistentOrderedSet<T> BuildFromItems(IReadOnlyList<T> items, IEqualityComparer<T> comparer)
    {
        if (items.Count == 0)
            return Create(comparer);

        var entries = new Entry[items.Count];
        var pairs = new KeyValuePair<T, long>[items.Count];
        for (var i = 0; i < items.Count; i++)
        {
            var stamp = checked((long)i * StampStride);
            entries[i] = new Entry(stamp, items[i]);
            pairs[i] = KeyValuePair.Create(items[i], stamp);
        }

        return new(
            FingerTreeDeque<Entry>.Create(entries),
            PersistentHashMap<T, long>.CreateRange(pairs, comparer));
    }

    private static PersistentHashMap<T, long> BuildIndex(
        FingerTreeDeque<Entry> order,
        IEqualityComparer<T> comparer)
    {
        var pairs = new KeyValuePair<T, long>[order.Count];
        var index = 0;
        foreach (var entry in order)
            pairs[index++] = KeyValuePair.Create(entry.Item, entry.Stamp);
        return PersistentHashMap<T, long>.CreateRange(pairs, comparer);
    }

    private static PersistentOrderedSet<T> Wrap(
        FingerTreeDeque<Entry> order,
        PersistentHashMap<T, long> stamps)
    {
        if (!order.IsEmpty)
            return new(order, stamps);
        return ReferenceEquals(stamps, PersistentHashMap<T, long>.Empty)
            ? EmptyInstance
            : new(order, stamps);
    }

    private void CheckRange(int index, int count)
    {
        if ((uint)index > (uint)Count)
            throw IndexError(index, allowEnd: true);
        if ((uint)count > (uint)(Count - index))
            throw new ArgumentOutOfRangeException(nameof(count), count, "The range extends past the end of the set.");
    }

    private void CheckCount(int count)
    {
        if ((uint)count > (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(count), count, "Count must lie within 0 through the set count.");
    }

    private static InvalidOperationException EmptyError() => new("The ordered set is empty.");

    private static KeyNotFoundException MissingItemError(T equalValue) =>
        new($"The value '{equalValue}' is absent from the ordered set.");

    private static ArgumentOutOfRangeException IndexError(int index, bool allowEnd) =>
        new(nameof(index), index, allowEnd
            ? "Index must lie within 0 through the set count."
            : "Index must identify an element in the set.");

    private static InvalidOperationException IndexDisagreementError() =>
        new("The ordered set's membership and order indexes disagree.");

    private static OverflowException CountOverflowError() =>
        new("The operation would create more than Int32.MaxValue ordered-set elements.");
}
