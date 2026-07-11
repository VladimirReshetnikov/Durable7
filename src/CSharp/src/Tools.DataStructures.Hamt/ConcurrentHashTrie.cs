using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Threading;

namespace Tools.DataStructures.Hamt;

/// <summary>Represents a lock-free mutable hash trie with constant-time immutable snapshots.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <remarks>
/// Updates use GCAS descriptors on the indirection node that owns the changed C-node or collision
/// node. <see cref="Snapshot"/> advances the root generation through a root/main RDCSS descriptor in
/// O(1); writers subsequently copy old-generation indirection nodes only along paths they modify.
/// Deletion tombs and then promotes empty or singleton children so obsolete path skeletons do not
/// accumulate. Factories can run repeatedly under contention and must be free of non-repeatable
/// side effects.
/// </remarks>
[DebuggerDisplay("Count = {Count}, Generation = {Generation}")]
public sealed class ConcurrentHashTrie<TKey, TValue> : IReadOnlyDictionary<TKey, TValue>
{
    private readonly IEqualityComparer<TKey> _comparer;
    private object _root;
    private long _revision;

    internal Action? SnapshotMainReadHookForTesting;
    internal Action? GcasInstalledHookForTesting;

    /// <summary>Initializes an empty trie with the default key comparer.</summary>
    public ConcurrentHashTrie() : this(comparer: null) { }

    /// <summary>Initializes an empty trie with a supplied key comparer.</summary>
    public ConcurrentHashTrie(IEqualityComparer<TKey>? comparer)
    {
        _comparer = comparer ?? EqualityComparer<TKey>.Default;
        var generation = new TrieGeneration();
        _root = new Root(new INode(CNode.Empty, generation), generation);
    }

    /// <summary>Gets the number of entries in a stable current snapshot.</summary>
    public int Count => Snapshot().Count;

    /// <summary>Gets whether a stable current snapshot is empty.</summary>
    public bool IsEmpty => Snapshot().Count == 0;

    /// <summary>Gets the comparer retained by all generations.</summary>
    public IEqualityComparer<TKey> Comparer => _comparer;

    /// <summary>Gets the number of completed content-changing update calls.</summary>
    /// <remarks>
    /// This diagnostic counter is not an atomic version paired with arbitrary concurrent reads:
    /// content linearizes at descriptor commit before the owning call increments the counter.
    /// </remarks>
    public long Generation => Volatile.Read(ref _revision);

    /// <summary>Gets keys from a stable snapshot.</summary>
    public IEnumerable<TKey> Keys => Snapshot().Keys;

    /// <summary>Gets values from a stable snapshot.</summary>
    public IEnumerable<TValue> Values => Snapshot().Values;

    /// <summary>Gets or sets the value associated with a key.</summary>
    public TValue this[TKey key]
    {
        get => TryGetValue(key, out var value) ? value : throw new KeyNotFoundException();
        set => SetItem(key, value);
    }

