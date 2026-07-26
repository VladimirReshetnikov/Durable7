// Represents an immutable unordered multiset backed by a persistent hash-array mapped trie.

using System.Collections;
using System.Diagnostics;

namespace Durable7.Hamt;

/// <summary>
/// Represents an immutable unordered multiset backed by a persistent hash-array mapped trie.
/// </summary>
/// <typeparam name="T">The type of items stored in the bag.</typeparam>
/// <remarks>
/// <para>
/// One stored representative and one positive <see cref="int"/> multiplicity are retained for each
/// comparer equivalence class. <see cref="DistinctCount"/> counts those classes, while
/// <see cref="TotalCount"/> counts expanded occurrences and may exceed <see cref="int.MaxValue"/>.
/// </para>
/// <para>
/// Every update returns a new bag version and leaves the original unchanged. Untouched HAMT
/// subtrees are shared by reference. Enumeration repeats each stored representative by its
/// multiplicity in stable-for-one-version, otherwise unspecified trie order.
/// </para>
/// </remarks>
[DebuggerDisplay("DistinctCount = {DistinctCount}, TotalCount = {TotalCount}")]
[DebuggerTypeProxy(typeof(PersistentHashBagDebugView<>))]
public sealed class PersistentHashBag<T> : IEnumerable<T>
{
    /// <summary>
    /// Gets the shared empty bag that uses <see cref="EqualityComparer{T}.Default"/>.
    /// </summary>
    public static PersistentHashBag<T> Empty { get; } =
        new(PersistentHashMap<T, int>.Empty, totalCount: 0);

    private readonly PersistentHashMap<T, int> _counts;
    private readonly long _totalCount;

    private PersistentHashBag(PersistentHashMap<T, int> counts, long totalCount)
    {
        _counts = counts;
        _totalCount = totalCount;
    }

    /// <summary>Gets the number of comparer equivalence classes in the bag.</summary>
    public int DistinctCount => _counts.Count;

    /// <summary>Gets the expanded number of occurrences in the bag.</summary>
    /// <remarks>This value may exceed <see cref="int.MaxValue"/>.</remarks>
    public long TotalCount => _totalCount;

    /// <summary>Gets whether the bag contains no occurrences.</summary>
    public bool IsEmpty => _counts.IsEmpty;

    /// <summary>Gets the comparer that defines item hashing and equality.</summary>
    public IEqualityComparer<T> Comparer => _counts.Comparer;

    /// <summary>
    /// Gets an enumerable view containing one stored representative per equivalence class.
    /// </summary>
    /// <remarks>
    /// The view follows stable-for-one-version, otherwise unspecified HAMT order. Each enumeration
    /// allocates the map's key-view iterator.
    /// </remarks>
    public IEnumerable<T> DistinctItems => _counts.Keys;

    /// <summary>
    /// Gets an enumerable view of stored representative/multiplicity pairs.
    /// </summary>
    /// <remarks>
    /// The view has the same distinct relative order as <see cref="DistinctItems"/>. Each
    /// enumeration allocates one iterator object and does not expose the backing map object.
    /// </remarks>
    public IEnumerable<KeyValuePair<T, int>> Entries
    {
        get
        {
            foreach (var entry in _counts)
                yield return entry;
        }
    }

    internal object? RootForTesting => _counts.RootForTesting;

    /// <summary>Creates an empty bag with the specified item comparer.</summary>
    /// <param name="comparer">
    /// The comparer that defines item hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>An empty bag using <paramref name="comparer"/>.</returns>
    public static PersistentHashBag<T> Create(IEqualityComparer<T>? comparer = null) =>
        Wrap(PersistentHashMap<T, int>.Create(comparer), totalCount: 0);

    /// <summary>Creates a bag from an enumerable sequence of occurrences.</summary>
    /// <param name="items">The occurrences to add in enumeration order.</param>
    /// <param name="comparer">
    /// The comparer that defines item hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>
    /// A bag containing the supplied occurrences. The first item of each comparer equivalence class
    /// is retained as that class's stored representative.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">
    /// One equivalence class occurs more than <see cref="int.MaxValue"/> times.
    /// </exception>
    /// <remarks>
    /// Runs in O(n (w + c)), where w is the bounded trie depth and c is the applicable equal-hash
    /// collision scan. A mutable unpublished trie aggregates input and is frozen once.
    /// </remarks>
    public static PersistentHashBag<T> CreateRange(
        IEnumerable<T> items,
        IEqualityComparer<T>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var builder = PersistentHashMap<T, int>.CreateBulkBuilder(comparer);
        long totalCount = 0;
        foreach (var item in items)
        {
            builder.AddOrUpdate(item, 1, static (count, increment) => checked(count + increment));
            totalCount = checked(totalCount + 1);
        }

        return Wrap(builder.ToImmutable(), totalCount);
    }

