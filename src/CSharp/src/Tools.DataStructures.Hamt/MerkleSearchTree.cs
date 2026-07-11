using System.Buffers.Binary;
using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;

namespace Tools.DataStructures.Hamt;

/// <summary>
/// Represents an immutable ordered content-addressed map using canonical wide Merkle-search-tree
/// blocks.
/// </summary>
/// <typeparam name="TKey">The ordered key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// Keys are assigned geometric layers from leading zero base-16 digits in their policy-bound
/// SHA-256 digest. A block stores every consecutive key at one layer and child blocks for the key
/// intervals between them. The resulting B=16 shape is determined entirely by policy-bound encoded
/// contents, never insertion or deletion history.
/// </remarks>
[DebuggerDisplay("Count = {Count}, Height = {Height}, Blocks = {BlockCount}, RootHash = {RootHash}")]
public sealed partial class MerkleSearchTree<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private const int DigestLength = 32;
    private const int BlockHeaderLength = 4 + 1 + DigestLength + 1 + sizeof(int) + sizeof(int);
    private const byte NodeBlockTag = 1;
    private static readonly byte[] BlockMagic = "MST2"u8.ToArray();

    private readonly Node? _root;

    private MerkleSearchTree(Node? root, MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        _root = root;
        Policy = policy;
    }

    /// <summary>Gets the deterministic comparison, codec, and hash-domain policy.</summary>
    public MerkleSearchTreePolicy<TKey, TValue> Policy { get; }

    /// <summary>Gets the number of entries.</summary>
    public int Count => _root?.Count ?? 0;

    /// <summary>Gets whether the tree contains no entries.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the number of block levels on the deepest path.</summary>
    public int Height => _root?.Height ?? 0;

    /// <summary>Gets the number of content-addressed blocks.</summary>
    public int BlockCount => _root?.BlockCount ?? 0;

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

    internal (int Level, TKey Key, int EntriesInBlock, int SubtreeCount)[] ShapeForTesting()
    {
        if (_root is null)
            return [];

        var result = new List<(int, TKey, int, int)>(Count);
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            foreach (var entry in node.Entries)
                result.Add((node.Level, entry.Key, node.Entries.Length, node.Count));
            for (var index = node.Children.Length - 1; index >= 0; index--)
            {
                if (node.Children[index] is not null)
                    stack.Push(node.Children[index]!);
            }
        }
        return [.. result];
    }

    internal IReadOnlyList<(MerkleDigest Digest, byte[] Bytes)> BlocksForTesting()
    {
        if (_root is null)
            return [];
        var result = new List<(MerkleDigest, byte[])>(_root.BlockCount);
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            result.Add((node.Digest, [.. node.BlockBytes]));
            foreach (var child in node.Children)
            {
                if (child is not null)
                    stack.Push(child);
            }
        }
        return result;
    }

    /// <summary>Creates an empty tree with an explicit deterministic policy.</summary>
    public static MerkleSearchTree<TKey, TValue> Create(MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        ArgumentNullException.ThrowIfNull(policy);
        return new MerkleSearchTree<TKey, TValue>(null, policy);
    }

    /// <summary>Creates a canonical tree with last-value/first-equivalent-key semantics.</summary>
    public static MerkleSearchTree<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> entries,
        MerkleSearchTreePolicy<TKey, TValue> policy)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var tree = Create(policy);
        var pending = new List<PendingEntry>();
        var sequence = 0;
        foreach (var entry in entries)
            pending.Add(new PendingEntry(entry.Key, entry.Value, checked(sequence++)));
        if (pending.Count == 0)
            return tree;

        pending.Sort((left, right) =>
        {
            var comparison = policy.Comparer.Compare(left.Key, right.Key);
            return comparison != 0 ? comparison : left.Sequence.CompareTo(right.Sequence);
        });

        var items = new List<EntryRecord>(pending.Count);
        for (var index = 0; index < pending.Count;)
        {
            var end = index + 1;
            while (end < pending.Count && policy.Comparer.Compare(pending[index].Key, pending[end].Key) == 0)
                end++;
            items.Add(tree.CreateItem(pending[index].Key, pending[end - 1].Value));
            index = end;
        }

        var array = items.ToArray();
        return new MerkleSearchTree<TKey, TValue>(tree.BuildCanonical(array, 0, array.Length), policy);
    }

    /// <summary>Determines whether a key is present.</summary>
    public bool ContainsKey(TKey key) => TryFind(key, out _);

    /// <summary>Tries to retrieve a value by key.</summary>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        if (TryFind(key, out var item))
        {
            value = item.Value;
            return true;
        }
        value = default;
        return false;
    }

    /// <summary>Adds or replaces an entry by copying only affected blocks.</summary>
    public MerkleSearchTree<TKey, TValue> SetItem(TKey key, TValue value)
    {
        if (TryFind(key, out var existing))
        {
            var valueBytes = EncodeOwned(Policy.ValueCodec, value);
            if (existing.ValueBytes.AsSpan().SequenceEqual(valueBytes))
                return this;
            var replacement = new EntryRecord(
                existing.Key,
                value,
                existing.KeyBytes,
                valueBytes,
                existing.Layer);
            return new MerkleSearchTree<TKey, TValue>(UpdateValue(_root!, key, replacement), Policy);
        }

        return new MerkleSearchTree<TKey, TValue>(Insert(_root, CreateItem(key, value)), Policy);
    }

    /// <summary>Removes a key and contracts any empty block shell.</summary>
    public MerkleSearchTree<TKey, TValue> Remove(TKey key)
    {
        var root = Remove(_root, key, out var removed);
        return removed ? new MerkleSearchTree<TKey, TValue>(root, Policy) : this;
    }

    /// <summary>Returns an empty tree retaining the same deterministic policy.</summary>
    public MerkleSearchTree<TKey, TValue> Clear() => IsEmpty ? this : Create(Policy);

    /// <summary>Compares policy domains and root content addresses in O(1).</summary>
    /// <remarks>This treats SHA-256 collision resistance as the content-addressing assumption.</remarks>
    public bool ContentEquals(MerkleSearchTree<TKey, TValue>? other) =>
        other is not null
        && Policy.DomainDigest == other.Policy.DomainDigest
        && RootHash == other.RootHash;

    /// <summary>Compares semantic map contents, using content addresses and block identity as fast paths.</summary>
    public bool MapEquals(
        MerkleSearchTree<TKey, TValue>? other,
        IEqualityComparer<TValue>? valueComparer = null)
    {
        if (ReferenceEquals(this, other))
            return true;
        if (other is null || Count != other.Count || Policy.DomainDigest != other.Policy.DomainDigest)
            return false;

        var values = valueComparer ?? EqualityComparer<TValue>.Default;
        if (RootHash == other.RootHash)
            return NodesEqual(_root, other._root, values);
        return EnumeratedEquals(other, values);
    }

    /// <summary>Computes a block-digest-pruned semantic diff.</summary>
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
    public IEnumerable<KeyValuePair<TKey, TValue>> EnumerateRange(TKey minimumKey, TKey maximumKey)
    {
        if (Policy.Comparer.Compare(minimumKey, maximumKey) > 0)
            throw new ArgumentException("The minimum key must not follow the maximum key.", nameof(minimumKey));
        return EnumerateRangeCore(_root, minimumKey, maximumKey);
    }

    /// <summary>Validates ordering, layering, cached metadata, and exact block bytes.</summary>
    public MerkleSearchTreeStatistics ValidateStructure()
    {
        if (_root is null)
            return new MerkleSearchTreeStatistics(0, 0, 0, 0, 0, 0, 0);

        var accumulator = new ValidationAccumulator();
        ValidateNode(_root, accumulator);
        if (accumulator.EntryCount != Count || accumulator.BlockCount != BlockCount)
            throw new InvalidOperationException("Merkle root metadata disagrees with validated contents.");
        return accumulator.ToStatistics(Height);
    }

    /// <summary>Returns an enumerator in key order.</summary>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => Enumerate(_root).GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private EntryRecord CreateItem(TKey key, TValue value)
    {
        var keyBytes = EncodeOwned(Policy.KeyCodec, key);
        var valueBytes = EncodeOwned(Policy.ValueCodec, value);
        return new EntryRecord(key, value, keyBytes, valueBytes, Layer(Policy.HashKey(keyBytes)));
    }

    private static byte[] EncodeOwned<T>(IMerkleCodec<T> codec, T value)
    {
        var encoded = codec.Encode(value)
            ?? throw new InvalidOperationException($"Merkle codec '{codec.EncodingId}' returned null bytes.");
        return [.. encoded];
    }

    private bool TryFind(TKey key, [NotNullWhen(true)] out EntryRecord? result)
    {
        var node = _root;
        while (node is not null)
        {
            var position = FindPosition(node.Entries, key, out var found);
            if (found)
            {
                result = node.Entries[position];
                return true;
            }
            node = node.Children[position];
        }
        result = null;
        return false;
    }

    private int FindPosition(EntryRecord[] entries, TKey key, out bool found)
    {
        var low = 0;
        var high = entries.Length;
        while (low < high)
        {
            var middle = (low + high) >>> 1;
            var comparison = Policy.Comparer.Compare(entries[middle].Key, key);
            if (comparison < 0)
                low = middle + 1;
            else
                high = middle;
        }
        found = low < entries.Length && Policy.Comparer.Compare(entries[low].Key, key) == 0;
        return low;
    }

    private Node? BuildCanonical(EntryRecord[] items, int start, int length)
    {
        if (length == 0)
            return null;

        var maximumLayer = 0;
        for (var index = start; index < start + length; index++)
            maximumLayer = Math.Max(maximumLayer, items[index].Layer);

        var entries = new List<EntryRecord>();
        var children = new List<Node?>();
        var segmentStart = start;
        for (var index = start; index < start + length; index++)
        {
            if (items[index].Layer != maximumLayer)
                continue;
            children.Add(BuildCanonical(items, segmentStart, index - segmentStart));
            entries.Add(items[index]);
            segmentStart = index + 1;
        }
        children.Add(BuildCanonical(items, segmentStart, start + length - segmentStart));
        return NewNode(maximumLayer, [.. entries], [.. children]);
    }

    private Node UpdateValue(Node node, TKey key, EntryRecord replacement)
    {
        var position = FindPosition(node.Entries, key, out var found);
        if (found)
        {
            var entries = (EntryRecord[])node.Entries.Clone();
            entries[position] = replacement;
            return NewNode(node.Level, entries, node.Children);
        }

        var children = (Node?[])node.Children.Clone();
        children[position] = UpdateValue(children[position]!, key, replacement);
        return NewNode(node.Level, node.Entries, children);
    }

    private Node Insert(Node? node, EntryRecord item)
    {
        if (node is null)
            return NewNode(item.Layer, [item], [null, null]);

        if (item.Layer > node.Level)
        {
            var split = Split(node, item.Key);
            return NewNode(item.Layer, [item], [split.Left, split.Right]);
        }

        var position = FindPosition(node.Entries, item.Key, out var found);
        Debug.Assert(!found, "SetItem checks for equivalent keys before insertion.");
        if (item.Layer < node.Level)
        {
            var children = (Node?[])node.Children.Clone();
            children[position] = Insert(children[position], item);
            return NewNode(node.Level, node.Entries, children);
        }

        var childSplit = Split(node.Children[position], item.Key);
        var entries = new EntryRecord[node.Entries.Length + 1];
        Array.Copy(node.Entries, 0, entries, 0, position);
        entries[position] = item;
        Array.Copy(node.Entries, position, entries, position + 1, node.Entries.Length - position);

        var expandedChildren = new Node?[node.Children.Length + 1];
        Array.Copy(node.Children, 0, expandedChildren, 0, position);
        expandedChildren[position] = childSplit.Left;
        expandedChildren[position + 1] = childSplit.Right;
        Array.Copy(
            node.Children,
            position + 1,
            expandedChildren,
            position + 2,
            node.Children.Length - position - 1);
        return NewNode(node.Level, entries, expandedChildren);
    }

    private (Node? Left, Node? Right) Split(Node? node, TKey key)
    {
        if (node is null)
            return (null, null);

        var position = FindPosition(node.Entries, key, out var found);
        Debug.Assert(!found, "Split is used only for keys absent from the tree.");
        var childSplit = Split(node.Children[position], key);

        var leftEntries = node.Entries[..position];
        var leftChildren = new Node?[position + 1];
        Array.Copy(node.Children, 0, leftChildren, 0, position);
        leftChildren[position] = childSplit.Left;

        var rightEntries = node.Entries[position..];
        var rightChildren = new Node?[rightEntries.Length + 1];
        rightChildren[0] = childSplit.Right;
        Array.Copy(node.Children, position + 1, rightChildren, 1, rightEntries.Length);

        return (
            CreateOrCollapse(node.Level, leftEntries, leftChildren),
            CreateOrCollapse(node.Level, rightEntries, rightChildren));
    }

    private Node? Remove(Node? node, TKey key, out bool removed)
    {
        if (node is null)
        {
            removed = false;
            return null;
        }

        var position = FindPosition(node.Entries, key, out var found);
        if (!found)
        {
            var child = Remove(node.Children[position], key, out removed);
            if (!removed)
                return node;
            var children = (Node?[])node.Children.Clone();
            children[position] = child;
            return NewNode(node.Level, node.Entries, children);
        }

        removed = true;
        var entries = new EntryRecord[node.Entries.Length - 1];
        Array.Copy(node.Entries, 0, entries, 0, position);
        Array.Copy(node.Entries, position + 1, entries, position, entries.Length - position);

        var childrenAfterRemoval = new Node?[node.Children.Length - 1];
        Array.Copy(node.Children, 0, childrenAfterRemoval, 0, position);
        childrenAfterRemoval[position] = Join(node.Children[position], node.Children[position + 1]);
        Array.Copy(
            node.Children,
            position + 2,
            childrenAfterRemoval,
            position + 1,
            node.Children.Length - position - 2);
        return CreateOrCollapse(node.Level, entries, childrenAfterRemoval);
    }

    private Node? Join(Node? left, Node? right)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;

        if (left.Level > right.Level)
        {
            var children = (Node?[])left.Children.Clone();
            children[^1] = Join(children[^1], right);
            return NewNode(left.Level, left.Entries, children);
        }
        if (left.Level < right.Level)
        {
            var children = (Node?[])right.Children.Clone();
            children[0] = Join(left, children[0]);
            return NewNode(right.Level, right.Entries, children);
        }

        var entries = new EntryRecord[left.Entries.Length + right.Entries.Length];
        left.Entries.CopyTo(entries, 0);
        right.Entries.CopyTo(entries, left.Entries.Length);
        var combinedChildren = new Node?[entries.Length + 1];
        Array.Copy(left.Children, 0, combinedChildren, 0, left.Entries.Length);
        combinedChildren[left.Entries.Length] = Join(left.Children[^1], right.Children[0]);
        Array.Copy(right.Children, 1, combinedChildren, left.Entries.Length + 1, right.Entries.Length);
        return NewNode(left.Level, entries, combinedChildren);
    }

    private Node? CreateOrCollapse(int level, EntryRecord[] entries, Node?[] children)
    {
        Debug.Assert(children.Length == entries.Length + 1);
        return entries.Length == 0 ? children[0] : NewNode(level, entries, children);
    }

    private Node NewNode(int level, EntryRecord[] entries, Node?[] children)
    {
        if (level is < 0 or > 64)
            throw new InvalidOperationException("An MST layer must be between zero and 64.");
        if (entries.Length == 0 || children.Length != entries.Length + 1)
            throw new InvalidOperationException("An MST block must contain entries plus one child interval.");

        var count = entries.Length;
        var height = 1;
        var blockCount = 1;
        foreach (var child in children)
        {
            count = checked(count + (child?.Count ?? 0));
            height = Math.Max(height, checked(1 + (child?.Height ?? 0)));
            blockCount = checked(blockCount + (child?.BlockCount ?? 0));
        }

        var bytes = EncodeBlock(level, count, entries, children);
        var digest = new MerkleDigest(SHA256.HashData(bytes));
        var minimumKey = children[0] is { } firstChild ? firstChild.MinimumKey : entries[0].Key;
        var maximumKey = children[^1] is { } lastChild ? lastChild.MaximumKey : entries[^1].Key;
        return new Node(
            level,
            entries,
            children,
            count,
            height,
            blockCount,
            minimumKey,
            maximumKey,
            bytes,
            digest);
    }

    private byte[] EncodeBlock(int level, int subtreeCount, EntryRecord[] entries, Node?[] children)
    {
        var length = checked(BlockHeaderLength + checked(children.Length * DigestLength));
        foreach (var entry in entries)
            length = checked(length + sizeof(int) + entry.KeyBytes.Length + sizeof(int) + entry.ValueBytes.Length);

        var result = new byte[length];
        var offset = 0;
        BlockMagic.CopyTo(result, offset);
        offset += BlockMagic.Length;
        result[offset++] = NodeBlockTag;
        Policy.DomainDigest.WriteTo(result.AsSpan(offset, DigestLength));
        offset += DigestLength;
        result[offset++] = checked((byte)level);
        BinaryPrimitives.WriteInt32BigEndian(result.AsSpan(offset, sizeof(int)), subtreeCount);
        offset += sizeof(int);
        BinaryPrimitives.WriteInt32BigEndian(result.AsSpan(offset, sizeof(int)), entries.Length);
        offset += sizeof(int);

        foreach (var entry in entries)
        {
            BinaryPrimitives.WriteInt32BigEndian(result.AsSpan(offset, sizeof(int)), entry.KeyBytes.Length);
            offset += sizeof(int);
            entry.KeyBytes.CopyTo(result, offset);
            offset += entry.KeyBytes.Length;
            BinaryPrimitives.WriteInt32BigEndian(result.AsSpan(offset, sizeof(int)), entry.ValueBytes.Length);
            offset += sizeof(int);
            entry.ValueBytes.CopyTo(result, offset);
            offset += entry.ValueBytes.Length;
        }

        foreach (var child in children)
        {
            (child?.Digest ?? Policy.EmptyDigest).WriteTo(result.AsSpan(offset, DigestLength));
            offset += DigestLength;
        }
        Debug.Assert(offset == result.Length);
        return result;
    }

    private static int Layer(MerkleDigest digest)
    {
        Span<byte> bytes = stackalloc byte[DigestLength];
        digest.WriteTo(bytes);
        var result = 0;
        foreach (var value in bytes)
        {
            if ((value & 0xf0) == 0)
                result++;
            else
                return result;
            if ((value & 0x0f) == 0)
                result++;
            else
                return result;
        }
        return result;
    }

    private bool NodesEqual(Node? left, Node? right, IEqualityComparer<TValue> values)
    {
        if (ReferenceEquals(left, right))
            return true;
        if (left is null || right is null
            || left.Level != right.Level
            || left.Entries.Length != right.Entries.Length)
        {
            return false;
        }

        for (var index = 0; index < left.Entries.Length; index++)
        {
            if (Policy.Comparer.Compare(left.Entries[index].Key, right.Entries[index].Key) != 0
                || !values.Equals(left.Entries[index].Value, right.Entries[index].Value))
            {
                return false;
            }
        }
        for (var index = 0; index < left.Children.Length; index++)
        {
            if (!NodesEqual(left.Children[index], right.Children[index], values))
                return false;
        }
        return true;
    }

    private bool EnumeratedEquals(MerkleSearchTree<TKey, TValue> other, IEqualityComparer<TValue> values)
    {
        using var left = GetEnumerator();
        using var right = other.GetEnumerator();
        while (left.MoveNext())
        {
            if (!right.MoveNext()
                || Policy.Comparer.Compare(left.Current.Key, right.Current.Key) != 0
                || !values.Equals(left.Current.Value, right.Current.Value))
            {
                return false;
            }
        }
        return !right.MoveNext();
    }

    private void DiffNodes(
        Node? left,
        Node? right,
        IEqualityComparer<TValue> values,
        List<MerkleMapDifference<TKey, TValue>> result)
    {
        if (ReferenceEquals(left, right) || left?.Digest == right?.Digest)
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

        if (SameSeparators(left, right))
        {
            for (var index = 0; index < left.Entries.Length; index++)
            {
                DiffNodes(left.Children[index], right.Children[index], values, result);
                if (!values.Equals(left.Entries[index].Value, right.Entries[index].Value))
                {
                    result.Add(MerkleMapDifference<TKey, TValue>.Changed(
                        left.Entries[index].Key,
                        left.Entries[index].Value,
                        right.Entries[index].Value));
                }
            }
            DiffNodes(left.Children[^1], right.Children[^1], values, result);
            return;
        }

        MergeDiff(left, right, values, result);
    }

    private bool SameSeparators(Node left, Node right)
    {
        if (left.Level != right.Level || left.Entries.Length != right.Entries.Length)
            return false;
        for (var index = 0; index < left.Entries.Length; index++)
        {
            if (Policy.Comparer.Compare(left.Entries[index].Key, right.Entries[index].Key) != 0)
                return false;
        }
        return true;
    }

    private void MergeDiff(
        Node left,
        Node right,
        IEqualityComparer<TValue> values,
        List<MerkleMapDifference<TKey, TValue>> result)
    {
        using var oldEntries = Enumerate(left).GetEnumerator();
        using var newEntries = Enumerate(right).GetEnumerator();
        var hasOld = oldEntries.MoveNext();
        var hasNew = newEntries.MoveNext();
        while (hasOld || hasNew)
        {
            var comparison = !hasOld ? 1 : !hasNew ? -1 : Policy.Comparer.Compare(oldEntries.Current.Key, newEntries.Current.Key);
            if (comparison < 0)
            {
                result.Add(MerkleMapDifference<TKey, TValue>.Removed(oldEntries.Current.Key, oldEntries.Current.Value));
                hasOld = oldEntries.MoveNext();
            }
            else if (comparison > 0)
            {
                result.Add(MerkleMapDifference<TKey, TValue>.Added(newEntries.Current.Key, newEntries.Current.Value));
                hasNew = newEntries.MoveNext();
            }
            else
            {
                if (!values.Equals(oldEntries.Current.Value, newEntries.Current.Value))
                {
                    result.Add(MerkleMapDifference<TKey, TValue>.Changed(
                        oldEntries.Current.Key,
                        oldEntries.Current.Value,
                        newEntries.Current.Value));
                }
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

    private static IEnumerable<KeyValuePair<TKey, TValue>> Enumerate(Node? node)
    {
        if (node is null)
            yield break;
        for (var index = 0; index < node.Entries.Length; index++)
        {
            foreach (var entry in Enumerate(node.Children[index]))
                yield return entry;
            yield return KeyValuePair.Create(node.Entries[index].Key, node.Entries[index].Value);
        }
        foreach (var entry in Enumerate(node.Children[^1]))
            yield return entry;
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> EnumerateRangeCore(
        Node? node,
        TKey minimumKey,
        TKey maximumKey)
    {
        if (node is null)
            yield break;
        for (var index = 0; index < node.Entries.Length; index++)
        {
            var low = Policy.Comparer.Compare(node.Entries[index].Key, minimumKey);
            var high = Policy.Comparer.Compare(node.Entries[index].Key, maximumKey);
            if (low > 0)
            {
                foreach (var entry in EnumerateRangeCore(node.Children[index], minimumKey, maximumKey))
                    yield return entry;
            }
            if (low >= 0 && high <= 0)
                yield return KeyValuePair.Create(node.Entries[index].Key, node.Entries[index].Value);
            if (high > 0)
                yield break;
        }

        if (Policy.Comparer.Compare(node.Entries[^1].Key, maximumKey) < 0)
        {
            foreach (var entry in EnumerateRangeCore(node.Children[^1], minimumKey, maximumKey))
                yield return entry;
        }
    }

    private void ValidateNode(Node node, ValidationAccumulator accumulator)
    {
        if (node.Entries.Length == 0 || node.Children.Length != node.Entries.Length + 1)
            throw new InvalidOperationException("An MST block has an invalid entry/child arity.");
        if (node.Level is < 0 or > 64)
            throw new InvalidOperationException("An MST block has an invalid layer.");

        var count = node.Entries.Length;
        var height = 1;
        var blocks = 1;
        for (var index = 0; index < node.Entries.Length; index++)
        {
            var entry = node.Entries[index];
            if (entry.Layer != node.Level)
                throw new InvalidOperationException("An MST entry is stored in the wrong layer.");
            if (index != 0 && Policy.Comparer.Compare(node.Entries[index - 1].Key, entry.Key) >= 0)
                throw new InvalidOperationException("MST block entries are not strictly ordered.");
        }

        for (var index = 0; index < node.Children.Length; index++)
        {
            var child = node.Children[index];
            if (child is null)
                continue;
            if (child.Level >= node.Level)
                throw new InvalidOperationException("An MST child does not occupy a lower layer.");
            if (index != 0 && Policy.Comparer.Compare(child.MinimumKey, node.Entries[index - 1].Key) <= 0)
                throw new InvalidOperationException("An MST child crosses its lower key separator.");
            if (index != node.Entries.Length && Policy.Comparer.Compare(child.MaximumKey, node.Entries[index].Key) >= 0)
                throw new InvalidOperationException("An MST child crosses its upper key separator.");
            ValidateNode(child, accumulator);
            count = checked(count + child.Count);
            height = Math.Max(height, checked(1 + child.Height));
            blocks = checked(blocks + child.BlockCount);
        }

        if (count != node.Count || height != node.Height || blocks != node.BlockCount)
            throw new InvalidOperationException("An MST block has invalid cached metadata.");
        var encoded = EncodeBlock(node.Level, node.Count, node.Entries, node.Children);
        if (!encoded.AsSpan().SequenceEqual(node.BlockBytes)
            || new MerkleDigest(SHA256.HashData(node.BlockBytes)) != node.Digest)
        {
            throw new InvalidOperationException("An MST block's canonical bytes or digest are invalid.");
        }
        accumulator.Add(node);
    }

    private sealed class ValidationAccumulator
    {
        internal int EntryCount;
        internal int BlockCount;
        private int _minimumEntries = int.MaxValue;
        private int _maximumEntries;
        private int _minimumBlockBytes = int.MaxValue;
        private int _maximumBlockBytes;

        internal void Add(Node node)
        {
            EntryCount = checked(EntryCount + node.Entries.Length);
            BlockCount++;
            _minimumEntries = Math.Min(_minimumEntries, node.Entries.Length);
            _maximumEntries = Math.Max(_maximumEntries, node.Entries.Length);
            _minimumBlockBytes = Math.Min(_minimumBlockBytes, node.BlockBytes.Length);
            _maximumBlockBytes = Math.Max(_maximumBlockBytes, node.BlockBytes.Length);
        }

        internal MerkleSearchTreeStatistics ToStatistics(int height) => new(
            EntryCount,
            BlockCount,
            height,
            BlockCount == 0 ? 0 : _minimumEntries,
            _maximumEntries,
            BlockCount == 0 ? 0 : _minimumBlockBytes,
            _maximumBlockBytes);
    }

    private readonly record struct PendingEntry(TKey Key, TValue Value, int Sequence);

    private sealed class EntryRecord(
        TKey key,
        TValue value,
        byte[] keyBytes,
        byte[] valueBytes,
        int layer)
    {
        internal TKey Key { get; } = key;
        internal TValue Value { get; } = value;
        internal byte[] KeyBytes { get; } = keyBytes;
        internal byte[] ValueBytes { get; } = valueBytes;
        internal int Layer { get; } = layer;
    }

    private sealed class Node(
        int level,
        EntryRecord[] entries,
        Node?[] children,
        int count,
        int height,
        int blockCount,
        TKey minimumKey,
        TKey maximumKey,
        byte[] blockBytes,
        MerkleDigest digest)
    {
        internal int Level { get; } = level;
        internal EntryRecord[] Entries { get; } = entries;
        internal Node?[] Children { get; } = children;
        internal int Count { get; } = count;
        internal int Height { get; } = height;
        internal int BlockCount { get; } = blockCount;
        internal TKey MinimumKey { get; } = minimumKey;
        internal TKey MaximumKey { get; } = maximumKey;
        internal byte[] BlockBytes { get; } = blockBytes;
        internal MerkleDigest Digest { get; } = digest;
    }
}

/// <summary>Describes validated wide-block Merkle-search-tree representation statistics.</summary>
/// <param name="Count">The entry count.</param>
/// <param name="BlockCount">The content-addressed block count.</param>
/// <param name="Height">The maximum block-path length.</param>
/// <param name="MinimumEntriesPerBlock">The smallest block entry run.</param>
/// <param name="MaximumEntriesPerBlock">The largest block entry run.</param>
/// <param name="MinimumBlockBytes">The smallest canonical block encoding.</param>
/// <param name="MaximumBlockBytes">The largest canonical block encoding.</param>
public readonly record struct MerkleSearchTreeStatistics(
    int Count,
    int BlockCount,
    int Height,
    int MinimumEntriesPerBlock,
    int MaximumEntriesPerBlock,
    int MinimumBlockBytes,
    int MaximumBlockBytes);

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
public readonly record struct MerkleMapDifference<TKey, TValue>(
    MerkleMapDifferenceKind Kind,
    TKey Key,
    TValue? OldValue,
    TValue? NewValue)
{
    internal static MerkleMapDifference<TKey, TValue> Added(TKey key, TValue value) =>
        new(MerkleMapDifferenceKind.Added, key, default, value);

    internal static MerkleMapDifference<TKey, TValue> Removed(TKey key, TValue value) =>
        new(MerkleMapDifferenceKind.Removed, key, value, default);

    internal static MerkleMapDifference<TKey, TValue> Changed(TKey key, TValue oldValue, TValue newValue) =>
        new(MerkleMapDifferenceKind.Changed, key, oldValue, newValue);
}
