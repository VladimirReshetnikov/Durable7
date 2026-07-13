namespace Tools.DataStructures.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    /// <summary>
    /// Collision node for the second T1 ownership layout. Unlike <see cref="CollisionNode"/>, this
    /// type exists only after an edit enters the separate-node kernel; ordinary persistent
    /// collision nodes do not reserve its token field.
    /// </summary>
    internal sealed class SeparateTransientCollisionNode : CollisionNodeBase
    {
        private Entry[] _entries;

        internal SeparateTransientCollisionNode(uint hash, Entry[] entries, EditToken owner)
            : base(hash)
        {
            _entries = entries;
            Owner = owner;
        }

        internal override Entry[] Entries => _entries;

        internal EditToken Owner { get; }

        internal bool CanWriteEntries(EditToken token) =>
            ReferenceEquals(Owner, token) && token.IsActive;

        internal void CommitTransient(
            Entry[] entries,
            bool writeEntry,
            int entryIndex,
            Entry entry)
        {
            if (writeEntry)
                entries[entryIndex] = entry;
            _entries = entries;
        }
    }

    /// <summary>
    /// Bitmap branch for the second T1 ownership layout. Creation receives two arrays owned by this
    /// node, so wrapper ownership and array ownership cannot diverge. Sealing <see cref="Owner"/>
    /// publishes the node without rewriting it or walking its descendants.
    /// </summary>
    internal sealed class SeparateTransientBranchNode : BranchNode
    {
        private uint _dataMap;
        private Entry[] _data;
        private uint _nodeMap;
        private Node[] _children;
        private int _count;

        internal SeparateTransientBranchNode(
            uint dataMap,
            Entry[] data,
            uint nodeMap,
            Node[] children,
            int count,
            EditToken owner)
        {
            _dataMap = dataMap;
            _data = data;
            _nodeMap = nodeMap;
            _children = children;
            _count = count;
            Owner = owner;
        }

        internal override int Count => _count;

        internal override uint DataMap => _dataMap;

        internal override Entry[] Data => _data;

        internal override uint NodeMap => _nodeMap;

        internal override Node[] Children => _children;

        internal EditToken Owner { get; }

        internal bool CanWriteData(EditToken token) =>
            ReferenceEquals(Owner, token) && token.IsActive;

        internal bool CanWriteChildren(EditToken token) =>
            ReferenceEquals(Owner, token) && token.IsActive;

        internal void CommitTransient(
            uint dataMap,
            Entry[] data,
            uint nodeMap,
            Node[] children,
            int count,
            bool writeData,
            int dataIndex,
            Entry dataEntry,
            bool writeChild,
            int childIndex,
            Node? child)
        {
            if (writeData)
                data[dataIndex] = dataEntry;
            if (writeChild)
                children[childIndex] = child!;

            _dataMap = dataMap;
            _data = data;
            _nodeMap = nodeMap;
            _children = children;
            _count = count;
        }
    }
}
