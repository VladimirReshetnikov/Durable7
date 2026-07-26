// Represents an immutable unordered dictionary backed by a hash-array mapped trie.

using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Durable7.Hamt;

/// <summary>
/// Represents an immutable unordered dictionary backed by a hash-array mapped trie.
/// </summary>
/// <typeparam name="TKey">The type of keys stored in the map.</typeparam>
/// <typeparam name="TValue">The type of values stored in the map.</typeparam>
/// <remarks>
/// <para>
/// Every mutating operation returns a new map version and leaves the original version unchanged. The
/// new version shares every untouched HAMT subtree with the original, so adding, replacing, and
/// removing clone only the search path and, for equal-hash collisions, the touched collision bucket.
/// </para>
/// <para>
/// Single-key operations visit at most seven trie levels for 32-bit hashes plus, for equal full
/// hashes, a linear collision-bucket scan. Lookups and enumeration allocate nothing; updates allocate
/// only the rebuilt search path. Published trie nodes are never mutated, so any version can be read
/// and enumerated concurrently with updates that produce new versions.
/// </para>
/// <para>
/// Enumeration order follows the trie bitmap order and collision-bucket order. It is stable for an
/// unchanged version but is not insertion order, sorted order, or part of the semantic contract.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}")]
[DebuggerTypeProxy(typeof(PersistentHashMapDebugView<,>))]
public sealed partial class PersistentHashMap<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private const int BitsPerLevel = 5;
    private const int BranchMask = (1 << BitsPerLevel) - 1;

    /// <summary>
    /// Gets the shared empty map that uses <see cref="EqualityComparer{T}.Default"/> for keys.
    /// </summary>
    public static PersistentHashMap<TKey, TValue> Empty { get; } =
        new(root: null, count: 0, comparer: EqualityComparer<TKey>.Default);

    private readonly Node? _root;
    private readonly int _count;
    private readonly IEqualityComparer<TKey> _comparer;

    private PersistentHashMap(Node? root, int count, IEqualityComparer<TKey> comparer)
    {
        _root = root;
        _count = count;
        _comparer = comparer;
    }

    /// <summary>
    /// Gets the number of key/value pairs in the map.
    /// </summary>
    public int Count => _count;

    /// <summary>
    /// Gets whether the map contains no key/value pairs.
    /// </summary>
    public bool IsEmpty => _count == 0;

    /// <summary>
    /// Gets the equality comparer that defines key hashing and equality.
    /// </summary>
    public IEqualityComparer<TKey> Comparer => _comparer;

    /// <summary>
    /// Gets an enumerable view of the keys in the map's trie enumeration order.
    /// </summary>
    /// <remarks>Each enumeration of the view allocates one iterator object.</remarks>
    public IEnumerable<TKey> Keys
    {
        get
        {
            foreach (var entry in this)
                yield return entry.Key;
        }
    }

    /// <summary>
    /// Gets an enumerable view of the values in the map's trie enumeration order.
    /// </summary>
    /// <remarks>Each enumeration of the view allocates one iterator object.</remarks>
    public IEnumerable<TValue> Values
    {
        get
        {
            foreach (var entry in this)
                yield return entry.Value;
        }
    }

    /// <summary>
    /// Gets the value associated with the specified key.
    /// </summary>
    /// <param name="key">The key to locate.</param>
    /// <returns>The value associated with <paramref name="key"/>.</returns>
    /// <exception cref="KeyNotFoundException">The key is not present in the map.</exception>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan and allocates
    /// nothing.
    /// </remarks>
    public TValue this[TKey key]
    {
        get
        {
            if (TryGetValue(key, out var value))
                return value;

            throw new KeyNotFoundException($"The key '{key}' was not present in the map.");
        }
    }

    internal Node? RootForTesting => _root;

    /// <summary>
    /// Creates an empty map with the specified key comparer.
    /// </summary>
    /// <param name="comparer">
    /// The comparer that defines key hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>An empty map using <paramref name="comparer"/>.</returns>
    public static PersistentHashMap<TKey, TValue> Create(IEqualityComparer<TKey>? comparer = null) =>
        EmptyFor(comparer ?? EqualityComparer<TKey>.Default);

    /// <summary>Creates an internal mutable builder for one-pass bulk construction.</summary>
    internal static BulkBuilder CreateBulkBuilder(IEqualityComparer<TKey>? comparer = null) =>
        new(comparer ?? EqualityComparer<TKey>.Default);

    /// <summary>
    /// Creates a map from an enumerable sequence of key/value pairs.
    /// </summary>
    /// <param name="items">The key/value pairs to add in enumeration order.</param>
    /// <param name="comparer">
    /// The comparer that defines key hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>
    /// A map containing the supplied entries. When two entries have equivalent keys, the value from
    /// the later entry wins and the first equivalent key object remains the enumerated key. When a
    /// later value compares equal to the stored value under <see cref="EqualityComparer{T}.Default"/>
    /// for values, the earlier stored value object is retained.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// Runs in O(n (w + c)), where w is the bounded trie depth and c is the applicable equal-hash
    /// collision scan. A mutable unpublished trie is frozen once, avoiding persistent path copies
    /// between successive input entries.
    /// </remarks>
    public static PersistentHashMap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var builder = CreateBulkBuilder(comparer);
        foreach (var (key, value) in items)
            builder.SetItem(key, value);

        return builder.ToImmutable();
    }

    /// <summary>
    /// Determines whether the map contains the specified key.
    /// </summary>
    /// <param name="key">The key to locate.</param>
    /// <returns><see langword="true"/> when the key is present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan and allocates
    /// nothing.
    /// </remarks>
    public bool ContainsKey(TKey key) => TryGetValue(key, out _);

    /// <summary>
    /// Gets the value associated with the specified key, when present.
    /// </summary>
    /// <param name="key">The key to locate.</param>
    /// <param name="value">
    /// When this method returns, contains the value associated with <paramref name="key"/> if found,
    /// or the default value of <typeparamref name="TValue"/> otherwise.
    /// </param>
    /// <returns><see langword="true"/> when the key is present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan and allocates
    /// nothing.
    /// </remarks>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value) =>
        TryGetEntry(key, out _, out value);

    /// <summary>
    /// Searches for the stored key equivalent to the specified key.
    /// </summary>
    /// <param name="equalKey">The key to search for.</param>
    /// <param name="actualKey">
    /// When this method returns, contains the originally stored key object when an equivalent key is
    /// present, or <paramref name="equalKey"/> otherwise.
    /// </param>
    /// <returns><see langword="true"/> when an equivalent key is present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// <para>
    /// Because updates of an equivalent key retain the originally stored key object, this method is
    /// the O(trie-depth) way to recover that canonical object, for example when interning.
    /// </para>
    /// <para>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan and allocates
    /// nothing.
    /// </para>
    /// </remarks>
    public bool TryGetKey(TKey equalKey, out TKey actualKey)
    {
        if (TryGetEntry(equalKey, out var storedKey, out _))
        {
            actualKey = storedKey;
            return true;
        }

        actualKey = equalKey;
        return false;
    }

    /// <summary>
    /// Adds or replaces a key/value pair.
    /// </summary>
    /// <param name="key">The key to add or replace.</param>
    /// <param name="value">The value to associate with <paramref name="key"/>.</param>
    /// <returns>
    /// A map containing the supplied key/value pair. If the existing value compares equal to
    /// <paramref name="value"/> under <see cref="EqualityComparer{T}.Default"/> for values, this
    /// method returns the current map instance and retains the stored value object. When an
    /// equivalent key is already present, the originally stored key object is retained.
    /// </returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan; allocates only the
    /// rebuilt search path (at most one node per level plus a leaf or cloned collision bucket) and
    /// shares every untouched subtree with the current version.
    /// </remarks>
    public PersistentHashMap<TKey, TValue> SetItem(TKey key, TValue value)
    {
        var hash = GetHash(key);
        if (_root is null)
            return new PersistentHashMap<TKey, TValue>(new LeafNode(hash, key, value), count: 1, _comparer);

        var newRoot = _root.Set(key, value, hash, shift: 0, _comparer, overwrite: true, out var added);
        if (ReferenceEquals(newRoot, _root))
            return this;

        return new PersistentHashMap<TKey, TValue>(newRoot, checked(_count + (added ? 1 : 0)), _comparer);
    }

    /// <summary>
    /// Adds a key/value pair and rejects duplicate keys.
    /// </summary>
    /// <param name="key">The key to add.</param>
    /// <param name="value">The value to associate with <paramref name="key"/>.</param>
    /// <returns>A map containing the supplied key/value pair.</returns>
    /// <exception cref="ArgumentException">An equivalent key is already present.</exception>
    /// <remarks>
    /// Hashes the key once and walks the trie once. Unlike
    /// <c>System.Collections.Immutable.ImmutableDictionary&lt;TKey, TValue&gt;.Add</c>, this method
    /// throws for any existing equivalent key, including a re-add of an equal value.
    /// </remarks>
    public PersistentHashMap<TKey, TValue> Add(TKey key, TValue value)
    {
        if (!TryAdd(key, value, out var result))
            throw new ArgumentException($"An equivalent key '{key}' is already present.", nameof(key));

        return result;
    }

    /// <summary>
    /// Tries to add a key/value pair without throwing for duplicate keys.
    /// </summary>
    /// <param name="key">The key to add.</param>
    /// <param name="value">The value to associate with <paramref name="key"/>.</param>
    /// <param name="result">
    /// When this method returns, contains the updated map on success or the current map when the key
    /// already exists.
    /// </param>
    /// <returns><see langword="true"/> when the key was added; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Hashes the key once and walks the trie once; when the key already exists, nothing is
    /// allocated.
    /// </remarks>
    public bool TryAdd(TKey key, TValue value, out PersistentHashMap<TKey, TValue> result)
    {
        var hash = GetHash(key);
        if (_root is null)
        {
            result = new PersistentHashMap<TKey, TValue>(new LeafNode(hash, key, value), count: 1, _comparer);
            return true;
        }

        var newRoot = _root.Set(key, value, hash, shift: 0, _comparer, overwrite: false, out var added);
        if (!added)
        {
            result = this;
            return false;
        }

        result = new PersistentHashMap<TKey, TValue>(newRoot, checked(_count + 1), _comparer);
        return true;
    }

    /// <summary>
    /// Adds or replaces every key/value pair from the specified sequence.
    /// </summary>
    /// <param name="items">The key/value pairs to add in enumeration order.</param>
    /// <returns>
    /// A map containing all supplied key/value pairs, applied in enumeration order with last-wins
    /// semantics. When a supplied value compares equal to the stored value under
    /// <see cref="EqualityComparer{T}.Default"/> for values, the earlier stored value object is
    /// retained; when every entry leaves the map unchanged, the current map instance is returned.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <remarks>Runs in O(n) single-key updates with structural sharing during the build.</remarks>
    public PersistentHashMap<TKey, TValue> SetItems(IEnumerable<KeyValuePair<TKey, TValue>> items)
    {
        ArgumentNullException.ThrowIfNull(items);

        var map = this;
        foreach (var (key, value) in items)
            map = map.SetItem(key, value);

        return map;
    }

    /// <summary>
    /// Removes the specified key when present.
    /// </summary>
    /// <param name="key">The key to remove.</param>
    /// <returns>
    /// A map without <paramref name="key"/>. If the key is absent, this method returns the current
    /// map instance.
    /// </returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan; allocates only the
    /// rebuilt search path and shares every untouched subtree with the current version.
    /// </remarks>
    public PersistentHashMap<TKey, TValue> Remove(TKey key)
    {
        if (_root is null)
            return this;

        var newRoot = _root.Remove(key, GetHash(key), shift: 0, _comparer, out var removed, out _);
        if (!removed)
            return this;

        return FromRoot(newRoot, checked(_count - 1), _comparer);
    }

    /// <summary>
    /// Tries to remove the specified key.
    /// </summary>
    /// <param name="key">The key to remove.</param>
    /// <param name="result">
    /// When this method returns, contains the updated map on success or the current map when the key
    /// was absent.
    /// </param>
    /// <param name="value">
    /// When this method returns, contains the removed value on success or the default value of
    /// <typeparamref name="TValue"/> when the key was absent.
    /// </param>
    /// <returns><see langword="true"/> when the key was present; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Visits at most seven trie levels plus any equal-hash collision-bucket scan; allocates only the
    /// rebuilt search path and shares every untouched subtree with the current version.
    /// </remarks>
    public bool TryRemove(TKey key, out PersistentHashMap<TKey, TValue> result, [MaybeNullWhen(false)] out TValue value)
    {
        if (_root is null)
        {
            result = this;
            value = default;
            return false;
        }

        var newRoot = _root.Remove(key, GetHash(key), shift: 0, _comparer, out var removed, out value);
        if (!removed)
        {
            result = this;
            return false;
        }

        result = FromRoot(newRoot, checked(_count - 1), _comparer);
        return true;
    }

    /// <summary>
    /// Returns an empty map that preserves this map's comparer.
    /// </summary>
    /// <returns>
    /// An empty map with the same key comparer. If the map is already empty, this method returns the
    /// current map instance.
    /// </returns>
    public PersistentHashMap<TKey, TValue> Clear() => _count == 0 ? this : EmptyFor(_comparer);

    /// <summary>Returns the right-value-biased structural union with another compatible map.</summary>
    public PersistentHashMap<TKey, TValue> Union(PersistentHashMap<TKey, TValue> other) =>
        Combine(other, SetOperation.Union);

    /// <summary>Returns the structural intersection, retaining this map's keys and values.</summary>
    public PersistentHashMap<TKey, TValue> Intersect(PersistentHashMap<TKey, TValue> other) =>
        Combine(other, SetOperation.Intersect);

    /// <summary>Returns this map without keys present in another compatible map.</summary>
    public PersistentHashMap<TKey, TValue> Except(PersistentHashMap<TKey, TValue> other) =>
        Combine(other, SetOperation.Except);

    /// <summary>Returns entries whose keys occur in exactly one compatible map.</summary>
    public PersistentHashMap<TKey, TValue> SymmetricExcept(PersistentHashMap<TKey, TValue> other) =>
        Combine(other, SetOperation.SymmetricExcept);

    /// <summary>Determines whether another map has the same comparer, keys, and values.</summary>
    /// <param name="other">The map to compare with this map.</param>
    /// <param name="valueComparer">
    /// The value comparer, or <see langword="null"/> to use <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns><see langword="true"/> when the maps are semantically equal; otherwise, <see langword="false"/>.</returns>
    /// <remarks>
    /// Comparers must be the same object. Canonical CHAMP shape permits a lockstep traversal with
    /// reference-equal subtree pruning; equal-hash collision buckets are compared without regard to
    /// their insertion order.
    /// </remarks>
    public bool MapEquals(
        PersistentHashMap<TKey, TValue>? other,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        if (ReferenceEquals(this, other))
            return true;
        if (other is null || _count != other._count || !ReferenceEquals(_comparer, other._comparer))
            return false;

        return NodesEqual(_root, other._root, _comparer, valueComparer ?? EqualityComparer<TValue>.Default);
    }

    /// <summary>Enumerates the semantic changes needed to transform this map into another map.</summary>
    /// <param name="other">The target map.</param>
    /// <param name="valueComparer">
    /// The value comparer, or <see langword="null"/> to use <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>
    /// Added, removed, and changed entries in deterministic logical trie-slot order. The precise
    /// ordering is an implementation detail.
    /// </returns>
    /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException">The maps do not share the same comparer object.</exception>
    /// <remarks>
    /// Logical bitmap slots are traversed in lockstep and reference-equal subtries are skipped. The
    /// work is therefore proportional to the trie regions that do not share identity plus the
    /// reported differences. Independently built maps can still require a complete O(n) traversal;
    /// equal-hash collision runs require pairwise key matching because their order is not observable.
    /// </remarks>
    public IEnumerable<MapDifference<TKey, TValue>> Diff(
        PersistentHashMap<TKey, TValue> other,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(_comparer, other._comparer))
            throw new ArgumentException("Maps must use the same comparer object.", nameof(other));
        if (ReferenceEquals(_root, other._root))
            return Array.Empty<MapDifference<TKey, TValue>>();

        var values = valueComparer ?? EqualityComparer<TValue>.Default;
        List<MapDifference<TKey, TValue>> differences = [];
        DiffNodes(_root, other._root, shift: 0, _comparer, values, differences);
        return differences;
    }

    /// <summary>
    /// Returns an enumerator over the map's key/value pairs in trie order.
    /// </summary>
    /// <returns>An enumerator over the map.</returns>
    /// <remarks>
    /// Enumeration is O(n) with O(1) amortized cost per entry. The enumerator keeps its whole
    /// traversal state inline, so obtaining and draining it allocates nothing.
    /// </remarks>
    public Enumerator GetEnumerator() => new(_root);

    IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator() => GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <summary>Gets the canonical stored key and its value in one trie traversal.</summary>
    internal bool TryGetEntry(
        TKey key,
        [MaybeNullWhen(false)] out TKey actualKey,
        [MaybeNullWhen(false)] out TValue value)
    {
        var node = _root;
        if (node is not null)
        {
            var hash = GetHash(key);
            var shift = 0;
            while (node is BitmapIndexedNode branch)
            {
                var bit = Bit(Index(hash, shift));
                if ((branch.DataMap & bit) != 0)
                {
                    var entry = branch.Data[Slot(branch.DataMap, bit)];
                    if (entry.Hash == hash && _comparer.Equals(entry.Key, key))
                    {
                        actualKey = entry.Key;
                        value = entry.Value;
                        return true;
                    }

                    actualKey = default;
                    value = default;
                    return false;
                }

                if ((branch.NodeMap & bit) == 0)
                {
                    actualKey = default;
                    value = default;
                    return false;
                }

                node = branch.Children[Slot(branch.NodeMap, bit)];
                shift += BitsPerLevel;
            }

            while (node is SeparateTransientBranchNode or BitmapIndexedNode)
            {
                uint dataMap;
                Entry[] data;
                uint nodeMap;
                Node[] children;
                if (node is SeparateTransientBranchNode separate)
                {
                    dataMap = separate.DataMap;
                    data = separate.Data;
                    nodeMap = separate.NodeMap;
                    children = separate.Children;
                }
                else
                {
                    var ordinary = (BitmapIndexedNode)node;
                    dataMap = ordinary.DataMap;
                    data = ordinary.Data;
                    nodeMap = ordinary.NodeMap;
                    children = ordinary.Children;
                }

                var bit = Bit(Index(hash, shift));
                if ((dataMap & bit) != 0)
                {
                    var entry = data[Slot(dataMap, bit)];
                    if (entry.Hash == hash && _comparer.Equals(entry.Key, key))
                    {
                        actualKey = entry.Key;
                        value = entry.Value;
                        return true;
                    }

                    actualKey = default;
                    value = default;
                    return false;
                }

                if ((nodeMap & bit) == 0)
                {
                    actualKey = default;
                    value = default;
                    return false;
                }

                node = children[Slot(nodeMap, bit)];
                shift += BitsPerLevel;
            }

            if (node is LeafNode leaf)
            {
                if (leaf.Hash == hash && _comparer.Equals(leaf.Key, key))
                {
                    actualKey = leaf.Key;
                    value = leaf.Value;
                    return true;
                }
            }
            else
            {
                var collision = (HashNode)node;
                var entries = collision switch
                {
                    CollisionNode ordinary => ordinary.Entries,
                    SeparateTransientCollisionNode separate => separate.Entries,
                    _ => throw new InvalidOperationException("Unknown CHAMP hash node kind."),
                };
                if (collision.Hash == hash)
                {
                    foreach (var entry in entries)
                    {
                        if (_comparer.Equals(entry.Key, key))
                        {
                            actualKey = entry.Key;
                            value = entry.Value;
                            return true;
                        }
                    }
                }
            }
        }

        actualKey = default;
        value = default;
        return false;
    }

    private static PersistentHashMap<TKey, TValue> FromRoot(
        Node? root,
        int count,
        IEqualityComparer<TKey> comparer)
    {
        if (count == 0)
            return EmptyFor(comparer);

        return new PersistentHashMap<TKey, TValue>(root!, count, comparer);
    }

    private static PersistentHashMap<TKey, TValue> EmptyFor(IEqualityComparer<TKey> comparer) =>
        ReferenceEquals(comparer, EqualityComparer<TKey>.Default)
            ? Empty
            : new PersistentHashMap<TKey, TValue>(root: null, count: 0, comparer);

    private PersistentHashMap<TKey, TValue> Combine(
        PersistentHashMap<TKey, TValue> other,
        SetOperation operation)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (!ReferenceEquals(_comparer, other._comparer))
            throw new ArgumentException("Maps must use the same comparer object.", nameof(other));

        var root = CombineNodes(_root, other._root, shift: 0, operation, _comparer);
        if (ReferenceEquals(root, _root))
            return this;
        if (root is null)
            return EmptyFor(_comparer);
        if (ReferenceEquals(root, other._root))
            return other;
        return new PersistentHashMap<TKey, TValue>(root, root.Count, _comparer);
    }

    private uint GetHash(TKey key) => unchecked((uint)_comparer.GetHashCode(key!));

    private static bool ValuesEqual(TValue? left, TValue? right) =>
        (!typeof(TValue).IsValueType && ReferenceEquals(left, right)) ||
        EqualityComparer<TValue>.Default.Equals(left!, right!);

    private static bool ValuesEqual(
        TValue? left,
        TValue? right,
        IEqualityComparer<TValue> comparer) =>
        (!typeof(TValue).IsValueType && ReferenceEquals(left, right)) ||
        comparer.Equals(left!, right!);

    private static int Index(uint hash, int shift) => (int)((hash >> shift) & BranchMask);

    private static uint Bit(int index) => 1u << index;

    private static int Slot(uint bitmap, uint bit) => BitOperations.PopCount(bitmap & (bit - 1));

    private enum SetOperation
    {
        Union,
        Intersect,
        Except,
        SymmetricExcept,
    }

    private static Node? CombineNodes(
        Node? left,
        Node? right,
        int shift,
        SetOperation operation,
        IEqualityComparer<TKey> keyComparer)
    {
        if (ReferenceEquals(left, right))
        {
            return operation is SetOperation.Union or SetOperation.Intersect ? left : null;
        }
        if (left is null)
        {
            return operation is SetOperation.Union or SetOperation.SymmetricExcept ? right : null;
        }
        if (right is null)
        {
            return operation is SetOperation.Union or SetOperation.Except or SetOperation.SymmetricExcept
                ? left
                : null;
        }
        if (left is HashNode leftHash && right is HashNode rightHash)
            return CombineHashNodes(leftHash, rightHash, shift, operation, keyComparer);

        var slots = new Node?[32];
        var childShift = shift + BitsPerLevel;
        for (var index = 0; index < slots.Length; index++)
        {
            slots[index] = CombineNodes(
                GetLogicalSlot(left, index, shift),
                GetLogicalSlot(right, index, shift),
                childShift,
                operation,
                keyComparer);
        }
        return BuildLogicalNode(slots, left, shift, keyComparer);
    }

    private static Node? CombineHashNodes(
        HashNode left,
        HashNode right,
        int shift,
        SetOperation operation,
        IEqualityComparer<TKey> keyComparer)
    {
        if (left.Hash != right.Hash)
        {
            if (operation == SetOperation.Intersect)
                return null;
            if (operation == SetOperation.Except)
                return left;
            var slots = new Node?[32];
            var leftIndex = Index(left.Hash, shift);
            var rightIndex = Index(right.Hash, shift);
            if (leftIndex != rightIndex)
            {
                slots[leftIndex] = left;
                slots[rightIndex] = right;
            }
            else
            {
                slots[leftIndex] = CombineHashNodes(
                    left,
                    right,
                    shift + BitsPerLevel,
                    operation,
                    keyComparer);
            }
            return BuildLogicalNode(slots, left, shift, keyComparer);
        }

        var leftEntries = GetEntries(left);
        var rightEntries = GetEntries(right);
        List<Entry> result = [];
        foreach (var leftEntry in leftEntries)
        {
            var rightIndex = FindEntry(rightEntries, leftEntry.Key, keyComparer);
            switch (operation)
            {
                case SetOperation.Union:
                    result.Add(rightIndex < 0
                        ? leftEntry
                        : new Entry(
                            leftEntry.Hash,
                            leftEntry.Key,
                            ValuesEqual(leftEntry.Value, rightEntries[rightIndex].Value)
                                ? leftEntry.Value
                                : rightEntries[rightIndex].Value));
                    break;
                case SetOperation.Intersect when rightIndex >= 0:
                    result.Add(leftEntry);
                    break;
                case SetOperation.Except when rightIndex < 0:
                case SetOperation.SymmetricExcept when rightIndex < 0:
                    result.Add(leftEntry);
                    break;
            }
        }
        if (operation is SetOperation.Union or SetOperation.SymmetricExcept)
        {
            foreach (var rightEntry in rightEntries)
            {
                if (FindEntry(leftEntries, rightEntry.Key, keyComparer) < 0)
                    result.Add(rightEntry);
            }
        }

        if (EntriesMatch(leftEntries, result, keyComparer))
            return left;
        return CreateHashNode(left.Hash, result);
    }

    private static Entry[] GetEntries(HashNode node) => node switch
    {
        LeafNode leaf => [Entry.From(leaf)],
        CollisionNode collision => collision.Entries,
        SeparateTransientCollisionNode collision => collision.Entries,
        _ => throw new InvalidOperationException(),
    };

    private static int FindEntry(IReadOnlyList<Entry> entries, TKey key, IEqualityComparer<TKey> comparer)
    {
        for (var index = 0; index < entries.Count; index++)
        {
            if (comparer.Equals(entries[index].Key, key))
                return index;
        }
        return -1;
    }

    private static bool EntriesMatch(
        IReadOnlyList<Entry> left,
        IReadOnlyList<Entry> right,
        IEqualityComparer<TKey> keyComparer)
    {
        if (left.Count != right.Count)
            return false;
        for (var index = 0; index < left.Count; index++)
        {
            if (left[index].Hash != right[index].Hash
                || !keyComparer.Equals(left[index].Key, right[index].Key)
                || !ValuesEqual(left[index].Value, right[index].Value))
                return false;
        }
        return true;
    }

    private static Node? CreateHashNode(uint hash, List<Entry> entries) => entries.Count switch
    {
        0 => null,
        1 => new LeafNode(hash, entries[0].Key, entries[0].Value),
        _ => new CollisionNode(hash, [.. entries]),
    };

    private static Node? GetLogicalSlot(Node node, int index, int shift)
    {
        if (node is HashNode hashNode)
            return Index(hashNode.Hash, shift) == index ? hashNode : null;

        var branch = BranchView.Create(node);
        var bit = Bit(index);
        if ((branch.DataMap & bit) != 0)
        {
            var entry = branch.Data[Slot(branch.DataMap, bit)];
            return new LeafNode(entry.Hash, entry.Key, entry.Value);
        }
        return (branch.NodeMap & bit) != 0 ? branch.Children[Slot(branch.NodeMap, bit)] : null;
    }

    private static Node? BuildLogicalNode(
        Node?[] slots,
        Node originalLeft,
        int shift,
        IEqualityComparer<TKey> keyComparer)
    {
        if (LogicalSlotsMatch(slots, originalLeft, shift, keyComparer))
            return originalLeft;

        var dataMap = 0u;
        var nodeMap = 0u;
        List<Entry> data = [];
        List<Node> children = [];
        for (var index = 0; index < slots.Length; index++)
        {
            var node = slots[index];
            if (node is null)
                continue;
            var bit = Bit(index);
            if (node is LeafNode leaf)
            {
                dataMap |= bit;
                data.Add(Entry.From(leaf));
            }
            else
            {
                nodeMap |= bit;
                children.Add(node);
            }
        }
        if (data.Count == 0 && children.Count == 0)
            return null;
        if (data.Count == 1 && children.Count == 0)
            return new LeafNode(data[0].Hash, data[0].Key, data[0].Value);
        if (data.Count == 0 && children.Count == 1 && children[0] is HashNode)
            return children[0];
        return new BitmapIndexedNode(dataMap, [.. data], nodeMap, [.. children]);
    }

    private static bool LogicalSlotsMatch(
        Node?[] slots,
        Node original,
        int shift,
        IEqualityComparer<TKey> keyComparer)
    {
        for (var index = 0; index < slots.Length; index++)
        {
            var expected = GetLogicalSlot(original, index, shift);
            var actual = slots[index];
            if (ReferenceEquals(expected, actual))
                continue;
            if (expected is LeafNode leftLeaf && actual is LeafNode rightLeaf
                && leftLeaf.Hash == rightLeaf.Hash
                && keyComparer.Equals(leftLeaf.Key, rightLeaf.Key)
                && ValuesEqual(leftLeaf.Value, rightLeaf.Value))
                continue;
            return false;
        }
        return true;
    }

    private static bool NodesEqual(
        Node? left,
        Node? right,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer)
    {
        if (ReferenceEquals(left, right))
            return true;
        if (left is null || right is null)
            return false;

        if (left is LeafNode leftLeaf)
        {
            if (right is not LeafNode rightLeaf)
                return false;
            return leftLeaf.Hash == rightLeaf.Hash
                && keyComparer.Equals(leftLeaf.Key, rightLeaf.Key)
                && ValuesEqual(leftLeaf.Value, rightLeaf.Value, valueComparer);
        }
        if (right is LeafNode)
            return false;

        if (TryGetCollision(left, out var leftHash, out var leftEntries))
        {
            if (!TryGetCollision(right, out var rightHash, out var rightEntries)
                || leftHash != rightHash
                || leftEntries.Length != rightEntries.Length)
                return false;

            foreach (var leftEntry in leftEntries)
            {
                var found = false;
                foreach (var rightEntry in rightEntries)
                {
                    if (keyComparer.Equals(leftEntry.Key, rightEntry.Key)
                        && ValuesEqual(leftEntry.Value, rightEntry.Value, valueComparer))
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                    return false;
            }

            return true;
        }
        if (TryGetCollision(right, out _, out _))
            return false;

        if (!BranchView.TryCreate(left, out var leftBranch)
            || !BranchView.TryCreate(right, out var rightBranch))
            return false;
        if (leftBranch.DataMap != rightBranch.DataMap || leftBranch.NodeMap != rightBranch.NodeMap)
            return false;
        for (var i = 0; i < leftBranch.Data.Length; i++)
        {
            var l = leftBranch.Data[i];
            var r = rightBranch.Data[i];
            if (l.Hash != r.Hash || !keyComparer.Equals(l.Key, r.Key) || !ValuesEqual(l.Value, r.Value, valueComparer))
                return false;
        }

        for (var i = 0; i < leftBranch.Children.Length; i++)
        {
            if (!NodesEqual(leftBranch.Children[i], rightBranch.Children[i], keyComparer, valueComparer))
                return false;
        }

        return true;
    }

    private static bool TryGetCollision(Node node, out uint hash, out Entry[] entries)
    {
        if (node is CollisionNode ordinary)
        {
            hash = ordinary.Hash;
            entries = ordinary.Entries;
            return true;
        }

        if (node is SeparateTransientCollisionNode separate)
        {
            hash = separate.Hash;
            entries = separate.Entries;
            return true;
        }

        hash = 0;
        entries = null!;
        return false;
    }

    private static void DiffNodes(
        Node? left,
        Node? right,
        int shift,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        if (ReferenceEquals(left, right))
            return;
        if (left is null)
        {
            AppendSubtree(right!, added: true, differences);
            return;
        }

        if (right is null)
        {
            AppendSubtree(left, added: false, differences);
            return;
        }

        if (left is HashNode leftHash)
        {
            var leftRun = new EntryRun(leftHash);
            if (right is HashNode rightHash)
                DiffEntryRuns(leftRun, new EntryRun(rightHash), keyComparer, valueComparer, differences);
            else
                DiffRunAndBranch(leftRun, BranchView.Create(right), shift, keyComparer, valueComparer, differences);
            return;
        }

        if (right is HashNode rightHashNode)
        {
            DiffBranchAndRun(
                BranchView.Create(left),
                new EntryRun(rightHashNode),
                shift,
                keyComparer,
                valueComparer,
                differences);
            return;
        }

        DiffBranches(
            BranchView.Create(left),
            BranchView.Create(right),
            shift,
            keyComparer,
            valueComparer,
            differences);
    }

    private static void DiffBranches(
        BranchView left,
        BranchView right,
        int shift,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        var childShift = shift + BitsPerLevel;
        for (var index = 0; index < 32; index++)
        {
            var bit = Bit(index);
            var leftHasData = (left.DataMap & bit) != 0;
            var leftHasNode = (left.NodeMap & bit) != 0;
            var rightHasData = (right.DataMap & bit) != 0;
            var rightHasNode = (right.NodeMap & bit) != 0;

            if (leftHasData)
            {
                var leftEntry = left.Data[Slot(left.DataMap, bit)];
                if (rightHasData)
                {
                    var rightEntry = right.Data[Slot(right.DataMap, bit)];
                    DiffEntryRuns(
                        new EntryRun(leftEntry),
                        new EntryRun(rightEntry),
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else if (rightHasNode)
                {
                    DiffRunAndNode(
                        new EntryRun(leftEntry),
                        right.Children[Slot(right.NodeMap, bit)],
                        childShift,
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else
                {
                    AppendEntry(leftEntry, added: false, differences);
                }

                continue;
            }

            if (leftHasNode)
            {
                var leftChild = left.Children[Slot(left.NodeMap, bit)];
                if (rightHasData)
                {
                    DiffNodeAndRun(
                        leftChild,
                        new EntryRun(right.Data[Slot(right.DataMap, bit)]),
                        childShift,
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else if (rightHasNode)
                {
                    DiffNodes(
                        leftChild,
                        right.Children[Slot(right.NodeMap, bit)],
                        childShift,
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else
                {
                    AppendSubtree(leftChild, added: false, differences);
                }

                continue;
            }

            if (rightHasData)
                AppendEntry(right.Data[Slot(right.DataMap, bit)], added: true, differences);
            else if (rightHasNode)
                AppendSubtree(right.Children[Slot(right.NodeMap, bit)], added: true, differences);
        }
    }

    private static void DiffRunAndNode(
        EntryRun left,
        Node right,
        int shift,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        if (right is HashNode rightHash)
        {
            DiffEntryRuns(left, new EntryRun(rightHash), keyComparer, valueComparer, differences);
            return;
        }

        DiffRunAndBranch(left, BranchView.Create(right), shift, keyComparer, valueComparer, differences);
    }

    private static void DiffNodeAndRun(
        Node left,
        EntryRun right,
        int shift,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        if (left is HashNode leftHash)
        {
            DiffEntryRuns(new EntryRun(leftHash), right, keyComparer, valueComparer, differences);
            return;
        }

        DiffBranchAndRun(BranchView.Create(left), right, shift, keyComparer, valueComparer, differences);
    }

    private static void DiffRunAndBranch(
        EntryRun left,
        BranchView right,
        int shift,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        var matchingIndex = Index(left.Hash, shift);
        for (var index = 0; index < 32; index++)
        {
            var bit = Bit(index);
            var rightHasData = (right.DataMap & bit) != 0;
            var rightHasNode = (right.NodeMap & bit) != 0;
            if (index == matchingIndex)
            {
                if (rightHasData)
                {
                    DiffEntryRuns(
                        left,
                        new EntryRun(right.Data[Slot(right.DataMap, bit)]),
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else if (rightHasNode)
                {
                    DiffRunAndNode(
                        left,
                        right.Children[Slot(right.NodeMap, bit)],
                        shift + BitsPerLevel,
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else
                {
                    AppendRun(left, added: false, differences);
                }
            }
            else if (rightHasData)
            {
                AppendEntry(right.Data[Slot(right.DataMap, bit)], added: true, differences);
            }
            else if (rightHasNode)
            {
                AppendSubtree(right.Children[Slot(right.NodeMap, bit)], added: true, differences);
            }
        }
    }

    private static void DiffBranchAndRun(
        BranchView left,
        EntryRun right,
        int shift,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        var matchingIndex = Index(right.Hash, shift);
        for (var index = 0; index < 32; index++)
        {
            var bit = Bit(index);
            var leftHasData = (left.DataMap & bit) != 0;
            var leftHasNode = (left.NodeMap & bit) != 0;
            if (index == matchingIndex)
            {
                if (leftHasData)
                {
                    DiffEntryRuns(
                        new EntryRun(left.Data[Slot(left.DataMap, bit)]),
                        right,
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else if (leftHasNode)
                {
                    DiffNodeAndRun(
                        left.Children[Slot(left.NodeMap, bit)],
                        right,
                        shift + BitsPerLevel,
                        keyComparer,
                        valueComparer,
                        differences);
                }
                else
                {
                    AppendRun(right, added: true, differences);
                }
            }
            else if (leftHasData)
            {
                AppendEntry(left.Data[Slot(left.DataMap, bit)], added: false, differences);
            }
            else if (leftHasNode)
            {
                AppendSubtree(left.Children[Slot(left.NodeMap, bit)], added: false, differences);
            }
        }
    }

    private static void DiffEntryRuns(
        EntryRun left,
        EntryRun right,
        IEqualityComparer<TKey> keyComparer,
        IEqualityComparer<TValue> valueComparer,
        List<MapDifference<TKey, TValue>> differences)
    {
        if (left.Hash != right.Hash)
        {
            AppendRun(left, added: false, differences);
            AppendRun(right, added: true, differences);
            return;
        }

        for (var leftIndex = 0; leftIndex < left.Count; leftIndex++)
        {
            var leftEntry = left[leftIndex];
            var rightIndex = FindEntry(right, leftEntry.Key, keyComparer);
            if (rightIndex < 0)
            {
                AppendEntry(leftEntry, added: false, differences);
                continue;
            }

            var rightEntry = right[rightIndex];
            if (!ValuesEqual(leftEntry.Value, rightEntry.Value, valueComparer))
            {
                differences.Add(MapDifference<TKey, TValue>.Changed(
                    leftEntry.Key,
                    leftEntry.Value,
                    rightEntry.Value));
            }
        }

        for (var rightIndex = 0; rightIndex < right.Count; rightIndex++)
        {
            var rightEntry = right[rightIndex];
            if (FindEntry(left, rightEntry.Key, keyComparer) < 0)
                AppendEntry(rightEntry, added: true, differences);
        }
    }

    private static int FindEntry(EntryRun entries, TKey key, IEqualityComparer<TKey> keyComparer)
    {
        for (var index = 0; index < entries.Count; index++)
        {
            if (keyComparer.Equals(entries[index].Key, key))
                return index;
        }

        return -1;
    }

    private static void AppendRun(
        EntryRun entries,
        bool added,
        List<MapDifference<TKey, TValue>> differences)
    {
        for (var index = 0; index < entries.Count; index++)
            AppendEntry(entries[index], added, differences);
    }

    private static void AppendSubtree(
        Node node,
        bool added,
        List<MapDifference<TKey, TValue>> differences)
    {
        var enumerator = new Enumerator(node);
        while (enumerator.MoveNext())
        {
            var entry = enumerator.Current;
            differences.Add(added
                ? MapDifference<TKey, TValue>.Added(entry.Key, entry.Value)
                : MapDifference<TKey, TValue>.Removed(entry.Key, entry.Value));
        }
    }

    private static void AppendEntry(
        Entry entry,
        bool added,
        List<MapDifference<TKey, TValue>> differences) =>
        differences.Add(added
            ? MapDifference<TKey, TValue>.Added(entry.Key, entry.Value)
            : MapDifference<TKey, TValue>.Removed(entry.Key, entry.Value));

    private readonly struct EntryRun
    {
        private readonly HashNode? _node;
        private readonly Entry _single;

        internal EntryRun(HashNode node)
        {
            _node = node;
            _single = default;
        }

        internal EntryRun(Entry single)
        {
            _node = null;
            _single = single;
        }

        internal uint Hash => _node is null ? _single.Hash : _node.Hash;

        internal int Count => _node switch
        {
            null => 1,
            LeafNode => 1,
            CollisionNode collision => collision.Entries.Length,
            SeparateTransientCollisionNode collision => collision.Entries.Length,
            _ => throw new UnreachableException(),
        };

        internal Entry this[int index]
        {
            get
            {
                if (_node is null)
                {
                    Debug.Assert(index == 0);
                    return _single;
                }

                if (_node is LeafNode leaf)
                {
                    Debug.Assert(index == 0);
                    return Entry.From(leaf);
                }

                return _node switch
                {
                    CollisionNode collision => collision.Entries[index],
                    SeparateTransientCollisionNode collision => collision.Entries[index],
                    _ => throw new UnreachableException(),
                };
            }
        }
    }

    private static Node MergeHashNodes(HashNode left, LeafNode right, int shift)
    {
        if (left.Hash == right.Hash)
            return CollisionNode.Create(left, right);

        // Two differing 32-bit hashes must split at or before shift 30, so this guard is an
        // unreachable invariant assertion.
        if (shift >= 32)
            throw new InvalidOperationException("Different 32-bit hashes cannot share every HAMT level.");

        var leftIndex = Index(left.Hash, shift);
        var rightIndex = Index(right.Hash, shift);
        var leftBit = Bit(leftIndex);
        var rightBit = Bit(rightIndex);

        if (leftIndex == rightIndex)
        {
            var child = MergeHashNodes(left, right, shift + BitsPerLevel);
            return new BitmapIndexedNode(0, [], leftBit, [child]);
        }

        if (left is LeafNode leftLeaf)
        {
            return leftIndex < rightIndex
                ? new BitmapIndexedNode(leftBit | rightBit, [Entry.From(leftLeaf), Entry.From(right)], 0, [])
                : new BitmapIndexedNode(leftBit | rightBit, [Entry.From(right), Entry.From(leftLeaf)], 0, []);
        }

        // Equal-hash collision buckets remain child nodes; ordinary leaves are inlined.
        return new BitmapIndexedNode(rightBit, [Entry.From(right)], leftBit, [left]);
    }

    /// <summary>
    /// Enumerates the key/value pairs in a <see cref="PersistentHashMap{TKey, TValue}"/>.
    /// </summary>
    /// <remarks>
    /// The enumerator keeps its entire traversal state inline, so obtaining and draining one
    /// allocates nothing, and a copied enumerator advances independently of the original. Enumerating
    /// any map version is safe while newer versions are produced, because published trie nodes are
    /// never mutated.
    /// </remarks>
    public struct Enumerator : IEnumerator<KeyValuePair<TKey, TValue>>
    {
        // Bitmap-indexed branches exist only at hash shifts 0, 5, ..., 30, so a branch chain is at
        // most seven frames deep for 32-bit hashes.
        private const int MaxDepth = 7;

        private Node? _next;
        private FrameStack _frames;
        private int _depth;
        private Entry[]? _collisionEntries;
        private int _collisionIndex;
        private KeyValuePair<TKey, TValue> _current;

        internal Enumerator(Node? root)
        {
            _next = root;
        }

        /// <summary>
        /// Gets the key/value pair at the current enumerator position.
        /// </summary>
        public readonly KeyValuePair<TKey, TValue> Current => _current;

        readonly object IEnumerator.Current => Current;

        /// <summary>
        /// Advances the enumerator to the next key/value pair.
        /// </summary>
        /// <returns>
        /// <see langword="true"/> when the enumerator advanced to an entry; otherwise,
        /// <see langword="false"/> after the final entry.
        /// </returns>
        public bool MoveNext()
        {
            if (MoveNextCollisionEntry())
                return true;

            var node = _next;
            _next = null;

            while (true)
            {
                if (node is null)
                {
                    if (_depth == 0)
                    {
                        _current = default;
                        return false;
                    }

                    ref var top = ref _frames[_depth - 1];
                    if (top.DataIndex < top.Data.Length)
                    {
                        var entry = top.Data[top.DataIndex++];
                        _current = new KeyValuePair<TKey, TValue>(entry.Key, entry.Value);
                        return true;
                    }

                    if (top.ChildIndex == top.Children.Length)
                    {
                        _frames[--_depth] = default;
                        continue;
                    }

                    node = top.Children[top.ChildIndex++];
                }

                if (node is LeafNode leaf)
                {
                    _current = new KeyValuePair<TKey, TValue>(leaf.Key, leaf.Value);
                    return true;
                }

                if (node is CollisionNode collision)
                {
                    // Collision buckets always hold at least two entries.
                    _collisionEntries = collision.Entries;
                    _collisionIndex = 0;
                    MoveNextCollisionEntry();
                    return true;
                }

                if (node is SeparateTransientCollisionNode separateCollision)
                {
                    _collisionEntries = separateCollision.Entries;
                    _collisionIndex = 0;
                    MoveNextCollisionEntry();
                    return true;
                }

                if (node is BitmapIndexedNode branch)
                {
                    _frames[_depth++] = new Frame(branch.Data, branch.Children);
                    node = null;
                    continue;
                }

                var separateBranch = (SeparateTransientBranchNode)node;
                _frames[_depth++] = new Frame(separateBranch.Data, separateBranch.Children);
                node = null;
            }
        }

        /// <summary>
        /// Releases resources held by the enumerator.
        /// </summary>
        public readonly void Dispose()
        {
        }

        readonly void IEnumerator.Reset() =>
            throw new NotSupportedException("Resetting this enumerator is not supported; create a new enumerator instead.");

        private bool MoveNextCollisionEntry()
        {
            if (_collisionEntries is null)
                return false;

            if (_collisionIndex < _collisionEntries.Length)
            {
                var entry = _collisionEntries[_collisionIndex++];
                _current = new KeyValuePair<TKey, TValue>(entry.Key, entry.Value);
                return true;
            }

            _collisionEntries = null;
            _collisionIndex = 0;
            return false;
        }

        private struct Frame(Entry[] data, Node[] children)
        {
            public readonly Entry[] Data = data;
            public readonly Node[] Children = children;
            public int DataIndex;
            public int ChildIndex;
        }

        [InlineArray(MaxDepth)]
        private struct FrameStack
        {
            private Frame _frame0;
        }
    }

    /// <summary>
    /// Stages entries by full hash and freezes each snapshot directly into canonical CHAMP shape.
    /// Published snapshots own their arrays and remain isolated from subsequent builder updates.
    /// </summary>
    internal sealed class BulkBuilder(IEqualityComparer<TKey> comparer)
    {
        private readonly IEqualityComparer<TKey> _comparer = comparer;
        private readonly List<Entry> _entries = [];
        private readonly Dictionary<uint, List<int>> _hashBuckets = [];

        internal int Count => _entries.Count;

        internal void SetItem(TKey key, TValue value)
        {
            var hash = unchecked((uint)_comparer.GetHashCode(key!));
            if (_hashBuckets.TryGetValue(hash, out var bucket))
            {
                foreach (var index in bucket)
                {
                    var entry = _entries[index];
                    if (!_comparer.Equals(entry.Key, key))
                        continue;

                    if (!ValuesEqual(entry.Value, value))
                        _entries[index] = new Entry(hash, entry.Key, value);
                    return;
                }
            }
            else
            {
                bucket = [];
                _hashBuckets.Add(hash, bucket);
            }

            bucket.Add(_entries.Count);
            _entries.Add(new Entry(hash, key, value));
        }

        /// <summary>
        /// Adds a value for a missing key or replaces an existing value computed from the stored
        /// value, retaining the first equivalent key and an equal stored value representative.
        /// </summary>
        /// <param name="key">The key to add or update.</param>
        /// <param name="addValue">The value to store when no equivalent key exists.</param>
        /// <param name="updateFactory">
        /// The function that computes a candidate replacement from the stored and incoming values.
        /// </param>
        /// <returns>
        /// The value stored after the operation. When the candidate replacement compares equal to
        /// the existing value, the existing value representative is returned.
        /// </returns>
        /// <exception cref="ArgumentNullException">
        /// <paramref name="updateFactory"/> is <see langword="null"/>.
        /// </exception>
        /// <remarks>
        /// The delegate is validated before hashing. The key is hashed once, one full-hash bucket is
        /// scanned, and <paramref name="updateFactory"/> is invoked exactly once only on a hit. A
        /// delegate or equality failure occurs before builder state is changed.
        /// </remarks>
        internal TValue AddOrUpdate(
            TKey key,
            TValue addValue,
            Func<TValue, TValue, TValue> updateFactory)
        {
            ArgumentNullException.ThrowIfNull(updateFactory);

            var hash = unchecked((uint)_comparer.GetHashCode(key!));
            if (_hashBuckets.TryGetValue(hash, out var bucket))
            {
                foreach (var index in bucket)
                {
                    var entry = _entries[index];
                    if (!_comparer.Equals(entry.Key, key))
                        continue;

                    var candidate = updateFactory(entry.Value, addValue);
                    if (ValuesEqual(entry.Value, candidate))
                        return entry.Value;

                    _entries[index] = new Entry(hash, entry.Key, candidate);
                    return candidate;
                }
            }
            else
            {
                bucket = [];
                _hashBuckets.Add(hash, bucket);
            }

            bucket.Add(_entries.Count);
            _entries.Add(new Entry(hash, key, addValue));
            return addValue;
        }

        internal PersistentHashMap<TKey, TValue> ToImmutable()
        {
            if (_entries.Count == 0)
                return EmptyFor(_comparer);

            return new PersistentHashMap<TKey, TValue>(BuildCanonical([.. _entries], 0), _entries.Count, _comparer);
        }

        private static Node BuildCanonical(Entry[] entries, int shift)
        {
            if (entries.Length == 1)
            {
                var entry = entries[0];
                return new LeafNode(entry.Hash, entry.Key, entry.Value);
            }

            var firstHash = entries[0].Hash;
            if (entries.All(entry => entry.Hash == firstHash))
                return new CollisionNode(firstHash, entries);

            Debug.Assert(shift < 32);
            var groups = new List<Entry>?[32];
            foreach (var entry in entries)
                (groups[Index(entry.Hash, shift)] ??= []).Add(entry);

            uint dataMap = 0;
            uint nodeMap = 0;
            var data = new List<Entry>();
            var children = new List<Node>();
            for (var index = 0; index < groups.Length; index++)
            {
                var group = groups[index];
                if (group is null)
                    continue;

                var bit = Bit(index);
                if (group.Count == 1)
                {
                    dataMap |= bit;
                    data.Add(group[0]);
                }
                else
                {
                    nodeMap |= bit;
                    children.Add(BuildCanonical([.. group], shift + BitsPerLevel));
                }
            }

            return new BitmapIndexedNode(dataMap, [.. data], nodeMap, [.. children]);
        }
    }

    internal readonly struct Entry(uint hash, TKey key, TValue value)
    {
        public readonly uint Hash = hash;
        public readonly TKey Key = key;
        public readonly TValue Value = value;

        internal static Entry From(LeafNode leaf) => new(leaf.Hash, leaf.Key, leaf.Value);
    }

    internal abstract class Node
    {
        internal abstract int Count { get; }

        internal abstract Node Set(
            TKey key,
            TValue value,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            bool overwrite,
            out bool added);

        internal abstract Node? Remove(
            TKey key,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            out bool removed,
            out TValue value);
    }

    internal abstract class HashNode(uint hash) : Node
    {
        internal readonly uint Hash = hash;
    }

    internal sealed class LeafNode(uint hash, TKey key, TValue value) : HashNode(hash)
    {
        internal override int Count => 1;

        internal TKey Key { get; } = key;

        internal TValue Value { get; } = value;

        internal override Node Set(
            TKey key,
            TValue value,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            bool overwrite,
            out bool added)
        {
            if (Hash == hash && comparer.Equals(Key, key))
            {
                added = false;
                if (!overwrite || ValuesEqual(Value, value))
                    return this;

                return new LeafNode(Hash, Key, value);
            }

            added = true;
            return MergeHashNodes(this, new LeafNode(hash, key, value), shift);
        }

        internal override Node? Remove(
            TKey key,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            out bool removed,
            out TValue value)
        {
            if (Hash == hash && comparer.Equals(Key, key))
            {
                removed = true;
                value = Value;
                return null;
            }

            removed = false;
            value = default!;
            return this;
        }
    }

    internal sealed class CollisionNode(uint hash, Entry[] entries) : HashNode(hash)
    {
        internal override int Count => Entries.Length;

        internal Entry[] Entries { get; } = entries;

        internal static CollisionNode Create(HashNode left, LeafNode right)
        {
            // Precondition: left is a LeafNode whose key differs from right's under the map's
            // comparer. Equal-hash inserts into an existing collision node are handled inside
            // CollisionNode.Set, so MergeHashNodes only reaches this equal-hash path with two
            // leaves; appending right without a duplicate-key scan is safe only under that
            // precondition.
            Debug.Assert(left is LeafNode, "Equal-hash merges must combine two leaves.");
            var leaf = (LeafNode)left;
            var entries = new Entry[2];
            entries[0] = new Entry(leaf.Hash, leaf.Key, leaf.Value);
            entries[1] = new Entry(right.Hash, right.Key, right.Value);
            return new CollisionNode(left.Hash, entries);
        }

        internal override Node Set(
            TKey key,
            TValue value,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            bool overwrite,
            out bool added)
        {
            if (Hash != hash)
            {
                added = true;
                return MergeHashNodes(this, new LeafNode(hash, key, value), shift);
            }

            for (var i = 0; i < Entries.Length; i++)
            {
                if (!comparer.Equals(Entries[i].Key, key))
                    continue;

                added = false;
                if (!overwrite || ValuesEqual(Entries[i].Value, value))
                    return this;

                var replaced = (Entry[])Entries.Clone();
                replaced[i] = new Entry(Hash, Entries[i].Key, value);
                return new CollisionNode(Hash, replaced);
            }

            var entries = new Entry[Entries.Length + 1];
            Array.Copy(Entries, entries, Entries.Length);
            entries[^1] = new Entry(hash, key, value);
            added = true;
            return new CollisionNode(Hash, entries);
        }

        internal override Node? Remove(
            TKey key,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            out bool removed,
            out TValue value)
        {
            if (Hash != hash)
            {
                removed = false;
                value = default!;
                return this;
            }

            for (var i = 0; i < Entries.Length; i++)
            {
                if (!comparer.Equals(Entries[i].Key, key))
                    continue;

                removed = true;
                value = Entries[i].Value;

                if (Entries.Length == 2)
                {
                    var remaining = Entries[1 - i];
                    return new LeafNode(Hash, remaining.Key, remaining.Value);
                }

                var entries = new Entry[Entries.Length - 1];
                if (i > 0)
                    Array.Copy(Entries, 0, entries, 0, i);
                if (i < Entries.Length - 1)
                    Array.Copy(Entries, i + 1, entries, i, Entries.Length - i - 1);
                return new CollisionNode(Hash, entries);
            }

            removed = false;
            value = default!;
            return this;
        }
    }

    internal sealed class BitmapIndexedNode(
        uint dataMap,
        Entry[] data,
        uint nodeMap,
        Node[] children) : Node
    {
        internal override int Count { get; } = CountEntries(data, children);

        internal uint DataMap { get; } = dataMap;

        internal Entry[] Data { get; } = data;

        internal uint NodeMap { get; } = nodeMap;

        internal Node[] Children { get; } = children;

        private static int CountEntries(Entry[] entries, Node[] nodes)
        {
            var count = entries.Length;
            foreach (var node in nodes)
                count = checked(count + node.Count);
            return count;
        }

        internal override Node Set(
            TKey key,
            TValue value,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            bool overwrite,
            out bool added)
        {
            var bit = Bit(Index(hash, shift));
            if ((DataMap & bit) != 0)
            {
                var slot = Slot(DataMap, bit);
                var entry = Data[slot];
                if (entry.Hash == hash && comparer.Equals(entry.Key, key))
                {
                    added = false;
                    if (!overwrite || ValuesEqual(entry.Value, value))
                        return this;

                    var replacedData = (Entry[])Data.Clone();
                    replacedData[slot] = new Entry(hash, entry.Key, value);
                    return new BitmapIndexedNode(DataMap, replacedData, NodeMap, Children);
                }

                var child = MergeHashNodes(
                    new LeafNode(entry.Hash, entry.Key, entry.Value),
                    new LeafNode(hash, key, value),
                    shift + BitsPerLevel);
                added = true;
                return MoveDataToNode(bit, slot, child);
            }

            if ((NodeMap & bit) != 0)
            {
                var slot = Slot(NodeMap, bit);
                var oldChild = Children[slot];
                var newChild = oldChild.Set(key, value, hash, shift + BitsPerLevel, comparer, overwrite, out added);
                if (ReferenceEquals(newChild, oldChild))
                    return this;

                var replaced = (Node[])Children.Clone();
                replaced[slot] = newChild;
                return new BitmapIndexedNode(DataMap, Data, NodeMap, replaced);
            }

            var dataSlot = Slot(DataMap, bit);
            var inserted = Insert(Data, dataSlot, new Entry(hash, key, value));
            added = true;
            return new BitmapIndexedNode(DataMap | bit, inserted, NodeMap, Children);
        }

        internal override Node? Remove(
            TKey key,
            uint hash,
            int shift,
            IEqualityComparer<TKey> comparer,
            out bool removed,
            out TValue value)
        {
            var bit = Bit(Index(hash, shift));
            if ((DataMap & bit) != 0)
            {
                var slot = Slot(DataMap, bit);
                var entry = Data[slot];
                if (entry.Hash != hash || !comparer.Equals(entry.Key, key))
                {
                    removed = false;
                    value = default!;
                    return this;
                }

                removed = true;
                value = entry.Value;
                return Rebuild(DataMap ^ bit, RemoveAt(Data, slot), NodeMap, Children);
            }

            if ((NodeMap & bit) == 0)
            {
                removed = false;
                value = default!;
                return this;
            }

            var nodeSlot = Slot(NodeMap, bit);
            var oldChild = Children[nodeSlot];
            var newChild = oldChild.Remove(key, hash, shift + BitsPerLevel, comparer, out removed, out value);
            if (!removed)
                return this;

            if (newChild is null)
                return Rebuild(DataMap, Data, NodeMap ^ bit, RemoveAt(Children, nodeSlot));

            if (newChild is LeafNode leaf)
            {
                var dataSlot = Slot(DataMap, bit);
                return Rebuild(
                    DataMap | bit,
                    Insert(Data, dataSlot, Entry.From(leaf)),
                    NodeMap ^ bit,
                    RemoveAt(Children, nodeSlot));
            }

            var replaced = (Node[])Children.Clone();
            replaced[nodeSlot] = newChild;
            return Rebuild(DataMap, Data, NodeMap, replaced);
        }

        private Node MoveDataToNode(uint bit, int dataSlot, Node child)
        {
            var nodeSlot = Slot(NodeMap, bit);
            return new BitmapIndexedNode(
                DataMap ^ bit,
                RemoveAt(Data, dataSlot),
                NodeMap | bit,
                Insert(Children, nodeSlot, child));
        }

        private static Node? Rebuild(uint dataMap, Entry[] data, uint nodeMap, Node[] children)
        {
            if (data.Length == 0 && children.Length == 0)
                return null;
            if (data.Length == 1 && children.Length == 0)
            {
                var entry = data[0];
                return new LeafNode(entry.Hash, entry.Key, entry.Value);
            }
            if (data.Length == 0 && children.Length == 1 && children[0] is HashNode hashNode)
                return hashNode;

            return new BitmapIndexedNode(dataMap, data, nodeMap, children);
        }

        private static T[] Insert<T>(T[] source, int index, T value)
        {
            var result = new T[source.Length + 1];
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            result[index] = value;
            if (index < source.Length)
                Array.Copy(source, index, result, index + 1, source.Length - index);
            return result;
        }

        private static T[] RemoveAt<T>(T[] source, int index)
        {
            var result = new T[source.Length - 1];
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            if (index < source.Length - 1)
                Array.Copy(source, index + 1, result, index, source.Length - index - 1);
            return result;
        }
    }
}

/// <summary>The debugger's view of a map: its entries, rather than its trie nodes.</summary>
internal sealed class PersistentHashMapDebugView<TKey, TValue>(PersistentHashMap<TKey, TValue> map)
{
    [DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public KeyValuePair<TKey, TValue>[] Items => [.. map];
}
