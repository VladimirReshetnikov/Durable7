// Persistent insertion-ordered map shaped for Tungsten-Association semantics.
//
// Composition (docs/reference/derived-structure-catalog.md, "PersistentOrderedMap" pattern with
// the values-in-both variant, specialized by the Tungsten consumer case study):
//
//   - Index: PersistentHashMap<TKey, (stamp, value)> — allocation-free O(w + c) keyed lookup.
//   - Order: FingerTreeDeque<(stamp, key, value)> kept sorted by strictly ascending stamp —
//     O(log n) positional access, O(1) worst-case ends, hash-free ordered enumeration.
//   - Stamps are gapped order-maintenance labels (append max + G, prepend min - G, positional
//     insert the midpoint, full relabel on gap exhaustion). Stamps are the rendezvous key
//     between the two structures; positions and ranks are never stored in the hash side.
//
// The target semantics are the kernel-verified Tungsten Association behaviors recorded in the
// Tungsten design study (C:\Smithereens\src\Tungsten\docs\reports\
// 2026-07-03-list-association-persistent-backends.md) and re-verified against Tungsten 15.0
// on 2026-07-07:
//
//   1. Duplicate keys at construction keep the FIRST occurrence's position with the LAST value.
//   2. Append/Prepend on an existing key REMOVE the old entry and re-add at the end/front.
//   3. AssociateTo-style writes (SetItem) update in place, keeping the key's position;
//      new keys append at the end.
//   4. Positional operations (GetAt, Take, Drop, GetRange, RemoveAt, Insert, RemoveFirst,
//      RemoveLast, Reverse) act on association order.
//   5. Insert with an existing key gives the inserted occurrence's position and value, with the
//      position interpreted against the association BEFORE the old occurrence is removed.
//   6. Join keeps the first operand's key positions with the second operand's values; keys new
//      to the first operand append in the second operand's order.
//   7. Sort orders by values, KeySort by keys; sorted results are ordinary associations that do
//      not stay sorted under later writes.

using System.Collections;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using Tools.DataStructures.FingerTree;
using Tools.DataStructures.Hamt;

namespace Tools.DataStructures.Tungsten;

/// <summary>
/// Non-instantiable factory entry points for <see cref="PersistentAssociation{TKey, TValue}"/>
/// that infer the key and value types from the arguments.
/// </summary>
public static class PersistentAssociation
{
    /// <summary>
    /// Creates a persistent association from a key/value pair sequence.
    /// </summary>
    /// <typeparam name="TKey">Key type.</typeparam>
    /// <typeparam name="TValue">Value type.</typeparam>
    /// <param name="pairs">Pairs in front-to-back association order.</param>
    /// <param name="comparer">Key equality comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>
    /// An association containing <paramref name="pairs"/> in enumeration order. A key occurring
    /// several times keeps its first occurrence's position with its last occurrence's value,
    /// matching Tungsten <c>Association</c> construction.
    /// </returns>
    /// <remarks>O(n (w + c)) for n distinct keys, where w ≤ 7 is the hash-trie depth and c the equal-hash collision-bucket scan; each duplicate key adds an O(log n) in-place update.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="pairs"/> is <see langword="null"/>.</exception>
    public static PersistentAssociation<TKey, TValue> CreateRange<TKey, TValue>(
        IEnumerable<KeyValuePair<TKey, TValue>> pairs,
        IEqualityComparer<TKey>? comparer = null)
        where TKey : notnull =>
        PersistentAssociation<TKey, TValue>.CreateRange(pairs, comparer);
}