    /// <summary>Determines whether the bag contains an occurrence equivalent to an item.</summary>
    /// <param name="item">The item to locate.</param>
    /// <returns><see langword="true"/> when the item's multiplicity is positive; otherwise, <see langword="false"/>.</returns>
    /// <remarks>Visits at most seven trie levels plus one equal-hash collision scan and allocates nothing.</remarks>
    public bool Contains(T item) => _counts.ContainsKey(item);

    /// <summary>Gets the multiplicity of an item equivalence class.</summary>
    /// <param name="item">The item whose multiplicity to retrieve.</param>
    /// <returns>The positive stored multiplicity, or zero when the class is absent.</returns>
    /// <remarks>Visits at most seven trie levels plus one equal-hash collision scan and allocates nothing.</remarks>
    public int CountOf(T item) => _counts.TryGetValue(item, out var count) ? count : 0;

    /// <summary>Searches for the stored representative equivalent to a specified value.</summary>
    /// <param name="equalValue">The value to search for.</param>
    /// <param name="actualValue">
    /// When this method returns, contains the stored representative on success or
    /// <paramref name="equalValue"/> on failure.
    /// </param>
    /// <returns><see langword="true"/> when an equivalent class is present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>Visits at most seven trie levels plus one equal-hash collision scan and allocates nothing.</remarks>
    public bool TryGetValue(T equalValue, out T actualValue) =>
        _counts.TryGetKey(equalValue, out actualValue);

    /// <summary>Adds one occurrence of an item.</summary>
    /// <param name="item">The item to add.</param>
    /// <returns>
    /// A bag with one additional occurrence. An existing stored representative is retained.
    /// </returns>
    /// <exception cref="OverflowException">
    /// The item's existing multiplicity is <see cref="int.MaxValue"/>.
    /// </exception>
    public PersistentHashBag<T> Add(T item)
    {
        var totalCount = checked(_totalCount + 1);
        var counts = _counts.AddOrUpdate(
            item,
            static _ => 1,
            static (_, existingCount) => checked(existingCount + 1),
            out _);
        return WithCounts(counts, totalCount);
    }

    /// <summary>Adds a specified number of occurrences of an item.</summary>
    /// <param name="item">The item to add.</param>
    /// <param name="count">The number of occurrences to add.</param>
    /// <returns>
    /// A bag with the occurrences added. Zero returns the current instance without hashing.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative.</exception>
    /// <exception cref="OverflowException">The resulting per-item multiplicity exceeds <see cref="int.MaxValue"/>.</exception>
    /// <remarks>
    /// A positive update hashes once, walks the trie once, and rebuilds only the changed search
    /// path. No result is published if hashing, equality, or checked arithmetic throws.
    /// </remarks>
    public PersistentHashBag<T> AddCopies(T item, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (count == 0)
            return this;
        if (count == 1)
            return Add(item);

        return AddCopiesPositive(item, count);
    }

    private PersistentHashBag<T> AddCopiesPositive(T item, int count)
    {
        var totalCount = checked(_totalCount + count);
        var counts = _counts.AddOrUpdate(
            item,
            _ => count,
            (_, existingCount) => checked(existingCount + count),
            out _);
        return WithCounts(counts, totalCount);
    }

    /// <summary>Removes one occurrence of an item when present.</summary>
    /// <param name="item">The item to remove.</param>
    /// <returns>
    /// A bag with one fewer occurrence, or the current instance when the item is absent.
    /// </returns>
    public PersistentHashBag<T> Remove(T item) => RemoveCopies(item, 1);

    /// <summary>Removes up to a specified number of occurrences of an item.</summary>
    /// <param name="item">The item to remove.</param>
    /// <param name="count">The maximum number of occurrences to remove.</param>
    /// <returns>
    /// A bag with saturated subtraction applied. Zero or a missing class returns the current
    /// instance; a class whose remaining count is zero is removed entirely.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative.</exception>
    /// <remarks>
    /// A positive changed update may perform a lookup followed by one map update, but rebuilds only
    /// one search path. Zero returns before hashing.
    /// </remarks>
    public PersistentHashBag<T> RemoveCopies(T item, int count)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(count);
        if (count == 0 || !_counts.TryGetValue(item, out var existingCount))
            return this;

