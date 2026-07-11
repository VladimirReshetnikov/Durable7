using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.Hamt;

/// <summary>Represents an immutable ordered content-addressed map with deterministic Merkle shape.</summary>
/// <typeparam name="TKey">The ordered key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
[DebuggerDisplay("Count = {Count}, Height = {Height}, RootHash = {RootHash}")]
public sealed class MerkleSearchTree<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private readonly Node? _root;

    private MerkleSearchTree(Node? root, MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        _root = root;
        Policy = policy;
    }

    /// <summary>Gets the deterministic comparison/encoding/hash policy.</summary>
    public MerkleSearchTreePolicy<TKey, TValue> Policy { get; }

    /// <summary>Gets the number of entries.</summary>
    public int Count => _root?.Count ?? 0;

    /// <summary>Gets whether the tree is empty.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the tree height for diagnostics.</summary>
    public int Height => _root?.Height ?? 0;

    /// <summary>Gets the SHA-256 digest naming this exact policy-bound map content.</summary>
    public MerkleDigest RootHash => _root?.Digest ?? Policy.EmptyDigest;

    /// <summary>Gets keys in comparer order.</summary>
    public IEnumerable<TKey> Keys => this.Select(entry => entry.Key);

    /// <summary>Gets values in comparer-key order.</summary>
    public IEnumerable<TValue> Values => this.Select(entry => entry.Value);

    /// <summary>Gets the value associated with a key.</summary>
    /// <param name="key">The key.</param>
    /// <exception cref="KeyNotFoundException">The key is absent.</exception>
    public TValue this[TKey key] => TryGetValue(key, out var value)
        ? value
        : throw new KeyNotFoundException($"The key '{key}' was not present in the Merkle search tree.");

    internal object? RootIdentity => _root;

    internal (TKey Key, int LeftCount, int RightCount)[] ShapeForTesting()
    {
        if (_root is null)
            return [];
        var result = new List<(TKey, int, int)>(Count);
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            result.Add((node.Key, node.Left?.Count ?? 0, node.Right?.Count ?? 0));
            if (node.Right is not null) stack.Push(node.Right);
            if (node.Left is not null) stack.Push(node.Left);
        }
        return [.. result];
    }

    /// <summary>Creates an empty tree with an explicit deterministic policy.</summary>
    /// <param name="policy">The deterministic policy.</param>
    /// <returns>An empty tree.</returns>
    public static MerkleSearchTree<TKey, TValue> Create(MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        ArgumentNullException.ThrowIfNull(policy);
        return new MerkleSearchTree<TKey, TValue>(null, policy);
    }

    /// <summary>Creates a tree from entries with last-wins duplicate-key semantics.</summary>
    /// <param name="entries">The entries to add.</param>
    /// <param name="policy">The deterministic policy.</param>
    /// <returns>A tree containing the entries.</returns>
    public static MerkleSearchTree<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> entries,
        MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var result = Create(policy);
        foreach (var (key, value) in entries)
            result = result.SetItem(key, value);
        return result;
    }

    /// <summary>Determines whether a key is present.</summary>
    /// <param name="key">The key.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool ContainsKey(TKey key) => TryFind(key, out _);

    /// <summary>Tries to retrieve a value by key.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value on success.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        if (TryFind(key, out var node))
        {
            value = node.Value;
            return true;
        }
        value = default;
        return false;
    }

    /// <summary>Adds or replaces an entry and recomputes only the copied Merkle path.</summary>
    /// <param name="key">The key.</param>
    /// <param name="value">The value.</param>
    /// <returns>The updated tree, or this tree for an equal-value no-op.</returns>
    public MerkleSearchTree<TKey, TValue> SetItem(TKey key, TValue value)
    {
        if (TryFind(key, out var existing))
        {
            if (EqualityComparer<TValue>.Default.Equals(existing.Value, value))
                return this;
            var valueBytes = Policy.ValueCodec.Encode(value);
            var root = UpdateValue(_root!, key, value, valueBytes);
            return new MerkleSearchTree<TKey, TValue>(root, Policy);
        }

        var keyBytes = Policy.KeyCodec.Encode(key);
        var valueEncoding = Policy.ValueCodec.Encode(value);
        var rank = Policy.HashKey(keyBytes);
        var item = NewNode(key, value, keyBytes, valueEncoding, rank, null, null);
        return new MerkleSearchTree<TKey, TValue>(Insert(_root, item), Policy);
    }

    /// <summary>Removes a key, preserving identity when absent.</summary>
    /// <param name="key">The key.</param>
    /// <returns>The updated tree.</returns>
    public MerkleSearchTree<TKey, TValue> Remove(TKey key)
    {
        var root = Remove(_root, key, out var removed);
        return removed ? new MerkleSearchTree<TKey, TValue>(root, Policy) : this;
    }

    /// <summary>Returns an empty tree retaining the same deterministic policy.</summary>
    /// <returns>An empty tree, or this tree when already empty.</returns>
    public MerkleSearchTree<TKey, TValue> Clear() => IsEmpty ? this : Create(Policy);

    /// <summary>Compares policy domain and root digest in O(1).</summary>
    /// <param name="other">The other tree.</param>
    /// <returns><see langword="true"/> when the SHA-256 content addresses match.</returns>
    /// <remarks>This treats SHA-256 collision resistance as the content-addressing assumption.</remarks>
    public bool ContentEquals(MerkleSearchTree<TKey, TValue>? other) =>
        other is not null
        && Policy.DomainDigest == other.Policy.DomainDigest
        && RootHash == other.RootHash;

    /// <summary>Verifies semantic map equality after digest and policy fast paths.</summary>
    /// <param name="other">The other tree.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns><see langword="true"/> when the maps are semantically equal.</returns>
    public bool MapEquals(
        MerkleSearchTree<TKey, TValue>? other,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        if (ReferenceEquals(this, other))
            return true;
        if (other is null || Count != other.Count || !ContentEquals(other))
            return false;
        return NodesEqual(_root, other._root, valueComparer ?? EqualityComparer<TValue>.Default);
    }

    /// <summary>Computes a digest-pruned semantic diff.</summary>
    /// <param name="other">The target tree.</param>
    /// <param name="valueComparer">The value comparer, or <see langword="null"/> for the default.</param>
    /// <returns>Added, removed, and changed entries.</returns>
    public IReadOnlyList<MerkleMapDifference<TKey, TValue>> Diff(
        MerkleSearchTree<TKey, TValue> other,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        EnsureCompatible(other);
        var result = new List<MerkleMapDifference<TKey, TValue>>();
        DiffNodes(_root, other._root, valueComparer ?? EqualityComparer<TValue>.Default, result);
        return result;
    }

    /// <summary>Enumerates an inclusive key range in comparer order.</summary>
    /// <param name="minimumKey">The inclusive lower bound.</param>
    /// <param name="maximumKey">The inclusive upper bound.</param>
    /// <returns>The entries in range.</returns>
    public IEnumerable<KeyValuePair<TKey, TValue>> EnumerateRange(TKey minimumKey, TKey maximumKey)
    {
        if (Policy.Comparer.Compare(minimumKey, maximumKey) > 0)
            throw new ArgumentException("The minimum key must not follow the maximum key.", nameof(minimumKey));
        return EnumerateRangeCore(_root, minimumKey, maximumKey);
    }

    /// <summary>Returns an enumerator in key order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => Enumerate(_root).GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private bool TryFind(TKey key, [NotNullWhen(true)] out Node? result)
    {
        var node = _root;
        while (node is not null)
        {
            var comparison = Policy.Comparer.Compare(key, node.Key);
            if (comparison == 0)
            {
                result = node;
                return true;
            }
            node = comparison < 0 ? node.Left : node.Right;
        }
        result = null;
        return false;
    }

    private Node UpdateValue(Node node, TKey key, TValue value, byte[] valueBytes)
    {
        var comparison = Policy.Comparer.Compare(key, node.Key);
        if (comparison == 0)
            return NewNode(node.Key, value, node.KeyBytes, valueBytes, node.Rank, node.Left, node.Right);
        return comparison < 0
            ? NewNode(node.Key, node.Value, node.KeyBytes, node.ValueBytes, node.Rank, UpdateValue(node.Left!, key, value, valueBytes), node.Right)
            : NewNode(node.Key, node.Value, node.KeyBytes, node.ValueBytes, node.Rank, node.Left, UpdateValue(node.Right!, key, value, valueBytes));
    }

    private Node Insert(Node? root, Node item)
    {
        if (root is null)
            return item;
        if (Higher(item, root))
        {
            var (left, right) = Split(root, item.Key);
            return NewNode(item.Key, item.Value, item.KeyBytes, item.ValueBytes, item.Rank, left, right);
        }
        return Policy.Comparer.Compare(item.Key, root.Key) < 0
            ? NewNode(root.Key, root.Value, root.KeyBytes, root.ValueBytes, root.Rank, Insert(root.Left, item), root.Right)
            : NewNode(root.Key, root.Value, root.KeyBytes, root.ValueBytes, root.Rank, root.Left, Insert(root.Right, item));
    }

    private (Node? Left, Node? Right) Split(Node? root, TKey key)
    {
        if (root is null)
            return (null, null);
        if (Policy.Comparer.Compare(key, root.Key) < 0)
        {
            var (left, right) = Split(root.Left, key);
            return (left, NewNode(root.Key, root.Value, root.KeyBytes, root.ValueBytes, root.Rank, right, root.Right));
        }
        var (newLeft, newRight) = Split(root.Right, key);
        return (NewNode(root.Key, root.Value, root.KeyBytes, root.ValueBytes, root.Rank, root.Left, newLeft), newRight);
    }

    private Node? Remove(Node? root, TKey key, out bool removed)
    {
        if (root is null)
        {
            removed = false;
            return null;
        }
        var comparison = Policy.Comparer.Compare(key, root.Key);
        if (comparison == 0)
        {
            removed = true;
            return Merge(root.Left, root.Right);
        }
        if (comparison < 0)
        {
            var left = Remove(root.Left, key, out removed);
            return removed ? NewNode(root.Key, root.Value, root.KeyBytes, root.ValueBytes, root.Rank, left, root.Right) : root;
        }
        var right = Remove(root.Right, key, out removed);
        return removed ? NewNode(root.Key, root.Value, root.KeyBytes, root.ValueBytes, root.Rank, root.Left, right) : root;
    }

    private Node? Merge(Node? left, Node? right)
    {
        if (left is null) return right;
        if (right is null) return left;
        return Higher(left, right)
            ? NewNode(left.Key, left.Value, left.KeyBytes, left.ValueBytes, left.Rank, left.Left, Merge(left.Right, right))
            : NewNode(right.Key, right.Value, right.KeyBytes, right.ValueBytes, right.Rank, Merge(left, right.Left), right.Right);
    }

    private bool Higher(Node left, Node right)
    {
        var level = left.Level.CompareTo(right.Level);
        if (level != 0) return level > 0;
        var digest = left.Rank.CompareTo(right.Rank);
        if (digest != 0) return digest > 0;
        return Policy.Comparer.Compare(left.Key, right.Key) < 0;
    }

    private Node NewNode(
        TKey key,
        TValue value,
        byte[] keyBytes,
        byte[] valueBytes,
        MerkleDigest rank,
        Node? left,
        Node? right) =>
        new(
            key,
            value,
            keyBytes,
            valueBytes,
            rank,
            LeadingZeroBits(rank),
            left,
            right,
            Policy.HashNode(keyBytes, valueBytes, left?.Digest ?? Policy.EmptyDigest, right?.Digest ?? Policy.EmptyDigest));

    private static int LeadingZeroBits(MerkleDigest digest)
    {
        var bytes = digest.ToArray();
        var count = 0;
        foreach (var value in bytes)
        {
            if (value == 0)
            {
                count += 8;
                continue;
            }
            count += System.Numerics.BitOperations.LeadingZeroCount((uint)value) - 24;
            break;
        }
        return count;
    }

    private bool NodesEqual(Node? left, Node? right, IEqualityComparer<TValue> valueComparer)
    {
        if (ReferenceEquals(left, right)) return true;
        if (left is null || right is null || Policy.Comparer.Compare(left.Key, right.Key) != 0)
            return false;
        return valueComparer.Equals(left.Value, right.Value)
            && NodesEqual(left.Left, right.Left, valueComparer)
            && NodesEqual(left.Right, right.Right, valueComparer);
    }

    private void DiffNodes(
        Node? left,
        Node? right,
        IEqualityComparer<TValue> values,
        List<MerkleMapDifference<TKey, TValue>> result)
    {
        if (left?.Digest == right?.Digest)
            return;
        if (left is null)
        {
            foreach (var entry in Enumerate(right))
                result.Add(MerkleMapDifference<TKey, TValue>.Added(entry.Key, entry.Value));
            return;
        }
        if (right is null)
        {
            foreach (var entry in Enumerate(left))
                result.Add(MerkleMapDifference<TKey, TValue>.Removed(entry.Key, entry.Value));
            return;
        }
        if (Policy.Comparer.Compare(left.Key, right.Key) == 0)
        {
            if (!values.Equals(left.Value, right.Value))
                result.Add(MerkleMapDifference<TKey, TValue>.Changed(left.Key, left.Value, right.Value));
            DiffNodes(left.Left, right.Left, values, result);
            DiffNodes(left.Right, right.Right, values, result);
            return;
        }

        using var oldEntries = Enumerate(left).GetEnumerator();
        using var newEntries = Enumerate(right).GetEnumerator();
        var hasOld = oldEntries.MoveNext();
        var hasNew = newEntries.MoveNext();
        while (hasOld || hasNew)
        {
            if (!hasNew || (hasOld && Policy.Comparer.Compare(oldEntries.Current.Key, newEntries.Current.Key) < 0))
            {
                result.Add(MerkleMapDifference<TKey, TValue>.Removed(oldEntries.Current.Key, oldEntries.Current.Value));
                hasOld = oldEntries.MoveNext();
            }
            else if (!hasOld || Policy.Comparer.Compare(oldEntries.Current.Key, newEntries.Current.Key) > 0)
            {
                result.Add(MerkleMapDifference<TKey, TValue>.Added(newEntries.Current.Key, newEntries.Current.Value));
                hasNew = newEntries.MoveNext();
            }
            else
            {
                if (!values.Equals(oldEntries.Current.Value, newEntries.Current.Value))
                    result.Add(MerkleMapDifference<TKey, TValue>.Changed(oldEntries.Current.Key, oldEntries.Current.Value, newEntries.Current.Value));
                hasOld = oldEntries.MoveNext();
                hasNew = newEntries.MoveNext();
            }
        }
    }

    private void EnsureCompatible(MerkleSearchTree<TKey, TValue> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (Policy.DomainDigest != other.Policy.DomainDigest)
            throw new ArgumentException("Merkle trees must use the same algorithm/policy/codec domain.", nameof(other));
    }

    private static IEnumerable<KeyValuePair<TKey, TValue>> Enumerate(Node? root)
    {
        var stack = new Stack<Node>();
        var node = root;
        while (node is not null || stack.Count != 0)
        {
            while (node is not null)
            {
                stack.Push(node);
                node = node.Left;
            }
            node = stack.Pop();
            yield return KeyValuePair.Create(node.Key, node.Value);
            node = node.Right;
        }
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> EnumerateRangeCore(
        Node? node,
        TKey minimumKey,
        TKey maximumKey)
    {
        if (node is null) yield break;
        var low = Policy.Comparer.Compare(node.Key, minimumKey);
        var high = Policy.Comparer.Compare(node.Key, maximumKey);
        if (low > 0)
        {
            foreach (var entry in EnumerateRangeCore(node.Left, minimumKey, maximumKey))
                yield return entry;
        }
        if (low >= 0 && high <= 0)
            yield return KeyValuePair.Create(node.Key, node.Value);
        if (high < 0)
        {
            foreach (var entry in EnumerateRangeCore(node.Right, minimumKey, maximumKey))
                yield return entry;
        }
    }

    private sealed class Node(
        TKey key,
        TValue value,
        byte[] keyBytes,
        byte[] valueBytes,
        MerkleDigest rank,
        int level,
        Node? left,
        Node? right,
        MerkleDigest digest)
    {
        internal TKey Key { get; } = key;
        internal TValue Value { get; } = value;
        internal byte[] KeyBytes { get; } = keyBytes;
        internal byte[] ValueBytes { get; } = valueBytes;
        internal MerkleDigest Rank { get; } = rank;
        internal int Level { get; } = level;
        internal Node? Left { get; } = left;
        internal Node? Right { get; } = right;
        internal MerkleDigest Digest { get; } = digest;
        internal int Count { get; } = checked(1 + (left?.Count ?? 0) + (right?.Count ?? 0));
        internal int Height { get; } = checked(1 + Math.Max(left?.Height ?? 0, right?.Height ?? 0));
    }
}