    /// <summary>Creates a trie populated from a sequence with last-wins semantics.</summary>
    public static ConcurrentHashTrie<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> items,
        IEqualityComparer<TKey>? comparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var result = new ConcurrentHashTrie<TKey, TValue>(comparer);
        foreach (var (key, value) in items)
            result.SetItem(key, value);
        Volatile.Write(ref result._revision, 0);
        return result;
    }

    /// <summary>Captures the current generation in O(1).</summary>
    /// <returns>An immutable view whose nodes become frozen by the generation change.</returns>
    public SnapshotView Snapshot()
    {
        while (true)
        {
            var before = ReadRoot();
            var main = ReadMain(before.Node);
            Volatile.Read(ref SnapshotMainReadHookForTesting)?.Invoke();
            var generation = new TrieGeneration();
            var after = new Root(new INode(main, generation), generation);
            var descriptor = new RootDescriptor(before, main, after);
            if (!ReferenceEquals(Interlocked.CompareExchange(ref _root, descriptor, before), before))
                continue;

            Complete(descriptor);
            if (Volatile.Read(ref descriptor.Status) == RootDescriptor.Committed)
                return new SnapshotView(this, before);
        }
    }

    /// <summary>Determines whether the current generation contains a key.</summary>
    public bool ContainsKey(TKey key) => TryGetValue(key, out _);

    /// <summary>Tries to read a value from one observed generation.</summary>
    public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value) =>
        TryGetValue(ReadRoot(), key, out value);

    /// <summary>Adds or replaces a key/value pair atomically.</summary>
    public void SetItem(TKey key, TValue value) =>
        Mutate(key, (exists, current) => exists && EqualityComparer<TValue>.Default.Equals(current, value)
            ? Decision.None
            : Decision.Set(value));

    /// <summary>Attempts to add a key/value pair atomically.</summary>
    public bool TryAdd(TKey key, TValue value) =>
        Mutate(key, (exists, _) => exists ? Decision.None : Decision.Set(value)).Changed;

    /// <summary>Gets an existing value or atomically adds a factory-produced value.</summary>
    public TValue GetOrAdd(TKey key, Func<TKey, TValue> valueFactory)
    {
        ArgumentNullException.ThrowIfNull(valueFactory);
        var result = Mutate(key, (exists, current) => exists
            ? Decision.Return(current)
            : Decision.Set(valueFactory(key)));
        return result.Value!;
    }

    /// <summary>Adds a missing key or atomically updates an existing key.</summary>
    public TValue AddOrUpdate(
        TKey key,
        Func<TKey, TValue> addFactory,
        Func<TKey, TValue, TValue> updateFactory)
    {
        ArgumentNullException.ThrowIfNull(addFactory);
        ArgumentNullException.ThrowIfNull(updateFactory);
        var values = EqualityComparer<TValue>.Default;
        var result = Mutate(key, (exists, current) =>
        {
            var value = exists ? updateFactory(key, current!) : addFactory(key);
            return exists && values.Equals(current, value) ? Decision.Return(current) : Decision.Set(value);
        });
        return result.Value!;
    }

    /// <summary>Replaces a value only when it equals a comparison value.</summary>
    public bool TryUpdate(TKey key, TValue newValue, TValue comparisonValue)
    {
        var values = EqualityComparer<TValue>.Default;
        var matched = false;
        var result = Mutate(key, (exists, current) =>
        {
            matched = exists && values.Equals(current, comparisonValue);
            return matched
                ? values.Equals(current, newValue) ? Decision.Return(current) : Decision.Set(newValue)
                : Decision.None;
        });
        return matched && (result.Changed || values.Equals(result.Value, newValue));
    }

    /// <summary>Attempts to remove a key/value pair atomically.</summary>
    public bool TryRemove(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        var result = Mutate(key, (exists, current) => exists ? Decision.Remove(current) : Decision.None);
        value = result.Changed ? result.Value : default;
        return result.Changed;
    }

    /// <summary>Atomically replaces a nonempty root with an empty C-node.</summary>
    public void Clear()
    {
        while (true)
        {
            var root = ReadRoot();
            var main = ReadMain(root.Node);
            if (main is CNode { Bitmap: 0 })
                return;
            if (Gcas(root.Node, main, CNode.Empty, root))
            {
                Interlocked.Increment(ref _revision);
                return;
            }
        }
    }

    /// <summary>Returns an enumerator bound to an immutable generation.</summary>
    public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => Snapshot().GetEnumerator();

    IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator() => GetEnumerator();
    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private MutationResult Mutate(TKey key, Func<bool, TValue?, Decision> transform)
    {
        var hash = unchecked((uint)_comparer.GetHashCode(key!));
        while (true)
        {
            var root = ReadRoot();
            var node = root.Node;
            INode? parentNode = null;
            CNode? parentMain = null;
            var parentPosition = 0;
            var parentFlag = 0u;
            var shift = 0;
            while (true)
            {
                var main = ReadMain(node);
                if (main is TNode tomb)
                {
                    if (parentNode is null || parentMain is null)
                        throw new InvalidOperationException("The root indirection node cannot contain a tomb.");

                    var cleaned = tomb.Entry is null
                        ? parentMain.Remove(parentPosition, parentFlag)
                        : parentMain.Replace(parentPosition, tomb.Entry);
                    Gcas(
                        parentNode,
                        parentMain,
                        Contract(cleaned, ReferenceEquals(parentNode, root.Node)),
                        root);
                    break;
                }

                if (main is LNode collision)
                {
                    var index = collision.Find(hash, key, _comparer);
                    var exists = index >= 0;
                    var current = exists ? collision.Entries[index].Value : default;
                    var decision = transform(exists, current);
                    if (decision.Kind == DecisionKind.None)
                        return new MutationResult(false, current);
                    MainNode replacement;
                    if (decision.Kind == DecisionKind.Remove)
                    {
                        var entries = RemoveAt(collision.Entries, index);
                        replacement = ContractCollision(
                            entries,
                            collision.Entries[index].Hash,
                            ReferenceEquals(node, root.Node));
                    }
                    else
                    {
                        var storedKey = exists ? collision.Entries[index].Key : key;
                        var storedHash = exists ? collision.Entries[index].Hash : hash;
                        var entry = new SNode(storedHash, storedKey, decision.Value!);
                        replacement = exists
                            ? new LNode(ReplaceAt(collision.Entries, index, entry))
                            : collision.Entries[0].Hash == hash
                                ? new LNode(Append(collision.Entries, entry))
                                : Merge(collision, entry, shift, root.Generation);
                    }
                    if (!Gcas(node, main, replacement, root))
                        break;
                    Interlocked.Increment(ref _revision);
                    if (decision.Kind == DecisionKind.Remove)
                        CleanTombs(hash);
                    return new MutationResult(true, decision.ResultValue);
                }

                var cnode = (CNode)main;
                var flag = 1u << (int)((hash >> shift) & 31);
                var position = BitOperations.PopCount(cnode.Bitmap & (flag - 1));
                if ((cnode.Bitmap & flag) == 0)
                {
                    var decision = transform(false, default);
                    if (decision.Kind == DecisionKind.None)
                        return new MutationResult(false, decision.ResultValue);
                    var replacement = cnode.Insert(position, flag, new SNode(hash, key, decision.Value!));
                    if (!Gcas(node, cnode, replacement, root))
                        break;
                    Interlocked.Increment(ref _revision);
                    return new MutationResult(true, decision.ResultValue);
                }

                var branch = cnode.Branches[position];
                if (branch is SNode singleton)
                {
                    if (_comparer.Equals(singleton.Key, key))
                    {
                        var decision = transform(true, singleton.Value);
                        if (decision.Kind == DecisionKind.None)
                            return new MutationResult(false, decision.ResultValue ?? singleton.Value);
                        MainNode replacement = decision.Kind == DecisionKind.Remove
                            ? Contract(cnode.Remove(position, flag), ReferenceEquals(node, root.Node))
                            : cnode.Replace(
                                position,
                                new SNode(singleton.Hash, singleton.Key, decision.Value!));
                        if (!Gcas(node, cnode, replacement, root))
                            break;
                        Interlocked.Increment(ref _revision);
                        if (decision.Kind == DecisionKind.Remove)
                            CleanTombs(hash);
                        return new MutationResult(true, decision.ResultValue);
                    }

                    var absent = transform(false, default);
                    if (absent.Kind == DecisionKind.None)
                        return new MutationResult(false, absent.ResultValue);
                    var child = Merge(singleton, new SNode(hash, key, absent.Value!), shift + 5, root.Generation);
                    if (!Gcas(node, cnode, cnode.Replace(position, child), root))
                        break;
                    Interlocked.Increment(ref _revision);
                    return new MutationResult(true, absent.ResultValue);
                }

                var inode = (INode)branch;
                if (!ReferenceEquals(inode.Generation, root.Generation))
                {
                    var renewed = new INode(ReadMain(inode), root.Generation);
                    var renewedParent = cnode.Replace(position, renewed);
                    if (!Gcas(node, cnode, renewedParent, root))
                        break;
                    inode = renewed;
                    cnode = renewedParent;
                }

                parentNode = node;
                parentMain = cnode;
                parentPosition = position;
                parentFlag = flag;
                node = inode;
                shift += 5;
            }
        }
    }

    private bool TryGetValue(Root root, TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        var hash = unchecked((uint)_comparer.GetHashCode(key!));
        var node = root.Node;
        var shift = 0;
        while (true)
        {
            var main = ReadMain(node);
            if (main is TNode tomb)
            {
                var entry = tomb.Entry;
                if (entry is not null && entry.Hash == hash && _comparer.Equals(entry.Key, key))
                {
                    value = entry.Value;
                    return true;
                }

                value = default;
                return false;
            }

            if (main is LNode collision)
            {
                var index = collision.Find(hash, key, _comparer);
                if (index >= 0)
                {
                    value = collision.Entries[index].Value;
                    return true;
                }
                value = default;
                return false;
            }
            var cnode = (CNode)main;
            var flag = 1u << (int)((hash >> shift) & 31);
            if ((cnode.Bitmap & flag) == 0)
            {
                value = default;
                return false;
            }
            var position = BitOperations.PopCount(cnode.Bitmap & (flag - 1));
            var branch = cnode.Branches[position];
            if (branch is SNode singleton)
            {
                if (singleton.Hash == hash && _comparer.Equals(singleton.Key, key))
                {
                    value = singleton.Value;
                    return true;
                }
                value = default;
                return false;
            }
            node = (INode)branch;
            shift += 5;
        }
    }

    private void CleanTombs(uint hash)
    {
        while (true)
        {
            var root = ReadRoot();
            var node = root.Node;
            INode? parentNode = null;
            CNode? parentMain = null;
            var parentPosition = 0;
            var parentFlag = 0u;
            var shift = 0;
            while (true)
            {
                var main = ReadMain(node);
                if (main is TNode tomb)
                {
                    if (parentNode is null || parentMain is null)
                        throw new InvalidOperationException("The root indirection node cannot contain a tomb.");

                    var cleaned = tomb.Entry is null
                        ? parentMain.Remove(parentPosition, parentFlag)
                        : parentMain.Replace(parentPosition, tomb.Entry);
                    Gcas(
                        parentNode,
                        parentMain,
                        Contract(cleaned, ReferenceEquals(parentNode, root.Node)),
                        root);
                    break;
                }

                if (main is LNode)
                    return;

                var cnode = (CNode)main;
                var flag = 1u << (int)((hash >> shift) & 31);
                if ((cnode.Bitmap & flag) == 0)
                    return;

                var position = BitOperations.PopCount(cnode.Bitmap & (flag - 1));
                var branch = cnode.Branches[position];
                if (branch is SNode)
                    return;

                var inode = (INode)branch;
                if (!ReferenceEquals(inode.Generation, root.Generation))
                {
                    var renewed = new INode(ReadMain(inode), root.Generation);
                    var renewedParent = cnode.Replace(position, renewed);
                    if (!Gcas(node, cnode, renewedParent, root))
                        break;
                    inode = renewed;
                    cnode = renewedParent;
                }

                parentNode = node;
                parentMain = cnode;
                parentPosition = position;
                parentFlag = flag;
                node = inode;
                shift += 5;
            }
        }
    }

    private static MainNode Contract(CNode node, bool isRoot)
    {
        if (isRoot || node.Branches.Length > 1)
            return node;
        if (node.Branches.Length == 0)
            return TNode.Empty;
        return node.Branches[0] is SNode singleton ? new TNode(singleton) : node;
    }

    private static MainNode ContractCollision(SNode[] entries, uint hash, bool isRoot)
    {
        if (entries.Length > 1)
            return new LNode(entries);
        if (entries.Length == 0)
            return isRoot ? CNode.Empty : TNode.Empty;
        if (!isRoot)
            return new TNode(entries[0]);

        var flag = 1u << (int)(hash & 31);
        return new CNode(flag, [entries[0]]);
    }

    private INode Merge(SNode first, SNode second, int shift, TrieGeneration generation)
    {
        if (first.Hash == second.Hash || shift >= 32)
            return new INode(new LNode([first, second]), generation);
        var firstFlag = 1u << (int)((first.Hash >> shift) & 31);
        var secondFlag = 1u << (int)((second.Hash >> shift) & 31);
        if (firstFlag != secondFlag)
        {
            Branch[] branches = firstFlag < secondFlag ? [first, second] : [second, first];
            return new INode(new CNode(firstFlag | secondFlag, branches), generation);
        }
        return new INode(new CNode(firstFlag, [Merge(first, second, shift + 5, generation)]), generation);
    }

    private MainNode Merge(LNode collision, SNode singleton, int shift, TrieGeneration generation)
    {
        var collisionHash = collision.Entries[0].Hash;
        Debug.Assert(collision.Entries.All(entry => entry.Hash == collisionHash));
        Debug.Assert(collisionHash != singleton.Hash);
        if (shift >= 32)
            throw new InvalidOperationException("Different 32-bit hashes cannot share every Ctrie level.");

        var collisionFlag = 1u << (int)((collisionHash >> shift) & 31);
        var singletonFlag = 1u << (int)((singleton.Hash >> shift) & 31);
        if (collisionFlag != singletonFlag)
        {
            var collisionBranch = new INode(collision, generation);
            Branch[] branches = collisionFlag < singletonFlag
                ? [collisionBranch, singleton]
                : [singleton, collisionBranch];
            return new CNode(collisionFlag | singletonFlag, branches);
        }

        var child = new INode(Merge(collision, singleton, shift + 5, generation), generation);
        return new CNode(collisionFlag, [child]);
    }

    private bool Gcas(INode node, MainNode before, MainNode after, Root root)
    {
        var descriptor = new Descriptor(before, after, root);
        if (!ReferenceEquals(Interlocked.CompareExchange(ref node.Main, descriptor, before), before))
            return false;
        Volatile.Read(ref GcasInstalledHookForTesting)?.Invoke();
        Complete(descriptor);
        return Volatile.Read(ref descriptor.Status) == Descriptor.Committed;
    }

    private MainNode ReadMain(INode node)
    {
        while (true)
        {
            var value = Volatile.Read(ref node.Main);
            if (value is MainNode main)
                return main;
            var descriptor = (Descriptor)value;
            Complete(descriptor);
            var selected = Volatile.Read(ref descriptor.Status) == Descriptor.Committed
                ? descriptor.After
                : descriptor.Before;
            Interlocked.CompareExchange(ref node.Main, selected, descriptor);
        }
    }

    private void Complete(Descriptor descriptor)
    {
        var status = ReferenceEquals(ReadRoot(), descriptor.Root)
            ? Descriptor.Committed
            : Descriptor.Aborted;
        Interlocked.CompareExchange(ref descriptor.Status, status, Descriptor.Undecided);
    }

    private Root ReadRoot()
    {
        while (true)
        {
            var value = Volatile.Read(ref _root);
            if (value is Root root)
                return root;

            var descriptor = (RootDescriptor)value;
            Complete(descriptor);
        }
    }

    private void Complete(RootDescriptor descriptor)
    {
        // Do not help a node-local GCAS descriptor while deciding the root transition: that GCAS
        // in turn needs to observe the root, which would create a helping cycle. Seeing any value
        // other than the expected stable main node aborts this attempt; the retry will first help
        // the competing GCAS and copy its result.
        var status = ReferenceEquals(Volatile.Read(ref descriptor.Before.Node.Main), descriptor.ExpectedMain)
            ? RootDescriptor.Committed
            : RootDescriptor.Aborted;
        Interlocked.CompareExchange(ref descriptor.Status, status, RootDescriptor.Undecided);
        var selected = Volatile.Read(ref descriptor.Status) == RootDescriptor.Committed
            ? descriptor.After
            : descriptor.Before;
        Interlocked.CompareExchange(ref _root, selected, descriptor);
    }

    private IEnumerable<KeyValuePair<TKey, TValue>> Enumerate(Root root) => Enumerate(root.Node);

    private IEnumerable<KeyValuePair<TKey, TValue>> Enumerate(INode node)
    {
        var main = ReadMain(node);
        if (main is TNode tomb)
        {
            if (tomb.Entry is not null)
                yield return KeyValuePair.Create(tomb.Entry.Key, tomb.Entry.Value);
            yield break;
        }

        if (main is LNode collision)
        {
            foreach (var entry in collision.Entries)
                yield return KeyValuePair.Create(entry.Key, entry.Value);
            yield break;
        }
        foreach (var branch in ((CNode)main).Branches)
        {
            if (branch is SNode singleton)
                yield return KeyValuePair.Create(singleton.Key, singleton.Value);
            else
                foreach (var entry in Enumerate((INode)branch))
                    yield return entry;
        }
    }

    private static T[] ReplaceAt<T>(T[] source, int index, T value)
    {
        var result = (T[])source.Clone();
        result[index] = value;
        return result;
    }

    private static T[] Append<T>(T[] source, T value)
    {
        var result = new T[source.Length + 1];
        Array.Copy(source, result, source.Length);
        result[^1] = value;
        return result;
    }

    private static T[] RemoveAt<T>(T[] source, int index)
    {
        var result = new T[source.Length - 1];
        Array.Copy(source, 0, result, 0, index);
        Array.Copy(source, index + 1, result, index, source.Length - index - 1);
        return result;
    }

    internal CtrieStatistics ValidateStructureForTesting()
    {
        var root = ReadRoot();
        if (!ReferenceEquals(root.Node.Generation, root.Generation))
            throw new InvalidOperationException("The root indirection node has the wrong generation.");

        var visited = new HashSet<INode>();
        var indirectionNodes = 0;
        var collisionNodes = 0;
        var tombNodes = 0;
        var entryCount = 0;
        var maxDepth = 0;
        ValidateNode(
            root.Node,
            shift: 0,
            isRoot: true,
            visited,
            ref indirectionNodes,
            ref collisionNodes,
            ref tombNodes,
            ref entryCount,
            ref maxDepth);
        return new CtrieStatistics(indirectionNodes, collisionNodes, tombNodes, entryCount, maxDepth);
    }

    private List<uint> ValidateNode(
        INode node,
        int shift,
        bool isRoot,
        HashSet<INode> visited,
        ref int indirectionNodes,
        ref int collisionNodes,
        ref int tombNodes,
        ref int entryCount,
        ref int maxDepth)
    {
        if (!visited.Add(node))
            throw new InvalidOperationException("The Ctrie contains an indirection-node cycle.");

        indirectionNodes++;
        maxDepth = Math.Max(maxDepth, shift / 5);
        var main = ReadMain(node);
        if (main is TNode tomb)
        {
            if (isRoot)
                throw new InvalidOperationException("The root indirection node contains a tomb.");
            tombNodes++;
            if (tomb.Entry is null)
                return [];
            entryCount++;
            return [tomb.Entry.Hash];
        }

        if (main is LNode collision)
        {
            if (collision.Entries.Length < 2)
                throw new InvalidOperationException("A collision node contains fewer than two entries.");
            collisionNodes++;
            var hash = collision.Entries[0].Hash;
            for (var index = 0; index < collision.Entries.Length; index++)
            {
                var entry = collision.Entries[index];
                if (entry.Hash != hash)
                    throw new InvalidOperationException("A collision node contains mixed full hashes.");
                for (var other = 0; other < index; other++)
                {
                    if (_comparer.Equals(collision.Entries[other].Key, entry.Key))
                        throw new InvalidOperationException("A collision node contains equivalent duplicate keys.");
                }
            }

            entryCount += collision.Entries.Length;
            return collision.Entries.Select(entry => entry.Hash).ToList();
        }

        var cnode = (CNode)main;
        if (BitOperations.PopCount(cnode.Bitmap) != cnode.Branches.Length)
            throw new InvalidOperationException("A C-node bitmap disagrees with its compact branch array.");
        if (!isRoot && (cnode.Branches.Length == 0
            || (cnode.Branches.Length == 1 && cnode.Branches[0] is SNode)))
        {
            throw new InvalidOperationException("A non-root C-node was not contracted into a tomb.");
        }

        if (cnode.Branches.Length != 0 && shift >= 32)
            throw new InvalidOperationException("A C-node consumes more than 32 hash bits.");

        List<uint> hashes = [];
        var position = 0;
        for (var index = 0; index < 32; index++)
        {
            var flag = 1u << index;
            if ((cnode.Bitmap & flag) == 0)
                continue;

            var branch = cnode.Branches[position++];
            List<uint> branchHashes;
            if (branch is SNode singleton)
            {
                entryCount++;
                branchHashes = [singleton.Hash];
            }
            else
            {
                branchHashes = ValidateNode(
                    (INode)branch,
                    shift + 5,
                    isRoot: false,
                    visited,
                    ref indirectionNodes,
                    ref collisionNodes,
                    ref tombNodes,
                    ref entryCount,
                    ref maxDepth);
            }

            foreach (var hash in branchHashes)
            {
                if (((hash >> shift) & 31) != index)
                    throw new InvalidOperationException("A branch contains an entry in the wrong hash partition.");
            }
            hashes.AddRange(branchHashes);
        }

        return hashes;
    }

    /// <summary>An immutable O(1)-captured Ctrie generation.</summary>
    public sealed class SnapshotView : IReadOnlyDictionary<TKey, TValue>
    {
        private readonly ConcurrentHashTrie<TKey, TValue> _owner;
        private readonly Root _root;

        internal SnapshotView(ConcurrentHashTrie<TKey, TValue> owner, Root root)
        {
            _owner = owner;
            _root = root;
        }

        /// <inheritdoc />
        public int Count => _owner.Enumerate(_root).Count();
        /// <inheritdoc />
        public IEnumerable<TKey> Keys => _owner.Enumerate(_root).Select(entry => entry.Key);
        /// <inheritdoc />
        public IEnumerable<TValue> Values => _owner.Enumerate(_root).Select(entry => entry.Value);
        /// <inheritdoc />
        public TValue this[TKey key] => TryGetValue(key, out var value) ? value : throw new KeyNotFoundException();
        /// <inheritdoc />
        public bool ContainsKey(TKey key) => TryGetValue(key, out _);
        /// <inheritdoc />
        public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value) =>
            _owner.TryGetValue(_root, key, out value);

        /// <summary>Copies this generation into the canonical persistent CHAMP representation.</summary>
        public PersistentHashMap<TKey, TValue> ToPersistentHashMap() =>
            PersistentHashMap<TKey, TValue>.CreateRange(this, _owner._comparer);

        /// <inheritdoc />
        public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => _owner.Enumerate(_root).GetEnumerator();
        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
    }

    internal sealed class TrieGeneration;
    internal readonly record struct CtrieStatistics(
        int IndirectionNodeCount,
        int CollisionNodeCount,
        int TombNodeCount,
        int EntryCount,
        int MaxDepth);

    internal sealed class Root(INode node, TrieGeneration generation)
    {
        internal INode Node { get; } = node;
        internal TrieGeneration Generation { get; } = generation;
    }

    internal abstract class Branch;
    internal abstract class MainNode;

    internal sealed class INode(MainNode main, TrieGeneration generation) : Branch
    {
        internal object Main = main;
        internal TrieGeneration Generation { get; } = generation;
    }

    private sealed class SNode(uint hash, TKey key, TValue value) : Branch
    {
        internal uint Hash { get; } = hash;
        internal TKey Key { get; } = key;
        internal TValue Value { get; } = value;
    }

    private sealed class CNode(uint bitmap, Branch[] branches) : MainNode
    {
        internal static readonly CNode Empty = new(0, []);
        internal uint Bitmap { get; } = bitmap;
        internal Branch[] Branches { get; } = branches;

        internal CNode Insert(int index, uint flag, Branch branch)
        {
            var result = new Branch[Branches.Length + 1];
            Array.Copy(Branches, 0, result, 0, index);
            result[index] = branch;
            Array.Copy(Branches, index, result, index + 1, Branches.Length - index);
            return new CNode(Bitmap | flag, result);
        }

        internal CNode Replace(int index, Branch branch) => new(Bitmap, ReplaceAt(Branches, index, branch));
        internal CNode Remove(int index, uint flag) => new(Bitmap & ~flag, RemoveAt(Branches, index));
    }

    private sealed class LNode(SNode[] entries) : MainNode
    {
        internal SNode[] Entries { get; } = entries;
        internal int Find(uint hash, TKey key, IEqualityComparer<TKey> comparer)
        {
            for (var i = 0; i < Entries.Length; i++)
                if (Entries[i].Hash == hash && comparer.Equals(Entries[i].Key, key))
                    return i;
            return -1;
        }
    }

    private sealed class TNode(SNode? entry) : MainNode
    {
        internal static readonly TNode Empty = new(entry: null);
        internal SNode? Entry { get; } = entry;
    }

    private sealed class Descriptor(MainNode before, MainNode after, Root root)
    {
        internal const int Undecided = 0;
        internal const int Committed = 1;
        internal const int Aborted = 2;
        internal MainNode Before { get; } = before;
        internal MainNode After { get; } = after;
        internal Root Root { get; } = root;
        internal int Status;
    }

    private sealed class RootDescriptor(Root before, MainNode expectedMain, Root after)
    {
        internal const int Undecided = 0;
        internal const int Committed = 1;
        internal const int Aborted = 2;
        internal Root Before { get; } = before;
        internal MainNode ExpectedMain { get; } = expectedMain;
        internal Root After { get; } = after;
        internal int Status;
    }

    private enum DecisionKind { None, Set, Remove }
    private readonly record struct Decision(DecisionKind Kind, TValue? Value, TValue? ResultValue)
    {
        internal static Decision None => new(DecisionKind.None, default, default);
        internal static Decision Set(TValue value) => new(DecisionKind.Set, value, value);
        internal static Decision Remove(TValue? value) => new(DecisionKind.Remove, default, value);
        internal static Decision Return(TValue? value) => new(DecisionKind.None, default, value);
    }
    private readonly record struct MutationResult(bool Changed, TValue? Value);
}
