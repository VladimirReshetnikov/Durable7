using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>A presence-safe endpoint value in a persistent-map change.</summary>
/// <typeparam name="T">Value type.</typeparam>
public readonly struct DeltaMapValue<T>
{
    private readonly T _value;

    private DeltaMapValue(T value)
    {
        _value = value;
        HasValue = true;
    }

    /// <summary>Gets the absent endpoint value.</summary>
    public static DeltaMapValue<T> Absent => default;

    /// <summary>Gets a value indicating whether the endpoint contains a value.</summary>
    public bool HasValue { get; }

    /// <summary>Gets the present value, including <see langword="null"/> when <typeparamref name="T"/> permits it.</summary>
    /// <exception cref="InvalidOperationException">The endpoint is absent.</exception>
    public T Value => HasValue ? _value : throw new InvalidOperationException("The endpoint is absent.");

    /// <summary>Creates a present endpoint.</summary>
    /// <param name="value">Value to carry.</param>
    /// <returns>A present endpoint carrying <paramref name="value"/>.</returns>
    public static DeltaMapValue<T> Present(T value) => new(value);

    /// <summary>Returns a diagnostic representation that distinguishes absence from a present null.</summary>
    /// <returns>A diagnostic string.</returns>
    public override string ToString() => HasValue ? $"Present({_value})" : "Absent";
}

/// <summary>Classifies one net key change relative to a <see cref="PersistentDeltaMap{TKey, TValue}"/> checkpoint.</summary>
public enum PersistentMapChangeKind
{
    /// <summary>The key is absent at the checkpoint and present in the current map.</summary>
    Added,

    /// <summary>The key is present at the checkpoint and absent from the current map.</summary>
    Removed,

    /// <summary>The key is present at both endpoints with unequal values.</summary>
    Updated,
}

/// <summary>One exact net change between a persistent map's checkpoint and current state.</summary>
/// <typeparam name="TKey">Key type.</typeparam>
/// <typeparam name="TValue">Value type.</typeparam>
/// <param name="Key">The checkpoint representative when originally present; otherwise the current addition episode's first representative.</param>
/// <param name="Before">The checkpoint endpoint.</param>
/// <param name="After">The current endpoint.</param>
public readonly record struct PersistentMapChange<TKey, TValue>(
    TKey Key,
    DeltaMapValue<TValue> Before,
    DeltaMapValue<TValue> After)
{
    /// <summary>Gets the change classification implied by <see cref="Before"/> and <see cref="After"/>.</summary>
    /// <exception cref="InvalidOperationException">Both endpoints are absent, which is not a net change.</exception>
    public PersistentMapChangeKind Kind =>
        Before.HasValue
            ? After.HasValue ? PersistentMapChangeKind.Updated : PersistentMapChangeKind.Removed
            : After.HasValue ? PersistentMapChangeKind.Added : throw new InvalidOperationException("A change cannot be absent at both endpoints.");
}

