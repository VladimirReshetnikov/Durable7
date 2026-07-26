// The separate transient nodes of the persistent hash map.

using System.Diagnostics;

namespace Durable7.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    /// <summary>
    /// Collision node used only after an edit enters the separate-node T1 kernel. Ordinary
    /// persistent collisions remain the original sealed, readonly-shaped <see cref="CollisionNode"/>.
    /// </summary>
    internal sealed class SeparateTransientCollisionNode : HashNode
    {
        private const byte EntriesOwnedMask = 1;

        private Entry[] _entries;
        private byte _ownedArrays;

        internal SeparateTransientCollisionNode(
            uint hash,
            Entry[] entries,
            EditToken owner,
            bool entriesOwned)
            : base(hash)
        {
            _entries = entries;
            _ownedArrays = entries.Length != 0 && entriesOwned ? EntriesOwnedMask : (byte)0;
            Owner = owner;
        }

        internal override int Count => _entries.Length;
        internal Entry[] Entries => _entries;
        internal EditToken Owner { get; }
        internal bool EntriesOwned => (_ownedArrays & EntriesOwnedMask) != 0;

        internal bool CanWriteEntries(EditToken token) =>
            ReferenceEquals(Owner, token) && token.IsActive && EntriesOwned;

        internal void CommitTransient(
            Entry[] entries,
            bool entriesOwned,
            bool writeEntry,
            int entryIndex,
            Entry entry)
        {
            Debug.Assert(!writeEntry || entriesOwned);
            if (writeEntry)
                entries[entryIndex] = entry;
            _entries = entries;
            _ownedArrays = entries.Length != 0 && entriesOwned ? EntriesOwnedMask : (byte)0;
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

            for (var index = 0; index < _entries.Length; index++)
            {
                if (!comparer.Equals(_entries[index].Key, key))
                    continue;

                added = false;
                if (!overwrite || ValuesEqual(_entries[index].Value, value))
                    return this;

                var replaced = (Entry[])_entries.Clone();
                replaced[index] = new Entry(Hash, _entries[index].Key, value);
                return new CollisionNode(Hash, replaced);
            }

            var entries = new Entry[_entries.Length + 1];
            Array.Copy(_entries, entries, _entries.Length);
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

            for (var index = 0; index < _entries.Length; index++)
            {
                if (!comparer.Equals(_entries[index].Key, key))
                    continue;

                removed = true;
                value = _entries[index].Value;
                if (_entries.Length == 2)
                {
                    var remaining = _entries[1 - index];
                    return new LeafNode(Hash, remaining.Key, remaining.Value);
                }

                var entries = new Entry[_entries.Length - 1];
                if (index > 0)
                    Array.Copy(_entries, 0, entries, 0, index);
                if (index < _entries.Length - 1)
                    Array.Copy(_entries, index + 1, entries, index, _entries.Length - index - 1);
                return new CollisionNode(Hash, entries);
            }

            removed = false;
            value = default!;
            return this;
        }
    }

    /// <summary>
    /// Direct separately editable CHAMP branch. The two low ownership bits independently identify
    /// the data and child arrays that this token may mutate, so creating a wrapper never confers
    /// ownership of an unchanged shared array.
    /// </summary>
    internal sealed class SeparateTransientBranchNode : Node
    {
        private const byte DataOwnedMask = 1;
        private const byte ChildrenOwnedMask = 2;

        private int _count;
        private uint _dataMap;
        private Entry[] _data;
        private uint _nodeMap;
        private Node[] _children;
        private byte _ownedArrays;

        internal SeparateTransientBranchNode(
            uint dataMap,
            Entry[] data,
            bool dataOwned,
            uint nodeMap,
            Node[] children,
            bool childrenOwned,
            int count,
            EditToken owner)
        {
            _count = count;
            _dataMap = dataMap;
            _data = data;
            _nodeMap = nodeMap;
            _children = children;
            _ownedArrays = OwnershipMask(data, dataOwned, children, childrenOwned);
            Owner = owner;
        }

        internal override int Count => _count;
        internal uint DataMap => _dataMap;
        internal Entry[] Data => _data;
        internal uint NodeMap => _nodeMap;
        internal Node[] Children => _children;
        internal EditToken Owner { get; }
        internal bool DataOwned => (_ownedArrays & DataOwnedMask) != 0;
        internal bool ChildrenOwned => (_ownedArrays & ChildrenOwnedMask) != 0;

        internal bool CanWriteData(EditToken token) =>
            ReferenceEquals(Owner, token) && token.IsActive && DataOwned;

        internal bool CanWriteChildren(EditToken token) =>
            ReferenceEquals(Owner, token) && token.IsActive && ChildrenOwned;

        internal void CommitTransient(
            uint dataMap,
            Entry[] data,
            bool dataOwned,
            uint nodeMap,
            Node[] children,
            bool childrenOwned,
            int count,
            bool writeData,
            int dataIndex,
            Entry dataEntry,
            bool writeChild,
            int childIndex,
            Node? child)
        {
            Debug.Assert(!writeData || dataOwned);
            Debug.Assert(!writeChild || childrenOwned);
            if (writeData)
                data[dataIndex] = dataEntry;
            if (writeChild)
                children[childIndex] = child!;

            _count = count;
            _dataMap = dataMap;
            _data = data;
            _nodeMap = nodeMap;
            _children = children;
            _ownedArrays = OwnershipMask(data, dataOwned, children, childrenOwned);
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
            if ((_dataMap & bit) != 0)
            {
                var slot = Slot(_dataMap, bit);
                var entry = _data[slot];
                if (entry.Hash == hash && comparer.Equals(entry.Key, key))
                {
                    added = false;
                    if (!overwrite || ValuesEqual(entry.Value, value))
                        return this;

                    var replacedData = (Entry[])_data.Clone();
                    replacedData[slot] = new Entry(hash, entry.Key, value);
                    return new BitmapIndexedNode(_dataMap, replacedData, _nodeMap, _children);
                }

                var child = MergeHashNodes(
                    new LeafNode(entry.Hash, entry.Key, entry.Value),
                    new LeafNode(hash, key, value),
                    shift + BitsPerLevel);
                added = true;
                return MoveDataToPersistentNode(bit, slot, child);
            }

            if ((_nodeMap & bit) != 0)
            {
                var slot = Slot(_nodeMap, bit);
                var oldChild = _children[slot];
                var newChild = oldChild.Set(key, value, hash, shift + BitsPerLevel, comparer, overwrite, out added);
                if (ReferenceEquals(newChild, oldChild))
                    return this;

                var replaced = (Node[])_children.Clone();
                replaced[slot] = newChild;
                return new BitmapIndexedNode(_dataMap, _data, _nodeMap, replaced);
            }

            var dataSlot = Slot(_dataMap, bit);
            var inserted = InsertPersistent(_data, dataSlot, new Entry(hash, key, value));
            added = true;
            return new BitmapIndexedNode(_dataMap | bit, inserted, _nodeMap, _children);
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
            if ((_dataMap & bit) != 0)
            {
                var slot = Slot(_dataMap, bit);
                var entry = _data[slot];
                if (entry.Hash != hash || !comparer.Equals(entry.Key, key))
                {
                    removed = false;
                    value = default!;
                    return this;
                }

                removed = true;
                value = entry.Value;
                return RebuildPersistent(
                    _dataMap ^ bit,
                    RemovePersistentAt(_data, slot),
                    _nodeMap,
                    _children);
            }

            if ((_nodeMap & bit) == 0)
            {
                removed = false;
                value = default!;
                return this;
            }

            var nodeSlot = Slot(_nodeMap, bit);
            var oldChild = _children[nodeSlot];
            var newChild = oldChild.Remove(key, hash, shift + BitsPerLevel, comparer, out removed, out value);
            if (!removed)
                return this;

            if (newChild is null)
                return RebuildPersistent(_dataMap, _data, _nodeMap ^ bit, RemovePersistentAt(_children, nodeSlot));

            if (newChild is LeafNode leaf)
            {
                var dataSlot = Slot(_dataMap, bit);
                return RebuildPersistent(
                    _dataMap | bit,
                    InsertPersistent(_data, dataSlot, Entry.From(leaf)),
                    _nodeMap ^ bit,
                    RemovePersistentAt(_children, nodeSlot));
            }

            var replaced = (Node[])_children.Clone();
            replaced[nodeSlot] = newChild;
            return RebuildPersistent(_dataMap, _data, _nodeMap, replaced);
        }

        private Node MoveDataToPersistentNode(uint bit, int dataSlot, Node child)
        {
            var nodeSlot = Slot(_nodeMap, bit);
            return new BitmapIndexedNode(
                _dataMap ^ bit,
                RemovePersistentAt(_data, dataSlot),
                _nodeMap | bit,
                InsertPersistent(_children, nodeSlot, child));
        }

        private static Node? RebuildPersistent(uint dataMap, Entry[] data, uint nodeMap, Node[] children)
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

        private static T[] InsertPersistent<T>(T[] source, int index, T value)
        {
            var result = new T[source.Length + 1];
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            result[index] = value;
            if (index < source.Length)
                Array.Copy(source, index, result, index + 1, source.Length - index);
            return result;
        }

        private static T[] RemovePersistentAt<T>(T[] source, int index)
        {
            var result = new T[source.Length - 1];
            if (index > 0)
                Array.Copy(source, 0, result, 0, index);
            if (index < source.Length - 1)
                Array.Copy(source, index + 1, result, index, source.Length - index - 1);
            return result;
        }

        private static byte OwnershipMask(
            Entry[] data,
            bool dataOwned,
            Node[] children,
            bool childrenOwned) =>
            (byte)((data.Length != 0 && dataOwned ? DataOwnedMask : 0)
                | (children.Length != 0 && childrenOwned ? ChildrenOwnedMask : 0));
    }

    /// <summary>Exact-type, non-virtual view used by cold structural operations and the T1 kernel.</summary>
    internal readonly struct BranchView
    {
        private BranchView(
            Node source,
            uint dataMap,
            Entry[] data,
            uint nodeMap,
            Node[] children,
            int count)
        {
            Source = source;
            DataMap = dataMap;
            Data = data;
            NodeMap = nodeMap;
            Children = children;
            Count = count;
        }

        internal Node Source { get; }
        internal uint DataMap { get; }
        internal Entry[] Data { get; }
        internal uint NodeMap { get; }
        internal Node[] Children { get; }
        internal int Count { get; }

        internal static bool TryCreate(Node node, out BranchView view)
        {
            if (node is BitmapIndexedNode ordinary)
            {
                view = new BranchView(
                    ordinary,
                    ordinary.DataMap,
                    ordinary.Data,
                    ordinary.NodeMap,
                    ordinary.Children,
                    ordinary.Count);
                return true;
            }

            if (node is SeparateTransientBranchNode separate)
            {
                view = new BranchView(
                    separate,
                    separate.DataMap,
                    separate.Data,
                    separate.NodeMap,
                    separate.Children,
                    separate.Count);
                return true;
            }

            view = default;
            return false;
        }

        internal static BranchView Create(Node node)
        {
            if (TryCreate(node, out var view))
                return view;
            throw new InvalidOperationException("The CHAMP node is not a branch.");
        }
    }
}