/// <summary>Identifies a key-level difference between Merkle maps.</summary>
public enum MerkleMapDifferenceKind
{
    /// <summary>The key exists only in the target.</summary>
    Added,
    /// <summary>The key exists only in the source.</summary>
    Removed,
    /// <summary>The key exists in both with different values.</summary>
    Changed,
}

/// <summary>Describes one semantic difference between two Merkle maps.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <param name="Kind">The difference kind.</param>
/// <param name="Key">The affected key.</param>
/// <param name="OldValue">The old value for removed/changed entries.</param>
/// <param name="NewValue">The new value for added/changed entries.</param>
public readonly record struct MerkleMapDifference<TKey, TValue>(
    MerkleMapDifferenceKind Kind,
    TKey Key,
    TValue? OldValue,
    TValue? NewValue)
{
    internal static MerkleMapDifference<TKey, TValue> Added(TKey key, TValue value) => new(MerkleMapDifferenceKind.Added, key, default, value);
    internal static MerkleMapDifference<TKey, TValue> Removed(TKey key, TValue value) => new(MerkleMapDifferenceKind.Removed, key, value, default);
    internal static MerkleMapDifference<TKey, TValue> Changed(TKey key, TValue oldValue, TValue newValue) => new(MerkleMapDifferenceKind.Changed, key, oldValue, newValue);
}