/// <summary>
/// An immutable, fully persistent sorted map with an explicit checkpoint and a coalesced ordered index of
/// exact net changes from that checkpoint.
/// </summary>
/// <typeparam name="TKey">Key type.</typeparam>
/// <typeparam name="TValue">Value type.</typeparam>
/// <remarks>
/// <para>
/// The representation is a persistent <see cref="SortedDictionary{TKey, TValue}"/> for current state, a
/// shared checkpoint root, and a second persistent sorted dictionary containing one before/after record per
/// comparator-equivalent key whose current value differs from the checkpoint. Repeated writes to a key
/// coalesce, and returning a key to its checkpoint state removes its record. Every successor is immutable;
/// any retained version can be read or updated independently.
/// </para>
/// <para>
/// Let <c>N = max(2, |dom(checkpoint) union dom(current)|)</c>, counting comparator-equivalent key
/// classes, and let <c>k</c> be the
/// number of net-changed key classes. Point lookup, point update/removal, rank, and neighbor operations are
/// O(log N), matching the
/// underlying persistent ordered map. <see cref="Checkpoint"/> and <see cref="Rollback"/> are O(1).
/// Fully consuming <see cref="GetChanges()"/> is Theta(k + 1), which is output-optimal, avoiding the
/// Theta(k log(N/k + 1)) adversarial divergent-path walk of a conventional reference-pruned comparison
/// between constant-degree, path-copied balanced trees. A live
/// version occupies O(N + k) nodes; retaining every successor adds O(log N) nodes per changed point update,
/// the same asymptotic persistence cost as an ordinary path-copied ordered map.
/// </para>
/// <para>
/// The value comparer defines semantic no-ops and delta cancellation. Key identity and output order are
/// defined by the key comparer. Thus "exact" means exact extensionally under those two policies, rather than
/// object identity of comparator-equivalent key or value representatives. A baseline key representative is
/// retained across point updates and delete/re-add round trips. A newly added class retains its first
/// representative while its net-added record remains active; after complete add/remove cancellation, a later
/// addition begins a new representative episode. The key comparer must define a stable total preorder, and the
/// value comparer must define a stable reflexive, symmetric, transitive equivalence relation. Callback side
/// effects cannot be undone. Concurrent reads are safe when both supplied comparer objects are safe for
/// concurrent calls.
/// </para>
/// <para>
/// Policy exceptions propagate without publishing a partial successor, so the receiver and every retained
/// branch remain unchanged. <see cref="Checkpoint"/>, <see cref="Rollback"/>, and
/// <see cref="GetChanges()"/> invoke no key- or value-policy callbacks; the range-restricted
/// <see cref="GetChanges(TKey, TKey)"/> overload invokes only key comparisons, and only the
/// O(log(k + 1)) many needed to seek the range boundaries.
/// </para>
/// <para>
/// The point-update API is deliberate. An eager bulk clear would produce Theta(N) removal records and cannot
/// simultaneously retain an ordinary map's O(1) clear bound; callers can start from <see cref="Create"/> when
/// they do not need an enumerable delta, or remove entries explicitly when they do.
/// </para>
/// </remarks>
public sealed class PersistentDeltaMap<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private readonly record struct DeltaRecord(DeltaMapValue<TValue> Before, DeltaMapValue<TValue> After);

    private static readonly PersistentDeltaMap<TKey, TValue> EmptyDefault = new(
        SortedDictionary<TKey, TValue>.Empty,
        SortedDictionary<TKey, TValue>.Empty,
        SortedDictionary<TKey, DeltaRecord>.Empty,
        EqualityComparer<TValue>.Default);

    private readonly SortedDictionary<TKey, TValue> _current;
    private readonly SortedDictionary<TKey, TValue> _checkpoint;
    private readonly SortedDictionary<TKey, DeltaRecord> _changes;
    private readonly IEqualityComparer<TValue> _valueComparer;

    private PersistentDeltaMap(
        SortedDictionary<TKey, TValue> current,
        SortedDictionary<TKey, TValue> checkpoint,
        SortedDictionary<TKey, DeltaRecord> changes,
        IEqualityComparer<TValue> valueComparer)
    {
        _current = current;
        _checkpoint = checkpoint;
        _changes = changes;
        _valueComparer = valueComparer;
    }

    /// <summary>Gets the empty map using the default key and value comparers.</summary>
    public static PersistentDeltaMap<TKey, TValue> Empty => EmptyDefault;

    /// <summary>Creates an empty map with explicit key-order and value-equality policies.</summary>
    /// <param name="keyComparer">Key order, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <param name="valueComparer">Value equality, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>An empty map whose current state is also its checkpoint.</returns>
    public static PersistentDeltaMap<TKey, TValue> Create(
        IComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        var keys = keyComparer ?? Comparer<TKey>.Default;
        var values = valueComparer ?? EqualityComparer<TValue>.Default;
        if (ReferenceEquals(keys, Comparer<TKey>.Default) && ReferenceEquals(values, EqualityComparer<TValue>.Default))
            return EmptyDefault;

        var state = SortedDictionary<TKey, TValue>.Create(keys);
        return new PersistentDeltaMap<TKey, TValue>(
            state,
            state,
            SortedDictionary<TKey, DeltaRecord>.Create(keys),
            values);
    }

    /// <summary>Creates a clean checkpoint from entries, with the last comparator-equivalent key winning.</summary>
    /// <param name="entries">Entries in any order.</param>
    /// <param name="keyComparer">Key order, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <param name="valueComparer">Value equality, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>A map containing <paramref name="entries"/> and no changes.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="entries"/> is <see langword="null"/>.</exception>
    public static PersistentDeltaMap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> entries,
        IComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var keys = keyComparer ?? Comparer<TKey>.Default;
        var values = valueComparer ?? EqualityComparer<TValue>.Default;
        var state = SortedDictionary<TKey, TValue>.CreateRange(entries, keys);
        return state.IsEmpty && ReferenceEquals(keys, Comparer<TKey>.Default) && ReferenceEquals(values, EqualityComparer<TValue>.Default)
            ? EmptyDefault
            : new PersistentDeltaMap<TKey, TValue>(
                state,
                state,
                SortedDictionary<TKey, DeltaRecord>.Create(keys),
                values);
    }

    /// <summary>Gets the number of current entries. O(1) amortized.</summary>
    public int Count => _current.Count;

    /// <summary>Gets a value indicating whether the current map is empty. O(1).</summary>
    public bool IsEmpty => _current.IsEmpty;

    /// <summary>Gets the number of exact net-changed key classes. O(1) amortized.</summary>
    public int ChangeCount => _changes.Count;

    /// <summary>Gets a value indicating whether the current map differs semantically from its checkpoint.</summary>
    public bool HasChanges => !_changes.IsEmpty;

    /// <summary>Gets the comparer defining key equivalence and ascending enumeration order.</summary>
    public IComparer<TKey> Comparer => _current.Comparer;

    /// <summary>Gets the comparer defining value no-ops and change cancellation.</summary>
    public IEqualityComparer<TValue> ValueComparer => _valueComparer;

    /// <summary>Gets the current persistent ordered-map snapshot. O(1).</summary>
    public SortedDictionary<TKey, TValue> CurrentSnapshot => _current;

    /// <summary>Gets the checkpoint persistent ordered-map snapshot. O(1).</summary>
    public SortedDictionary<TKey, TValue> CheckpointSnapshot => _checkpoint;

    /// <summary>Gets the current keys in ascending order.</summary>
    public IEnumerable<TKey> Keys => _current.Keys;

    /// <summary>Gets the current values in ascending key order.</summary>
    public IEnumerable<TValue> Values => _current.Values;

    /// <summary>Gets the current value associated with <paramref name="key"/>. O(log N).</summary>
    /// <param name="key">Key to look up.</param>
    /// <returns>The current value.</returns>
    /// <exception cref="KeyNotFoundException"><paramref name="key"/> is absent.</exception>
    public TValue this[TKey key] => _current[key];

    /// <summary>Gets the least current entry by key. O(1).</summary>
    public KeyValuePair<TKey, TValue> MinEntry => _current.MinEntry;

    /// <summary>Gets the greatest current entry by key. O(1).</summary>
    public KeyValuePair<TKey, TValue> MaxEntry => _current.MaxEntry;

    /// <summary>Determines whether a current entry exists. O(log N).</summary>
    /// <param name="key">Key to test.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool ContainsKey(TKey key) => _current.ContainsKey(key);

    /// <summary>Looks up a current value. O(log N).</summary>
    /// <param name="key">Key to look up.</param>
    /// <param name="value">The value when present; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value) => _current.TryGetValue(key, out value);

    /// <summary>Returns the current entry at zero-based key rank. O(log N).</summary>
    /// <param name="index">Zero-based rank.</param>
    /// <returns>The entry at <paramref name="index"/>.</returns>
    public KeyValuePair<TKey, TValue> EntryAt(int index) => _current.EntryAt(index);

    /// <summary>Returns a current key's zero-based rank, or -1 when absent. O(log N).</summary>
    /// <param name="key">Key to locate.</param>
    /// <returns>The key rank, or -1.</returns>
    public int IndexOfKey(TKey key) => _current.IndexOfKey(key);

    /// <summary>Adds or replaces one current entry and coalesces its checkpoint-relative change. O(log N).</summary>
    /// <param name="key">Key to set.</param>
    /// <param name="value">Value to store.</param>
    /// <returns>The unchanged instance for a semantic value no-op; otherwise a persistent successor.</returns>
    /// <exception cref="OverflowException">The current map or change index already has maximum capacity.</exception>
    public PersistentDeltaMap<TKey, TValue> SetItem(TKey key, TValue value)
    {
        var wasPresent = TryGetCurrentEntry(key, out var currentEntry);
        if (wasPresent && _valueComparer.Equals(currentEntry.Value, value))
            return this;

        var hadChange = TryGetDeltaEntry(key, out var changeEntry);
        var representative = wasPresent ? currentEntry.Key : hadChange ? changeEntry.Key : key;
        var nextCurrent = _current.SetItem(representative, value);
        var before = hadChange
            ? changeEntry.Value.Before
            : wasPresent ? DeltaMapValue<TValue>.Present(currentEntry.Value) : DeltaMapValue<TValue>.Absent;
        var after = DeltaMapValue<TValue>.Present(value);
        var nextChanges = Equivalent(before, after)
            ? _changes.Remove(representative)
            : _changes.SetItem(representative, new DeltaRecord(before, after));
        if (nextChanges.IsEmpty)
            nextCurrent = _checkpoint;
        return new PersistentDeltaMap<TKey, TValue>(nextCurrent, _checkpoint, nextChanges, _valueComparer);
    }

    /// <summary>
    /// Applies a sequence of entries in enumeration order, exactly as repeated <see cref="SetItem"/> would.
    /// O(m log N) for m entries.
    /// </summary>
    /// <param name="entries">Entries to assign, in application order; a later comparator-equivalent key wins.</param>
    /// <returns>
    /// The unchanged instance when <paramref name="entries"/> is empty or every entry is a semantic value
    /// no-op; otherwise a persistent successor.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="entries"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The current map or change index already has maximum capacity.</exception>
    /// <remarks>
    /// This is a convenience and an allocation saving over repeated single assignment, not a different
    /// algorithm: it is exactly the left fold of <see cref="SetItem"/> over <paramref name="entries"/>, so
    /// every coalescing, cancellation, representative-retention, and checkpoint-snap rule holds verbatim and
    /// the asymptotic bound is that of the fold it replaces. Because each intermediate value is itself an
    /// immutable published-nothing successor, a policy exception thrown partway leaves the receiver and every
    /// retained branch unchanged.
    /// </remarks>
    public PersistentDeltaMap<TKey, TValue> SetItems(IEnumerable<KeyValuePair<TKey, TValue>> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var result = this;
        foreach (var entry in entries)
            result = result.SetItem(entry.Key, entry.Value);
        return result;
    }

    /// <summary>Removes one current entry and coalesces its checkpoint-relative change. O(log N).</summary>
    /// <param name="key">Key to remove.</param>
    /// <returns>The unchanged instance when absent; otherwise a persistent successor.</returns>
    /// <exception cref="OverflowException">Recording the removal would exceed change-index capacity.</exception>
    public PersistentDeltaMap<TKey, TValue> Remove(TKey key)
    {
        if (!TryGetCurrentEntry(key, out var currentEntry))
            return this;

        var hadChange = TryGetDeltaEntry(key, out var changeEntry);
        var representative = hadChange ? changeEntry.Key : currentEntry.Key;
        var nextCurrent = _current.Remove(currentEntry.Key);
        var before = hadChange
            ? changeEntry.Value.Before
            : DeltaMapValue<TValue>.Present(currentEntry.Value);
        var after = DeltaMapValue<TValue>.Absent;
        var nextChanges = Equivalent(before, after)
            ? _changes.Remove(representative)
            : _changes.SetItem(representative, new DeltaRecord(before, after));
        if (nextChanges.IsEmpty)
            nextCurrent = _checkpoint;
        return new PersistentDeltaMap<TKey, TValue>(nextCurrent, _checkpoint, nextChanges, _valueComparer);
    }

    /// <summary>
    /// Makes the current snapshot the new checkpoint and resets the change index. O(1).
    /// </summary>
    /// <returns>A clean map sharing all current-state storage.</returns>
    public PersistentDeltaMap<TKey, TValue> Checkpoint()
    {
        if (ReferenceEquals(_current, _checkpoint) && _changes.IsEmpty)
            return this;
        return new PersistentDeltaMap<TKey, TValue>(
            _current,
            _current,
            SortedDictionary<TKey, DeltaRecord>.Create(Comparer),
            _valueComparer);
    }

    /// <summary>
    /// Returns to this value's checkpoint and resets the change index. O(1).
    /// </summary>
    /// <returns>A clean map sharing the checkpoint state.</returns>
    public PersistentDeltaMap<TKey, TValue> Rollback()
    {
        if (ReferenceEquals(_current, _checkpoint) && _changes.IsEmpty)
            return this;
        return new PersistentDeltaMap<TKey, TValue>(
            _checkpoint,
            _checkpoint,
            SortedDictionary<TKey, DeltaRecord>.Create(Comparer),
            _valueComparer);
    }

    /// <summary>Looks up one exact checkpoint-relative net change. O(log(k + 1)).</summary>
    /// <param name="key">Key class to inspect.</param>
    /// <param name="change">The change when present; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the key has a net change.</returns>
    public bool TryGetChange(TKey key, out PersistentMapChange<TKey, TValue> change)
    {
        if (TryGetDeltaEntry(key, out var entry))
        {
            change = new PersistentMapChange<TKey, TValue>(entry.Key, entry.Value.Before, entry.Value.After);
            return true;
        }

        change = default;
        return false;
    }

    /// <summary>Enumerates the exact net changes once each in ascending key order. Theta(k + 1) when fully consumed.</summary>
    /// <returns>A lazy, repeatable enumeration over this immutable version's changes.</returns>
    public IEnumerable<PersistentMapChange<TKey, TValue>> GetChanges()
    {
        foreach (var entry in _changes)
            yield return new PersistentMapChange<TKey, TValue>(entry.Key, entry.Value.Before, entry.Value.After);
    }

    /// <summary>
    /// Enumerates the exact net changes whose keys lie in the inclusive range <c>[low, high]</c>, once each in
    /// the same ascending key order <see cref="GetChanges()"/> uses. O(log(k + 1)) to seek the range boundaries
    /// plus Theta(1) per yielded change, which is output-sensitive rather than a scan of all k changes.
    /// </summary>
    /// <param name="low">Inclusive lower key bound.</param>
    /// <param name="high">Inclusive upper key bound.</param>
    /// <returns>
    /// A lazy, repeatable enumeration over this immutable version's in-range changes; empty when
    /// <paramref name="low"/> compares greater than <paramref name="high"/>, matching
    /// <see cref="GetRange"/> on an inverted range.
    /// </returns>
    /// <remarks>
    /// The change index is itself an ordered map under the collection's key comparer, so the restriction is a
    /// boundary seek plus a bounded walk over the in-range sub-map, never a filter over the full change set.
    /// The seek is performed on first enumeration and invokes only key-order comparisons.
    /// </remarks>
    public IEnumerable<PersistentMapChange<TKey, TValue>> GetChanges(TKey low, TKey high)
    {
        foreach (var entry in _changes.GetRange(low, high))
            yield return new PersistentMapChange<TKey, TValue>(entry.Key, entry.Value.Before, entry.Value.After);
    }

    /// <summary>Finds the current floor entry. O(log N).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The floor entry when one exists.</param>
    /// <returns><see langword="true"/> when a floor exists.</returns>
    public bool TryFloorEntry(TKey key, out KeyValuePair<TKey, TValue> entry) => _current.TryFloorEntry(key, out entry);

    /// <summary>Finds the current ceiling entry. O(log N).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The ceiling entry when one exists.</param>
    /// <returns><see langword="true"/> when a ceiling exists.</returns>
    public bool TryCeilingEntry(TKey key, out KeyValuePair<TKey, TValue> entry) => _current.TryCeilingEntry(key, out entry);

    /// <summary>Finds the current strict predecessor. O(log N).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The predecessor when one exists.</param>
    /// <returns><see langword="true"/> when a predecessor exists.</returns>
    public bool TryLowerEntry(TKey key, out KeyValuePair<TKey, TValue> entry) => _current.TryLowerEntry(key, out entry);

    /// <summary>Finds the current strict successor. O(log N).</summary>
    /// <param name="key">Reference key.</param>
    /// <param name="entry">The successor when one exists.</param>
    /// <returns><see langword="true"/> when a successor exists.</returns>
    public bool TryHigherEntry(TKey key, out KeyValuePair<TKey, TValue> entry) => _current.TryHigherEntry(key, out entry);

    /// <summary>Returns a plain current-state snapshot for the inclusive key range. O(log N).</summary>
    /// <param name="low">Inclusive lower key.</param>
    /// <param name="high">Inclusive upper key.</param>
    /// <returns>The current entries in range.</returns>
    public SortedDictionary<TKey, TValue> GetRange(TKey low, TKey high) => _current.GetRange(low, high);

    /// <summary>Copies current entries to a sorted array. O(n).</summary>
    /// <returns>Current entries in ascending key order.</returns>
    public KeyValuePair<TKey, TValue>[] ToArray() => _current.ToArray();

    /// <summary>Returns an enumerator over current entries in ascending key order.</summary>
    /// <returns>An enumerator over current entries.</returns>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => _current.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        _checkpoint.ValidateInvariants();
        _current.ValidateInvariants();
        _changes.ValidateInvariants();

        foreach (var entry in _changes)
        {
            var record = entry.Value;
            if (Equivalent(record.Before, record.After))
                throw new InvalidOperationException("PersistentDeltaMap invariant violated: a delta has equal endpoints.");

            var beforePresent = TryGetEntry(_checkpoint, entry.Key, out var beforeEntry);
            var afterPresent = TryGetEntry(_current, entry.Key, out var afterEntry);
            if (beforePresent != record.Before.HasValue
                || (beforePresent && !_valueComparer.Equals(beforeEntry.Value, record.Before.Value)))
            {
                throw new InvalidOperationException("PersistentDeltaMap invariant violated: a delta's before endpoint differs from the checkpoint.");
            }

            if (afterPresent != record.After.HasValue
                || (afterPresent && !_valueComparer.Equals(afterEntry.Value, record.After.Value)))
            {
                throw new InvalidOperationException("PersistentDeltaMap invariant violated: a delta's after endpoint differs from current state.");
            }
        }

        var expectedChanges = 0;
        foreach (var entry in _checkpoint)
        {
            var currentPresent = TryGetEntry(_current, entry.Key, out var currentEntry);
            if (!currentPresent || !_valueComparer.Equals(entry.Value, currentEntry.Value))
            {
                expectedChanges++;
                if (!TryGetDeltaEntry(entry.Key, out _))
                    throw new InvalidOperationException("PersistentDeltaMap invariant violated: a checkpoint difference has no delta.");
            }
        }

        foreach (var entry in _current)
        {
            if (!TryGetEntry(_checkpoint, entry.Key, out _))
            {
                expectedChanges++;
                if (!TryGetDeltaEntry(entry.Key, out _))
                    throw new InvalidOperationException("PersistentDeltaMap invariant violated: a current addition has no delta.");
            }
        }

        if (expectedChanges != _changes.Count)
            throw new InvalidOperationException("PersistentDeltaMap invariant violated: the delta cardinality is not exact.");
    }

    private bool TryGetCurrentEntry(TKey key, out KeyValuePair<TKey, TValue> entry) =>
        TryGetEntry(_current, key, out entry);

    private bool TryGetDeltaEntry(TKey key, out KeyValuePair<TKey, DeltaRecord> entry) =>
        TryGetEntry(_changes, key, out entry);

    private bool TryGetEntry<TValueAtKey>(
        SortedDictionary<TKey, TValueAtKey> map,
        TKey key,
        out KeyValuePair<TKey, TValueAtKey> entry) =>
        map.TryCeilingEntry(key, out entry) && Comparer.Compare(entry.Key, key) == 0;

    private bool Equivalent(DeltaMapValue<TValue> left, DeltaMapValue<TValue> right) =>
        left.HasValue == right.HasValue
        && (!left.HasValue || _valueComparer.Equals(left.Value, right.Value));
}
