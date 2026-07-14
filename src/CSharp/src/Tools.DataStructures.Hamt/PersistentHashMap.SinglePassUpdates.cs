namespace Tools.DataStructures.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    /// <summary>
    /// Gets the value associated with <paramref name="key"/>, or adds a value produced by
    /// <paramref name="addFactory"/> when the key is absent.
    /// </summary>
    /// <param name="key">The key to locate or add.</param>
    /// <param name="addFactory">The factory invoked exactly once when the key is absent.</param>
    /// <param name="value">
    /// When this method returns, contains the stored value on a hit or the value selected by
    /// <paramref name="addFactory"/> on a miss.
    /// </param>
    /// <returns>
    /// The current map when <paramref name="key"/> is present; otherwise, a map containing the
    /// caller's key and the value produced by <paramref name="addFactory"/>.
    /// </returns>
    /// <exception cref="ArgumentNullException">
    /// <paramref name="addFactory"/> is <see langword="null"/>. Validation occurs before hashing
    /// <paramref name="key"/>, including when the key is already present.
    /// </exception>
    /// <remarks>
    /// The operation hashes the key once and performs one trie descent. The factory is not invoked
    /// on a hit. Every successor retains the exact key comparer, and every untouched subtree is
    /// shared with the current version. An exception from key hashing/equality or the factory
    /// propagates without publishing a successor; the source map remains unchanged.
    /// </remarks>
    public PersistentHashMap<TKey, TValue> GetOrAdd(
        TKey key,
        Func<TKey, TValue> addFactory,
        out TValue value)
    {
        ArgumentNullException.ThrowIfNull(addFactory);
        return ApplyFactoryUpdate(key, addFactory, updateFactory: null, out value);
    }

    /// <summary>
    /// Adds a factory-produced value for an absent key or updates the value of a present key in one
    /// persistent trie descent.
    /// </summary>
    /// <param name="key">The key to add or update.</param>
    /// <param name="addFactory">The factory invoked exactly once when the key is absent.</param>
    /// <param name="updateFactory">
    /// The factory invoked exactly once when the key is present. It receives the caller's lookup key
    /// and the stored value; the originally stored key representative remains in the map.
    /// </param>
    /// <param name="value">
    /// When this method returns, contains the actual stored value selected by the operation. When an
    /// update result compares equal to the previous value, the previous stored value representative
    /// is retained and returned.
    /// </param>
    /// <returns>
    /// A map containing the selected value. If an update result compares equal to the stored value
    /// under <see cref="EqualityComparer{T}.Default"/>, returns the current map instance.
    /// </returns>
    /// <exception cref="ArgumentNullException">
    /// <paramref name="addFactory"/> or <paramref name="updateFactory"/> is
    /// <see langword="null"/>. Both delegates are validated before hashing <paramref name="key"/>,
    /// regardless of which branch would be selected.
    /// </exception>
    /// <remarks>
    /// The operation hashes the key once, performs one trie descent, and invokes exactly one factory
    /// exactly once. Every successor retains the exact key comparer, and every untouched subtree is
    /// shared with the current version. An exception from key hashing/equality, either factory, or
    /// default value equality propagates without publishing a successor; the source map remains
    /// unchanged.
    /// </remarks>
    public PersistentHashMap<TKey, TValue> AddOrUpdate(
        TKey key,
        Func<TKey, TValue> addFactory,
        Func<TKey, TValue, TValue> updateFactory,
        out TValue value)
    {
        ArgumentNullException.ThrowIfNull(addFactory);
        ArgumentNullException.ThrowIfNull(updateFactory);
        return ApplyFactoryUpdate(key, addFactory, updateFactory, out value);
    }

    private PersistentHashMap<TKey, TValue> ApplyFactoryUpdate(
        TKey key,
        Func<TKey, TValue> addFactory,
        Func<TKey, TValue, TValue>? updateFactory,
        out TValue value)
    {
        var hash = GetHash(key);
        if (_root is null)
        {
            value = addFactory(key);
            return new PersistentHashMap<TKey, TValue>(
                new LeafNode(hash, key, value),
                count: 1,
                _comparer);
        }

        var newRoot = UpdateNode(
            _root,
            key,
            hash,
            shift: 0,
            _comparer,
            addFactory,
            updateFactory,
            out var added,
            out value);

        if (ReferenceEquals(newRoot, _root))
            return this;

        return new PersistentHashMap<TKey, TValue>(
            newRoot,
            checked(_count + (added ? 1 : 0)),
            _comparer);
    }

    private static Node UpdateNode(
        Node node,
        TKey key,
        uint hash,
        int shift,
        IEqualityComparer<TKey> comparer,
        Func<TKey, TValue> addFactory,
        Func<TKey, TValue, TValue>? updateFactory,
        out bool added,
        out TValue value)
    {
        if (node is HashNode hashNode)
        {
            return UpdateHashNode(
                hashNode,
                key,
                hash,
                shift,
                comparer,
                addFactory,
                updateFactory,
                out added,
                out value);
        }

        var branch = BranchView.Create(node);
        var bit = Bit(Index(hash, shift));
        if ((branch.DataMap & bit) != 0)
        {
            var dataSlot = Slot(branch.DataMap, bit);
            var entry = branch.Data[dataSlot];
            if (entry.Hash == hash && comparer.Equals(entry.Key, key))
            {
                added = false;
                if (updateFactory is null)
                {
                    value = entry.Value;
                    return node;
                }

                var selected = updateFactory(key, entry.Value);
                if (ValuesEqual(entry.Value, selected))
                {
                    value = entry.Value;
                    return node;
                }

                value = selected;
                var replaced = (Entry[])branch.Data.Clone();
                replaced[dataSlot] = new Entry(hash, entry.Key, selected);
                return new BitmapIndexedNode(branch.DataMap, replaced, branch.NodeMap, branch.Children);
            }

            value = addFactory(key);
            added = true;
            var child = MergeHashNodes(
                new LeafNode(entry.Hash, entry.Key, entry.Value),
                new LeafNode(hash, key, value),
                shift + BitsPerLevel);
            return new BitmapIndexedNode(
                branch.DataMap ^ bit,
                RemoveFactoryUpdateItem(branch.Data, dataSlot),
                branch.NodeMap | bit,
                InsertFactoryUpdateItem(branch.Children, Slot(branch.NodeMap, bit), child));
        }

        if ((branch.NodeMap & bit) != 0)
        {
            var childSlot = Slot(branch.NodeMap, bit);
            var oldChild = branch.Children[childSlot];
            var newChild = UpdateNode(
                oldChild,
                key,
                hash,
                shift + BitsPerLevel,
                comparer,
                addFactory,
                updateFactory,
                out added,
                out value);
            if (ReferenceEquals(newChild, oldChild))
                return node;

            var children = (Node[])branch.Children.Clone();
            children[childSlot] = newChild;
            return new BitmapIndexedNode(branch.DataMap, branch.Data, branch.NodeMap, children);
        }

        value = addFactory(key);
        added = true;
        return new BitmapIndexedNode(
            branch.DataMap | bit,
            InsertFactoryUpdateItem(branch.Data, Slot(branch.DataMap, bit), new Entry(hash, key, value)),
            branch.NodeMap,
            branch.Children);
    }

    private static Node UpdateHashNode(
        HashNode node,
        TKey key,
        uint hash,
        int shift,
        IEqualityComparer<TKey> comparer,
        Func<TKey, TValue> addFactory,
        Func<TKey, TValue, TValue>? updateFactory,
        out bool added,
        out TValue value)
    {
        if (node.Hash != hash)
        {
            value = addFactory(key);
            added = true;
            return MergeHashNodes(node, new LeafNode(hash, key, value), shift);
        }

        if (node is LeafNode leaf)
        {
            if (comparer.Equals(leaf.Key, key))
            {
                added = false;
                if (updateFactory is null)
                {
                    value = leaf.Value;
                    return leaf;
                }

                var selected = updateFactory(key, leaf.Value);
                if (ValuesEqual(leaf.Value, selected))
                {
                    value = leaf.Value;
                    return leaf;
                }

                value = selected;
                return new LeafNode(hash, leaf.Key, selected);
            }

            value = addFactory(key);
            added = true;
            return CollisionNode.Create(leaf, new LeafNode(hash, key, value));
        }

        var entries = GetEntries(node);
        for (var index = 0; index < entries.Length; index++)
        {
            var entry = entries[index];
            if (!comparer.Equals(entry.Key, key))
                continue;

            added = false;
            if (updateFactory is null)
            {
                value = entry.Value;
                return node;
            }

            var selected = updateFactory(key, entry.Value);
            if (ValuesEqual(entry.Value, selected))
            {
                value = entry.Value;
                return node;
            }

            value = selected;
            var replaced = (Entry[])entries.Clone();
            replaced[index] = new Entry(hash, entry.Key, selected);
            return new CollisionNode(hash, replaced);
        }

        value = addFactory(key);
        added = true;
        var appended = new Entry[entries.Length + 1];
        Array.Copy(entries, appended, entries.Length);
        appended[^1] = new Entry(hash, key, value);
        return new CollisionNode(hash, appended);
    }

    private static T[] InsertFactoryUpdateItem<T>(T[] source, int index, T value)
    {
        var result = new T[source.Length + 1];
        if (index > 0)
            Array.Copy(source, 0, result, 0, index);
        result[index] = value;
        if (index < source.Length)
            Array.Copy(source, index, result, index + 1, source.Length - index);
        return result;
    }

    private static T[] RemoveFactoryUpdateItem<T>(T[] source, int index)
    {
        var result = new T[source.Length - 1];
        if (index > 0)
            Array.Copy(source, 0, result, 0, index);
        if (index < source.Length - 1)
            Array.Copy(source, index + 1, result, index, source.Length - index - 1);
        return result;
    }
}