        if (count >= existingCount)
        {
            if (!_counts.TryRemove(item, out var counts, out var removedCount))
                throw new InvalidOperationException("A located bag entry could not be removed.");

            return WithCounts(counts, checked(_totalCount - removedCount));
        }

        var remainingCount = existingCount - count;
        return WithCounts(
            _counts.SetItem(item, remainingCount),
            checked(_totalCount - count));
    }

    /// <summary>Removes every occurrence of an item equivalence class.</summary>
    /// <param name="item">The item whose class to remove.</param>
    /// <returns>A bag without the class, or the current instance when it was absent.</returns>
    /// <remarks>Hashes once, walks the trie once, and obtains the removed multiplicity from that traversal.</remarks>
    public PersistentHashBag<T> RemoveAll(T item)
    {
        if (!_counts.TryRemove(item, out var counts, out var removedCount))
            return this;

        return WithCounts(counts, checked(_totalCount - removedCount));
    }

    /// <summary>Returns an empty bag that preserves this bag's comparer object.</summary>
    /// <returns>
    /// The current instance when already empty; otherwise, a comparer-preserving empty bag.
    /// </returns>
    public PersistentHashBag<T> Clear() => WithCounts(_counts.Clear(), totalCount: 0);

    /// <summary>Returns the multiset union using the maximum multiplicity of each class.</summary>
    /// <param name="other">The other bag.</param>
    /// <returns>
    /// A receiver-comparer bag in which surviving receiver representatives win. A logical no-op
    /// returns the current instance.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">
    /// Normalizing comparer-mismatched argument classes overflows an <see cref="int"/> multiplicity.
    /// </exception>
    public PersistentHashBag<T> Union(PersistentHashBag<T> other) =>
        Combine(other, BagOperation.Union);

    /// <summary>Returns the multiset intersection using the minimum multiplicity of each class.</summary>
    /// <param name="other">The other bag.</param>
    /// <returns>
    /// A receiver-comparer bag retaining receiver representatives. A logical no-op returns the
    /// current instance.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">
    /// Normalizing comparer-mismatched argument classes overflows an <see cref="int"/> multiplicity.
    /// </exception>
    public PersistentHashBag<T> Intersect(PersistentHashBag<T> other) =>
        Combine(other, BagOperation.Intersect);

    /// <summary>Returns the saturated multiset difference from another bag.</summary>
    /// <param name="other">The bag whose multiplicities to subtract.</param>
    /// <returns>
    /// A receiver-comparer bag retaining receiver representatives. A logical no-op returns the
    /// current instance.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">
    /// Normalizing comparer-mismatched argument classes overflows an <see cref="int"/> multiplicity.
    /// </exception>
    public PersistentHashBag<T> Except(PersistentHashBag<T> other) =>
        Combine(other, BagOperation.Except);

    /// <summary>Returns the additive multiset sum with another bag.</summary>
    /// <param name="other">The other bag.</param>
    /// <returns>
    /// A receiver-comparer bag retaining receiver representatives for existing classes and argument
    /// representatives only for receiver-absent classes.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">
    /// Normalization or a resulting per-class multiplicity exceeds <see cref="int.MaxValue"/>.
    /// </exception>
    public PersistentHashBag<T> Sum(PersistentHashBag<T> other) =>
        Combine(other, BagOperation.Sum);

    /// <summary>Copies expanded enumeration to a new array.</summary>
    /// <returns>An array containing each representative repeated by its multiplicity.</returns>
    /// <exception cref="OverflowException">
    /// <see cref="TotalCount"/> exceeds <see cref="Array.MaxLength"/>.
    /// </exception>
    /// <remarks>The array order is exactly the order produced by <see cref="GetEnumerator"/>.</remarks>
    public T[] ToArray()
    {
        if (_totalCount > Array.MaxLength)
        {
            throw new OverflowException(
                $"The expanded bag count {_totalCount} exceeds the maximum array length {Array.MaxLength}.");
        }

        if (_totalCount == 0)
            return Array.Empty<T>();

        var result = new T[(int)_totalCount];
        var index = 0;
        foreach (var entry in _counts)
        {
            Array.Fill(result, entry.Key, index, entry.Value);
            index += entry.Value;
        }
        return result;
    }

    /// <summary>Returns an allocation-free struct enumerator over expanded occurrences.</summary>
    /// <returns>An enumerator over the bag.</returns>
    public Enumerator GetEnumerator() => new(_counts.GetEnumerator());

    IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal PersistentHashBagCanonicalityDiagnostics ValidateCanonicalityForDiagnostics()
    {
        var mapDiagnostics = _counts.ValidateCanonicalityForDiagnostics();
        long totalCount = 0;
        foreach (var entry in _counts)
        {
            if (entry.Value <= 0)
                throw new InvalidOperationException("A bag entry has a nonpositive multiplicity.");

            try
            {
                totalCount = checked(totalCount + entry.Value);
            }
            catch (OverflowException exception)
            {
                throw new InvalidOperationException("Bag multiplicities overflow the total-count invariant.", exception);
            }
        }

        if (totalCount != _totalCount)
        {
            throw new InvalidOperationException(
                $"Bag total {_totalCount} differs from the checked multiplicity sum {totalCount}.");
        }

        return new PersistentHashBagCanonicalityDiagnostics(
            DistinctCount,
            totalCount,
            mapDiagnostics.NodeCount);
    }

    private PersistentHashBag<T> Combine(PersistentHashBag<T> other, BagOperation operation)
    {
        ArgumentNullException.ThrowIfNull(other);

        var normalized = ReferenceEquals(Comparer, other.Comparer)
            ? other
            : NormalizeArgument(other);

        if (ReferenceEquals(this, normalized))
        {
            return operation switch
            {
                BagOperation.Union or BagOperation.Intersect => this,
                BagOperation.Except => Clear(),
                BagOperation.Sum => SumNormalized(normalized),
                _ => throw new UnreachableException(),
            };
        }

        return operation switch
        {
            BagOperation.Union => UnionNormalized(normalized),
            BagOperation.Intersect => IntersectNormalized(normalized),
            BagOperation.Except => ExceptNormalized(normalized),
            BagOperation.Sum => SumNormalized(normalized),
            _ => throw new UnreachableException(),
        };
    }

    private PersistentHashBag<T> NormalizeArgument(PersistentHashBag<T> other)
    {
        var builder = PersistentHashMap<T, int>.CreateBulkBuilder(Comparer);
        long totalCount = 0;
        foreach (var entry in other._counts)
        {
            var argumentCount = entry.Value;
            builder.AddOrUpdate(
                entry.Key,
                argumentCount,
                static (existingCount, incomingCount) => checked(existingCount + incomingCount));
            totalCount = checked(totalCount + argumentCount);
        }

        return Wrap(builder.ToImmutable(), totalCount);
    }

    private PersistentHashBag<T> UnionNormalized(PersistentHashBag<T> other)
    {
        if (other.IsEmpty)
            return this;

        var counts = _counts;
        var totalCount = _totalCount;
        foreach (var entry in other._counts)
        {
            var argumentCount = entry.Value;
            var addedCount = argumentCount;
            var next = counts.AddOrUpdate(
                entry.Key,
                _ => argumentCount,
                (_, receiverCount) =>
                {
                    if (receiverCount >= argumentCount)
                    {
                        addedCount = 0;
                        return receiverCount;
                    }

                    addedCount = argumentCount - receiverCount;
                    return argumentCount;
                },
                out _);
            if (ReferenceEquals(next, counts))
                continue;

            counts = next;
            totalCount = checked(totalCount + addedCount);
        }

        return WithCounts(counts, totalCount);
    }

    private PersistentHashBag<T> IntersectNormalized(PersistentHashBag<T> other)
    {
        if (IsEmpty)
            return this;
        if (other.IsEmpty)
            return Clear();

        var counts = _counts;
        var totalCount = _totalCount;
        foreach (var entry in _counts)
        {
            if (!other._counts.TryGetValue(entry.Key, out var argumentCount))
            {
                if (!counts.TryRemove(entry.Key, out counts, out var removedCount))
                    throw new InvalidOperationException("A located bag entry could not be removed.");
                totalCount = checked(totalCount - removedCount);
                continue;
            }

            if (argumentCount >= entry.Value)
                continue;

            counts = counts.SetItem(entry.Key, argumentCount);
            totalCount = checked(totalCount - (entry.Value - argumentCount));
        }

        return WithCounts(counts, totalCount);
    }

    private PersistentHashBag<T> ExceptNormalized(PersistentHashBag<T> other)
    {
        if (IsEmpty || other.IsEmpty)
            return this;

        var counts = _counts;
        var totalCount = _totalCount;
        foreach (var entry in _counts)
        {
            if (!other._counts.TryGetValue(entry.Key, out var argumentCount))
                continue;

            if (argumentCount >= entry.Value)
            {
                if (!counts.TryRemove(entry.Key, out counts, out var removedCount))
                    throw new InvalidOperationException("A located bag entry could not be removed.");
                totalCount = checked(totalCount - removedCount);
                continue;
            }

            counts = counts.SetItem(entry.Key, entry.Value - argumentCount);
            totalCount = checked(totalCount - argumentCount);
        }

        return WithCounts(counts, totalCount);
    }

    private PersistentHashBag<T> SumNormalized(PersistentHashBag<T> other)
    {
        if (other.IsEmpty)
            return this;

        var counts = _counts;
        var totalCount = _totalCount;
        foreach (var entry in other._counts)
        {
            var argumentCount = entry.Value;
            counts = counts.AddOrUpdate(
                entry.Key,
                _ => argumentCount,
                (_, receiverCount) => checked(receiverCount + argumentCount),
                out _);
            totalCount = checked(totalCount + argumentCount);
        }

        return WithCounts(counts, totalCount);
    }

    private static PersistentHashBag<T> Wrap(PersistentHashMap<T, int> counts, long totalCount)
    {
        if (ReferenceEquals(counts, PersistentHashMap<T, int>.Empty))
            return Empty;

        return new PersistentHashBag<T>(counts, totalCount);
    }

    private PersistentHashBag<T> WithCounts(PersistentHashMap<T, int> counts, long totalCount)
    {
        if (!ReferenceEquals(counts, _counts))
            return Wrap(counts, totalCount);

        if (totalCount != _totalCount)
            throw new InvalidOperationException("An unchanged bag map cannot have a different total count.");
        return this;
    }

    private enum BagOperation
    {
        Union,
        Intersect,
        Except,
        Sum,
    }

    /// <summary>Enumerates expanded occurrences in a <see cref="PersistentHashBag{T}"/>.</summary>
    /// <remarks>
    /// The enumerator keeps all traversal and repetition state inline. It allocates nothing through
    /// the concrete surface, and a copied enumerator advances independently.
    /// </remarks>
    public struct Enumerator : IEnumerator<T>
    {
        private PersistentHashMap<T, int>.Enumerator _inner;
        private T? _current;
        private int _remaining;

        internal Enumerator(PersistentHashMap<T, int>.Enumerator inner)
        {
            _inner = inner;
        }

        /// <summary>Gets the occurrence at the current enumerator position.</summary>
        public readonly T Current => _current!;

        readonly object? IEnumerator.Current => Current;

        /// <summary>Advances to the next expanded occurrence.</summary>
        /// <returns><see langword="true"/> when an occurrence is available; otherwise, <see langword="false"/>.</returns>
        public bool MoveNext()
        {
            if (_remaining > 0)
            {
                _remaining--;
                return true;
            }

            if (!_inner.MoveNext())
            {
                _current = default;
                return false;
            }

            var entry = _inner.Current;
            if (entry.Value <= 0)
                throw new InvalidOperationException("A bag entry has a nonpositive multiplicity.");
            _current = entry.Key;
            _remaining = entry.Value - 1;
            return true;
        }

        /// <summary>Releases resources held by the enumerator.</summary>
        public readonly void Dispose()
        {
        }

        readonly void IEnumerator.Reset() =>
            throw new NotSupportedException("Resetting this enumerator is not supported; create a new enumerator instead.");
    }
}

/// <summary>
/// Evidence that the bag's shape is a function of its contents, for tests that different edit
/// histories converge on one structure.
/// </summary>
internal readonly record struct PersistentHashBagCanonicalityDiagnostics(
    int DistinctCount,
    long TotalCount,
    int NodeCount);

/// <summary>
/// The debugger's view of a bag: its distinct elements with their multiplicities, rather than its
/// trie nodes.
/// </summary>
internal sealed class PersistentHashBagDebugView<T>(PersistentHashBag<T> bag)
{
    [DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public KeyValuePair<T, int>[] Items => [.. bag.Entries];
}
