using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Runtime.CompilerServices;

namespace Tools.DataStructures.Hamt;

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
public sealed class PersistentHashMap<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
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
    /// <remarks>Runs in O(n) single-key updates with structural sharing during the build.</remarks>
    public static PersistentHashMap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);

        var map = Create(comparer);
        foreach (var (key, value) in items)
            map = map.SetItem(key, value);

        return map;
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

    private bool TryGetEntry(
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
                if ((branch.Bitmap & bit) == 0)
                {
                    actualKey = default;
                    value = default;
                    return false;
                }

                node = branch.Children[Slot(branch.Bitmap, bit)];
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
                var collision = (CollisionNode)node;
                if (collision.Hash == hash)
                {
                    foreach (var entry in collision.Entries)
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

    private uint GetHash(TKey key) => unchecked((uint)_comparer.GetHashCode(key!));

    private static int Index(uint hash, int shift) => (int)((hash >> shift) & BranchMask);

    private static uint Bit(int index) => 1u << index;

    private static int Slot(uint bitmap, uint bit) => BitOperations.PopCount(bitmap & (bit - 1));

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
            return new BitmapIndexedNode(leftBit, [child]);
        }

        return leftIndex < rightIndex
            ? new BitmapIndexedNode(leftBit | rightBit, [left, right])
            : new BitmapIndexedNode(leftBit | rightBit, [right, left]);
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
                    if (top.Index == top.Children.Length)
                    {
                        _frames[--_depth] = default;
                        continue;
                    }

                    node = top.Children[top.Index++];
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

                _frames[_depth++] = new Frame(((BitmapIndexedNode)node).Children);
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

        private struct Frame(Node[] children)
        {
            public readonly Node[] Children = children;
            public int Index;
        }

        [InlineArray(MaxDepth)]
        private struct FrameStack
        {
            private Frame _frame0;
        }
    }

    internal readonly struct Entry(TKey key, TValue value)
    {
        public readonly TKey Key = key;
        public readonly TValue Value = value;
    }

    internal abstract class Node
    {
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
                if (!overwrite || EqualityComparer<TValue>.Default.Equals(Value, value))
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
            entries[0] = new Entry(leaf.Key, leaf.Value);
            entries[1] = new Entry(right.Key, right.Value);
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
                if (!overwrite || EqualityComparer<TValue>.Default.Equals(Entries[i].Value, value))
                    return this;

                var replaced = (Entry[])Entries.Clone();
                replaced[i] = new Entry(Entries[i].Key, value);
                return new CollisionNode(Hash, replaced);
            }

            var entries = new Entry[Entries.Length + 1];
            Array.Copy(Entries, entries, Entries.Length);
            entries[^1] = new Entry(key, value);
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

    internal sealed class BitmapIndexedNode(uint bitmap, Node[] children) : Node
    {
        internal uint Bitmap { get; } = bitmap;

        internal Node[] Children { get; } = children;

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
            var slot = Slot(Bitmap, bit);

            if ((Bitmap & bit) == 0)
            {
                var children = new Node[Children.Length + 1];
                Array.Copy(Children, 0, children, 0, slot);
                children[slot] = new LeafNode(hash, key, value);
                Array.Copy(Children, slot, children, slot + 1, Children.Length - slot);
                added = true;
                return new BitmapIndexedNode(Bitmap | bit, children);
            }

            var oldChild = Children[slot];
            var newChild = oldChild.Set(key, value, hash, shift + BitsPerLevel, comparer, overwrite, out added);
            if (ReferenceEquals(newChild, oldChild))
                return this;

            var replaced = (Node[])Children.Clone();
            replaced[slot] = newChild;
            return new BitmapIndexedNode(Bitmap, replaced);
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
            if ((Bitmap & bit) == 0)
            {
                removed = false;
                value = default!;
                return this;
            }

            var slot = Slot(Bitmap, bit);
            var oldChild = Children[slot];
            var newChild = oldChild.Remove(key, hash, shift + BitsPerLevel, comparer, out removed, out value);
            if (!removed)
                return this;

            if (newChild is null)
            {
                if (Children.Length == 1)
                    return null;

                var children = new Node[Children.Length - 1];
                if (slot > 0)
                    Array.Copy(Children, 0, children, 0, slot);
                if (slot < Children.Length - 1)
                    Array.Copy(Children, slot + 1, children, slot, Children.Length - slot - 1);

                return Rebuild(Bitmap ^ bit, children);
            }

            var replaced = (Node[])Children.Clone();
            replaced[slot] = newChild;
            return Rebuild(Bitmap, replaced);
        }

        private static Node Rebuild(uint bitmap, Node[] children)
        {
            if (children.Length == 1 && children[0] is HashNode hashNode)
                return hashNode;

            return new BitmapIndexedNode(bitmap, children);
        }
    }
}

internal sealed class PersistentHashMapDebugView<TKey, TValue>(PersistentHashMap<TKey, TValue> map)
{
    [DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public KeyValuePair<TKey, TValue>[] Items => [.. map];
}