/// <summary>
/// An immutable, persistent, insertion-ordered key/value map shaped for representing a Tungsten
/// Language <c>Association</c> expression: <c>&lt;|k1 -&gt; v1, k2 -&gt; v2, ...|&gt;</c>.
/// </summary>
/// <typeparam name="TKey">Key type. Keys are compared with the association's
/// <see cref="Comparer"/> and must not be <see langword="null"/>.</typeparam>
/// <typeparam name="TValue">Value type.</typeparam>
/// <remarks>
/// <para>
/// The association couples a persistent hash-array mapped trie (keyed lookup) with a persistent
/// finger-tree sequence (association order), so both access styles are first-class: keyed reads
/// (<see cref="this[TKey]"/>, <see cref="TryGetValue"/>) are O(w + c) hash probes that allocate
/// nothing, ordered reads (<see cref="Keys"/>, <see cref="Values"/>, enumeration,
/// <see cref="GetAt"/>) stream off the sequence without hashing, and positional structure edits
/// (<see cref="RemoveAt"/>, <see cref="Insert"/>, <see cref="Take"/>, <see cref="Drop"/>) are
/// O(log n) plus the hash-side work for the keys they touch. Every mutating-style operation
/// returns a new association that shares structure with its source; existing versions are never
/// observably changed, including under branching histories.
/// </para>
/// <para>
/// Tungsten operation correspondence (indexing is zero-based here, one-based in Tungsten):
/// <c>assoc[k]</c> → <see cref="this[TKey]"/> / <see cref="TryGetValue"/> (absence is an
/// exception or <see langword="false"/> rather than <c>Missing["KeyAbsent", k]</c> — a client
/// engine maps absence to its own missing value), <c>AssociateTo</c> / <c>assoc[k] = v</c> →
/// <see cref="SetItem"/>, <c>Append</c> → <see cref="Append"/>, <c>Prepend</c> →
/// <see cref="Prepend"/>, <c>Join</c> → <see cref="Join"/> and <see cref="SetItems"/>,
/// <c>KeyDrop</c> → <see cref="Remove"/> and <see cref="RemoveRange"/>, <c>KeyTake</c> →
/// <see cref="KeyTake"/>, <c>KeyExistsQ</c> → <see cref="ContainsKey"/>, <c>Keys</c> →
/// <see cref="Keys"/>, <c>Values</c> → <see cref="Values"/>, <c>Normal</c> → enumeration,
/// <c>assoc[[n]]</c> → <see cref="GetAt"/>, <c>Insert</c> → <see cref="Insert"/>,
/// <c>Delete</c> → <see cref="RemoveAt"/>, <c>Take</c>/<c>Drop</c> → <see cref="Take"/> /
/// <see cref="Drop"/>, <c>First</c>/<c>Last</c> → <see cref="First"/> / <see cref="Last"/>,
/// <c>Rest</c>/<c>Most</c> → <see cref="RemoveFirst"/> / <see cref="RemoveLast"/>,
/// <c>Reverse</c> → <see cref="Reverse"/>, <c>Sort</c> → <see cref="Sort"/>, <c>KeySort</c> →
/// <see cref="KeySort"/>.
/// </para>
/// <para>
/// Ordering semantics follow the kernel-verified Tungsten rules: construction keeps a duplicate
/// key's first position with its last value; <see cref="SetItem"/> updates an existing key in
/// place; <see cref="Append"/>/<see cref="Prepend"/> move an existing key to the end/front;
/// <see cref="Join"/> keeps this association's positions and takes the argument's values.
/// </para>
/// <para>
/// Writes that change nothing observable return the same instance: <see cref="SetItem"/> with a
/// value equal (by <see cref="EqualityComparer{T}.Default"/>) to the stored one,
/// <see cref="Remove"/> of an absent key, and empty-argument bulk operations. Callers can use
/// reference equality as a "nothing changed" signal.
/// </para>
/// <para>
/// Association order is maintained with gapped stamp labels shared by both internal structures.
/// End insertions consume one label step each; a positional <see cref="Insert"/> takes the
/// midpoint of its neighbors' labels and triggers a full O(n (w + c)) relabel of the
/// association only when the local gap is exhausted (at least 20 consecutive same-point inserts
/// after a fresh labeling). The relabel cost is per produced version and is not amortized under
/// branching persistence: re-branching repeatedly from the same pre-relabel version re-pays it.
/// All other operations never relabel.
/// </para>
/// <para>
/// The type is a reference type; <c>default(PersistentAssociation&lt;TKey, TValue&gt;)</c> is
/// <see langword="null"/>, not an empty association. Use <see cref="Empty"/> or
/// <see cref="Create"/> for empty values. Instances are safe for concurrent readers without
/// synchronization.
/// </para>
/// </remarks>
public sealed class PersistentAssociation<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
    where TKey : notnull
{
    /// <summary>
    /// Label gap between adjacent stamps assigned by end insertions and relabels. A fresh gap
    /// supports 20 midpoint splits (2^20) at one point before a relabel.
    /// </summary>
    private const long StampGap = 1L << 20;

    private static readonly PersistentAssociation<TKey, TValue> EmptyInstance = new(
        FingerTreeDeque<Entry>.Empty,
        PersistentHashMap<TKey, Slot>.Empty);

    /// <summary>Entries in association order, sorted by strictly ascending stamp.</summary>
    private readonly FingerTreeDeque<Entry> _entries;

    /// <summary>Keyed index carrying each key's stamp and value (values live in both structures).</summary>
    private readonly PersistentHashMap<TKey, Slot> _index;

    private PersistentAssociation(FingerTreeDeque<Entry> entries, PersistentHashMap<TKey, Slot> index)
    {
        Debug.Assert(entries.Count == index.Count, "entry sequence and keyed index must stay in lockstep");
        _entries = entries;
        _index = index;
    }

    private readonly struct Slot(long stamp, TValue value)
    {
        public readonly long Stamp = stamp;
        public readonly TValue Value = value;
    }

    internal readonly struct Entry(long stamp, TKey key, TValue value)
    {
        public readonly long Stamp = stamp;
        public readonly TKey Key = key;
        public readonly TValue Value = value;

        public KeyValuePair<TKey, TValue> ToPair() => new(Key, Value);
    }

    private sealed class StampOrder : IComparer<Entry>
    {
        public static readonly StampOrder Instance = new();

        public int Compare(Entry x, Entry y) => x.Stamp.CompareTo(y.Stamp);
    }

    /// <summary>
    /// Gets the canonical empty association using <see cref="EqualityComparer{T}.Default"/> for keys.
    /// </summary>
    /// <remarks>O(1). A single shared instance; empty results with the default comparer reuse it.</remarks>
    public static PersistentAssociation<TKey, TValue> Empty => EmptyInstance;

    /// <summary>
    /// Gets the number of entries. Tungsten: <c>Length</c>.
    /// </summary>
    /// <remarks>O(1).</remarks>
    public int Count => _entries.Count;

    /// <summary>
    /// Gets a value indicating whether the association has no entries.
    /// </summary>
    /// <remarks>O(1).</remarks>
    public bool IsEmpty => _entries.IsEmpty;

    /// <summary>
    /// Gets the key equality comparer supplied at creation.
    /// </summary>
    /// <remarks>O(1). Derived associations preserve the comparer.</remarks>
    public IEqualityComparer<TKey> Comparer => _index.Comparer;

    /// <summary>
    /// Gets the first entry in association order. Tungsten: <c>First</c> (which returns the value; take <c>.Value</c>).
    /// </summary>
    /// <remarks>O(1) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The association is empty.</exception>
    public KeyValuePair<TKey, TValue> First =>
        IsEmpty ? throw EmptyError() : _entries.First.ToPair();

    /// <summary>
    /// Gets the last entry in association order. Tungsten: <c>Last</c> (which returns the value; take <c>.Value</c>).
    /// </summary>
    /// <remarks>O(1) worst-case.</remarks>
    /// <exception cref="InvalidOperationException">The association is empty.</exception>
    public KeyValuePair<TKey, TValue> Last =>
        IsEmpty ? throw EmptyError() : _entries.Last.ToPair();

    /// <summary>
    /// Gets the keys in association order. Tungsten: <c>Keys</c>.
    /// </summary>
    /// <remarks>O(n) to enumerate, streaming off the order structure without hashing.</remarks>
    public IEnumerable<TKey> Keys
    {
        get
        {
            foreach (var entry in _entries)
                yield return entry.Key;
        }
    }

    /// <summary>
    /// Gets the values in association order. Tungsten: <c>Values</c>.
    /// </summary>
    /// <remarks>O(n) to enumerate, streaming off the order structure without hashing.</remarks>
    public IEnumerable<TValue> Values
    {
        get
        {
            foreach (var entry in _entries)
                yield return entry.Value;
        }
    }

    /// <summary>
    /// Gets the value stored under a key. Tungsten: <c>assoc[k]</c>.
    /// </summary>
    /// <param name="key">Lookup key.</param>
    /// <remarks>
    /// O(w + c) where w ≤ 7 is the hash-trie depth and c the equal-hash collision-bucket scan;
    /// allocates nothing. Tungsten returns <c>Missing["KeyAbsent", k]</c> for an absent key; this
    /// indexer throws instead — clients that need missing-value semantics use
    /// <see cref="TryGetValue"/>.
    /// </remarks>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public TValue this[TKey key] =>
        TryGetValue(key, out var value)
            ? value
            : throw new KeyNotFoundException($"The key '{key}' is absent from the association.");

    /// <summary>
    /// Creates an empty association with an explicit key comparer.
    /// </summary>
    /// <param name="comparer">Key equality comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>An empty association whose derived associations all use <paramref name="comparer"/>.</returns>
    /// <remarks>O(1). With the default comparer this returns the shared <see cref="Empty"/> instance.</remarks>
    public static PersistentAssociation<TKey, TValue> Create(IEqualityComparer<TKey>? comparer = null)
    {
        var index = PersistentHashMap<TKey, Slot>.Create(comparer);
        return ReferenceEquals(index, PersistentHashMap<TKey, Slot>.Empty)
            ? EmptyInstance
            : new(FingerTreeDeque<Entry>.Empty, index);
    }

    /// <summary>
    /// Creates a persistent association from a key/value pair sequence.
    /// </summary>
    /// <param name="pairs">Pairs in front-to-back association order.</param>
    /// <param name="comparer">Key equality comparer, or <see langword="null"/> for <see cref="EqualityComparer{T}.Default"/>.</param>
    /// <returns>
    /// An association containing <paramref name="pairs"/> in enumeration order. A key occurring
    /// several times keeps its first occurrence's position with its last occurrence's value,
    /// matching Tungsten <c>Association</c> construction.
    /// </returns>
    /// <remarks>O(n (w + c)) for n distinct keys; each duplicate key adds an O(log n) in-place update.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="pairs"/> is <see langword="null"/>.</exception>
    public static PersistentAssociation<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> pairs,
        IEqualityComparer<TKey>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(pairs);
        return Create(comparer).SetItems(pairs);
    }

    /// <summary>
    /// Determines whether a key is present. Tungsten: <c>KeyExistsQ</c>.
    /// </summary>
    /// <param name="key">Lookup key.</param>
    /// <returns><see langword="true"/> when the key is present; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(w + c); allocates nothing.</remarks>
    public bool ContainsKey(TKey key) => _index.ContainsKey(key);

    /// <summary>
    /// Tries to get the value stored under a key. Tungsten: <c>Lookup</c>.
    /// </summary>
    /// <param name="key">Lookup key.</param>
    /// <param name="value">The stored value when present; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the key is present; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(w + c); allocates nothing.</remarks>
    public bool TryGetValue(TKey key, [System.Diagnostics.CodeAnalysis.MaybeNullWhen(false)] out TValue value)
    {
        if (_index.TryGetValue(key, out var slot))
        {
            value = slot.Value;
            return true;
        }

        value = default;
        return false;
    }

    /// <summary>
    /// Retrieves the stored key instance equal to a lookup key.
    /// </summary>
    /// <param name="equalKey">Lookup key.</param>
    /// <param name="actualKey">The stored key instance when present; otherwise <paramref name="equalKey"/>.</param>
    /// <returns><see langword="true"/> when an equal key is present; otherwise <see langword="false"/>.</returns>
    /// <remarks>
    /// O(w + c). Useful with comparers under which distinct key instances compare equal
    /// (canonical-instance recovery), mirroring the hash map's contract.
    /// </remarks>
    public bool TryGetKey(TKey equalKey, out TKey actualKey) => _index.TryGetKey(equalKey, out actualKey);

    /// <summary>
    /// Gets the entry at a zero-based position in association order. Tungsten: <c>assoc[[n]]</c>
    /// (one-based there, and returning only the value; take <c>.Value</c>).
    /// </summary>
    /// <param name="index">Zero-based position.</param>
    /// <returns>The key/value pair at the position.</returns>
    /// <remarks>O(log min(index + 1, Count - index)).</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or ≥ <see cref="Count"/>.</exception>
    public KeyValuePair<TKey, TValue> GetAt(int index) => _entries[index].ToPair();

    /// <summary>
    /// Finds the zero-based position of a key in association order.
    /// </summary>
    /// <param name="key">Lookup key.</param>
    /// <returns>The zero-based position, or -1 when the key is absent.</returns>
    /// <remarks>O(w + c + log n): one hash probe for the key's stamp, then a stamp search in the order structure.</remarks>
    public int IndexOfKey(TKey key) =>
        _index.TryGetValue(key, out var slot) ? IndexOfStamp(slot.Stamp) : -1;

    /// <summary>
    /// Returns an association with a key set to a value, updating an existing key in place.
    /// Tungsten: <c>AssociateTo</c> / <c>assoc[k] = v</c>.
    /// </summary>
    /// <param name="key">Key to set.</param>
    /// <param name="value">Value to store.</param>
    /// <returns>
    /// A new association; the source is unchanged. An existing key keeps its position and its
    /// originally stored key instance; a new key appends at the end. Returns the same instance
    /// when the key is present with an equal value (by <see cref="EqualityComparer{T}.Default"/>).
    /// </returns>
    /// <remarks>O(w + c + log n); O(w + c) amortized for a new key on a linear history.</remarks>
    public PersistentAssociation<TKey, TValue> SetItem(TKey key, TValue value)
    {
        if (!_index.TryGetValue(key, out var slot))
            return AppendNew(key, value);

        if (EqualityComparer<TValue>.Default.Equals(slot.Value, value))
            return this;

        // UpdateAt fuses the fetch and the replacement into one tree walk, and the hash map
        // retains its originally stored key instance on SetItem by contract, so neither side
        // needs a separate stored-key fetch.
        var position = IndexOfStamp(slot.Stamp);
        return new(
            _entries.UpdateAt(position, stored => new Entry(stored.Stamp, stored.Key, value)),
            _index.SetItem(key, new Slot(slot.Stamp, value)));
    }

    /// <summary>
    /// Returns an association with every pair set in order via <see cref="SetItem"/> semantics.
    /// </summary>
    /// <param name="pairs">Pairs to set, applied front to back.</param>
    /// <returns>
    /// A new association; the source is unchanged. Existing keys keep their positions with the
    /// last supplied value; new keys append in first-occurrence order. Returns the same instance
    /// when nothing changes.
    /// </returns>
    /// <remarks>O(m (w + c)) for m pairs when the keys are new (appends); each pair whose key already exists costs O(w + c + log n). The source association is never rebuilt wholesale.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="pairs"/> is <see langword="null"/>.</exception>
    public PersistentAssociation<TKey, TValue> SetItems(IEnumerable<KeyValuePair<TKey, TValue>> pairs)
    {
        ArgumentNullException.ThrowIfNull(pairs);
        var result = this;
        foreach (var pair in pairs)
            result = result.SetItem(pair.Key, pair.Value);
        return result;
    }

    /// <summary>
    /// Returns the association joined with another, keeping this association's key positions.
    /// Tungsten: <c>Join</c>.
    /// </summary>
    /// <param name="other">Association whose entries are set over this one.</param>
    /// <returns>
    /// A new association; both sources are unchanged. Keys of <paramref name="other"/> already
    /// present here keep their positions with <paramref name="other"/>'s values; new keys append
    /// in <paramref name="other"/>'s order. Returns the same instance when nothing changes.
    /// </returns>
    /// <remarks>
    /// O(m (w + c + log n)) worst-case and O(m (w + c)) for all-new keys, for m entries in
    /// <paramref name="other"/> — small-into-large never touches the large side's untouched
    /// entries. Key equality uses this association's <see cref="Comparer"/>.
    /// </remarks>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    public PersistentAssociation<TKey, TValue> Join(PersistentAssociation<TKey, TValue> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (IsEmpty && ReferenceEquals(Comparer, other.Comparer))
            return other;
        return SetItems(other);
    }

    /// <summary>
    /// Returns an association with a key/value pair at the end. Tungsten: <c>Append</c>.
    /// </summary>
    /// <param name="key">Key to append.</param>
    /// <param name="value">Value to store.</param>
    /// <returns>
    /// A new association ending with the pair; the source is unchanged. An existing key is
    /// removed from its old position and re-added at the end with the supplied key instance and
    /// value, matching the kernel-verified Tungsten rule. Returns the same instance when the key
    /// is already last with an equal value.
    /// </returns>
    /// <remarks>O(w + c + log n).</remarks>
    public PersistentAssociation<TKey, TValue> Append(TKey key, TValue value)
    {
        if (!_index.TryGetValue(key, out var slot))
            return AppendNew(key, value);

        var position = IndexOfStamp(slot.Stamp);
        if (position == Count - 1 && EqualityComparer<TValue>.Default.Equals(slot.Value, value))
            return this;

        var entries = _entries.RemoveAt(position);
        return WithEntryInserted(entries, _index.Remove(key), entries.Count, key, value);
    }

    /// <summary>
    /// Returns an association with a key/value pair at the front. Tungsten: <c>Prepend</c>.
    /// </summary>
    /// <param name="key">Key to prepend.</param>
    /// <param name="value">Value to store.</param>
    /// <returns>
    /// A new association starting with the pair; the source is unchanged. An existing key is
    /// removed from its old position and re-added at the front with the supplied key instance
    /// and value, matching the kernel-verified Tungsten rule. Returns the same instance when the
    /// key is already first with an equal value.
    /// </returns>
    /// <remarks>O(w + c + log n).</remarks>
    public PersistentAssociation<TKey, TValue> Prepend(TKey key, TValue value)
    {
        if (_index.TryGetValue(key, out var slot))
        {
            var position = IndexOfStamp(slot.Stamp);
            if (position == 0 && EqualityComparer<TValue>.Default.Equals(slot.Value, value))
                return this;

            var trimmed = _entries.RemoveAt(position);
            return WithEntryInserted(trimmed, _index.Remove(key), 0, key, value);
        }

        return WithEntryInserted(_entries, _index, 0, key, value);
    }

    /// <summary>
    /// Returns an association with a pair inserted before a zero-based position. Tungsten:
    /// <c>Insert</c> (one-based there).
    /// </summary>
    /// <param name="index">Zero-based insertion position, interpreted against the current
    /// entries including any old occurrence of <paramref name="key"/>; <see cref="Count"/>
    /// appends at the end.</param>
    /// <param name="key">Key to insert.</param>
    /// <param name="value">Value to store.</param>
    /// <returns>
    /// A new association; the source is unchanged. When the key already exists, the inserted
    /// occurrence wins both position and value and the old occurrence disappears, matching the
    /// kernel-verified Tungsten rule.
    /// </returns>
    /// <remarks>
    /// O(w + c + log n) while the local stamp gap lasts; O(n (w + c)) when the gap between
    /// the insertion point's neighbors is exhausted and the association relabels (at least 20
    /// consecutive same-point inserts after a fresh labeling; see the class remarks).
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentAssociation<TKey, TValue> Insert(int index, TKey key, TValue value)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(index);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(index, Count);

        var entries = _entries;
        var indexSide = _index;
        var insertAt = index;
        if (indexSide.TryGetValue(key, out var slot))
        {
            var oldPosition = IndexOfStamp(slot.Stamp);
            entries = entries.RemoveAt(oldPosition);
            indexSide = indexSide.Remove(key);
            if (oldPosition < insertAt)
                insertAt--;
        }

        return WithEntryInserted(entries, indexSide, insertAt, key, value);
    }

    /// <summary>
    /// Returns an association without a key. Tungsten: <c>KeyDrop</c> with one key.
    /// </summary>
    /// <param name="key">Key to remove.</param>
    /// <returns>A new association; the source is unchanged. Returns the same instance when the key is absent.</returns>
    /// <remarks>O(w + c + log n).</remarks>
    public PersistentAssociation<TKey, TValue> Remove(TKey key) =>
        TryRemove(key, out var result, out _) ? result : this;

    /// <summary>
    /// Tries to remove a key, reporting the removed value.
    /// </summary>
    /// <param name="key">Key to remove.</param>
    /// <param name="result">The association without the key on success; the current instance otherwise.</param>
    /// <param name="value">The removed value on success; otherwise <see langword="default"/>.</param>
    /// <returns><see langword="true"/> when the key was present; otherwise <see langword="false"/>.</returns>
    /// <remarks>O(w + c + log n).</remarks>
    public bool TryRemove(
        TKey key,
        out PersistentAssociation<TKey, TValue> result,
        [System.Diagnostics.CodeAnalysis.MaybeNullWhen(false)] out TValue value)
    {
        if (!_index.TryRemove(key, out var trimmedIndex, out var slot))
        {
            result = this;
            value = default;
            return false;
        }

        var entries = _entries.RemoveAt(IndexOfStamp(slot.Stamp));
        result = entries.IsEmpty && ReferenceEquals(trimmedIndex, PersistentHashMap<TKey, Slot>.Empty)
            ? EmptyInstance
            : new(entries, trimmedIndex);
        value = slot.Value;
        return true;
    }

    /// <summary>
    /// Returns an association without any of the specified keys. Tungsten: <c>KeyDrop</c>.
    /// </summary>
    /// <param name="keys">Keys to remove; absent keys are skipped.</param>
    /// <returns>A new association preserving the order of the surviving entries; the source is
    /// unchanged. Returns the same instance when no key was present.</returns>
    /// <remarks>O(m (w + c + log n)) for m keys.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="keys"/> is <see langword="null"/>.</exception>
    public PersistentAssociation<TKey, TValue> RemoveRange(IEnumerable<TKey> keys)
    {
        ArgumentNullException.ThrowIfNull(keys);
        var result = this;
        foreach (var key in keys)
            result = result.Remove(key);
        return result;
    }

    /// <summary>
    /// Returns the sub-association of the specified keys in the requested order. Tungsten:
    /// <c>KeyTake</c>.
    /// </summary>
    /// <param name="keys">Keys to keep, in the order the result should present them; absent keys
    /// are skipped and a key repeated in <paramref name="keys"/> keeps its first requested
    /// position.</param>
    /// <returns>A new association with the requested entries; the source is unchanged.</returns>
    /// <remarks>O(m (w + c)) for m requested keys; never touches unrequested entries and never searches the order structure.</remarks>
    /// <exception cref="ArgumentNullException"><paramref name="keys"/> is <see langword="null"/>.</exception>
    public PersistentAssociation<TKey, TValue> KeyTake(IEnumerable<TKey> keys)
    {
        ArgumentNullException.ThrowIfNull(keys);
        var result = Create(Comparer);
        foreach (var key in keys)
        {
            if (_index.TryGetValue(key, out var slot) && !result.ContainsKey(key))
            {
                // The result presents the stored key instances; TryGetKey recovers one in a
                // hash probe instead of a stamp search over the order structure.
                _index.TryGetKey(key, out var storedKey);
                result = result.AppendNew(storedKey, slot.Value);
            }
        }
        return result;
    }

    /// <summary>
    /// Returns an association without the entry at a zero-based position. Tungsten: <c>Delete</c>
    /// (one-based there).
    /// </summary>
    /// <param name="index">Zero-based position.</param>
    /// <returns>A new association one entry shorter; the source is unchanged.</returns>
    /// <remarks>O(w + c + log min(index + 1, Count - index)) amortized.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is negative or ≥ <see cref="Count"/>.</exception>
    public PersistentAssociation<TKey, TValue> RemoveAt(int index)
    {
        var entry = _entries[index];
        var entries = _entries.RemoveAt(index);
        var trimmedIndex = _index.Remove(entry.Key);
        return entries.IsEmpty && ReferenceEquals(trimmedIndex, PersistentHashMap<TKey, Slot>.Empty)
            ? EmptyInstance
            : new(entries, trimmedIndex);
    }

    /// <summary>
    /// Returns the association without its first entry. Tungsten: <c>Rest</c>.
    /// </summary>
    /// <returns>A new association one entry shorter; the source is unchanged.</returns>
    /// <remarks>O(w + c) amortized — the ends of the order structure are O(1) amortized — and the surviving entries are shared, so Rest-driven recursion over an association costs O(n (w + c)) total, not quadratic.</remarks>
    /// <exception cref="InvalidOperationException">The association is empty.</exception>
    public PersistentAssociation<TKey, TValue> RemoveFirst() =>
        IsEmpty ? throw EmptyError() : RemoveAt(0);

    /// <summary>
    /// Returns the association without its last entry. Tungsten: <c>Most</c>.
    /// </summary>
    /// <returns>A new association one entry shorter; the source is unchanged.</returns>
    /// <remarks>O(w + c) amortized; O(w + c + log n) worst-case for a single call.</remarks>
    /// <exception cref="InvalidOperationException">The association is empty.</exception>
    public PersistentAssociation<TKey, TValue> RemoveLast() =>
        IsEmpty ? throw EmptyError() : RemoveAt(Count - 1);

    /// <summary>
    /// Returns the contiguous sub-association starting at a zero-based position. Tungsten:
    /// <c>Part</c> with a span (one-based there).
    /// </summary>
    /// <param name="index">Zero-based position of the first kept entry.</param>
    /// <param name="count">Number of entries to keep.</param>
    /// <returns>A new association with the kept entries in unchanged order; the source is unchanged.</returns>
    /// <remarks>
    /// O(log n) to slice the order structure plus O(min(kept, removed) (w + c)) to reconcile the
    /// keyed index — whichever of the kept and removed key sets is smaller drives the cost.
    /// </remarks>
    /// <exception cref="ArgumentOutOfRangeException">The range does not lie within the association.</exception>
    public PersistentAssociation<TKey, TValue> GetRange(int index, int count)
    {
        var split = _entries.SplitRange(index, count);
        var kept = split.Range;
        if (kept.Count == Count)
            return this;
        if (kept.IsEmpty)
            return Create(Comparer);

        var removedCount = Count - kept.Count;
        PersistentHashMap<TKey, Slot> indexSide;
        if (kept.Count <= removedCount)
        {
            indexSide = PersistentHashMap<TKey, Slot>.Create(Comparer);
            foreach (var entry in kept)
                indexSide = indexSide.SetItem(entry.Key, new Slot(entry.Stamp, entry.Value));
        }
        else
        {
            indexSide = _index;
            foreach (var entry in split.Before)
                indexSide = indexSide.Remove(entry.Key);
            foreach (var entry in split.After)
                indexSide = indexSide.Remove(entry.Key);
        }

        return new(kept, indexSide);
    }

    /// <summary>
    /// Returns the first <paramref name="count"/> entries. Tungsten: <c>Take[assoc, count]</c>.
    /// </summary>
    /// <param name="count">Number of leading entries to keep.</param>
    /// <returns>A new association; the source is unchanged.</returns>
    /// <remarks>Cost of <see cref="GetRange"/>.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentAssociation<TKey, TValue> Take(int count) => GetRange(0, count);

    /// <summary>
    /// Returns the association without its first <paramref name="count"/> entries. Tungsten:
    /// <c>Drop[assoc, count]</c>.
    /// </summary>
    /// <param name="count">Number of leading entries to remove.</param>
    /// <returns>A new association; the source is unchanged.</returns>
    /// <remarks>Cost of <see cref="GetRange"/>.</remarks>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="count"/> is negative or &gt; <see cref="Count"/>.</exception>
    public PersistentAssociation<TKey, TValue> Drop(int count) => GetRange(count, Count - count);

    /// <summary>
    /// Returns the association with entry order reversed. Tungsten: <c>Reverse</c>.
    /// </summary>
    /// <returns>A new association with freshly labeled entries; the source is unchanged.</returns>
    /// <remarks>
    /// O(n (w + c)), matching the reference Tungsten cost. An O(1) reversal bit was considered
    /// and rejected: it inverts the stamp-ascending invariant that keyed position lookups rely
    /// on, taxing every other operation with direction dispatch (see the design provenance note
    /// at the top of this file).
    /// </remarks>
    public PersistentAssociation<TKey, TValue> Reverse()
    {
        if (Count <= 1)
            return this;
        var entries = _entries.ToArray();
        Array.Reverse(entries);
        return Rebuilt(entries);
    }

    /// <summary>
    /// Returns the association sorted by key. Tungsten: <c>KeySort</c>.
    /// </summary>
    /// <param name="comparer">Key order comparer, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <returns>
    /// A new association with freshly labeled entries; the source is unchanged. The sort is
    /// stable, and the result is an ordinary association that does not stay sorted under later
    /// writes.
    /// </returns>
    /// <remarks>O(n log n) comparer calls plus an O(n (w + c)) rebuild. Exceptions thrown by the comparer — including the default comparer's failure for incomparable key types — propagate and leave the source unchanged.</remarks>
    public PersistentAssociation<TKey, TValue> KeySort(IComparer<TKey>? comparer = null) =>
        SortedBy(entry => entry.Key, comparer ?? System.Collections.Generic.Comparer<TKey>.Default);

    /// <summary>
    /// Returns the association sorted by value. Tungsten: <c>Sort</c> (which orders associations by their values).
    /// </summary>
    /// <param name="comparer">Value order comparer, or <see langword="null"/> for <see cref="Comparer{T}.Default"/>.</param>
    /// <returns>
    /// A new association with freshly labeled entries; the source is unchanged. The sort is
    /// stable, and the result is an ordinary association that does not stay sorted under later
    /// writes.
    /// </returns>
    /// <remarks>O(n log n) comparer calls plus an O(n (w + c)) rebuild. Exceptions thrown by the comparer — including the default comparer's failure for incomparable value types — propagate and leave the source unchanged.</remarks>
    public PersistentAssociation<TKey, TValue> Sort(IComparer<TValue>? comparer = null) =>
        SortedBy(entry => entry.Value, comparer ?? System.Collections.Generic.Comparer<TValue>.Default);

    /// <summary>
    /// Copies the entries to a new array in association order. Tungsten: <c>Normal</c>.
    /// </summary>
    /// <returns>A fresh array of length <see cref="Count"/>.</returns>
    /// <remarks>O(n), streaming off the order structure without hashing.</remarks>
    public KeyValuePair<TKey, TValue>[] ToArray()
    {
        var array = new KeyValuePair<TKey, TValue>[Count];
        var i = 0;
        foreach (var entry in _entries)
            array[i++] = entry.ToPair();
        return array;
    }

    /// <summary>
    /// Returns a non-allocating struct enumerator over the entries in association order.
    /// Tungsten: <c>Normal</c>.
    /// </summary>
    /// <returns>An enumerator positioned before the first entry.</returns>
    /// <remarks>
    /// Enumerates the immutable snapshot; it can never observe later versions. O(1) per step
    /// amortized, without hashing.
    /// </remarks>
    public Enumerator GetEnumerator() => new(_entries.GetEnumerator());

    IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator() =>
        GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>
    /// A struct enumerator over association entries in association order.
    /// </summary>
    /// <remarks>
    /// Wraps the order structure's struct enumerator; obtaining one allocates nothing on the
    /// heap. Enumerator instances must not be shared across threads.
    /// </remarks>
    public struct Enumerator : IEnumerator<KeyValuePair<TKey, TValue>>
    {
        private FingerTreeDeque<Entry>.Enumerator _inner;

        internal Enumerator(FingerTreeDeque<Entry>.Enumerator inner) => _inner = inner;

        /// <summary>
        /// Gets the entry at the current position.
        /// </summary>
        /// <remarks>Unspecified before the first <see cref="MoveNext"/> or after enumeration ends.</remarks>
        public readonly KeyValuePair<TKey, TValue> Current => _inner.Current.ToPair();

        readonly object IEnumerator.Current => Current;

        /// <summary>
        /// Advances to the next entry.
        /// </summary>
        /// <returns><see langword="true"/> when a next entry exists; otherwise <see langword="false"/>.</returns>
        public bool MoveNext() => _inner.MoveNext();

        /// <summary>
        /// Not supported; the enumerator cannot be reset. Create a new enumerator instead.
        /// </summary>
        /// <exception cref="NotSupportedException">Always thrown.</exception>
        void IEnumerator.Reset() => throw new NotSupportedException();

        /// <summary>
        /// Releases nothing; the enumerator holds no disposable resources.
        /// </summary>
        public readonly void Dispose()
        {
        }
    }

    private static InvalidOperationException EmptyError() => new("The association is empty.");

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private int IndexOfStamp(long stamp)
    {
        var position = _entries.SortedBinarySearch(new Entry(stamp, default!, default!), StampOrder.Instance);
        Debug.Assert(position >= 0, "a stamp recorded in the keyed index must exist in the entry sequence");
        return position;
    }

    /// <summary>Appends a key known to be absent from the keyed index.</summary>
    private PersistentAssociation<TKey, TValue> AppendNew(TKey key, TValue value) =>
        WithEntryInserted(_entries, _index, _entries.Count, key, value);

    /// <summary>
    /// Inserts an entry before <paramref name="insertAt"/> in <paramref name="entries"/>, picking
    /// a fresh stamp and falling back to a full relabel when the local label gap (or, after
    /// astronomically many end insertions, the label range itself) is exhausted.
    /// <paramref name="indexSide"/> must already have <paramref name="key"/> absent.
    /// </summary>
    private PersistentAssociation<TKey, TValue> WithEntryInserted(
        FingerTreeDeque<Entry> entries,
        PersistentHashMap<TKey, Slot> indexSide,
        int insertAt,
        TKey key,
        TValue value)
    {
        if (!TryPickStamp(entries, insertAt, out var stamp))
            return Relabeled(entries, insertAt, new Entry(0, key, value));

        var entry = new Entry(stamp, key, value);
        var grown = insertAt == entries.Count
            ? entries.AddLast(entry)
            : insertAt == 0
                ? entries.AddFirst(entry)
                : entries.InsertAt(insertAt, entry);
        return new(grown, indexSide.SetItem(key, new Slot(stamp, value)));
    }

    /// <summary>
    /// Picks a stamp strictly between the labels adjacent to an insertion point, or reports label
    /// exhaustion (an interior gap of less than 2, or an end insertion beyond the label range).
    /// </summary>
    private static bool TryPickStamp(FingerTreeDeque<Entry> entries, int insertAt, out long stamp)
    {
        stamp = 0;
        if (entries.IsEmpty)
            return true;

        if (insertAt == 0)
        {
            var first = entries.First.Stamp;
            if (first < long.MinValue + StampGap)
                return false;
            stamp = first - StampGap;
            return true;
        }

        if (insertAt == entries.Count)
        {
            var last = entries.Last.Stamp;
            if (last > long.MaxValue - StampGap)
                return false;
            stamp = last + StampGap;
            return true;
        }

        var left = entries[insertAt - 1].Stamp;
        var right = entries[insertAt].Stamp;
        // Strict stamp ascent makes right - left positive; compute it as unsigned so a span
        // straddling most of the label range cannot overflow.
        var gap = unchecked((ulong)(right - left));
        if (gap < 2)
            return false;
        stamp = unchecked(left + (long)(gap >> 1));
        return true;
    }

    /// <summary>Rebuilds with fresh gapped labels and one entry inserted; the O(n) relabel path.</summary>
    private PersistentAssociation<TKey, TValue> Relabeled(FingerTreeDeque<Entry> entries, int insertAt, Entry inserted)
    {
        var array = new Entry[entries.Count + 1];
        var i = 0;
        foreach (var entry in entries)
        {
            if (i == insertAt)
                i++;
            array[i++] = entry;
        }
        array[insertAt] = inserted;
        return Rebuilt(array);
    }

    /// <summary>Rebuilds both structures from entries in the given order, assigning fresh gapped labels.</summary>
    private PersistentAssociation<TKey, TValue> Rebuilt(Entry[] ordered)
    {
        var indexSide = PersistentHashMap<TKey, Slot>.Create(Comparer);
        for (var i = 0; i < ordered.Length; i++)
        {
            var stamp = (long)i * StampGap;
            ordered[i] = new Entry(stamp, ordered[i].Key, ordered[i].Value);
            indexSide = indexSide.SetItem(ordered[i].Key, new Slot(stamp, ordered[i].Value));
        }
        return new(FingerTreeDeque<Entry>.Create(ordered), indexSide);
    }

    private PersistentAssociation<TKey, TValue> SortedBy<TOrder>(Func<Entry, TOrder> selector, IComparer<TOrder> comparer)
    {
        if (Count <= 1)
            return this;
        var ordered = _entries.ToArray();
        // Stable sort (Tungsten's Sort/KeySort keep equal entries in association order); the
        // stamps carried by the unsorted entries are the association order, so they are the
        // tiebreak. Array.Sort alone is not stable.
        var keys = new (TOrder Order, long Stamp)[ordered.Length];
        for (var i = 0; i < ordered.Length; i++)
            keys[i] = (selector(ordered[i]), ordered[i].Stamp);
        Array.Sort(keys, ordered, new StableOrder<TOrder>(comparer));
        return Rebuilt(ordered);
    }

    private sealed class StableOrder<TOrder>(IComparer<TOrder> comparer) : IComparer<(TOrder Order, long Stamp)>
    {
        public int Compare((TOrder Order, long Stamp) x, (TOrder Order, long Stamp) y)
        {
            var byOrder = comparer.Compare(x.Order, y.Order);
            return byOrder != 0 ? byOrder : x.Stamp.CompareTo(y.Stamp);
        }
    }
}
